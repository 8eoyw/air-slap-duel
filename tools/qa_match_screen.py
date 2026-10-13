import sys, time
from playwright.sync_api import sync_playwright
URL = "http://127.0.0.1:8777/"; out = sys.argv[1]; picks = sys.argv[2] if len(sys.argv) > 2 else "[0,1]"
errs = []
with sync_playwright() as p:
    b = p.chromium.launch(channel="chrome", headless=True)
    pg = b.new_page(viewport={"width": 1920, "height": 1080})
    pg.on("console", lambda m: m.type == "error" and errs.append(m.text)); pg.on("pageerror", lambda e: errs.append(str(e)))
    pg.goto(URL); pg.evaluate(f"localStorage.setItem('slap.streamers', '{picks}')"); pg.reload(); time.sleep(1)
    pg.keyboard.press("KeyR"); time.sleep(1); pg.screenshot(path=f"{out}_idle.png")
    pg.keyboard.press("Space"); shots = 0; t0 = time.time()
    while time.time() - t0 < 90:
        st = pg.evaluate("[S.state, S.attacker, S.over, S.hp]")
        if st[2]: break
        if st[0] == "ARMED":
            time.sleep(.3); pg.keyboard.down("Shift"); pg.keyboard.press("KeyA" if st[1] == 0 else "KeyJ"); pg.keyboard.up("Shift")
            if shots < 2:
                time.sleep(.95); pg.screenshot(path=f"{out}_crack{shots}.png"); shots += 1
            time.sleep(1.5)
        time.sleep(.2)
    for k, dt in enumerate([1.6, 1.6]):
        time.sleep(dt); pg.screenshot(path=f"{out}_end{k}.png")
    print("final", pg.evaluate("[S.hp, S.winner]")); errs and print("ERR", errs[:5])
    b.close()
