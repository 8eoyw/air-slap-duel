"""Train an IMU gesture classifier and export FP32 / int8 TFLite + model.h.

    python train.py --task attacker                  # subject-wise split, export
    python train.py --task defender --model dense
    python train.py --task attacker --loso           # leave-one-subject-out (experiment 4)
    python train.py --task attacker --data ../data_synth   # pipeline smoke test

Data layout: <data>/<subject>/<class>/<take>.csv with columns t_ms,ax,ay,az,gx,gy,gz.
Outputs in out/<task>/: model_fp32.tflite, model_int8.tflite, model.h, report.json, confusion.png
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path

os.environ.setdefault("TF_CPP_MIN_LOG_LEVEL", "2")
# TF 2.16 ships Keras 3, whose models the TFLite converter mis-handles (Conv1D).
# Use the Keras 2 package (tf_keras) instead.
os.environ.setdefault("TF_USE_LEGACY_KERAS", "1")
import numpy as np

from slapml import (Config, build_model, confusion, fit, load_takes, make_windows, to_tflite,
                    tflite_predict, write_model_h)

HERE = Path(__file__).parent


def split_subjects(subjects: list[str], test_subject: str | None):
    s = sorted(set(subjects))
    if len(s) < 3:
        return None
    test = test_subject or s[-1]
    rest = [x for x in s if x != test]
    return rest[:-1], [rest[-1]], [test]


def normalize_stats(X: np.ndarray):
    mean = X.reshape(-1, 6).mean(0)
    std = X.reshape(-1, 6).std(0) + 1e-6
    return mean, std


def train_once(takes, cfg: Config, train_s, val_s, test_s, rng, verbose=0):
    pick = lambda ss: [t for t in takes if t.subject in ss]
    Xtr, ytr, _, sk1 = make_windows(pick(train_s), cfg, rng, augment=True)
    Xva, yva, _, sk2 = make_windows(pick(val_s), cfg, rng, augment=False)
    Xte, yte, _, sk3 = make_windows(pick(test_s), cfg, rng, augment=False)
    mean, std = normalize_stats(Xtr)
    n = lambda X: (X - mean) / std
    Xtr, Xva, Xte = n(Xtr), n(Xva), n(Xte)

    import tensorflow as tf
    tf.keras.utils.set_random_seed(cfg.seed)
    model = build_model(cfg, len(cfg.classes))
    fit(model, Xtr, ytr, Xva, yva, cfg, verbose=verbose)
    return model, (Xtr, ytr), (Xte, yte), mean, std, sk1 + sk2 + sk3


def run_split(takes, cfg: Config, args, out_dir: Path):
    rng = np.random.default_rng(cfg.seed)
    sp = split_subjects([t.subject for t in takes], args.test_subject)
    if sp is None:
        raise SystemExit("need >= 3 subjects for a subject-wise split (train/val/test)")
    train_s, val_s, test_s = sp
    print(f"train={train_s} val={val_s} test={test_s}")
    model, (Xtr, _), (Xte, yte), mean, std, skipped = train_once(
        takes, cfg, train_s, val_s, test_s, rng, verbose=args.verbose)
    if skipped:
        print(f"warning: {skipped} gesture takes never crossed the trigger threshold and were skipped")

    fp32 = to_tflite(model, None)
    int8 = to_tflite(model, Xtr)
    p32 = tflite_predict(fp32, Xte).argmax(1)
    p8 = tflite_predict(int8, Xte).argmax(1)
    acc32 = float((p32 == yte).mean())
    acc8 = float((p8 == yte).mean())
    cm = confusion(yte, p8, len(cfg.classes))

    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / "model_fp32.tflite").write_bytes(fp32)
    (out_dir / "model_int8.tflite").write_bytes(int8)
    report = {
        "task": cfg.task, "model": cfg.model, "window_s": cfg.window_s, "hz": cfg.hz,
        "params": int(model.count_params()),
        "subjects": {"train": train_s, "val": val_s, "test": test_s},
        "n_test": int(len(yte)), "skipped_takes": skipped,
        "fp32": {"bytes": len(fp32), "acc": acc32},
        "int8": {"bytes": len(int8), "acc": acc8},
        "classes": cfg.classes, "confusion_int8": cm.tolist(),
    }
    write_model_h(out_dir / "model.h", int8, cfg, mean, std,
                  {k: report[k] for k in ("task", "model", "window_s", "hz")} | {"int8_acc": round(acc8, 4)})
    (out_dir / "report.json").write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding="utf-8")
    plot_confusion(cm, cfg.classes, out_dir / "confusion.png", f"{cfg.task} int8 (test={test_s[0]})")
    print(f"fp32 {len(fp32)/1024:.1f} KB acc={acc32:.3f} | int8 {len(int8)/1024:.1f} KB acc={acc8:.3f}")
    print(f"-> {out_dir}")
    return report


def run_loso(takes, cfg: Config, args, out_dir: Path):
    subjects = sorted({t.subject for t in takes})
    if len(subjects) < 3:
        raise SystemExit("need >= 3 subjects for LOSO")
    rows, total_cm = [], np.zeros((len(cfg.classes),) * 2, int)
    for test in subjects:
        rest = [s for s in subjects if s != test]
        rng = np.random.default_rng(cfg.seed)
        model, (Xtr, _), (Xte, yte), *_ = train_once(takes, cfg, rest[:-1], rest[-1:], [test], rng)
        int8 = to_tflite(model, Xtr)
        p = tflite_predict(int8, Xte).argmax(1)
        acc = float((p == yte).mean()) if len(yte) else float("nan")
        total_cm += confusion(yte, p, len(cfg.classes))
        rows.append({"subject": test, "n": int(len(yte)), "acc_int8": acc})
        print(f"  {test}: n={len(yte)} acc={acc:.3f}")
    accs = [r["acc_int8"] for r in rows]
    report = {"task": cfg.task, "model": cfg.model, "window_s": cfg.window_s, "hz": cfg.hz,
              "loso": rows, "mean": float(np.nanmean(accs)), "std": float(np.nanstd(accs)),
              "classes": cfg.classes, "confusion_int8": total_cm.tolist()}
    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / "loso.json").write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding="utf-8")
    plot_confusion(total_cm, cfg.classes, out_dir / "loso_confusion.png", f"{cfg.task} LOSO (all folds)")
    print(f"LOSO mean={report['mean']:.3f} ± {report['std']:.3f} -> {out_dir}")
    return report


def plot_confusion(cm, labels, path: Path, title: str):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    fig, ax = plt.subplots(figsize=(4.2, 3.8))
    norm = cm / np.maximum(cm.sum(1, keepdims=True), 1)
    ax.imshow(norm, cmap="Blues", vmin=0, vmax=1)
    for i in range(len(labels)):
        for j in range(len(labels)):
            ax.text(j, i, cm[i, j], ha="center", va="center",
                    color="white" if norm[i, j] > 0.6 else "black", fontsize=9)
    ax.set_xticks(range(len(labels)), labels, rotation=35, ha="right", fontsize=8)
    ax.set_yticks(range(len(labels)), labels, fontsize=8)
    ax.set_xlabel("predicted")
    ax.set_ylabel("true")
    ax.set_title(title, fontsize=9)
    fig.tight_layout()
    fig.savefig(path, dpi=150)
    plt.close(fig)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--data", type=Path, default=HERE.parent / "data")
    ap.add_argument("--task", choices=["attacker", "defender"], required=True)
    ap.add_argument("--window", type=float, default=1.0, help="seconds")
    ap.add_argument("--hz", type=int, default=100, choices=[25, 50, 100])
    ap.add_argument("--model", choices=["dense", "cnn"], default="cnn")
    ap.add_argument("--epochs", type=int, default=80)
    ap.add_argument("--acc-th", type=float, help="override trigger |a| threshold (g)")
    ap.add_argument("--gyro-th", type=float, help="override trigger |g| threshold (dps)")
    ap.add_argument("--test-subject")
    ap.add_argument("--loso", action="store_true", help="leave-one-subject-out CV")
    ap.add_argument("--out", type=Path)
    ap.add_argument("--verbose", type=int, default=0)
    args = ap.parse_args()

    cfg = Config(args.task, window_s=args.window, hz=args.hz, model=args.model, epochs=args.epochs,
                 acc_th=args.acc_th, gyro_th=args.gyro_th)
    takes = load_takes(args.data, cfg)
    if not takes:
        raise SystemExit(f"no takes under {args.data} for classes {cfg.classes}")
    per = {}
    for t in takes:
        per.setdefault(t.subject, [0] * len(cfg.classes))[t.cls] += 1
    print(f"{len(takes)} takes; per subject (count per class {cfg.classes}):")
    for s, c in sorted(per.items()):
        print(f"  {s}: {c}")

    out = args.out or HERE / "out" / args.task
    if args.loso:
        run_loso(takes, cfg, args, out)
    else:
        run_split(takes, cfg, args, out)


if __name__ == "__main__":
    main()
