"""Record one take from firmware/imu_collect into data/<subject>/<class>/<timestamp>.csv.

    python serial_to_csv.py --port COM5 --subject s01 --cls forehand --seconds 3
"""
import argparse
import time
from pathlib import Path

import serial  # pip install pyserial


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", required=True)
    ap.add_argument("--subject", required=True)
    ap.add_argument("--cls", required=True)
    ap.add_argument("--seconds", type=float, default=3.0)
    ap.add_argument("--out", type=Path, default=Path(__file__).parent.parent / "data")
    args = ap.parse_args()

    path = args.out / args.subject / args.cls / f"{time.strftime('%Y%m%d-%H%M%S')}.csv"
    path.parent.mkdir(parents=True, exist_ok=True)

    with serial.Serial(args.port, 115200, timeout=1) as ser, open(path, "w") as f:
        time.sleep(2)  # board resets on open
        ser.reset_input_buffer()
        f.write("t_ms,ax,ay,az,gx,gy,gz\n")
        ser.write(b"r")
        end = time.time() + args.seconds
        n = 0
        while time.time() < end:
            line = ser.readline().decode(errors="ignore").strip()
            if line and not line.startswith("#"):
                f.write(line + "\n")
                n += 1
        ser.write(b"s")
    print(f"{n} samples -> {path}")


if __name__ == "__main__":
    main()
