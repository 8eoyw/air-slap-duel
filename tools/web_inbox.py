"""Hand-made art: a local page with the same prompts and references, for image sites on the web.

    python tools/web_inbox.py          # http://127.0.0.1:8780/

Copy a card's prompt, make the image on the site, then download it: a new image in the Downloads folder goes to the
marked card (pasting or dropping onto a card works too). It lands under the right name and goes through the same steps as generated art, so the running match page picks it up on reload.
Cards are in order of what the demo needs most; what nobody makes here is still filled in by build/char_gen/intro_loop.sh.
"""
import argparse
import io
import json
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

from PIL import Image

import gen_chars
import gen_intro
from gen_chars import CHARS, POSES, WORK

DOWNLOADS = Path.home() / "Downloads"
PICTURES = {".png", ".jpg", ".jpeg", ".webp"}
MUST = ["hit", "slap", "windup", "laugh", "block", "dodge", "win"]
LATER = ["dizzy", "laugh2"]


def body(template, **kw):
    """The agy prompts without the lines that talk to agy: first and last."""
    return "\n".join(template.format(refs="", name="", **kw).splitlines()[1:-1])


def ref_files(char):
    return [gen_intro.ref_for(char)]


def build_jobs():
    jobs = []
    for shot, (cast, scene) in gen_intro.SHOTS.items():
        who = "\n".join(f"- {gen_intro.NAMES[c]}: {CHARS[c][1]}" for c in cast) or "(no characters in this shot)"
        jobs.append({"id": shot, "kind": "shot", "group": "1 故事分镜", "ratio": "16:9",
                     "refs": [f for c in cast for f in ref_files(c)] + [f"intro/{next(iter(gen_intro.SHOTS))}.jpg"],
                     "prompt": "Generate one image from the attached references.\n" + body(gen_intro.PROMPT, cast=who, scene=scene)})
    # 奶茶 already has placeholders for idle, hit, laugh and windup, so its redo comes last
    have = {"naicha": ["hit", "windup", "laugh"]}
    for tier, poses in (("2 比赛姿势", MUST), ("3 有空再做", LATER)):
        for p in poses:
            for c in CHARS:
                if p in have.get(c, []):
                    continue
                jobs.append(pose_job(c, p, tier))
    for p in ["idle"] + have["naicha"]:
        jobs.append(pose_job("naicha", p, "3 有空再做"))
    return jobs


def pose_job(c, p, group):
    return {"id": f"{c}_{p}", "kind": "pose", "group": group, "ratio": "3:4", "refs": ref_files(c),
            "prompt": "Generate one image from the attached references.\n" + body(gen_chars.PROMPT, identity=CHARS[c][1], pose=POSES[p])}


JOBS = build_jobs()
BY_ID = {j["id"]: j for j in JOBS}


def target(job):
    return (gen_intro.RAW if job["kind"] == "shot" else WORK / "raw") / f"{job['id']}.jpg"


def incoming():
    """Images downloaded since this server started, oldest first; a file still being written is left for the next look."""
    now = time.time()
    new = [f for f in DOWNLOADS.iterdir() if f.suffix.lower() in PICTURES and f.name not in seen and now - f.stat().st_mtime > .7]
    return [f.name for f in sorted(new, key=lambda f: f.stat().st_mtime)]


seen = {f.name for f in DOWNLOADS.iterdir()} if DOWNLOADS.is_dir() else set()


def put(job, data):
    """Returns an error for the page, or None."""
    try:
        im = Image.open(io.BytesIO(data)).convert("RGB")
    except Exception:
        return "这不是图片"
    raw = target(job)
    raw.parent.mkdir(parents=True, exist_ok=True)
    im.save(raw, quality=96)
    if job["kind"] == "shot":
        gen_intro.OUT.mkdir(parents=True, exist_ok=True)
        gen_intro.publish(job["id"])
        return None
    c, p = job["id"].rsplit("_", 1)
    if isinstance(gen_chars.publish(c, p), str):
        return "背景不是洋红，抠不了图：请在网站上要求重画成纯洋红底"
    gen_chars.manifest()
    return None


PAGE = """<!doctype html><meta charset=utf-8><title>收图</title>
<style>
body { margin: 0; font: 15px/1.5 "Microsoft YaHei UI", sans-serif; background: #f3f0e8; color: #1c1a17; }
header { position: sticky; top: 0; z-index: 2; display: flex; gap: 20px; align-items: baseline; padding: 12px 24px; background: #1c1a17; color: #f3f0e8; }
header b { font-size: 20px; }
main { max-width: 1120px; margin: 0 auto; padding: 16px 24px 80px; }
h2 { margin: 28px 0 10px; font-size: 18px; }
.card { display: grid; grid-template-columns: 200px 1fr 200px; gap: 14px; padding: 12px; margin-bottom: 10px; background: #fff; border: 3px solid transparent; border-radius: 10px; }
.card.cur { border-color: #e4412a; }
.card.done { opacity: .55; }
.card.bad { border-color: #b00; }
.name { font-size: 17px; font-weight: 700; }
.refs { display: flex; flex-wrap: wrap; gap: 4px; margin: 6px 0; }
.refs img { height: 62px; border-radius: 4px; }
pre { margin: 0; max-height: 92px; overflow: auto; white-space: pre-wrap; font: 12px/1.4 Consolas, monospace; color: #555; }
button { padding: 7px 16px; font: inherit; font-weight: 700; color: #fff; background: #e4412a; border: 0; border-radius: 6px; cursor: pointer; }
.drop { display: grid; place-items: center; min-height: 112px; padding: 6px; text-align: center; color: #888; background: #f3f0e8 center/contain no-repeat; border: 2px dashed #bbb; border-radius: 8px; }
.drop.over { border-color: #e4412a; color: #e4412a; }
.err { color: #b00; font-weight: 700; }
</style>
<header><b>收图</b><span id=count></span><span>① 复制提示词并上传参考图 → ② 网站出图 → ③ 在网站按下载：图自动进红框这张卡，也可以把图拖到卡片</span></header>
<main id=main></main>
<script>
"use strict";
let jobs = [], cur = null;
const card = (j) => document.getElementById("c-" + j.id);
function count() { const d = jobs.filter(j => j.done).length; document.getElementById("count").textContent = `${d} / ${jobs.length}`; }
function setCur(j) {
  document.querySelectorAll(".cur").forEach(c => c.classList.remove("cur"));
  cur = j; if (j) { card(j).classList.add("cur"); }
}
async function send(j, blob, url) {
  const c = card(j), d = c.querySelector(".drop"), e = c.querySelector(".msg");
  e.textContent = "处理中…"; c.classList.remove("bad");
  const r = await fetch(url || "/put/" + j.id, { method: "POST", body: blob });
  const t = await r.text();
  if (!r.ok) { e.textContent = t; e.className = "msg err"; c.classList.add("bad"); j.done = false; count(); return; }
  e.textContent = "已保存"; e.className = "msg";
  j.done = true; c.classList.add("done");
  d.style.backgroundImage = `url(/raw/${j.id}?${Date.now()})`; d.textContent = "";
  count();
  const next = jobs.slice(jobs.indexOf(j) + 1).find(x => !x.done);
  if (next) { setCur(next); card(next).scrollIntoView({ block: "center", behavior: "smooth" }); }
}
async function boot() {
  jobs = await (await fetch("/jobs")).json();
  const main = document.getElementById("main");
  let group = "";
  for (const j of jobs) {
    if (j.group !== group) { group = j.group; const h = document.createElement("h2"); h.textContent = group; main.appendChild(h); }
    const c = document.createElement("div"); c.className = "card" + (j.done ? " done" : ""); c.id = "c-" + j.id;
    c.innerHTML = `<div><div class=name></div><div>比例 ${j.ratio}</div><div class=refs></div><button>复制提示词</button></div><pre></pre><div><div class=drop>粘贴或拖到这里</div><div class=msg></div></div>`;
    c.querySelector(".name").textContent = j.id;
    c.querySelector("pre").textContent = j.prompt;
    for (const f of j.refs) { const m = document.createElement("img"); m.src = "/ref/" + f; m.title = f; c.querySelector(".refs").appendChild(m); }
    if (j.done) { const d = c.querySelector(".drop"); d.style.backgroundImage = `url(/raw/${j.id})`; d.textContent = ""; }
    c.querySelector("button").addEventListener("click", async (e) => {
      await navigator.clipboard.writeText(j.prompt); setCur(j);
      e.target.textContent = "已复制"; setTimeout(() => e.target.textContent = "复制提示词", 1200);
    });
    c.addEventListener("click", () => setCur(j));
    c.addEventListener("dragover", (e) => { e.preventDefault(); c.querySelector(".drop").classList.add("over"); });
    c.addEventListener("dragleave", () => c.querySelector(".drop").classList.remove("over"));
    c.addEventListener("drop", (e) => {
      e.preventDefault(); c.querySelector(".drop").classList.remove("over");
      const f = e.dataTransfer.files[0]; if (f) { setCur(j); send(j, f); }
    });
    main.appendChild(c);
  }
  count();
  setCur(jobs.find(j => !j.done) || null);
}
let taking = false;
setInterval(async () => {
  if (!cur || taking) return;
  taking = true;
  try {
    const names = await (await fetch("/incoming")).json();
    if (names.length) await send(cur, names[0], "/take/" + cur.id);
  } finally { taking = false; }
}, 1500);
addEventListener("paste", (e) => {
  const it = [...e.clipboardData.items].find(i => i.type.startsWith("image/"));
  if (it && cur) { e.preventDefault(); send(cur, it.getAsFile()); }
});
boot();
</script>"""


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def reply(self, code, body, kind="text/plain; charset=utf-8"):
        self.send_response(code)
        self.send_header("Content-Type", kind)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        path = self.path.split("?")[0]
        if path == "/":
            self.reply(200, PAGE.encode("utf8"), "text/html; charset=utf-8")
        elif path == "/jobs":
            self.reply(200, json.dumps([{**j, "done": target(j).exists()} for j in JOBS]).encode("utf8"), "application/json")
        elif path == "/incoming":
            self.reply(200, json.dumps(incoming() if DOWNLOADS.is_dir() else []).encode("utf8"), "application/json")
        elif path.startswith("/raw/") and path[5:] in BY_ID and target(BY_ID[path[5:]]).exists():
            self.reply(200, target(BY_ID[path[5:]]).read_bytes(), "image/jpeg")
        elif path.startswith("/ref/"):
            f = (WORK / path[5:]).resolve()
            if WORK.resolve() in f.parents and f.is_file():
                self.reply(200, f.read_bytes(), "image/jpeg" if f.suffix == ".jpg" else "image/png")
            else:
                self.reply(404, b"")
        else:
            self.reply(404, b"")

    def do_POST(self):
        job = BY_ID.get(self.path[6:] if self.path.startswith("/take/") else self.path[5:] if self.path.startswith("/put/") else None)
        if not job:
            self.reply(404, b"")
            return
        data = self.rfile.read(int(self.headers["Content-Length"]))
        if self.path.startswith("/take/"):      # the body names a file in Downloads; it is left where it is
            name = data.decode("utf8")
            if name not in incoming():
                self.reply(404, b"")
                return
            seen.add(name)
            data = (DOWNLOADS / name).read_bytes()
        error = put(job, data)
        self.reply(422 if error else 204, (error or "").encode("utf8"))


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=8780)
    port = ap.parse_args().port
    print(f"http://127.0.0.1:{port}/  {len(JOBS)} cards", flush=True)
    ThreadingHTTPServer(("127.0.0.1", port), Handler).serve_forever()
