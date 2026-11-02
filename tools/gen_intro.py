"""The opening: a short story told in stills, generated the same way as the character poses.

    python tools/gen_intro.py gen                 # missing shots, in story order
    python tools/gen_intro.py gen --only table
    python tools/gen_intro.py build               # build/char_gen/intro/*.jpg -> tools/web/intro/*.webp
    python tools/gen_intro.py tts                 # narration from tools/web/intro/story.json -> tools/web/intro/say/*.mp3

The first shot sets the room; it is attached to every later shot so the set and the lighting stay the same.
What is written on screen is drawn by the page, never by the image model: it cannot be trusted with Chinese text.
"""
import argparse
import json
import shutil
import subprocess
import sys
import threading
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

from PIL import Image, ImageOps

sys.path.insert(0, str(Path(__file__).resolve().parent))
from gen_chars import BRAIN, CHARS, MODEL, WORK

ROOT = WORK.parent.parent
RAW = WORK / "intro"
OUT = ROOT / "tools" / "web" / "intro"
SIZE = (1920, 1080)
VOICE = "zh-CN-YunyangNeural"   # a news anchor reading this straight is most of the joke

NAMES = {"naiwa": "the frog-like Naiwa", "naicha": "the living mug Naicha", "dounai": "the small doll Dounai", "naidan": "the big egg Naidan"}

# id: who is in frame, what happens. Caption is what the page shows while the shot holds.
SHOTS = {
    "table": (["naiwa", "naicha", "dounai", "naidan"],
              "Wide establishing shot of a cosy small dining room at night, warm pendant lamp over a round wooden table, dark window, "
              "patterned wallpaper. All four characters sit around the table facing each other in silence. In the exact centre of the "
              "table, one single small caramel pudding on a white saucer, lit by the lamp."),
    "pudding": ([],
                "Extreme close-up of the single caramel pudding on its white saucer in the centre of the wooden table. It glistens and "
                "glows under the lamp like a sacred treasure. Dark blurred background."),
    "hands": (["naiwa", "naicha"],
              "Tense close-up over the table: Naiwa's olive-brown four-finger hand enters from the left and Naicha's grey-brown "
              "four-finger hand enters from the right, both frozen in mid-air a few centimetres from the pudding between them. "
              "We see only the hands and the pudding."),
    "rule": (["naidan"],
             "Naidan stands in the dining room, stern, one thin arm pointing up at a large hanging paper scroll on the wallpaper. "
             "The scroll is completely blank cream paper with a dark wooden rod. Naidan is on the left, the scroll fills the right half."),
    "stance": (["naiwa", "naicha"],
               "Western gunfight standoff in the dining room: Naiwa stands at the far left, Naicha at the far right, two metres of empty "
               "floor between them, facing each other in profile, shot low and wide, the lamp hanging between them. Dead serious."),
    "air": (["naiwa"],
            "Naiwa alone, side view facing screen right, mid-swing, one open palm slicing through the empty air, far from anyone. "
            "A faint transparent ring of distorted air flies off the palm toward screen right."),
    "recoil": (["naicha"],
               "Naicha alone on the right side of the frame, violently recoiling toward screen right as if slapped by an invisible hand, "
               "the mug's face squashed sideways, eyes huge, tea leaves and droplets flung out of the top. Nobody is near it. A faint "
               "transparent ring of distorted air arrives from screen left."),
    "silence": (["dounai", "naidan"],
                "Low angle across the table: Dounai and Naidan frozen in their chairs, eyes wide open, mouths shut, staring at something "
                "off screen to the left. A spoon has just slipped out of Dounai's hand and hangs in mid-air."),
    "laugh": (["naidan"],
              "Close-up of Naidan at the table losing the battle not to laugh: eyes squeezed, cheeks puffed, lips clamped and trembling, "
              "one tear escaping, the towel still neatly on its head."),
    "arena": (["naiwa", "naicha", "dounai", "naidan"],
              "The dining room turned into a fight venue: table pushed against the wall, the pendant lamp pulled low like a boxing ring "
              "light over bare floor. Naiwa and Naicha face each other under the light, arms raised, ready to slap the air. Dounai and "
              "Naidan watch from two chairs as judges. Epic, wide, symmetrical."),
    "empty": (["naiwa", "naicha"],
              "The white saucer sits on the table in the lower middle of the frame, whole and fully visible with a margin below it, now empty "
              "except for a smear of caramel. Behind it, slightly soft, exactly two characters stare down at it in shock: Naiwa on the left and "
              "the living mug Naicha on the right. Nobody else is in the shot; the small doll from the reference still is not here."),
    "hall": (["dounai", "naidan"],
             "A dark narrow hallway outside a bathroom, not the dining room, night. Steam curls from a half-open door. Dounai on the left "
             "holds a torch whose beam lands on Naidan, standing stiffly in the doorway on the right with the towel on its head, avoiding "
             "eye contact. Detective film lighting."),
    "roof": ([],
             "An empty flat rooftop of a small house at night, not the dining room. Full moon, laundry lines moving in the wind, a single "
             "bare bulb on a pole lighting a circle of concrete. Nobody in frame. Lonely, grand, the place for a final duel."),
    "towel": (["naidan"],
              "Close-up of Naidan from the front against a plain dark wall; there is no lamp and nothing else above its head. Its folded white "
              "towel has slid off and hangs from one side, revealing a whole caramel pudding sitting on the very top of its smooth head, clearly "
              "visible, brown caramel on top, the same pudding as in the references, the brightest thing in the frame. Naidan is frozen, eyes "
              "sliding sideways, one drop of sweat on its face."),
    "family": (["naiwa", "naicha", "dounai", "naidan"],
               "All four characters at the dining table collapsing in helpless laughter, heads thrown back, mouths wide open, tears flying, "
               "one nearly falling off a chair, the pendant lamp swinging. Warm and joyful."),
    "win_naiwa": (["naiwa"],
                  "Naiwa alone at the dining table under the lamp, holding a tiny spoon with a piece of caramel pudding, the pudding on its "
                  "saucer in front of it, deadpan, one small tear of joy, a champion's silence."),
    "win_naicha": (["naicha"],
                   "Naicha alone at the dining table under the lamp, holding a tiny spoon with a piece of caramel pudding, the pudding on its "
                   "saucer in front of it, smug half smile, steam curling from the tea leaves."),
    "win_dounai": (["dounai"],
                   "Dounai alone at the dining table under the lamp, holding a tiny spoon with a piece of caramel pudding, the pudding on its "
                   "saucer in front of it, eyes closed in bliss, composed like a queen."),
    "win_naidan": (["naidan"],
                   "Naidan alone at the dining table under the lamp, holding a tiny spoon with a piece of caramel pudding, the pudding on its "
                   "saucer in front of it, towel freshly folded on its head, serene and guilty at once."),
}

PROMPT = """Do not use run_command. Use invoke_subagent with the image-generator subagent exactly once, attaching {refs} from the current folder as reference images. Generate ONE image named {name}.
It must be a wide 16:9 landscape image, wider than it is tall.
The references are the cast and a still from the same film; keep its look. Characters who appear are the EXACT same characters, not redesigns:
{cast}
Scene: {scene}
Polished soft 3D CGI animated film still, cinematic composition, shallow depth of field, warm dramatic lamp light. Play it completely straight, like a serious film.
No text, no letters, no subtitles, no logos anywhere in the image.
Finally print the absolute path of the image."""


out_of_quota = threading.Event()


def collect(name):
    hits = [p for p in BRAIN.glob(f"*/{name}*.jpg") if time.time() - p.stat().st_mtime < 1800]
    return max(hits, key=lambda p: p.stat().st_mtime) if hits else None


def ref_for(char):
    idle = WORK / "raw" / f"{char}_idle.jpg"
    return f"raw/{idle.name}" if idle.exists() else f"refs/{CHARS[char][0][0]}"


def generate(shot):
    name = f"intro_{shot}"
    raw = RAW / f"{shot}.jpg"
    if raw.exists():
        return name, "skip"
    cast, scene = SHOTS[shot]
    files = [ref_for(c) for c in cast]
    first = RAW / f"{next(iter(SHOTS))}.jpg"
    if first.exists():
        files.append(f"intro/{first.name}")
    who = "\n".join(f"- {NAMES[c]}: {CHARS[c][1]}" for c in cast)
    prompt = PROMPT.format(refs=" and ".join(files), name=name, cast=who, scene=scene)
    agy = shutil.which("agy")
    for attempt in range(2):
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
            return name, "ok"
        if "RESOURCE_EXHAUSTED" in text or "429" in text:
            out_of_quota.set()
            return name, "OUT OF QUOTA"
    return name, "FAILED"


def publish(shot):
    ImageOps.fit(Image.open(RAW / f"{shot}.jpg").convert("RGB"), SIZE, Image.LANCZOS).save(OUT / f"{shot}.webp", quality=88, method=6)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("cmd", choices=["gen", "build", "tts"])
    ap.add_argument("--only", nargs="*", default=list(SHOTS))
    ap.add_argument("--workers", type=int, default=3)
    a = ap.parse_args()
    RAW.mkdir(parents=True, exist_ok=True)
    if a.cmd == "gen":
        with ThreadPoolExecutor(a.workers) as ex:
            for name, status in ex.map(generate, a.only):
                print(name, status, flush=True)
        return
    OUT.mkdir(parents=True, exist_ok=True)
    if a.cmd == "tts":
        (OUT / "say").mkdir(exist_ok=True)
        for scene, beats in json.loads((OUT / "story.json").read_text(encoding="utf8"))["scenes"].items():
            for n, b in enumerate(beats):
                subprocess.run([shutil.which("uvx"), "--quiet", "edge-tts", "--voice", VOICE, "--rate=-8%", "--text", b["say"],
                                "--write-media", str(OUT / "say" / f"{scene}_{n}.mp3")], check=True)
                print(scene, n, flush=True)
        return
    for shot in SHOTS:
        if (RAW / f"{shot}.jpg").exists():
            publish(shot)


if __name__ == "__main__":
    sys.exit(main())
