"""Shared pieces of the IMU pipeline: loading, windowing (must match the
firmware trigger in firmware/libraries/SlapCommon/src/slap_node.h), models, export."""
from __future__ import annotations

import json
from dataclasses import dataclass, field, asdict
from pathlib import Path

import numpy as np
import pandas as pd

CHANNELS = ["ax", "ay", "az", "gx", "gy", "gz"]
RAW_HZ = 100

CLASSES = {
    "attacker": ["background", "forehand", "backhand", "feint"],
    "defender": ["background", "dodge_left", "dodge_right", "block"],
}
# Model output index -> SlapClass id in slap_protocol.h
SLAP_IDS = {
    "attacker": [0, 1, 2, 3],
    "defender": [0, 10, 11, 12],
}
# Must match the defaults in nano_attacker.ino / nano_defender.ino; they are
# exported into model.h so device and training always agree.
TRIGGERS = {
    "attacker": (2.5, 600.0),
    "defender": (1.6, 250.0),
}


@dataclass
class Config:
    task: str
    window_s: float = 1.0
    hz: int = 100
    pre_frac: float = 0.5
    acc_th: float | None = None
    gyro_th: float | None = None
    cooldown_s: float = 0.4
    model: str = "cnn"
    epochs: int = 80
    seed: int = 0
    augment: int = 4           # extra jittered copies per training window
    shift_s: float = 0.1       # max trigger jitter for augmentation

    def __post_init__(self):
        a, g = TRIGGERS[self.task]
        self.acc_th = a if self.acc_th is None else self.acc_th
        self.gyro_th = g if self.gyro_th is None else self.gyro_th

    @property
    def T(self) -> int:
        return int(round(self.window_s * self.hz))

    @property
    def pre(self) -> int:
        return int(round(self.T * self.pre_frac))

    @property
    def classes(self) -> list[str]:
        return CLASSES[self.task]


@dataclass
class Take:
    subject: str
    cls: int
    x: np.ndarray  # (n, 6) at cfg.hz
    path: str


def load_takes(data_dir: Path, cfg: Config) -> list[Take]:
    takes = []
    step = RAW_HZ // cfg.hz
    if RAW_HZ % cfg.hz:
        raise ValueError(f"hz must divide {RAW_HZ}")
    for csv in sorted(Path(data_dir).glob("*/*/*.csv")):
        name = csv.parent.name
        if name not in cfg.classes:
            continue
        df = pd.read_csv(csv, comment="#")
        if len(df) < 10:
            continue
        x = df[CHANNELS].to_numpy(np.float32)[::step]
        takes.append(Take(csv.parent.parent.name, cfg.classes.index(name), x, str(csv)))
    return takes


def trigger_mask(x: np.ndarray, cfg: Config) -> np.ndarray:
    a = np.linalg.norm(x[:, :3], axis=1)
    g = np.linalg.norm(x[:, 3:], axis=1)
    return (a > cfg.acc_th) | (g > cfg.gyro_th)


def trigger_points(x: np.ndarray, cfg: Config) -> list[int]:
    """Replay the firmware trigger: first crossing, then cooldown, repeat."""
    mask = trigger_mask(x, cfg)
    cool = int(cfg.cooldown_s * cfg.hz)
    pts, i = [], 0
    while i < len(x):
        if mask[i]:
            pts.append(i)
            i += max(cool, cfg.T - cfg.pre)  # firmware also waits for the window to fill
        else:
            i += 1
    return pts


def cut(x: np.ndarray, trig: int, cfg: Config) -> np.ndarray:
    start = trig - cfg.pre
    idx = np.clip(np.arange(start, start + cfg.T), 0, len(x) - 1)  # edge-pad
    return x[idx]


def make_windows(takes: list[Take], cfg: Config, rng: np.random.Generator, augment: bool):
    """Gesture takes -> one window at their first trigger.
    Background takes -> a window at every trigger the firmware would fire."""
    X, y, subj, skipped = [], [], [], 0
    shift = int(cfg.shift_s * cfg.hz)
    for t in takes:
        pts = trigger_points(t.x, cfg)
        if t.cls != 0:
            if not pts:
                skipped += 1
                continue
            pts = pts[:1]
        for p in pts:
            reps = [0] + (list(rng.integers(-shift, shift + 1, cfg.augment)) if augment else [])
            for s in reps:
                w = cut(t.x, p + int(s), cfg)
                if augment and s != 0:
                    w = w * rng.uniform(0.8, 1.2) + rng.normal(0, 0.02, w.shape).astype(np.float32) * w.std(0)
                X.append(w.astype(np.float32))
                y.append(t.cls)
                subj.append(t.subject)
    if not X:
        return np.zeros((0, cfg.T, 6), np.float32), np.zeros(0, int), np.zeros(0, str), skipped
    return np.stack(X), np.array(y), np.array(subj), skipped


def build_model(cfg: Config, n_cls: int):
    import tensorflow as tf
    inp = tf.keras.Input((cfg.T, len(CHANNELS)))
    if cfg.model == "dense":
        x = tf.keras.layers.Flatten()(inp)
        x = tf.keras.layers.Dense(32, activation="relu")(x)
        x = tf.keras.layers.Dropout(0.2)(x)
        x = tf.keras.layers.Dense(16, activation="relu")(x)
    else:
        x = tf.keras.layers.Conv1D(16, 5, activation="relu")(inp)
        x = tf.keras.layers.MaxPooling1D(2)(x)
        x = tf.keras.layers.Conv1D(32, 5, activation="relu")(x)
        x = tf.keras.layers.MaxPooling1D(2)(x)
        x = tf.keras.layers.Conv1D(32, 3, activation="relu")(x)
        x = tf.keras.layers.GlobalAveragePooling1D()(x)
        x = tf.keras.layers.Dropout(0.2)(x)
    out = tf.keras.layers.Dense(n_cls, activation="softmax")(x)
    m = tf.keras.Model(inp, out)
    m.compile(tf.keras.optimizers.Adam(2e-3), "sparse_categorical_crossentropy", metrics=["accuracy"])
    return m


def fit(model, Xtr, ytr, Xva, yva, cfg: Config, verbose=0):
    import tensorflow as tf
    counts = np.bincount(ytr, minlength=len(cfg.classes)).astype(float)
    weights = {i: (len(ytr) / (len(counts) * c)) if c else 0.0 for i, c in enumerate(counts)}
    cb = [tf.keras.callbacks.EarlyStopping("val_loss", patience=12, restore_best_weights=True)]
    return model.fit(Xtr, ytr, validation_data=(Xva, yva), epochs=cfg.epochs, batch_size=32,
                     class_weight=weights, callbacks=cb, verbose=verbose)


def to_tflite(model, rep: np.ndarray | None) -> bytes:
    """rep=None -> float32 model; otherwise full-integer int8 with int8 I/O."""
    import tensorflow as tf
    conv = tf.lite.TFLiteConverter.from_keras_model(model)
    if rep is not None:
        conv.optimizations = [tf.lite.Optimize.DEFAULT]

        def gen():
            for i in range(min(len(rep), 300)):
                yield [rep[i:i + 1]]
        conv.representative_dataset = gen
        conv.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS_INT8]
        conv.inference_input_type = tf.int8
        conv.inference_output_type = tf.int8
    return conv.convert()


def tflite_predict(blob: bytes, X: np.ndarray) -> np.ndarray:
    import tensorflow as tf
    it = tf.lite.Interpreter(model_content=blob)
    it.allocate_tensors()
    i, o = it.get_input_details()[0], it.get_output_details()[0]
    preds = []
    for x in X:
        x = x[None]
        if i["dtype"] == np.int8:
            s, z = i["quantization"]
            x = np.clip(np.round(x / s + z), -128, 127).astype(np.int8)
        it.set_tensor(i["index"], x)
        it.invoke()
        p = it.get_tensor(o["index"])[0]
        if o["dtype"] == np.int8:
            s, z = o["quantization"]
            p = (p.astype(np.float32) - z) * s
        preds.append(p)
    return np.array(preds)


def write_model_h(path: Path, blob: bytes, cfg: Config, mean, std, meta: dict):
    lines = [
        "// Generated by ml/train.py — do not edit.",
        f"// {json.dumps(meta, ensure_ascii=False)}",
        "#pragma once",
        "#include <stdint.h>",
        f"#define SLAP_MODEL_TASK \"{cfg.task}\"",
        f"#define SAMPLE_HZ {cfg.hz}",
        f"#define SLAP_WINDOW {cfg.T}",
        f"#define SLAP_PRE {cfg.pre}",
        f"#define SLAP_ACC_TRIGGER_G {cfg.acc_th:.3f}f",
        f"#define SLAP_GYRO_TRIGGER_DPS {cfg.gyro_th:.1f}f",
        f"#define SLAP_N_CLASSES {len(cfg.classes)}",
        "static const uint8_t SLAP_CLASS_IDS[SLAP_N_CLASSES] = {"
        + ", ".join(str(v) for v in SLAP_IDS[cfg.task]) + "};  // "
        + ", ".join(cfg.classes),
        "static const float SLAP_MEAN[6] = {" + ", ".join(f"{v:.6f}f" for v in mean) + "};",
        "static const float SLAP_STD[6] = {" + ", ".join(f"{v:.6f}f" for v in std) + "};",
        f"static const unsigned int g_model_len = {len(blob)};",
        "alignas(16) static const unsigned char g_model[] = {",
    ]
    for k in range(0, len(blob), 16):
        lines.append("  " + ", ".join(f"0x{b:02x}" for b in blob[k:k + 16]) + ",")
    lines.append("};")
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def confusion(y_true, y_pred, n):
    m = np.zeros((n, n), int)
    for t, p in zip(y_true, y_pred):
        m[t, p] += 1
    return m
