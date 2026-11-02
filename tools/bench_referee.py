"""Bench test for the ESP32 referee when no player boards are available.

    python tools/bench_referee.py --port COM9

Drives the standalone firmware's console hook `ev <player> <class> <peak_dps>`
(see firmware/referee_xiaozhi/standalone/main/main.cc) and checks each outcome
against the judging table. No BLE, no wearables — the events are stamped with
the referee clock, so the same RefereeCore path runs as in a real match.

Sequence per round: reset -> start -> announce_done -> wait ARMED -> events.
`start` is ignored unless the core is IDLE/GAME_OVER, hence the reset first.
"""
from __future__ import annotations

import argparse
import json
import sys
import time

try:
    import serial
except ImportError:
    sys.exit("pip install pyserial")


class Bench:
    def __init__(self, port: str):
        self.s = serial.Serial(port, 115200, timeout=0.1)
        time.sleep(2.0)  # the board resets when the port opens
        self.s.reset_input_buffer()

    def close(self) -> None:
        try:
            self.send("reset")
            time.sleep(0.2)
        finally:
            self.s.close()

    def send(self, cmd: str) -> None:
        self.s.write((cmd + "\n").encode())
        self.s.flush()

    def drain(self, until: str | None, timeout: float) -> list[dict]:
        end = time.time() + timeout
        out = []
        while time.time() < end:
            line = self.s.readline().decode("utf-8", "ignore").strip()
            i = line.find('{"type"')
            if i < 0:
                continue
            try:
                m = json.loads(line[i:])
            except json.JSONDecodeError:
                continue
            out.append(m)
            if until and until in line:
                break
        return out

    def arm(self) -> bool:
        self.send("reset")
        time.sleep(0.25)
        self.send("start")
        time.sleep(0.30)
        self.send("announce_done")
        return any(m.get("state") == "ARMED" for m in self.drain('"state":"ARMED"', 3.0))

    def round(self, steps: list[tuple[str, float]]) -> dict | None:
        if not self.arm():
            return None
        for cmd, wait in steps:
            self.send(cmd)
            if wait:
                time.sleep(wait)
        for m in self.drain('"result"', 3.5):
            if m.get("type") == "result":
                return m
        return None


# (label, steps, expected outcome, expected damage to the defender)
CASES = [
    ("正手 + 右闪", [("ev 0 forehand 900", 0.35), ("ev 1 dodge_right 300", 0)], "MISS", 0),
    ("正手 + 无动作", [("ev 0 forehand 900", 0)], "HIT", 30),
    ("反手 + 格挡", [("ev 0 backhand 600", 0.35), ("ev 1 block 250", 0)], "BLOCKED", 10),
    ("正手 + 左闪", [("ev 0 forehand 900", 0.35), ("ev 1 dodge_left 300", 0)], "GRAZE", 15),
    ("假动作 + 上当 + 追击",
     [("ev 0 feint 300", 0.30), ("ev 1 dodge_left 300", 0.45), ("ev 0 forehand 950", 0)], "FEINT_PUNISHED", 45),
]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--port", required=True, help="referee TTL port, e.g. COM9")
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args()

    b = Bench(args.port)
    failures = 0
    print(f"{'case':22s} {'expected':15s} {'got':15s} {'dmg':>4s}  result")
    try:
        for label, steps, want, want_dmg in CASES:
            r = b.round(steps)
            got = r["outcome"] if r else "NO RESULT"
            dmg = r["damage"] if r else -1
            ok = got == want and dmg == want_dmg
            failures += 0 if ok else 1
            print(f"{label:22s} {want:15s} {got:15s} {dmg:4d}  {'PASS' if ok else 'FAIL'}")
            if args.verbose and r:
                print("   ", json.dumps(r, ensure_ascii=False))
    finally:
        b.close()

    print(f"\n{len(CASES) - failures}/{len(CASES)} passed")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
