"""Subset the match screen's fonts to the glyphs it uses, so tools/web works with no network at the demo.

    python tools/fetch_fonts.py        # rerun after adding text to tools/web/index.html

Google Fonts builds a one-file subset when given the characters in `text=`.
Glyphs are collected from the page, plus digits for the numbers filled in at runtime.
"""
import re
import urllib.parse
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PAGE = ROOT / "tools" / "web" / "index.html"
STORY = ROOT / "tools" / "web" / "intro" / "story.json"
OUT = ROOT / "tools" / "web" / "fonts"
UA = {"User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 Chrome/130 Safari/537.36"}
ASCII = "".join(chr(c) for c in range(32, 127)) + "−·°"

# (css2 family spec, file stem, glyph set)
FACES = [
    ("Noto+Sans+SC:wght@500", "noto-sans-500", "all"),
    ("Noto+Sans+SC:wght@900", "noto-sans-900", "all"),
    ("Noto+Serif+SC:wght@500", "noto-serif-500", "all"),
    ("Noto+Serif+SC:wght@900", "noto-serif-900", "all"),
    ("ZCOOL+QingKe+HuangYou", "zcool-qingke", "all"),
    ("Anton", "anton", "ascii"),
    ("Archivo:wdth,wght@125,800", "archivo", "ascii"),
    ("Cormorant+Garamond:ital,wght@1,500", "cormorant-italic", "ascii"),
]


def get(url):
    with urllib.request.urlopen(urllib.request.Request(url, headers=UA), timeout=40) as r:
        return r.read()


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    words = PAGE.read_text(encoding="utf8") + STORY.read_text(encoding="utf8")
    cjk = "".join(sorted(set(re.findall(r"[　-〿一-鿿＀-￯]", words))))
    css = []
    for spec, stem, kind in FACES:
        text = ASCII + cjk if kind == "all" else ASCII
        sheet = get(f"https://fonts.googleapis.com/css2?family={spec}&text={urllib.parse.quote(text)}").decode()
        url = re.search(r"url\((https://[^)]+)\)", sheet).group(1)
        (OUT / f"{stem}.woff2").write_bytes(get(url))
        css.append(re.sub(r"url\([^)]+\)", f"url(/fonts/{stem}.woff2)", sheet))
        print(stem, (OUT / f"{stem}.woff2").stat().st_size // 1024, "KB")
    (OUT / "fonts.css").write_text("\n".join(css), encoding="utf8")
    print(len(cjk), "CJK glyphs")


if __name__ == "__main__":
    main()
