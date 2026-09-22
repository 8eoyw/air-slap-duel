"""Experiment 2: sweep window length and sample rate (and model type), record
accuracy and model size, and plot. Arena usage has to be read on the device
(INFO,arena_used=... line) — flash the exported model.h of the interesting points.

    python sweep.py --task attacker
    python sweep.py --task attacker --data ../data_synth --windows 0.5 1.0 --hz 50 100
"""
from __future__ import annotations

import argparse
import csv
import os
from pathlib import Path

os.environ.setdefault("TF_CPP_MIN_LOG_LEVEL", "2")
os.environ.setdefault("TF_USE_LEGACY_KERAS", "1")

from slapml import Config, load_takes
from train import run_split

HERE = Path(__file__).parent


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--data", type=Path, default=HERE.parent / "data")
    ap.add_argument("--task", choices=["attacker", "defender"], required=True)
    ap.add_argument("--windows", type=float, nargs="+", default=[0.5, 0.75, 1.0, 1.5])
    ap.add_argument("--hz", type=int, nargs="+", default=[50, 100])
    ap.add_argument("--models", nargs="+", default=["cnn", "dense"])
    ap.add_argument("--epochs", type=int, default=80)
    args = ap.parse_args()

    out_root = HERE / "out" / f"sweep_{args.task}"
    rows = []
    for m in args.models:
        for hz in args.hz:
            takes = None
            for w in args.windows:
                cfg = Config(args.task, window_s=w, hz=hz, model=m, epochs=args.epochs)
                takes = takes or load_takes(args.data, cfg)
                ns = argparse.Namespace(test_subject=None, verbose=0)
                print(f"\n=== {m} {hz} Hz {w} s")
                r = run_split(takes, cfg, ns, out_root / f"{m}_{hz}hz_{w}s")
                rows.append({"model": m, "hz": hz, "window_s": w, "params": r["params"],
                             "fp32_kb": r["fp32"]["bytes"] / 1024, "int8_kb": r["int8"]["bytes"] / 1024,
                             "fp32_acc": r["fp32"]["acc"], "int8_acc": r["int8"]["acc"]})

    out_root.mkdir(parents=True, exist_ok=True)
    with open(out_root / "sweep.csv", "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0]))
        w.writeheader()
        w.writerows(rows)

    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    fig, (a1, a2) = plt.subplots(1, 2, figsize=(9, 3.4))
    for m in args.models:
        for hz in args.hz:
            pts = [r for r in rows if r["model"] == m and r["hz"] == hz]
            xs = [r["window_s"] for r in pts]
            a1.plot(xs, [r["int8_acc"] for r in pts], marker="o", label=f"{m} {hz} Hz")
            a2.plot(xs, [r["int8_kb"] for r in pts], marker="o", label=f"{m} {hz} Hz")
    a1.set_xlabel("window (s)"); a1.set_ylabel("int8 test accuracy"); a1.grid(alpha=0.3)
    a2.set_xlabel("window (s)"); a2.set_ylabel("int8 model size (KB)"); a2.grid(alpha=0.3)
    a1.legend(fontsize=8)
    fig.suptitle(f"Experiment 2 — {args.task}", fontsize=10)
    fig.tight_layout()
    fig.savefig(out_root / "sweep.png", dpi=150)
    print(f"\n-> {out_root / 'sweep.csv'}, sweep.png")


if __name__ == "__main__":
    main()
