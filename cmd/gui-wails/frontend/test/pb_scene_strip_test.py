"""V1 S4b: scene strip under the lanes — video thumbnails, scene markers
(diamonds = chapter starts, click seeks) and "+" (adds a chapter at the
playhead). Off by default in the Player (Owner: scenes are a Generator tool).

Run: python3 cmd/gui-wails/frontend/test/pb_scene_strip_test.py
"""

import pathlib
import sys

from playwright.sync_api import sync_playwright

from _harness import Checker, app_stub, serve

PAGE = """<!doctype html><html><head><link rel="stylesheet" href="/src/style.css?real"></head>
<body><div id="tab-playback"></div>
<script type="module">
  import { initPlayback } from '/src/playback.js';
  window.__pb = initPlayback(document.querySelector('#tab-playback'));
  window.__ready = true;
</script></body></html>"""

PNG = ("data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg==")


def main():
    check = Checker()
    data_js = ("const STROKE = []; for (let i = 0; i < 200; i++) STROKE.push({ atMs: i * 500, pos: i % 2 ? 10 : 90 });\n"
               "window.__chapters = [{ name: 'Intro', startTime: 10000, endTime: 0 }];\n"
               "window.__thumbCalls = 0;\n")
    base, shutdown = serve(data_js + app_stub({
        "PickFunscriptFile": "async () => '/tmp/t.samn'",
        "LoadFunscript": "async () => ({ path: '/tmp/t.samn', actionCount: STROKE.length, durationMs: 100000, videoPath: '/tmp/clip.mp4', hasVideo: true })",
        "GetScriptCurve": "async () => STROKE.slice()",
        "GetHeatmap": "async n => Array.from({ length: n }, (_, i) => ({ atMs: i * 1000, intensity: 0.5 }))",
        "GetMarker": "async () => null",
        "VideoFileURL": "async () => ''",
        "GetScriptChapterMarks": "async () => window.__chapters.slice()",
        "SaveScriptChapterMarks": "async (next) => { window.__chapters = next; window.__saved = next; }",
        "GetVideoThumbnails": ("async (n) => { window.__thumbCalls++; window.__thumbN = n; "
                               f"return Array.from({{ length: n }}, (_, i) => ({{ atMs: Math.round((i + 0.5) * 100000 / n), dataUrl: i === 3 ? '' : '{PNG}' }})); }}"),
    }))
    harness = pathlib.Path(__file__).resolve().parent / "_pb_scene_strip_harness.html"
    harness.write_text(PAGE)
    try:
        with sync_playwright() as p:
            b = p.chromium.launch()
            page = b.new_page(viewport={"width": 1280, "height": 900})
            page.on("pageerror", lambda e: print("   [pageerror]", e))
            page.add_init_script("try{localStorage.clear()}catch(e){}")
            page.goto(f"{base}/test/_pb_scene_strip_harness.html")
            page.wait_for_function("window.__ready === true")
            check("scenes toggle off by default (Player)", not page.is_checked("#pb-scenes-toggle"))
            page.click("#pb-choose")
            page.wait_for_function("!document.querySelector('#pb-ruler').hidden", timeout=5000)
            page.wait_for_timeout(300)
            check("strip hidden while off", page.is_hidden("#pb-scene-strip"))
            check("no thumbnail decode while off", page.evaluate("window.__thumbCalls") == 0)
            check("root state off", page.get_attribute("#tab-playback", "data-pb-scenes") == "off")

            page.check("#pb-scenes-toggle")
            page.wait_for_function("document.querySelectorAll('.pb-scene-thumb').length > 0", timeout=5000)
            check("strip shown when on", page.is_visible("#pb-scene-strip"))
            check("thumbnails fetched once", page.evaluate("window.__thumbCalls") == 1)
            n = page.evaluate("window.__thumbN")
            check("every slot drawn", page.evaluate("document.querySelectorAll('.pb-scene-thumb').length") == n)
            check("unreadable frame shows the Flow placeholder",
                  page.evaluate("[...document.querySelectorAll('.pb-scene-thumb.is-placeholder')].map(i => i.src.endsWith('/scene-placeholder.svg'))") == [True])
            check("toggle remembered", page.evaluate("localStorage.getItem('pb.scenes')") == "1")
            widths = page.evaluate("['#pb-curve', '#pb-scene-strip'].map(s => Math.round(document.querySelector(s).getBoundingClientRect().width))")
            check("strip as wide as the curve (same time scale)", widths[0] == widths[1] and widths[0] > 0, str(widths))
            check("+ sits inside the strip", page.evaluate(
                "(() => { const s = document.querySelector('#pb-scene-strip').getBoundingClientRect();"
                " const b = document.querySelector('#pb-scene-add').getBoundingClientRect();"
                " return b.left >= s.left && b.right <= s.right; })()"))
            marks = page.query_selector_all(".pb-scene-mark")
            check("one diamond per chapter", len(marks) == 1)
            left = page.evaluate("document.querySelector('.pb-scene-mark').style.left")
            check("diamond at the chapter start (10 %)", left.startswith("10"), left)

            # The diamond seeks to its chapter; "+" there must not add a duplicate.
            page.evaluate("document.querySelector('.pb-scene-mark').click()")
            page.wait_for_timeout(100)
            page.click("#pb-scene-add")
            page.wait_for_timeout(200)
            check("+ on an existing marker does not duplicate", not page.evaluate("window.__saved"))
            # Seek to ~40 % on the heatmap / curve, then "+" adds a new scene there.
            box = page.query_selector("#pb-heatmap").bounding_box() if page.is_visible("#pb-heatmap") else None
            if box:
                page.mouse.click(box["x"] + box["width"] * 0.4, box["y"] + box["height"] / 2)
            else:
                cbox = page.query_selector("#pb-curve").bounding_box()
                page.mouse.click(cbox["x"] + cbox["width"] * 0.4, cbox["y"] + cbox["height"] / 2)
            page.wait_for_timeout(150)
            page.click("#pb-scene-add")
            page.wait_for_function("Array.isArray(window.__saved) && window.__saved.length === 2", timeout=3000)
            saved = page.evaluate("window.__saved")
            check("+ adds a chapter at the playhead", saved[1]["name"] == "Scene 2" and 30000 < saved[1]["startTime"] < 50000,
                  str(saved))
            check("previous open chapter now ends at the new marker",
                  saved[0]["endTime"] == saved[1]["startTime"], str(saved))
            page.wait_for_function("document.querySelectorAll('.pb-scene-mark').length === 2", timeout=3000)
            check("strip shows the new diamond", True)

            page.uncheck("#pb-scenes-toggle")
            check("toggle off hides the strip", page.is_hidden("#pb-scene-strip"))
            b.close()
    finally:
        shutdown()
        harness.unlink(missing_ok=True)
    return check.report()


if __name__ == "__main__":
    sys.exit(main())
