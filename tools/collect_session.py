"""Guided recording session with firmware/imu_collect.

    python tools/collect_session.py --port COM5 --subject s01 --role attacker --reps 50
    python tools/collect_session.py --port COM5 --subject s01 --background 900   # 15 min

For each gesture class it counts down, records a short take while you perform
the move once, and saves data/<subject>/<class>/<timestamp>.csv. Press Enter
to keep a take, 'r' + Enter to redo it, 'q' + Enter to stop.
Classes are shuffled so fatigue doesn't correlate with the label.
"""
import argparse
import random
import sys
import time
from pathlib import Path

import serial  # pip install pyserial

CLASSES = {
    "attacker": ["forehand", "backhand", "feint"],
    "defender": ["dodge_left", "dodge_right", "block"],
}
HINT = {
    "forehand": "正手搧（掌心朝外，由外往内）",
    "backhand": "反手搧（手背，由内往外）",
    "feint": "假动作：起手像要搧，中途收回",
    "dodge_left": "头往左闪",
    "dodge_right": "头往右闪",
    "block": "抬手格挡",
}
ROOT = Path(__file__).resolve().parent.parent / "data"


def record(ser, seconds):
    ser.reset_input_buffer()
    ser.write(b"r")
    rows, end = [], time.time() + seconds
    while time.time() < end:
        line = ser.readline().decode(errors="ignore").strip()
        if line and not line.startswith("#"):
            rows.append(line)
    ser.write(b"s")
    time.sleep(0.05)
    ser.reset_input_buffer()
    return rows


def save(rows, subject, cls):
    path = ROOT / subject / cls / f"{time.strftime('%Y%m%d-%H%M%S')}-{int(time.time()*1000)%1000:03d}.csv"
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("t_ms,ax,ay,az,gx,gy,gz\n" + "\n".join(rows) + "\n")
    return path


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", required=True)
    ap.add_argument("--subject", required=True, help="code only, e.g. s01 — never a real name")
    ap.add_argument("--role", choices=CLASSES)
    ap.add_argument("--reps", type=int, default=50)
    ap.add_argument("--take-seconds", type=float, default=2.5)
    ap.add_argument("--background", type=int, metavar="SECONDS",
                    help="record free movement (hair, glasses, waving, walking) in 30 s chunks")
    ap.add_argument("--no-confirm", action="store_true", help="keep every take without asking")
    args = ap.parse_args()
    if not args.role and not args.background:
        ap.error("give --role or --background")

    with serial.Serial(args.port, 115200, timeout=0.5) as ser:
        time.sleep(2)  # the Nano resets when the port opens
        ser.reset_input_buffer()

        if args.background:
            print("自由活动：拨头发、推眼镜、挥手、拿水杯、走动…… 就是别做出招动作。")
            left = args.background
            while left > 0:
                chunk = min(30, left)
                rows = record(ser, chunk)
                p = save(rows, args.subject, "background")
                left -= chunk
                print(f"  {len(rows)} samples -> {p.name}  (剩 {left} s)")
            return

        plan = [c for c in CLASSES[args.role] for _ in range(args.reps)]
        random.shuffle(plan)
        done = {c: 0 for c in CLASSES[args.role]}
        i = 0
        while i < len(plan):
            cls = plan[i]
            print(f"\n[{i + 1}/{len(plan)}] {cls} — {HINT[cls]}")
            for n in (3, 2, 1):
                print(f"  {n}…", end=" ", flush=True)
                time.sleep(0.5)
            print("做！")
            rows = record(ser, args.take_seconds)
            if not args.no_confirm:
                ans = input(f"  {len(rows)} samples. Enter=保留  r=重来  q=结束 > ").strip().lower()
                if ans == "q":
                    break
                if ans == "r":
                    continue
            save(rows, args.subject, cls)
            done[cls] += 1
            i += 1
        print("\n完成:", done)


if __name__ == "__main__":
    sys.exit(main())
