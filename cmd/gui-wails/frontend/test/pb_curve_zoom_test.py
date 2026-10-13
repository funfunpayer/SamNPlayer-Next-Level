"""Play: curve/heatmap zoom window (display/nav only).

Acceptance:
1. Zoom controls present after load.
2. Zoom in changes the zoom label away from Full.
3. Zoom selection uses heatmap marker; Reset returns Full.
4. Seek via heatmap still works in zoomed window (no stroke edit APIs).

Run: python3 cmd/gui-wails/frontend/test/pb_curve_zoom_test.py
"""

import pathlib
import sys

from playwright.sync_api import sync_playwright

from _harness import Checker, app_stub, serve

PAGE = """<!doctype html><html><body><div id="root"></div>
<script type="module">
  import { initPlayback } from '/src/playback.js';
  window.__calls = [];
  window.__pb = initPlayback(document.querySelector('#root'));
  window.__ready = true;
</script></body></html>"""

TEST_DIR = pathlib.Path(__file__).resolve().parent


def main():
    check = Checker()
    base, shutdown = serve(app_stub({
        "PickFunscriptFile": "async () => '/tmp/zoom.funscript'",
        "LoadFunscript": (
            "async () => ({ path: '/tmp/zoom.funscript', actionCount: 4, "
            "durationMs: 100000, videoPath: '', hasVideo: false })"
        ),
        "GetScriptCurve": (
            "async () => ["
            "{ atMs: 0, pos: 20 }, { atMs: 25000, pos: 80 },"
            "{ atMs: 50000, pos: 30 }, { atMs: 100000, pos: 70 }]"
        ),
        "GetHeatmap": (
            "async n => Array.from({ length: n }, (_, i) => "
            "({ atMs: Math.round(i * 100000 / n), intensity: 0.45 }))"
        ),
        "GetMarker": "async () => null",
        "SaveMarker": "async (path, a, b) => { window.__calls.push(['SaveMarker', a, b]); }",
        "VideoFileURL": "async () => ''",
        "GetSpeedHighlights": "async () => []",
        "ExportScriptHeatmapPNG": "async () => '/tmp/h.png'",
        "SavePlaybackProject": "async () => '/tmp/p.snp.json'",
        "EditCapSpeedRange": "async () => { window.__calls.push(['EditCapSpeedRange']); }",
        "EditDeleteRange": "async () => { window.__calls.push(['EditDeleteRange']); }",
        "EditScaleRange": "async () => { window.__calls.push(['EditScaleRange']); }",
        "SnapTimeMs": "async (t, fps) => t",
        "GetScriptOffset": "async () => 0",
        "GetOMarkers": "async () => []",
        "SaveOMarkers": "async () => {}",
        "GetScriptBookmarks": "async () => []",
        "SaveScriptBookmarks": "async () => {}",
        "ScriptChapters": "async () => []",
        "AnalyzeScript": "async () => ({})",
        "GetScriptChapterMarks": "async () => []",
        "SaveScriptChapterMarks": "async () => {}",
    }))
    harness = TEST_DIR / "_pb_curve_zoom_harness.html"
    harness.write_text(PAGE)
    try:
        with sync_playwright() as p:
            browser = p.chromium.launch()
            page = browser.new_page()
            page.on("pageerror", lambda e: print("   [pageerror]", e))
            page.goto(f"{base}/test/_pb_curve_zoom_harness.html")
            page.wait_for_function("window.__ready === true")

            check("zoom controls present", page.locator("#pb-curve-zoom").count() == 1)
            check("zoom row hidden before load",
                  page.locator("#pb-curve-zoom").evaluate("e => e.style.display") == "none")

            page.click("#pb-choose")
            page.wait_for_function(
                "() => document.querySelector('#pb-curve-zoom').style.display === 'flex'",
                timeout=5000)
            check("zoom row visible after load", True)
            check("label starts Full",
                  page.locator("#pb-zoom-label").inner_text().strip() == "Full")
            check("zoom selection disabled without marker",
                  page.locator("#pb-zoom-sel").is_disabled())

            page.click("#pb-zoom-in")
            page.wait_for_timeout(40)
            lab = page.locator("#pb-zoom-label").inner_text().strip()
            check("zoom in changes label", lab != "Full" and "s–" in lab, lab)

            page.click("#pb-zoom-reset")
            page.wait_for_timeout(40)
            check("reset returns Full",
                  page.locator("#pb-zoom-label").inner_text().strip() == "Full")

            box = page.locator("#pb-heatmap").bounding_box()
            y = box["y"] + box["height"] / 2
            page.mouse.move(box["x"] + box["width"] * 0.20, y)
            page.mouse.down()
            page.mouse.move(box["x"] + box["width"] * 0.55, y)
            page.mouse.up()
            page.wait_for_function(
                "!document.querySelector('#pb-zoom-sel').disabled", timeout=3000)
            check("zoom selection enabled after drag", True)
            page.click("#pb-zoom-sel")
            page.wait_for_timeout(40)
            lab2 = page.locator("#pb-zoom-label").inner_text().strip()
            check("zoom selection sets window", lab2 != "Full", lab2)

            edits = page.evaluate(
                "() => (window.__calls || []).filter(c => "
                "['EditCapSpeedRange','EditScaleRange','EditDeleteRange'].includes(c[0])).length")
            check("zoom controls do not edit stroke", edits == 0, str(edits))

            browser.close()
    finally:
        shutdown()
        harness.unlink(missing_ok=True)
    return check.report()


if __name__ == "__main__":
    sys.exit(main())
