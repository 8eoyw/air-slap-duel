"""Train an IMU gesture classifier and export int8 TFLite + model.h.

Data layout: data/<subject>/<class>/<take>.csv with columns t_ms,ax,ay,az,gx,gy,gz.
Skeleton — the TODOs are the work for weeks 3-4.
"""
import argparse
from pathlib import Path

import numpy as np
import pandas as pd

CLASSES = {
    "attacker": ["background", "forehand", "backhand", "feint"],
    "defender": ["background", "dodge_left", "dodge_right", "block"],
}
CHANNELS = ["ax", "ay", "az", "gx", "gy", "gz"]
SAMPLE_HZ = 100


def load(data_dir: Path, task: str):
    """Return X (n, T, 6), y (n,), subjects (n,) from raw takes."""
    X, y, subj = [], [], []
    for csv in sorted(data_dir.glob("*/*/*.csv")):
        cls = csv.parent.name
        if cls not in CLASSES[task]:
            continue
        df = pd.read_csv(csv, comment="#")
        X.append(df[CHANNELS].to_numpy(np.float32))
        y.append(CLASSES[task].index(cls))
        subj.append(csv.parent.parent.name)
    return X, np.array(y), np.array(subj)


def make_windows(takes, labels, window_s: float):
    """TODO: center a fixed-length window on the |a| peak of each take;
    slice background takes into many windows."""
    raise NotImplementedError


def build_model(kind: str, T: int, n_cls: int):
    import tensorflow as tf
    inp = tf.keras.Input((T, len(CHANNELS)))
    if kind == "dense":
        x = tf.keras.layers.Flatten()(inp)
        x = tf.keras.layers.Dense(32, activation="relu")(x)
        x = tf.keras.layers.Dense(16, activation="relu")(x)
    else:
        x = tf.keras.layers.Conv1D(16, 5, activation="relu")(inp)
        x = tf.keras.layers.MaxPool1D(2)(x)
        x = tf.keras.layers.Conv1D(32, 5, activation="relu")(x)
        x = tf.keras.layers.GlobalAveragePooling1D()(x)
    out = tf.keras.layers.Dense(n_cls, activation="softmax")(x)
    m = tf.keras.Model(inp, out)
    m.compile("adam", "sparse_categorical_crossentropy", metrics=["accuracy"])
    return m


def export(model, rep_data, out_dir: Path):
    """TODO: FP32 + int8 (representative dataset) TFLite, then xxd-style model.h
    with normalization mean/std and class names. Record sizes in report.json."""
    raise NotImplementedError


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--data", type=Path, default=Path("../data"))
    ap.add_argument("--task", choices=CLASSES, required=True)
    ap.add_argument("--window", type=float, default=1.0)
    ap.add_argument("--model", choices=["dense", "cnn"], default="cnn")
    ap.add_argument("--loso", action="store_true", help="leave-one-subject-out CV")
    args = ap.parse_args()

    takes, labels, subjects = load(args.data, args.task)
    print(f"{len(takes)} takes, subjects={sorted(set(subjects))}")
    # TODO: windows -> subject-wise split (or LOSO loop) -> augment -> fit -> export


if __name__ == "__main__":
    main()
