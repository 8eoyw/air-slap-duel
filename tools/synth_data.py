"""Generate SYNTHETIC IMU takes in the real data layout, for testing the
pipeline before anyone has recorded data. Never mix this into data/.

    python tools/synth_data.py --out data_synth --subjects 5 --takes 40

Shapes are rough caricatures: a slap is a one-sided gyro burst with an accel
spike, a feint starts like a slap and reverses, dodges are lateral accel pulses.
"""
import argparse
from pathlib import Path

import numpy as np

HZ = 100


def pulse(n, center, width, amp):
    t = np.arange(n)
    return amp * np.exp(-0.5 * ((t - center) / width) ** 2)


def base(n, rng):
    x = np.zeros((n, 6), np.float32)
    x[:, 2] = 1.0  # gravity on z
    x[:, :3] += rng.normal(0, 0.02, (n, 3))
    x[:, 3:] += rng.normal(0, 3, (n, 3))
    return x


def gesture(cls, n, rng, subj):
    x = base(n, rng)
    c = rng.integers(int(0.9 * HZ), int(1.6 * HZ))
    speed = subj["speed"] * rng.uniform(0.85, 1.15)
    w = 6 / speed
    s = subj["strength"] * rng.uniform(0.7, 1.4)
    if cls in ("forehand", "backhand"):
        sign = 1 if cls == "forehand" else -1
        x[:, 5] += sign * pulse(n, c, w, 900 * s)
        x[:, 4] += pulse(n, c, w * 1.3, 200 * s) * subj["tilt"]
        x[:, 0] += sign * pulse(n, c + 2, w * 0.7, 3.0 * s)
        x[:, 1] += pulse(n, c + 4, w * 0.5, 1.5 * s)
    elif cls == "feint":
        x[:, 5] += pulse(n, c, w, 700 * s) - pulse(n, c + 3 * w, w, 600 * s)
        x[:, 0] += pulse(n, c, w * 0.7, 1.8 * s) - pulse(n, c + 3 * w, w, 1.5 * s)
    elif cls in ("dodge_left", "dodge_right"):
        sign = -1 if cls == "dodge_left" else 1
        x[:, 1] += sign * pulse(n, c, w * 2, 1.2 * s)
        x[:, 3] += sign * pulse(n, c, w * 2, 300 * s)
    elif cls == "block":
        x[:, 4] += pulse(n, c, w * 1.5, 350 * s)
        x[:, 2] += pulse(n, c, w, 0.9 * s)
    return x


def background(n, rng):
    x = base(n, rng)
    for _ in range(rng.integers(n // HZ // 2, n // HZ * 2)):
        c = rng.integers(0, n)
        ax = rng.integers(0, 6)
        amp = rng.uniform(0.5, 2.2) if ax < 3 else rng.uniform(100, 750)
        x[:, ax] += pulse(n, c, rng.uniform(4, 25), amp * rng.choice([-1, 1]))
    return x


def rotate(x, rng, deg):
    # Small strap-orientation difference per subject, applied to both sensors.
    a = np.deg2rad(rng.normal(0, deg, 3))
    cx, cy, cz = np.cos(a)
    sx, sy, sz = np.sin(a)
    R = (np.array([[1, 0, 0], [0, cx, -sx], [0, sx, cx]])
         @ np.array([[cy, 0, sy], [0, 1, 0], [-sy, 0, cy]])
         @ np.array([[cz, -sz, 0], [sz, cz, 0], [0, 0, 1]]))
    return np.hstack([x[:, :3] @ R.T, x[:, 3:] @ R.T]).astype(np.float32)


def save(path: Path, x):
    path.parent.mkdir(parents=True, exist_ok=True)
    t = np.arange(len(x)) * (1000 // HZ)
    with open(path, "w") as f:
        f.write("t_ms,ax,ay,az,gx,gy,gz\n")
        for ti, r in zip(t, x):
            f.write(f"{ti},{r[0]:.3f},{r[1]:.3f},{r[2]:.3f},{r[3]:.1f},{r[4]:.1f},{r[5]:.1f}\n")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", type=Path, default=Path(__file__).parent.parent / "data_synth")
    ap.add_argument("--subjects", type=int, default=5)
    ap.add_argument("--takes", type=int, default=40, help="per gesture class per subject")
    ap.add_argument("--bg-seconds", type=int, default=120, help="background per subject")
    ap.add_argument("--seed", type=int, default=0)
    args = ap.parse_args()
    rng = np.random.default_rng(args.seed)

    gestures = ["forehand", "backhand", "feint", "dodge_left", "dodge_right", "block"]
    for si in range(args.subjects):
        sid = f"syn{si + 1:02d}"
        subj = {"speed": rng.uniform(0.8, 1.25), "strength": rng.uniform(0.8, 1.2),
                "tilt": rng.uniform(-1, 1), "rot": rng.uniform(5, 15)}
        R_rng = np.random.default_rng(args.seed * 100 + si)
        for g in gestures:
            for k in range(args.takes):
                x = rotate(gesture(g, 3 * HZ, rng, subj), R_rng, subj["rot"] / 3)
                save(args.out / sid / g / f"{k:03d}.csv", x)
        for k in range(args.bg_seconds // 30):
            save(args.out / sid / "background" / f"{k:03d}.csv", background(30 * HZ, rng))
    print(f"synthetic data -> {args.out}")


if __name__ == "__main__":
    main()
