"""cut.py SPEC.json OUTDIR [--qa qa.jpg] — grab video frames, remove background, write WebP cutouts.
SPEC: list of {id, video, t (sec or [t0,t1,step]), box:[x0,y0,x1,y1] precrop (optional), keep:'largest'}"""
import sys, json, cv2, numpy as np
from PIL import Image
from rembg import remove, new_session
spec = json.load(open(sys.argv[1], encoding="utf-8")); out = sys.argv[2]
qa = sys.argv[sys.argv.index("--qa") + 1] if "--qa" in sys.argv else None
sess = new_session("isnet-general-use")
import os; os.makedirs(out, exist_ok=True)

def grab(video, t):
    cap = cv2.VideoCapture(f"vids/{video}.mp4"); cap.set(cv2.CAP_PROP_POS_MSEC, t * 1000); ok, f = cap.read(); assert ok, (video, t)
    return cv2.cvtColor(f, cv2.COLOR_BGR2RGB)

def cutout(rgb):
    rgba = np.array(remove(Image.fromarray(rgb), session=sess, post_process_mask=True))
    a = rgba[..., 3]
    # keep the largest blob only (drops stray props / watermark bits)
    n, lab, st, _ = cv2.connectedComponentsWithStats((a > 40).astype(np.uint8))
    if n > 2:
        big = 1 + np.argmax(st[1:, cv2.CC_STAT_AREA]); a = np.where(lab == big, a, 0).astype(np.uint8)
    k = max(5, int(min(a.shape) * 0.02) | 1)
    core = cv2.morphologyEx((a > 128).astype(np.uint8), cv2.MORPH_OPEN, cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (k, k)))
    a = np.where(cv2.dilate(core, np.ones((5, 5), np.uint8)) > 0, a, 0).astype(np.uint8)
    # tighten the edge a touch, then pull edge colours from the opaque interior (no light halo)
    a = cv2.erode(a, np.ones((3, 3), np.uint8)); a = cv2.GaussianBlur(a, (3, 3), 0)
    solid = (a > 230).astype(np.uint8)
    rgb2 = rgb.copy()
    if solid.any():
        inpaint_mask = ((a > 0) & (a <= 230)).astype(np.uint8) * 255
        bg = rgb.copy(); bg[solid == 0] = 0
        rgb2 = cv2.inpaint(rgb, inpaint_mask, 3, cv2.INPAINT_TELEA) if inpaint_mask.any() else rgb
        rgb2 = np.where(solid[..., None] == 1, rgb, rgb2)
    return np.dstack([rgb2, a])

def body(c):   # opaque, saturated yellow pixels = the character's skin
    hsv = cv2.cvtColor(c[..., :3], cv2.COLOR_RGB2HSV)
    return (c[..., 3] > 240) & (hsv[..., 0] > 12) & (hsv[..., 0] < 35) & (hsv[..., 1] > 110)
def match(c, ref):
    g = ref[..., :3][body(ref)].reshape(-1, 3).mean(0) / c[..., :3][body(c)].reshape(-1, 3).mean(0)
    c[..., :3] = np.clip(c[..., :3] * g, 0, 255).astype(np.uint8); return c

tiles = []
for s in spec:
    ts = s["t"] if isinstance(s["t"], list) and len(s["t"]) != 3 or not isinstance(s["t"], list) else list(np.arange(*s["t"]))
    ts = ts if isinstance(ts, list) else [ts]
    cuts = []
    for t in ts:
        f = grab(s["video"], t)
        if "box" in s: x0, y0, x1, y1 = s["box"]; f = f[y0:y1, x0:x1]
        c = cutout(f)
        if "dark_top" in s:   # door frame behind the head: drop dark pixels in the top band
            band = int(c.shape[0] * s["dark_top"]); v = c[:band, :, :3].max(axis=2)
            c[:band, :, 3][v < 120] = 0
        for x0f, y0f, x1f, y1f, mode in s.get("clear", []):   # props stuck to the outline
            H, W = c.shape[:2]; r = c[int(y0f * H):int(y1f * H), int(x0f * W):int(x1f * W)]
            hsv = cv2.cvtColor(np.ascontiguousarray(r[..., :3]), cv2.COLOR_RGB2HSV)
            sel = {"dark": hsv[..., 2] < 110, "grey": hsv[..., 1] < 90, "notyellow": (hsv[..., 0] < 10) | (hsv[..., 0] > 40) | (hsv[..., 1] < 80), "all": np.ones(r.shape[:2], bool)}[mode]
            r[..., 3][sel] = 0
        cuts.append(c)
    # one shared crop for the whole sequence keeps the frames registered
    keep = s.get("frames", list(range(len(cuts))))
    ys, xs = np.where(np.max([cuts[k][..., 3] for k in keep], axis=0) > 8)
    y0, y1, x0, x1 = ys.min(), ys.max() + 1, xs.min(), xs.max() + 1
    for n, k in enumerate(keep):
        c = cuts[k][y0:y1, x0:x1].copy()
        if "match" in s: c = match(c, np.array(Image.open(s["match"])))
        name = s["id"] if len(keep) == 1 else f'{s["id"]}_{n}'
        Image.fromarray(c).save(f"{out}/{name}.webp", quality=s.get("q", 86), method=6, alpha_quality=100)
        if qa:
            bgc = np.zeros_like(c[..., :3]); bgc[:] = (26, 20, 48); al = c[..., 3:4] / 255
            comp = (c[..., :3] * al + bgc * (1 - al)).astype(np.uint8)
            h = 300; comp = cv2.resize(comp, (int(comp.shape[1] * h / comp.shape[0]), h))
            cv2.putText(comp, name, (4, 18), 0, .5, (0, 255, 255), 1); tiles.append(cv2.cvtColor(comp, cv2.COLOR_RGB2BGR))
    print(s["id"], len(cuts), "frames", (x1 - x0, y1 - y0))
if qa:
    rows, cur, w = [], [], 0
    for t in tiles:
        if w + t.shape[1] > 1900 and cur: rows.append(cur); cur, w = [], 0
        cur.append(t); w += t.shape[1]
    rows.append(cur); W = max(sum(x.shape[1] for x in r) for r in rows)
    cv2.imwrite(qa, np.vstack([np.hstack(r + [np.zeros((300, W - sum(x.shape[1] for x in r), 3), np.uint8)]) for r in rows]))
