"""Terminal scoreboard for a match. Reads the JSON lines printed by either
firmware/nano_hub or firmware/referee_xiaozhi/standalone over serial, draws HP
bars and the last results, and appends everything to a JSONL log for the report.

    python tools/dashboard.py --port COM7
    python tools/dashboard.py --port COM7 --log results/match1.jsonl
    python tools/dashboard.py --replay results/match1.jsonl

Type 'start', 'swap_done' etc. + Enter to send commands to the referee.
"""
import argparse
import json
import sys
import threading
import time
from pathlib import Path

ZH = {"HIT": "命中", "JUMPED": "抢跑", "GRAZE": "擦过", "MISS": "闪开",
      "FEINT_FOOLED": "被假动作骗了", "FEINT_HELD": "没上当"}


class Board:
    def __init__(self):
        self.hp = [100, 100]
        self.state = "IDLE"
        self.attacker = 0
        self.round = 0
        self.peers = {}
        self.history = []
        self.last_events = []

    def feed(self, m: dict):
        t = m.get("type")
        if t == "state":
            self.state = m["state"]
        elif t == "announce":
            self.attacker, self.round = m["attacker"], m["round"]
        elif t == "result":
            self.hp = m["hp"]
            self.history.append(m)
        elif t == "game_over":
            self.hp = m["hp"]
            self.history.append({"outcome": f"GAME OVER — 玩家 {m['winner'] + 1} 胜"})
        elif t == "peer":
            self.peers[m["name"]] = m["connected"]
        elif t == "event":
            self.last_events = (self.last_events + [m])[-4:]

    def render(self):
        out = ["\x1b[2J\x1b[H空气巴掌对决", ""]
        peers = "  ".join(f"{n}:{'●' if c else '○'}" for n, c in sorted(self.peers.items())) or "(无设备)"
        out.append(f"状态 {self.state:10s} 第 {self.round} 回合  攻击方 玩家{self.attacker + 1}   {peers}")
        out.append("")
        for i in (0, 1):
            n = max(0, self.hp[i]) // 5
            out.append(f"玩家{i + 1} {'█' * n}{'·' * (20 - n)} {self.hp[i]:3d}")
        out.append("")
        for r in self.history[-6:]:
            if "attack" in r:
                out.append(f"  {r['attack']:9s} vs {r['defense']:11s} Δ={r['delta_ms']:5d}ms  "
                           f"{ZH.get(r['outcome'], r['outcome'])} -{r['damage']} ({r['strength']})")
            else:
                out.append(f"  {r['outcome']}")
        out.append("")
        for e in self.last_events:
            out.append(f"  evt {e['from']:9s} {e['cls']:12s} conf={e['conf']:3d} peak={e['peak']:4d} "
                       f"latency={e['rx'] - e['t']}ms")
        print("\n".join(out), flush=True)


def main():
    ap = argparse.ArgumentParser()
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--port")
    g.add_argument("--replay", type=Path)
    ap.add_argument("--log", type=Path)
    args = ap.parse_args()
    board = Board()

    if args.replay:
        for line in args.replay.read_text(encoding="utf-8").splitlines():
            board.feed(json.loads(line))
        board.render()
        return

    import serial
    ser = serial.Serial(args.port, 115200, timeout=0.2)
    log = open(args.log, "a", encoding="utf-8") if args.log else None

    def stdin_loop():
        for line in sys.stdin:
            ser.write((line.strip() + "\n").encode())
    threading.Thread(target=stdin_loop, daemon=True).start()

    last_draw = 0
    while True:
        line = ser.readline().decode(errors="ignore").strip()
        if line.startswith("{"):
            try:
                m = json.loads(line)
            except json.JSONDecodeError:
                continue
            m.setdefault("pc_time", time.time())
            board.feed(m)
            if log:
                log.write(json.dumps(m, ensure_ascii=False) + "\n")
                log.flush()
        if time.time() - last_draw > 0.1:
            board.render()
            last_draw = time.time()


if __name__ == "__main__":
    main()
