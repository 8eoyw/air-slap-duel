"""Turn node serial logs / monitor CSVs into the numbers for docs/experiments.md.

Capture a node's USB log with any terminal, or:
    python tools/analyze_log.py capture --port COM5 --seconds 900 --out results/free15min_atk.log

Then:
    python tools/analyze_log.py latency  results/atk.log           # exp 1: on-device inference time
    python tools/analyze_log.py falsetrig results/free15min_atk.log # exp 6: false triggers / minute
    python tools/analyze_log.py sync results/sync_atk.log results/sync_def.log   # exp 5
    python tools/analyze_log.py e2e results/atk_events.csv          # rx - t_onset from ble_monitor

Experiment 5 procedure: wire one GPIO to both Nanos' D2 (or tap both boards
together) so one physical event triggers both; the difference between their
reported referee-clock onsets is the sync residual.
"""
import argparse
import csv
import statistics as st
import time
from pathlib import Path


def parse(path: Path):
    rows = []
    for line in path.read_text(encoding="utf-8", errors="ignore").splitlines():
        parts = line.strip().split(",")
        if parts and parts[0] in ("EVT", "REJ", "TRIG", "SYNC", "INFO"):
            rows.append(parts)
    return rows


def summary(xs, unit):
    if not xs:
        return "n=0"
    xs = sorted(xs)
    p95 = xs[min(len(xs) - 1, int(0.95 * len(xs)))]
    return (f"n={len(xs)} mean={st.mean(xs):.1f}{unit} median={st.median(xs):.1f}{unit} "
            f"p95={p95:.1f}{unit} max={xs[-1]:.1f}{unit}")


def cmd_capture(a):
    import serial
    a.out.parent.mkdir(parents=True, exist_ok=True)
    end = time.time() + a.seconds
    with serial.Serial(a.port, 115200, timeout=0.5) as s, open(a.out, "w", encoding="utf-8") as f:
        while time.time() < end:
            line = s.readline().decode(errors="ignore")
            if line:
                f.write(line)
    print(f"-> {a.out}")


def cmd_latency(a):
    for p in a.logs:
        rows = parse(p)
        us = [int(r[6]) for r in rows if r[0] == "EVT"] + [int(r[4]) for r in rows if r[0] == "REJ"]
        arena = [r[1] for r in rows if r[0] == "INFO" and r[1].startswith("arena_used=")]
        print(f"{p.name}: inference {summary([u / 1000 for u in us], 'ms')}  {' '.join(arena)}")


def cmd_falsetrig(a):
    for p in a.logs:
        rows = parse(p)
        t = [int(r[1]) for r in rows if r[0] in ("TRIG",)]
        evt = [r for r in rows if r[0] == "EVT"]
        rej = [r for r in rows if r[0] == "REJ"]
        span_min = (max(t) - min(t)) / 60000 if len(t) > 1 else a.minutes
        span_min = a.minutes or span_min
        print(f"{p.name}: over {span_min:.1f} min — triggers={len(t)} "
              f"({len(t) / span_min:.2f}/min), rejected as background/low-conf={len(rej)}, "
              f"FALSE EVENTS SENT={len(evt)} ({len(evt) / span_min:.2f}/min)")


def cmd_sync(a):
    ev = []
    for p in a.logs:
        ev.append([int(r[2]) for r in parse(p) if r[0] == "EVT"])
    if len(ev) != 2:
        raise SystemExit("give exactly two logs (one per node)")
    a_, b_ = ev
    # Pair each event in A with the nearest in B.
    res = []
    for x in a_:
        y = min(b_, key=lambda v: abs(v - x), default=None)
        if y is not None and abs(y - x) < 500:
            res.append(y - x)
    print(f"sync residual (B - A): {summary(res, 'ms')}")


def cmd_e2e(a):
    with open(a.csv) as f:
        lat = [int(r["rx"]) - int(r["t_onset"]) for r in csv.DictReader(f)]
    print(f"rx - t_onset (includes the post-trigger window + inference + BLE): {summary(lat, 'ms')}")


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    c = sub.add_parser("capture"); c.add_argument("--port", required=True)
    c.add_argument("--seconds", type=float, default=900); c.add_argument("--out", type=Path, required=True)
    c = sub.add_parser("latency"); c.add_argument("logs", type=Path, nargs="+")
    c = sub.add_parser("falsetrig"); c.add_argument("logs", type=Path, nargs="+")
    c.add_argument("--minutes", type=float, default=0, help="session length if not inferable")
    c = sub.add_parser("sync"); c.add_argument("logs", type=Path, nargs=2)
    c = sub.add_parser("e2e"); c.add_argument("csv", type=Path)
    a = ap.parse_args()
    {"capture": cmd_capture, "latency": cmd_latency, "falsetrig": cmd_falsetrig,
     "sync": cmd_sync, "e2e": cmd_e2e}[a.cmd](a)


if __name__ == "__main__":
    main()
