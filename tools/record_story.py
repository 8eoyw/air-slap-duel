"""Screen-records the story mode without its matches (opening film, chapter films, stand-offs, all four endings) and
the cast screen with its records, then joins it into one showcase mp4, sound included.

    python tools/web_dashboard.py --keys --http-port 8779 --no-browser &
    python tools/record_story.py [out_dir] [--skip-story] [--post]

First plays the whole story once, unrecorded, so the records are real (--skip-story reuses the last run's profile).
Then a full-screen Chrome on the main display is grabbed with ffmpeg gdigrab, while the page's sound (every
AudioContext and <audio>, routed into one MediaRecorder) is recorded inside the page. Picture and sound are two
clocks, so each part starts with a white flash and a 3 kHz beep; every part is lined up on its own mark and timed
to the sound between its mark and the next one. Takes over the screen for about five minutes; --post only redoes
the cutting from the last recording.
"""
import base64
import ctypes
import json
import subprocess
import sys
import time
import wave
from pathlib import Path

import imageio_ffmpeg
import numpy as np
from playwright.sync_api import sync_playwright

URL = "http://127.0.0.1:8779/"
args = [a for a in sys.argv[1:] if not a.startswith("--")]
OUT = Path(args[0] if args else "build/video")
OUT.mkdir(parents=True, exist_ok=True)
PROFILE = OUT / "profile"
FF = imageio_ffmpeg.get_ffmpeg_exe()

# One shared context: the page's AudioContext is this one, <audio> clips are tapped into it, all of it is recorded.
TAP = """
(() => {
  const Real = window.AudioContext || window.webkitAudioContext;
  let ctx = null, rec = null;
  const master = () => {
    if (ctx) return ctx;
    ctx = new Real();
    rec = ctx.createMediaStreamDestination();
    const out = ctx.destination, connect = AudioNode.prototype.connect;
    AudioNode.prototype.connect = function (t, ...r) {
      if (t === out) connect.call(this, rec);
      return connect.call(this, t, ...r);
    };
    return ctx;
  };
  window.AudioContext = window.webkitAudioContext = function () { return master(); };
  const play = HTMLMediaElement.prototype.play;
  HTMLMediaElement.prototype.play = function () {
    if (!this._tapped) { this._tapped = true; const c = master(); c.createMediaElementSource(this).connect(c.destination); }
    return play.call(this);
  };
  window.__rec = {
    now() { return (performance.now() - this.t0) / 1000; },
    start() {
      const c = master(); c.resume();
      this.chunks = []; this.mr = new MediaRecorder(rec.stream, { mimeType: "audio/webm;codecs=opus", audioBitsPerSecond: 160000 });
      this.mr.ondataavailable = e => e.data.size && this.chunks.push(e.data);
      this.mr.start(1000); this.t0 = performance.now();
    },
    mark() {      // white frame and a 3 kHz beep at the same moment
      const c = master(), t = c.currentTime + .02, o = c.createOscillator(), g = c.createGain();
      o.frequency.value = 3000; g.gain.setValueAtTime(.5, t); g.gain.setValueAtTime(0, t + .15);
      o.connect(g); g.connect(c.destination); o.start(t); o.stop(t + .2);
      const d = document.createElement("div");
      d.style.cssText = "position:fixed;inset:0;background:#fff;z-index:99999";
      setTimeout(() => document.body.appendChild(d), 20); setTimeout(() => d.remove(), 300);
      return this.now() + .02;
    },
    stop() {
      return new Promise(res => {
        this.mr.onstop = async () => {
          const u = new Uint8Array(await new Blob(this.chunks).arrayBuffer());
          let s = ""; for (let i = 0; i < u.length; i += 32768) s += String.fromCharCode.apply(null, u.subarray(i, i + 32768));
          res(btoa(s));
        };
        this.mr.stop();
      });
    },
  };
})();
"""
HIDE = ".hintline { display: none !important; } * { cursor: none !important; }"

# The story in its own order, matches left out. Final pair and champion follow the records of the unrecorded run.
board = lambda a, b, title: f"PICK = ['{a}', '{b}']; buildFighter(0); buildFighter(1); Story.title('{title}'); idleLine(); await wait(700);"
PARTS = [
    ("menu + prologue", "Sfx.pad(false); Menu.at = 1; Menu.open(); await wait(1600); Menu.at = 0; Menu.draw(); await wait(1200);"
                        "stage.classList.add('story'); Menu.close(); await wait(300); await Film.play('prologue');"),
    ("vs 1", board("naiwa", "naicha", "谁先动的手") + "await Vs.run(); await wait(400);"),
    ("gone", "await Film.play('gone');"),
    ("hall", "await Film.play('hall', ['第二场', '毛巾之下']);"),
    ("vs 2", board("dounai", "naidan", "毛巾之下") + "await Vs.run(); await wait(400);"),
    ("roof", "await Film.play('roof', ['决赛', '屋顶决赛']);"),
    ("vs final", board("naiwa", "dounai", "屋顶决赛") + "await Vs.run(); await wait(400);"),
    ("reveal", "await Film.play('reveal');"),
    ("ending naiwa", "await Film.play('win_naiwa', ['结局', '奶蛙吃到了布丁']);"),
    ("epilogue", "await Film.play('epilogue');"),
    ("ending naicha", "await Film.play('win_naicha', ['另一种结局', '如果奶茶赢了']);"),
    ("ending dounai", "await Film.play('win_dounai', ['另一种结局', '如果豆奶赢了']);"),
    ("ending naidan", "await Film.play('win_naidan', ['另一种结局', '如果奶蛋赢了']);"),
    ("cast", "stage.classList.remove('story'); Story.title('绷不住大赛'); Sfx.pad(false); Menu.at = 0; Menu.open(); await wait(1200);"
             "Menu.at = 1; Menu.draw(); await wait(600); Menu.at = 2; Menu.draw(); await wait(1000); showCast(); await wait(10000);"),
]


def raise_window(title):
    """Windows will not let a background process take the foreground; topmost plus the Alt-key trick does."""
    u = ctypes.windll.user32
    found = []
    proc = ctypes.WINFUNCTYPE(ctypes.c_bool, ctypes.c_void_p, ctypes.c_void_p)

    def each(h, _):
        buf = ctypes.create_unicode_buffer(512); u.GetWindowTextW(h, buf, 512)
        if u.IsWindowVisible(h) and title in buf.value:
            found.append(h)
        return True
    u.EnumWindows(proc(each), 0)
    assert found, f"no window titled {title!r}"
    h = found[0]
    u.ShowWindow(h, 3)
    u.SetWindowPos(h, -1, 0, 0, 0, 0, 0x0001 | 0x0002 | 0x0040)          # HWND_TOPMOST, no move/size, show
    u.keybd_event(0x12, 0, 0, 0); u.SetForegroundWindow(h); u.keybd_event(0x12, 0, 2, 0)
    return h


def play_story(pg):
    """The whole story, films skipped, matches played on the keyboard simulator."""
    pg.evaluate("Menu.at = 0; Menu.draw()"); pg.keyboard.press("Enter")
    for _ in range(3):
        t0 = time.time()
        while not pg.evaluate("!Film.on && !Vs.on && S.state === 'IDLE' && Story.ready === null") and time.time() - t0 < 60:
            if pg.evaluate("Film.on"):
                pg.keyboard.press("Backspace")
            time.sleep(.3)
        pg.keyboard.press("Space")
        t0 = time.time()
        while not pg.evaluate("S.over") and time.time() - t0 < 180:
            st = pg.evaluate("[S.state, S.attacker]")
            if st[0] == "ARMED":
                time.sleep(.3)
                pg.keyboard.down("Shift"); pg.keyboard.press("KeyA" if st[1] == 0 else "KeyJ"); pg.keyboard.up("Shift")
            time.sleep(.2)
        time.sleep(1); pg.keyboard.press("Space"); time.sleep(.5)
    t0 = time.time()
    while not pg.evaluate("Menu.on") and time.time() - t0 < 60:
        if pg.evaluate("Film.on"):
            pg.keyboard.press("Backspace")
        time.sleep(.3)


def record():
    with sync_playwright() as p:
        if "--skip-story" not in sys.argv:
            ctx = p.chromium.launch_persistent_context(str(PROFILE), channel="chrome", headless=True, viewport={"width": 1920, "height": 1080})
            pg = ctx.new_page(); pg.goto(URL); pg.evaluate("localStorage.clear()"); pg.reload(); time.sleep(1.2)
            play_story(pg)
            ctx.close()

        ctx = p.chromium.launch_persistent_context(
            str(PROFILE), channel="chrome", headless=False, no_viewport=True,
            args=["--start-fullscreen", "--window-position=0,0", "--autoplay-policy=no-user-gesture-required",
                  "--disable-features=Translate", "--lang=zh-CN"],
            ignore_default_args=["--enable-automation", "--no-sandbox"])      # no infobar, no translate popup over the picture
        pg = ctx.pages[0] if ctx.pages else ctx.new_page()
        pg.add_init_script(TAP)
        pg.goto(URL); time.sleep(1.5)
        pg.add_style_tag(content=HIDE)
        print("records", pg.evaluate("record()"), flush=True)
        pg.evaluate("document.title = 'SLAP-REC'"); time.sleep(.3)
        h = raise_window("SLAP-REC"); pg.bring_to_front(); time.sleep(1.5)
        assert ctypes.windll.user32.GetForegroundWindow() == h, "Chrome did not come to the front; not recording the desktop"

        grab = subprocess.Popen([FF, "-y", "-loglevel", "error", "-f", "gdigrab", "-framerate", "30", "-draw_mouse", "0", "-i", "desktop",
                                 "-c:v", "libx264", "-preset", "ultrafast", "-crf", "14", str(OUT / "screen.mkv")], stdin=subprocess.PIPE)
        time.sleep(2.5)
        pg.evaluate("Sfx.wake(); __rec.start()"); time.sleep(.5)
        parts = []
        for name, js in PARTS:
            m, s, e = pg.evaluate(f"(async () => {{ const m = __rec.mark(); await wait(900); const s = __rec.now(); {js} return [m, s, __rec.now()]; }})()")
            parts.append({"name": name, "mark": m, "start": s, "end": e + .3})
            print(f"{name:>15} {s:7.1f} - {e:7.1f}s", flush=True)
            time.sleep(.3)
        end_mark = pg.evaluate("__rec.mark()"); time.sleep(1.5)

        grab.communicate(b"q", timeout=30)
        (OUT / "audio.webm").write_bytes(base64.b64decode(pg.evaluate("__rec.stop()")))
        ctx.close()
    (OUT / "parts.json").write_text(json.dumps({"parts": parts, "end_mark": end_mark}, ensure_ascii=False, indent=1), encoding="utf8")


if "--post" not in sys.argv:
    record()
rec = json.loads((OUT / "parts.json").read_text(encoding="utf8"))
parts, end_mark = rec["parts"], rec["end_mark"]

# ---- find every mark in both recordings ----
marks = [q["mark"] for q in parts] + [end_mark]
subprocess.run([FF, "-y", "-loglevel", "error", "-i", str(OUT / "audio.webm"),
                "-af", "aresample=async=1:first_pts=0", "-ac", "1", "-ar", "48000", str(OUT / "audio.wav")], check=True)
with wave.open(str(OUT / "audio.wav")) as w:
    pcm = np.frombuffer(w.readframes(w.getnframes()), np.int16).astype(np.float32) / 32768
SR, HOP, WIN = 48000, 240, 960
tone = np.exp(-2j * np.pi * 3000 * np.arange(WIN) / SR)


def beep_near(t):
    """First 5 ms step, from half a second before t, where a 3 kHz tone carries most of the sound."""
    for i in range(int((t - .5) * SR), int((t + 2) * SR), HOP):
        x = pcm[max(i, 0):i + WIN]
        if len(x) < WIN:
            break
        p = abs(np.dot(x, tone)) ** 2 * 2 / WIN, np.dot(x, x)
        if p[1] > 1 and p[0] / p[1] > .7:
            return i / SR
    raise SystemExit(f"no beep near audio {t:.1f}s")


probe = subprocess.run([FF, "-hide_banner", "-i", str(OUT / "screen.mkv"), "-vf", "scale=64:36,signalstats,metadata=print:key=lavfi.signalstats.YAVG",
                        "-f", "null", "-"], capture_output=True, text=True, encoding="utf8", errors="replace").stderr
luma, t = [], None
for line in probe.splitlines():
    if "pts_time:" in line:
        t = float(line.split("pts_time:")[1].split()[0])
    elif "YAVG=" in line:
        luma.append((t, float(line.split("YAVG=")[1])))


def flash_near(t):
    hit = next((a for a, y in luma if t - 1 <= a <= t + 2 and y > 200), None)
    if hit is None:
        raise SystemExit(f"no flash near screen {t:.1f}s")
    return hit


A = [beep_near(m) for m in marks]
first = next(a for a, y in luma if y > 200)
V = [flash_near(m - marks[0] + first) for m in marks]
print("screen - sound at each mark:", " ".join(f"{v - a:.3f}" for v, a in zip(V, A)))

# ---- cut each part on its own mark, picture timed to the sound clock, then join ----
ENC = ["-c:v", "libx264", "-preset", "slow", "-crf", "18", "-pix_fmt", "yuv420p", "-c:a", "pcm_s16le"]
pieces = []
for k, q in enumerate(parts):
    rate = (A[k + 1] - A[k]) / (V[k + 1] - V[k])          # sound seconds per screen second
    a0, v0, dur = A[k] + q["start"] - q["mark"], V[k] + (q["start"] - q["mark"]) / rate, q["end"] - q["start"]
    fade = f"fade=in:d=0.25,fade=out:st={dur - .3:.2f}:d=0.3"
    out = OUT / f"part{k:02d}.mkv"
    subprocess.run([FF, "-y", "-loglevel", "error", "-ss", f"{v0:.3f}", "-i", str(OUT / "screen.mkv"), "-ss", f"{a0:.3f}", "-t", f"{dur:.3f}", "-i", str(OUT / "audio.wav"),
                    "-map", "0:v", "-map", "1:a", "-t", f"{dur:.3f}",
                    "-vf", f"setpts=(PTS-STARTPTS)*{rate:.6f},fps=30,scale=1920:1080:flags=lanczos,{fade}",
                    "-af", f"afade=in:d=0.25,afade=out:st={dur - .3:.2f}:d=0.3", *ENC, str(out)], check=True)
    pieces.append(out)
    print(f"{q['name']:>15} rate {rate:.5f}")

(OUT / "parts.txt").write_text("".join(f"file '{x.name}'\n" for x in pieces), encoding="utf8")
final = OUT / "air-slap-duel_展示.mp4"
subprocess.run([FF, "-y", "-loglevel", "error", "-f", "concat", "-safe", "0", "-i", str(OUT / "parts.txt"),
                "-c:v", "libx264", "-preset", "slow", "-crf", "18", "-pix_fmt", "yuv420p", "-c:a", "aac", "-b:a", "192k",
                "-movflags", "+faststart", str(final)], check=True)
(OUT / "marks.json").write_text(json.dumps({"sound_marks": A, "screen_marks": V}, indent=1), encoding="utf8")
print("done", final)
