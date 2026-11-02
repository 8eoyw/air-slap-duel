"""Plays story mode from the menu to the ending against the keyboard simulator, with a screenshot of each stage.

    python tools/web_dashboard.py --keys --http-port 8779 &
    python tools/qa_story.py [out_prefix]
"""
import sys
import time

from playwright.sync_api import sync_playwright

URL = "http://127.0.0.1:8779/"
out = sys.argv[1] if len(sys.argv) > 1 else "build/qa_story"

errs = []


def play_match(pg, tag):
    pg.keyboard.press("Space"); time.sleep(1.8); pg.screenshot(path=f"{out}_{tag}_vs.png")
    pg.keyboard.press("Space")
    t0 = time.time(); said = False
    while time.time() - t0 < 120:
        st = pg.evaluate("[S.state, S.attacker, S.over]")
        if st[2]:
            break
        if st[0] == "ARMED":
            time.sleep(.3)
            pg.keyboard.down("Shift"); pg.keyboard.press("KeyA" if st[1] == 0 else "KeyJ"); pg.keyboard.up("Shift")
            if not said:
                try:      # a quarter of the results say nothing
                    pg.wait_for_selector(".bubble", timeout=2500)
                    pg.screenshot(path=f"{out}_{tag}_bubble.png"); said = True
                except Exception:
                    pass
        time.sleep(.2)
    time.sleep(2); pg.screenshot(path=f"{out}_{tag}_over.png")
    print(tag, "caption:", pg.evaluate("document.querySelector('#call .sub')?.textContent"))
    assert pg.evaluate("Story.ready !== null"), "match did not hand back to the story"
    pg.keyboard.press("Space"); time.sleep(.5)


def wait_match_ready(pg):
    # through whatever film stands before the match
    t0 = time.time()
    while pg.evaluate("Film.on") and time.time() - t0 < 30:
        pg.keyboard.press("Backspace"); time.sleep(.5)
    time.sleep(.4)


with sync_playwright() as p:
    b = p.chromium.launch(channel="chrome", headless=True, args=["--autoplay-policy=no-user-gesture-required"])
    pg = b.new_page(viewport={"width": 1920, "height": 1080})
    pg.on("console", lambda m: m.type == "error" and "404" not in m.text and errs.append(m.text))
    pg.on("pageerror", lambda e: errs.append(str(e)))
    pg.goto(URL); pg.evaluate("localStorage.clear()"); pg.reload(); time.sleep(1.2)
    pg.screenshot(path=f"{out}_menu.png")

    pg.keyboard.press("ArrowUp"); pg.keyboard.press("Enter"); time.sleep(.5); pg.screenshot(path=f"{out}_cast.png")
    pg.keyboard.press("Backspace"); pg.keyboard.press("ArrowDown")

    pg.keyboard.press("Enter"); time.sleep(.9)
    for n in range(12):
        if n in (0, 4, 7):
            time.sleep(.8); pg.screenshot(path=f"{out}_film{n}.png")
        pg.keyboard.press("Space"); time.sleep(.25)
    wait_match_ready(pg); pg.screenshot(path=f"{out}_ch1_wait.png")

    play_match(pg, "ch1")
    wait_match_ready(pg)
    play_match(pg, "ch2")
    wait_match_ready(pg)
    pair = pg.evaluate("[PICK, Story.wins]")
    print("final pair", pair)
    assert sorted(pair[0]) == sorted(pair[1]), "final is not the two winners"
    play_match(pg, "final")
    wait_match_ready(pg)
    time.sleep(1); wait_match_ready(pg); time.sleep(1); wait_match_ready(pg); time.sleep(1)
    print("menu back", pg.evaluate("Menu.on"), "story on", pg.evaluate("Story.on"), "tally", pg.evaluate("record()"))
    b.close()
print("errs", errs[:6])
