"""Play one match per theme against web_dashboard.py --keys --http-port 8779 and screenshot each beat.

    python tools/qa_match_screen.py <out_prefix> [theme ...] [--pick id,id]
"""
import sys, time
from playwright.sync_api import sync_playwright

URL = "http://127.0.0.1:8779/"
args = sys.argv[1:]
pick = "naiwa,naicha"
if "--pick" in args:
    k = args.index("--pick"); pick = args[k + 1]; del args[k:k + 2]
out, themes = args[0], args[1:] or ["a", "b", "c"]
ids = pick.split(",")
errs = []
with sync_playwright() as p:
    b = p.chromium.launch(channel="chrome", headless=True)
    pg = b.new_page(viewport={"width": 1920, "height": 1080})
    pg.on("console", lambda m: m.type == "error" and errs.append(m.text)); pg.on("pageerror", lambda e: errs.append(str(e)))
    pg.on("requestfailed", lambda r: errs.append("failed " + r.url))
    for th in themes:
        pg.goto(URL)
        pg.evaluate(f"localStorage.setItem('slap.theme','{th}'); localStorage.setItem('slap.fighters', JSON.stringify({ids}))")
        pg.reload(); time.sleep(1)
        pg.keyboard.press("ArrowDown"); pg.keyboard.press("Enter")     # 自由对战
        pg.keyboard.press("KeyR"); time.sleep(1); pg.screenshot(path=f"{out}_{th}_1idle.png")
        pg.keyboard.press("Space"); time.sleep(.6); pg.screenshot(path=f"{out}_{th}_1vs.png"); pg.keyboard.press("Space")
        shots = 0; t0 = time.time(); armed = False
        while time.time() - t0 < 80:
            st = pg.evaluate("[S.state, S.attacker, S.over, S.hp]")
            if st[2]: break
            if st[0] == "ARMED":
                time.sleep(.35)
                if not armed: pg.screenshot(path=f"{out}_{th}_2armed.png"); armed = True
                pg.keyboard.down("Shift"); pg.keyboard.press("KeyA" if st[1] == 0 else "KeyJ"); pg.keyboard.up("Shift")
                if shots < 1:
                    time.sleep(1.0); pg.screenshot(path=f"{out}_{th}_3hit.png"); shots += 1
                time.sleep(1.5)
            time.sleep(.2)
        time.sleep(2.6); pg.screenshot(path=f"{out}_{th}_4over.png")
        print(th, "final", pg.evaluate("[S.hp, S.winner]"))
    b.close()
print("errs", errs[:6])
