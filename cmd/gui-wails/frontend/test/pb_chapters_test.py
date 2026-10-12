"""Play chapter marks UI wires Get/SaveScriptChapterMarks (GUI load rule).

Run: python3 cmd/gui-wails/frontend/test/pb_chapters_test.py
"""

import pathlib
import sys

from playwright.sync_api import sync_playwright

from _harness import Checker, app_stub, serve

PAGE = """<!doctype html><html><body><div id="root"></div>
<script type="module">
  import { initPlayback } from '/src/playback.js';
  window.__calls = [];
  window.__chapters = [];
  window.__pb = initPlayback(document.querySelector('#root'));
  window.__ready = true;
</script></body></html>"""

TEST_DIR = pathlib.Path(__file__).resolve().parent


def main():
    check = Checker()
    base, shutdown = serve(app_stub({
        "PickFunscriptFile": "async () => '/tmp/test.funscript'",
        "LoadFunscript": "async () => ({ path: '/tmp/test.funscript', actionCount: 2, "
                         "durationMs: 100000, videoPath: '', hasVideo: false })",
        "GetScriptCurve": "async () => [{ atMs: 0, pos: 0 }, { atMs: 100000, pos: 100 }]",
        "GetHeatmap": "async n => Array.from({ length: n }, (_, i) => "
                      "({ atMs: i * 1000, intensity: 0.5 }))",
        "GetMarker": "async () => null",
        "SaveMarker": "async () => {}",
        "VideoFileURL": "async () => ''",
        "GetSpeedHighlights": "async () => []",
        "ExportScriptHeatmapPNG": "async () => '/tmp/h.png'",
        "SavePlaybackProject": "async () => '/tmp/p.snp.json'",
        "EditCapSpeedRange": "async () => {}",
        "EditDeleteRange": "async () => {}",
        "SnapTimeMs": "async (t, fps) => t",
        "GetScriptOffset": "async () => 0",
        "GetOMarkers": "async () => []",
        "SaveOMarkers": "async () => {}",
        "GetScriptBookmarks": "async () => []",
        "SaveScriptBookmarks": "async () => {}",
        "ScriptChapters": "async () => []",
        "AnalyzeScript": "async () => ({})",
        "GetScriptChapterMarks": "async () => window.__chapters.slice()",
        "SaveScriptChapterMarks": (
            "async (ch) => { window.__chapters = ch || []; "
            "window.__calls.push(['saveChapters', ch]); }"
        ),
    }))
    harness = TEST_DIR / "_pb_chapters_harness.html"
    harness.write_text(PAGE)
    try:
        with sync_playwright() as p:
            browser = p.chromium.launch()
            page = browser.new_page()
            page.on("pageerror", lambda e: print("   [pageerror]", e))
            page.goto(f"{base}/test/_pb_chapters_harness.html")
            page.wait_for_function("window.__ready === true")

            check("hidden before load",
                  page.locator("#pb-chapter-hint").evaluate("e => e.style.display") == "none"
                  and page.locator("#pb-chapter-add-row").evaluate(
                      "e => e.style.display") == "none")

            page.click("#pb-choose")
            page.wait_for_function(
                "document.querySelector('#pb-chapter-add-row').style.display === 'flex'",
                timeout=5000)
            page.locator("#pb-chapter-details").evaluate("el => { el.open = true }")
            check("add row after load", True)
            check("add disabled without selection",
                  page.locator("#pb-chapter-add").is_disabled())

            page.locator("#pb-heatmap").scroll_into_view_if_needed()  # layout may push it below the fold
            box = page.locator("#pb-heatmap").bounding_box()
            y = box["y"] + box["height"] / 2
            x_start = box["x"] + box["width"] * 0.10
            x_end = box["x"] + box["width"] * 0.40
            page.mouse.move(x_start, y)
            page.mouse.down()
            page.mouse.move(x_end, y)
            page.mouse.up()
            page.wait_for_function(
                "!document.querySelector('#pb-chapter-add').disabled", timeout=3000)
            check("add enabled after drag", True)

            page.fill("#pb-chapter-name", "Build")
            page.click("#pb-chapter-add")
            page.wait_for_function(
                "() => (window.__calls || []).some(c => c[0]==='saveChapters')",
                timeout=3000)
            saved = page.evaluate(
                "() => (window.__calls.find(c => c[0]==='saveChapters') || [])[1]")
            check("saved one chapter", isinstance(saved, list) and len(saved) == 1)
            check("name Build", saved and saved[0].get("name") == "Build")
            check("has start/end",
                  saved and "startTime" in saved[0] and "endTime" in saved[0]
                  and saved[0]["endTime"] > saved[0]["startTime"])
            check("list shows Build",
                  page.locator("#pb-chapter-list").inner_text().find("Build") >= 0)

            page.click("#pb-chapter-list button:text('Remove')")
            page.wait_for_function(
                "() => window.__chapters.length === 0", timeout=3000)
            check("remove clears", page.evaluate("() => window.__chapters.length") == 0)
            browser.close()
    finally:
        shutdown()
        harness.unlink(missing_ok=True)
    return check.report()


if __name__ == "__main__":
    sys.exit(main())
