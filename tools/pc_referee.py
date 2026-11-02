"""PC as the referee: BLE events from the wearables -> real referee core -> the web page.

    python tools/pc_referee.py | python tools/web_dashboard.py --stdin
    python tools/pc_referee.py --demo --seconds 10      # no board: scripted events

Why this works: build/sim_match --interactive links the same RefereeCore as the
ESP32 firmware and accepts "ev <player> <class> <peak>" lines, so the page is
driven by the real state machine instead of a browser-side reimplementation.

Timing note: sim_match stamps an event when it is injected, and both boards
report ~500 ms after the physical onset (the inference window), so the relative
attack/defence delta the referee judges is preserved; absolute onsets are not.
Bump the window down (train.py --window) if you want the page to react sooner.

Requires: pip install bleak
"""
from __future__ import annotations

import argparse
import asyncio
import json
import socket
import struct
import subprocess
import sys
import threading
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SIM = ROOT / "build" / ("sim_match.exe" if sys.platform == "win32" else "sim_match")
DASH = ROOT / "tools" / "web_dashboard.py"

EVENT = "5a1a0001-8c3b-4f7e-9d2a-000000000001"
SYNC = "5a1a0002-8c3b-4f7e-9d2a-000000000001"
CLS = {1: "forehand", 2: "backhand", 3: "feint",
       10: "dodge_left", 11: "dodge_right", 12: "block"}
# Player boards and single-role boards share a slot: 0 = left, 1 = right.
SLOTS = {"SLAP-P1": 0, "SLAP-ATK": 0, "SLAP-P2": 1, "SLAP-DEF": 1}


def now_ms() -> int:
    return int(time.monotonic() * 1000) & 0xFFFFFFFF


def _port_taken(port: int) -> bool:
    """True only if something is actually listening. A plain bind() would also
    fail on lingering TIME_WAIT sockets right after a previous run."""
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.settimeout(0.3)
        return s.connect_ex(("127.0.0.1", port)) == 0


class Bridge:
    def __init__(self, players: list[str], latency: int, demo: bool = False, solo: bool = False,
                 web_port: int | None = None, browser: bool = True):
        if not SIM.exists():
            sys.exit(f"{SIM} not found: build it with  make -C tests sim")
        self.players = players
        self.demo_mode = demo
        self.solo = solo
        self.stop = False
        self.started = False
        self.rematching = False
        self.turn = 0
        self.armed = False
        self.sim_lock = threading.Lock()
        self.out_lock = threading.Lock()
        self.dash = None
        if web_port is not None:
            if _port_taken(web_port):
                sys.exit(f"port {web_port} is already in use — close the old dashboard "
                         f"or pass --http-port {web_port + 1}")
            cmd = [sys.executable, str(DASH), "--stdin", "--http-port", str(web_port)]
            if not browser:
                cmd.append("--no-browser")
            self.dash = subprocess.Popen(
                cmd, stdin=subprocess.PIPE, text=True, bufsize=1,
                creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        self.sim = subprocess.Popen(
            [str(SIM), "--interactive", "--latency", str(latency)],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
            text=True, bufsize=1,
            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        threading.Thread(target=self.pump, daemon=True).start()

    # ---- output to web_dashboard.py --stdin --------------------------------
    def emit(self, m: dict) -> None:
        line = json.dumps(m, ensure_ascii=False)
        try:
            with self.out_lock:
                if self.dash and self.dash.stdin:
                    self.dash.stdin.write(line + "\n")
                    self.dash.stdin.flush()
                else:
                    print(line, flush=True)
        except (BrokenPipeError, OSError):
            self.stop = True

    def peer(self, name: str, connected: bool) -> None:
        self.emit({"type": "peer", "name": name, "connected": connected})

    def pump(self) -> None:
        assert self.sim.stdout is not None
        for line in self.sim.stdout:
            line = line.strip()
            if not line.startswith("{"):
                continue
            try:
                m = json.loads(line)
            except json.JSONDecodeError:
                continue
            if m.get("type") == "peer":
                continue  # sim claims both boards are up; we report the real ones
            if m.get("type") == "announce":
                self.turn = int(m.get("attacker", 0))
            if m.get("type") == "state":
                self.armed = m.get("state") == "ARMED"
            self.emit(m)
            if m.get("type") == "game_over":
                threading.Thread(target=self.rematch, daemon=True).start()

    # ---- input to sim_match ------------------------------------------------
    def to_sim(self, cmd: str) -> None:
        with self.sim_lock:
            if self.sim.poll() is None and self.sim.stdin:
                try:
                    self.sim.stdin.write(cmd + "\n")
                    self.sim.stdin.flush()
                except (BrokenPipeError, OSError):
                    self.stop = True

    def start_once(self) -> None:
        if not self.started:
            self.started = True
            self.to_sim("start")

    def rematch(self) -> None:
        if self.rematching:
            return
        self.rematching = True
        time.sleep(3)
        if not self.stop:
            self.to_sim("start")
        self.rematching = False

    def on_event(self, player: int, data: bytearray) -> None:
        if len(data) < 8:
            return
        _t, cls, _conf, peak = struct.unpack("<IBBH", data[:8])
        name = CLS.get(cls)
        if not name:  # background never gets here, but stay safe
            return
        if self.solo:
            # One board standing in for both sides: attack classes belong to
            # whoever is attacking this round, defence classes to the defender.
            player = self.turn if cls in (1, 2, 3) else 1 - self.turn
        self.start_once()
        self.to_sim(f"ev {player} {name} {peak}")

    # ---- BLE ---------------------------------------------------------------
    async def node(self, name: str) -> None:
        from bleak import BleakClient, BleakScanner
        slot = SLOTS[name]
        while not self.stop:
            print(f"scanning for {name} …", file=sys.stderr, flush=True)
            dev = await BleakScanner.find_device_by_name(name, timeout=8)
            if not dev:
                continue
            try:
                async with BleakClient(dev) as c:
                    print(f"{name} connected ({dev.address})", file=sys.stderr, flush=True)
                    self.peer(name, True)
                    self.start_once()
                    await c.start_notify(EVENT, lambda _, d: self.on_event(slot, d))
                    while c.is_connected and not self.stop:
                        await c.write_gatt_char(SYNC, struct.pack("<I", now_ms()), response=False)
                        await asyncio.sleep(1.0)
            except Exception as e:  # noqa: BLE001 — keep retrying through BLE hiccups
                print(f"{name}: {e}", file=sys.stderr, flush=True)
            self.peer(name, False)
            await asyncio.sleep(1)

    async def ble(self) -> None:
        await asyncio.gather(*(self.node(n) for n in self.players))

    async def wait_armed(self, timeout: float = 10.0) -> bool:
        end = time.monotonic() + timeout
        while not self.stop and time.monotonic() < end:
            if self.armed:
                return True
            await asyncio.sleep(0.05)
        return False

    # ---- demo without hardware --------------------------------------------
    async def demo(self) -> None:
        for n in self.players:
            self.peer(n, True)
        self.start_once()
        plan = [
            ("forehand", "dodge_right", 900),   # MISS
            ("backhand", "block", 600),         # BLOCKED
            ("forehand", None, 900),            # HIT
            ("feint", "dodge_left", 300),       # FEINT_PUNISHED after the follow-up
        ]
        for slap, defense, peak in plan:
            if not await self.wait_armed():
                return
            a, d = self.turn, 1 - self.turn
            self.to_sim(f"ev {a} {slap} {peak}")
            if defense:
                await asyncio.sleep(0.3)
                self.to_sim(f"ev {d} {defense} 300")
            if slap == "feint":
                await asyncio.sleep(0.5)
                self.to_sim(f"ev {a} forehand 950")
            await asyncio.sleep(0.5)

    async def run(self) -> None:
        if self.demo_mode:
            await self.demo()
        else:
            await self.ble()

    def close(self) -> None:
        self.stop = True
        if self.sim.poll() is None:
            self.sim.terminate()
        if self.dash and self.dash.poll() is None:
            self.dash.terminate()


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--players", nargs="+", default=["SLAP-P1", "SLAP-P2"],
                    choices=sorted(SLOTS), help="board names to connect (one board: pass just that one)")
    ap.add_argument("--latency", type=int, default=0,
                    help="extra ms sim_match adds before delivering an event")
    ap.add_argument("--demo", action="store_true", help="scripted events, no BLE")
    ap.add_argument("--solo", action="store_true",
                    help="one board plays both sides: attack classes go to the current attacker, "
                         "defence classes to the defender")
    ap.add_argument("--web", action="store_true",
                    help="start the web dashboard too (one command, no pipe); opens the browser")
    ap.add_argument("--http-port", type=int, default=8765, help="--web: dashboard port")
    ap.add_argument("--no-browser", action="store_true", help="--web: do not open a browser")
    ap.add_argument("--seconds", type=float, default=0, help="stop after N seconds (0 = until Ctrl+C)")
    args = ap.parse_args()

    bridge = Bridge(args.players, args.latency, demo=args.demo, solo=args.solo,
                    web_port=args.http_port if args.web else None, browser=not args.no_browser)
    if args.solo:
        print("solo: one board covers both sides — slap on your attack turn, dodge on defence",
              file=sys.stderr, flush=True)

    async def runner() -> None:
        task = asyncio.create_task(bridge.run())
        if args.seconds:
            await asyncio.sleep(args.seconds)
            task.cancel()
        else:
            await task

    try:
        asyncio.run(runner())
    except (KeyboardInterrupt, asyncio.CancelledError):
        pass
    finally:
        bridge.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
