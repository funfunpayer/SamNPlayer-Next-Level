"""Chapter bands (V1 target scene-map.png): with scenes on, every chapter
is a coloured band from its start to its end over the scene strip (an open
end runs to the next chapter or the script end). Names are text, a click
seeks to the chapter start, and the band under the playhead is marked.

Run: python3 cmd/gui-wails/frontend/test/pb_chapter_bands_test.py
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


def main():
    check = Checker()
    data_js = ("const STROKE = []; for (let i = 0; i < 200; i++) STROKE.push({ atMs: i * 500, pos: i % 2 ? 10 : 90 });\n"
               "window.__chapters = [\n"
               "  { name: 'Build <b>up</b>', startTime: 30000, endTime: 0 },\n"
               "  { name: 'Intro', startTime: 0, endTime: 30000 },\n"
               "  { name: 'Peak', startTime: 70000, endTime: 0 },\n"
               "];\n")
    base, shutdown = serve(data_js + app_stub({
        "PickFunscriptFile": "async () => '/tmp/t.samn'",
        "LoadFunscript": "async () => ({ path: '/tmp/t.samn', actionCount: STROKE.length, durationMs: 100000, videoPath: '', hasVideo: false })",
        "GetScriptCurve": "async () => STROKE.slice()",
        "GetHeatmap": "async n => Array.from({ length: n }, (_, i) => ({ atMs: i * 1000, intensity: 0.5 }))",
        "GetMarker": "async () => null",
        "VideoFileURL": "async () => ''",
        "SnapTimeMs": "async (t) => t",
        "GetScriptChapterMarks": "async () => window.__chapters.slice()",
        "GetVideoThumbnails": "async () => []",
    }))
    harness = pathlib.Path(__file__).resolve().parent / "_pb_chapter_bands_harness.html"
    harness.write_text(PAGE)
    bands = "[...document.querySelectorAll('#pb-chapter-bands .pb-chapter-band')]"
    try:
        with sync_playwright() as p:
            b = p.chromium.launch()
            page = b.new_page(viewport={"width": 1280, "height": 900})
            errors = []
            page.on("pageerror", lambda e: errors.append(str(e)))
            page.add_init_script("try{localStorage.clear()}catch(e){}")
            page.goto(f"{base}/test/_pb_chapter_bands_harness.html")
            page.wait_for_function("window.__ready === true")
            page.click("#pb-choose")
            page.wait_for_function("!document.querySelector('#pb-ruler').hidden", timeout=5000)
            page.wait_for_timeout(300)
            check("no bands while scenes are off", page.is_hidden("#pb-chapter-bands"))

            page.check("#pb-scenes-toggle")
            page.wait_for_function(f"{bands}.length === 3", timeout=5000)
            names = page.evaluate(f"{bands}.map(b => b.textContent)")
            check("one band per chapter in time order, names as text", names == ["Intro", "Build <b>up</b>", "Peak"], str(names))
            check("no HTML from a chapter name", page.evaluate("!document.querySelector('#pb-chapter-bands b')"))
            bw = page.evaluate("['#pb-curve', '#pb-chapter-bands'].map(s => Math.round(document.querySelector(s).getBoundingClientRect().width))")
            check("bands as wide as the curve (same time scale)", bw[0] == bw[1] and bw[0] > 0, str(bw))
            ranges = page.evaluate(f"{bands}.map(b => [+b.dataset.start, +b.dataset.end])")
            check("open ends run to the next chapter / script end",
                  ranges == [[0, 30000], [30000, 70000], [70000, 100000]], str(ranges))
            widths = page.evaluate(f"{bands}.map(b => parseFloat(b.style.width))")
            check("band widths follow the durations (30 · 40 · 30 %)",
                  all(abs(w - e) < 0.6 for w, e in zip(widths, [30, 40, 30])), str(widths))
            check("root reports chapter count", page.get_attribute("#tab-playback", "data-pb-chapters") == "3")

            page.evaluate(f"{bands}[1].click()")
            page.wait_for_function(f"{bands}[1].classList.contains('is-current')", timeout=3000)
            check("click seeks to the chapter start and marks that band",
                  page.evaluate(f"{bands}.map(b => b.classList.contains('is-current'))") == [False, True, False])
            page.evaluate("window.__pb.seekMs(80000)")
            page.wait_for_function(f"{bands}[2].classList.contains('is-current')", timeout=3000)
            check("current band follows the playhead", page.evaluate(f"{bands}.filter(b => b.classList.contains('is-current')).length") == 1)

            page.uncheck("#pb-scenes-toggle")
            page.wait_for_timeout(200)
            check("bands hide with the scene strip", page.is_hidden("#pb-chapter-bands"))
            check("no page errors", not errors, "; ".join(errors))
            b.close()
    finally:
        shutdown()
        harness.unlink(missing_ok=True)
    return check.report()


if __name__ == "__main__":
    sys.exit(main())
