"""Arcade-style match screen in the browser (for the demo / projector).

Reads the referee's JSON lines (firmware/referee_xiaozhi/standalone, firmware/nano_hub,
or tools/sim_match.cpp) and pushes them to tools/web/index.html over Server-Sent
Events. Buttons / keys on the page send commands (start, reset) back to the referee.
Standard library only (pyserial for --port).

    python tools/web_dashboard.py --port COM5               # ESP32 referee
    python tools/web_dashboard.py --sim                     # no hardware: build/sim_match
    build/sim_match --realtime | python tools/web_dashboard.py --stdin
    python tools/web_dashboard.py --replay results/match1.jsonl
    python tools/web_dashboard.py --port COM5 --host 0.0.0.0   # open from a phone / other PC

Then open http://127.0.0.1:8765 (done automatically unless --no-browser).
"""
import argparse
import json
import queue
import subprocess
import sys
import threading
import time
import webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PAGE = Path(__file__).resolve().parent / "web" / "index.html"
COMMANDS = {"start", "reset", "announce_done", "swap_done", "status",
            "left1", "right1", "left2", "right2"}


class Hub:
    """Fans every referee line out to the open pages; keeps enough to redraw a new page."""

    def __init__(self, log_path=None):
        self.lock = threading.Lock()
        self.clients = []
        self.peers = {}
        self.game = []  # messages since the current game's first announce
        self.log = open(log_path, "a", encoding="utf-8") if log_path else None

    def publish(self, m):
        with self.lock:
            t = m.get("type")
            if t == "peer":
                self.peers[m["name"]] = m
            elif t == "announce" and m.get("round") == 1:
                self.game = [m]
            elif t in ("state", "announce", "result", "game_over", "swap"):
                self.game.append(m)
            if self.log:
                self.log.write(json.dumps(m, ensure_ascii=False) + "\n")
                self.log.flush()
            for q in self.clients:
                q.put(m)

    def subscribe(self):
        q = queue.Queue()
        with self.lock:
            snapshot = {"type": "snapshot", "items": list(self.peers.values()) + self.game}
            self.clients.append(q)
        return q, snapshot

    def unsubscribe(self, q):
        with self.lock:
            if q in self.clients:
                self.clients.remove(q)


def parse(line, hub):
    line = line.strip()
    if not line.startswith("{"):
        return
    try:
        m = json.loads(line)
    except json.JSONDecodeError:
        return
    m.setdefault("pc_time", time.time())
    hub.publish(m)


class SerialSource:
    def __init__(self, port, hub):
        import serial
        self.ser = serial.Serial(port, 115200, timeout=0.2)
        self.hub = hub
        self.wlock = threading.Lock()
        threading.Thread(target=self.read, daemon=True).start()
        self.send("status")  # boards may have connected before we opened the port

    def read(self):
        while True:
            parse(self.ser.readline().decode(errors="ignore"), self.hub)

    def send(self, cmd):
        with self.wlock:
            self.ser.write((cmd + "\n").encode())
        return True


class SimSource:
    """Runs build/sim_match --realtime; 'start' begins a fresh match."""

    def __init__(self, hub, latency):
        exe = ROOT / "build" / ("sim_match.exe" if sys.platform == "win32" else "sim_match")
        if not exe.exists():
            sys.exit(f"{exe} not found: build it with  make -C tests sim")
        self.exe, self.hub, self.latency = exe, hub, latency
        self.proc = None
        self.lock = threading.Lock()
        # Show the two boards as connected and wait for 'start', like the real referee.
        for n in ("SLAP-P1", "SLAP-P2"):
            hub.publish({"type": "peer", "name": n, "connected": True})
        hub.publish({"type": "state", "state": "IDLE"})

    def close(self):
        with self.lock:
            if self.proc and self.proc.poll() is None:
                self.proc.kill()

    def send(self, cmd):
        if cmd not in ("start", "reset"):
            return False
        with self.lock:
            if self.proc and self.proc.poll() is None:
                self.proc.kill()
            self.proc = None
            if cmd == "reset":
                self.hub.publish({"type": "state", "state": "IDLE"})
                return True
            self.proc = subprocess.Popen([str(self.exe), "--realtime", "--latency", str(self.latency)],
                                         stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
            proc = self.proc
        threading.Thread(target=self.read, args=(proc,), daemon=True).start()
        return True

    def read(self, proc):
        for line in proc.stdout:
            if proc is not self.proc:
                return  # superseded by a newer 'start'
            parse(line, self.hub)


class StreamSource:
    """stdin or a replay file; commands have nowhere to go."""

    def __init__(self, hub, path=None, fast=False):
        self.hub, self.path, self.fast = hub, path, fast
        threading.Thread(target=self.read, daemon=True).start()

    def read(self):
        if self.path is None:
            for line in sys.stdin:
                parse(line, self.hub)
            return
        last = None
        for line in self.path.read_text(encoding="utf-8").splitlines():
            if not self.fast:
                try:
                    t = json.loads(line).get("pc_time")
                except json.JSONDecodeError:
                    t = None
                gap = min(2.0, t - last) if (t is not None and last is not None) else 0.25
                time.sleep(max(0.0, gap))
                last = t if t is not None else last
            parse(line, self.hub)

    def send(self, cmd):
        return False


def make_handler(hub, source):
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *_):
            pass

        def do_GET(self):
            if self.path in ("/", "/index.html"):
                body = PAGE.read_bytes()
                self.send_response(200)
                self.send_header("Content-Type", "text/html; charset=utf-8")
                self.send_header("Content-Length", str(len(body)))
                self.send_header("Cache-Control", "no-store")
                self.end_headers()
                self.wfile.write(body)
            elif self.path == "/events":
                self.stream()
            elif self.path == "/favicon.ico":
                self.send_response(204)
                self.end_headers()
            else:
                self.send_error(404)

        def stream(self):
            q, snapshot = hub.subscribe()
            self.send_response(200)
            self.send_header("Content-Type", "text/event-stream")
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            try:
                self.push(snapshot)
                while True:
                    try:
                        self.push(q.get(timeout=15))
                    except queue.Empty:
                        self.wfile.write(b": ping\n\n")
                        self.wfile.flush()
            except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
                pass
            finally:
                hub.unsubscribe(q)

        def push(self, m):
            self.wfile.write(f"data: {json.dumps(m, ensure_ascii=False)}\n\n".encode())
            self.wfile.flush()

        def do_POST(self):
            if self.path != "/cmd":
                self.send_error(404)
                return
            n = int(self.headers.get("Content-Length") or 0)
            cmd = self.rfile.read(n).decode(errors="ignore").strip()
            ok = cmd in COMMANDS and source.send(cmd)
            self.send_response(204 if ok else 409)
            self.end_headers()

    return Handler


def main():
    ap = argparse.ArgumentParser()
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--port", help="serial port of the referee (ESP32 standalone or nano_hub)")
    g.add_argument("--sim", action="store_true", help="simulated matches (build/sim_match)")
    g.add_argument("--stdin", action="store_true", help="JSON lines on stdin")
    g.add_argument("--replay", type=Path, help="JSONL log written by --log or dashboard.py")
    ap.add_argument("--fast", action="store_true", help="--replay: no pacing, jump to the end")
    ap.add_argument("--latency", type=int, default=515, help="--sim: event latency in ms")
    ap.add_argument("--log", type=Path, help="append every message to this JSONL file")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--http-port", type=int, default=8765)
    ap.add_argument("--no-browser", action="store_true")
    args = ap.parse_args()

    if args.log:
        args.log.parent.mkdir(parents=True, exist_ok=True)
    hub = Hub(args.log)
    if args.port:
        source = SerialSource(args.port, hub)
    elif args.sim:
        source = SimSource(hub, args.latency)
    else:
        source = StreamSource(hub, args.replay, args.fast)

    server = ThreadingHTTPServer((args.host, args.http_port), make_handler(hub, source))
    server.daemon_threads = True
    url = f"http://127.0.0.1:{args.http_port}/"
    print(f"match screen: {url}  (Ctrl+C to stop)", file=sys.stderr)
    if not args.no_browser:
        threading.Timer(0.5, webbrowser.open, args=(url,)).start()
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        if hasattr(source, "close"):
            source.close()


if __name__ == "__main__":
    main()
