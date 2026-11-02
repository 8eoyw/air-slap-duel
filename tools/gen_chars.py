"""Character art for the match screen: key poses generated from reference images, keyed to transparency.

    python tools/gen_chars.py gen                      # missing poses, every character
    python tools/gen_chars.py gen --only naidan --poses idle windup
    python tools/gen_chars.py key                      # build/char_gen/raw/*.jpg -> tools/web/chars/<id>/<pose>.webp

Images come from the image-generator subagent of the Antigravity CLI (`agy`), which accepts reference
files. A text prompt alone does not reproduce these characters: the identity comes from the attached
references in build/char_gen/refs, the prompt only pins what drifts. Once a character has an `idle`,
that image is attached too, so every pose starts from the same model.

Poses are shot on flat magenta because a cream belly next to a white background cannot be separated
cleanly; alpha is the distance from the key colour, and edge pixels are unmixed from it.
All poses face screen right; the page mirrors them for P2.
"""
import argparse
import re
import shutil
import subprocess
import json
import sys
import threading
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

import numpy as np
from PIL import Image
from scipy import ndimage as ni

ROOT = Path(__file__).resolve().parent.parent
WORK = ROOT / "build" / "char_gen"
OUT = ROOT / "tools" / "web" / "chars"
BRAIN = Path.home() / ".gemini" / "antigravity-cli" / "brain"
MODEL = "gemini-3.8-flash-high"
KEY_HEIGHT = 900

# id: reference files in build/char_gen/refs, and the traits that drift when they are not spelled out
CHARS = {
    "naiwa": (["naiwa.png", "naiwa_laugh.png"],
              "golden-yellow pear-shaped soft 3D creature, seamless neckless head and torso; two VERY LARGE bulging round "
              "eyes protruding from the top of the head, each with a wide pale grey-green ring around a big black pupil, no "
              "eyelids, no eyebrows; thin straight horizontal resting mouth; huge cream belly patch; olive-brown four-finger "
              "hands and feet; exactly two arms and two legs"),
    "naicha": (["naicha.png"],
               "living orange-yellow ceramic tea mug with a C-shaped handle on one side, dark green tea leaves visible at the "
               "open top, a face on the front of the mug, soft cream belly area, grey-brown four-finger hands, short legs with "
               "dark paws; exactly two arms and two legs"),
    "dounai": (["dounai.png"],
               "small stylised 3D doll character with a golden-tan rounded body, big glossy brown eyes, smooth brown hair parted "
               "at the middle, cream belly patch, short stubby limbs, like a soft vinyl toy; exactly two arms and two legs"),
    "naidan": (["naidan.png", "naidan2.png"],
               "large golden-yellow egg-shaped creature, smooth body with no neck, two small round eyes with a green ring and "
               "black pupil set wide apart, small thin smile, a folded white towel resting on top of its head; thin short golden "
               "arms with small hands and two short legs (add them even though the references hide them)"),
}

# in generation order: the ones the match cannot do without come first
POSES = {
    "idle":   "standing still in three-quarter view facing screen right, arms relaxed at the sides, calm deadpan expression",
    "hit":    "three-quarter view, whole body and head violently recoiling toward screen left from an invisible slap coming from the right, one cheek squashed in, eyes huge with shock, mouth stretched sideways, both arms flung out",
    "laugh":  "front view, head thrown back, eyes squeezed shut, mouth wide open showing teeth, both hands clutching the belly, helpless with laughter",
    "windup": "three-quarter view facing screen right, leaning back, one arm cocked far back behind the head with the open palm ready to slap, the other arm forward for balance, focused",
    "slap":   "side view facing screen right, lunging far forward mid-swing, one arm fully extended to the right with the open palm leading, the other arm trailing behind, body stretched by the speed",
    "block":  "three-quarter view facing screen right, crouched, both forearms raised crossed in front of the face as a guard, palms outward, eyes squeezed shut, bracing",
    "dodge":  "three-quarter view facing screen right, leaning sharply backward and away to screen left in a limbo-like dodge, arms out for balance, wide surprised eyes",
    "win":    "three-quarter view facing screen right, standing tall and smug, one arm raised in victory, chin up, calm satisfied half smile",
    "dizzy":  "front view, wobbling and tilted to one side, eyes rolling in different directions, tongue slightly out, arms hanging limp, confused",
    "laugh2": "front view, doubled over forward with laughter, knees buckling, one hand on the belly and the other pounding the air, mouth wide open, tears of laughter flying",
}

PROMPT = """Do not use run_command. Use invoke_subagent with the image-generator subagent exactly once, attaching {refs} from the current folder as reference images. Generate ONE image named {name}.
It must be the EXACT same character as the references, not a redesign: {identity}.
Pose: {pose}.
The whole body must fit inside the frame with a wide margin on every side, and nothing may be cropped.
Polished soft 3D CGI render with soft even neutral-white studio lighting and no coloured reflected light on the character.
The background must be one completely flat solid pure magenta colour (#FF00FF), edge to edge, with no gradient, no floor, no shadow, no vignette, no text.
Finally print the absolute path of the image."""


out_of_quota = threading.Event()


def collect(name):
    hits = [p for p in BRAIN.glob(f"*/{name}*.jpg") if time.time() - p.stat().st_mtime < 1800]
    return max(hits, key=lambda p: p.stat().st_mtime) if hits else None


def generate(job):
    char, pose = job
    name = f"{char}_{pose}"
    raw = WORK / "raw" / f"{name}.jpg"
    if raw.exists():
        return name, "skip"
    refs, identity = CHARS[char]
    files = [f"refs/{r}" for r in refs]
    base = WORK / "raw" / f"{char}_idle.jpg"
    if pose != "idle" and base.exists():
        files.append(f"raw/{base.name}")
    prompt = PROMPT.format(refs=" and ".join(files), name=name, identity=identity, pose=POSES[pose])
    agy = shutil.which("agy")
    for attempt in range(3):
        if out_of_quota.is_set():
            return name, "waiting for quota"
        text = ""
        try:
            r = subprocess.run([agy, "--model", MODEL, "-p", prompt, "--mode", "accept-edits", "--print-timeout", "240s"],
                               cwd=WORK, capture_output=True, timeout=270)
            text = r.stdout.decode("utf8", "replace")
        except subprocess.TimeoutExpired:
            pass
        made = collect(name)
        if made:
            shutil.copy(made, raw)
            return name, "ok" if attempt == 0 else f"ok (retry {attempt})"
        if "RESOURCE_EXHAUSTED" in text or "429" in text:
            out_of_quota.set()      # about 20 images per window; rerun later, finished poses are skipped
            return name, "OUT OF QUOTA: " + " ".join(re.findall(r"[^\n]*[Rr]eset[^\n]*", text))[:120]
    return name, "FAILED"


def key(path):
    a = np.asarray(Image.open(path).convert("RGB")).astype(float)
    border = np.r_[a[:12].reshape(-1, 3), a[-12:].reshape(-1, 3), a[:, :12].reshape(-1, 3), a[:, -12:].reshape(-1, 3)]
    K = np.median(border, axis=0)
    mag = np.minimum(a[..., 0], a[..., 2]) - a[..., 1]    # R and B above G: survives shading painted into the backdrop
    t = np.clip(mag / max(min(K[0], K[2]) - K[1], 1), 0, 1)
    al = 1 - np.clip((t - .10) / .62, 0, 1)
    solid = al > .5
    lab, n = ni.label(solid)
    if n > 1:
        sizes = ni.sum(solid, lab, range(1, n + 1))
        keep = np.isin(lab, 1 + np.where(sizes > sizes.max() * .004)[0])   # tears and tea leaves stay, specks go
        al = al * ni.binary_dilation(keep, iterations=3)
    al = ni.gaussian_filter(al, .6)
    al = np.clip((al - .04) / .92, 0, 1)
    A = al[..., None]
    F = np.where(A > .02, (a - (1 - A) * K) / np.maximum(A, .02), a)     # unmix: F = (C - (1-a)K) / a
    F[..., 2] = np.minimum(F[..., 2], F[..., 1] * 1.02)                  # magenta bounce light on the body
    rim = (al < .995) & (al > 0)
    F[..., 0] = np.where(rim, np.minimum(F[..., 0], F[..., 1] * 1.45 + 12), F[..., 0])
    im = Image.fromarray(np.dstack([np.clip(F, 0, 255), al * 255]).astype("uint8"))
    return im.crop(Image.fromarray(((al > .03) * 255).astype("uint8")).getbbox())


def publish(c, p):
    raw = WORK / "raw" / f"{c}_{p}.jpg"
    top = np.asarray(Image.open(raw).convert("RGB"))[:12].reshape(-1, 3).mean(0)
    if min(top[0], top[2]) - top[1] < 80:      # R and B well above G; a dark image has low G too
        raw.unlink()
        return "not on magenta, regenerate it"
    im = key(raw)
    if im.height > KEY_HEIGHT:
        im = im.resize((round(im.width * KEY_HEIGHT / im.height), KEY_HEIGHT), Image.LANCZOS)
    (OUT / c).mkdir(parents=True, exist_ok=True)
    im.save(OUT / c / f"{p}.webp", quality=93, method=6)
    return im.size


def manifest():
    # The page reads which poses exist, so a new pose shows up without touching index.html.
    made = {c: {f.stem: Image.open(f).size for f in sorted((OUT / c).glob("*.webp"))} for c in CHARS if (OUT / c).is_dir()}
    (OUT / "manifest.json").write_text(json.dumps(made), encoding="utf8")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("cmd", choices=["gen", "key"])
    ap.add_argument("--only", nargs="*", default=list(CHARS))
    ap.add_argument("--poses", nargs="*", default=list(POSES))
    ap.add_argument("--workers", type=int, default=4)
    a = ap.parse_args()
    (WORK / "raw").mkdir(parents=True, exist_ok=True)
    if a.cmd == "gen":
        # every character's idle first, then pose by pose, so a quota cut leaves the roster evenly covered
        jobs = [(c, p) for p in a.poses for c in a.only]
        with ThreadPoolExecutor(a.workers) as ex:
            for name, status in ex.map(generate, jobs):
                print(name, status, flush=True)
        return
    for c in a.only:
        for p in a.poses:
            raw = WORK / "raw" / f"{c}_{p}.jpg"
            if raw.exists():
                print(c, p, publish(c, p), flush=True)
    manifest()


if __name__ == "__main__":
    sys.exit(main())
