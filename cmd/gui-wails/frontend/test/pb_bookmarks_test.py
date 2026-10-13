"""Play bookmarks UI wires Get/SaveScriptBookmarks (GUI load rule).

Run: python3 cmd/gui-wails/frontend/test/pb_bookmarks_test.py
"""

import pathlib
import sys

from playwright.sync_api import sync_playwright

from _harness import Checker, app_stub, serve

PAGE = """<!doctype html><html><body><div id="root"></div>
<script type="module">
  import { initPlayback } from '/src/playback.js';
  window.__calls = [];
  window.__bookmarks = [];
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
        "GetScriptBookmarks": "async () => window.__bookmarks.slice()",
        "SaveScriptBookmarks": (
            "async (bm) => { window.__bookmarks = bm || []; "
            "window.__calls.push(['saveBookmarks', bm]); }"
        ),
    }))
    harness = TEST_DIR / "_pb_bookmarks_harness.html"
    harness.write_text(PAGE)
    try:
        with sync_playwright() as p:
            browser = p.chromium.launch()
            page = browser.new_page()
            page.on("pageerror", lambda e: print("   [pageerror]", e))
            page.goto(f"{base}/test/_pb_bookmarks_harness.html")
            page.wait_for_function("window.__ready === true")

            check("hidden before load",
                  page.locator("#pb-bookmark-hint").evaluate("e => e.style.display") == "none"
                  and page.locator("#pb-bookmark-add-row").evaluate(
                      "e => e.style.display") == "none")

            page.click("#pb-choose")
            page.wait_for_function(
                "document.querySelector('#pb-bookmark-add-row').style.display === 'flex'",
                timeout=5000)
            page.locator("#pb-bookmark-details").evaluate("el => { el.open = true }")
            check("add row after load", True)

            page.fill("#pb-bookmark-name", "Peak")
            page.click("#pb-bookmark-add")
            page.wait_for_function(
                "() => (window.__calls || []).some(c => c[0]==='saveBookmarks')",
                timeout=3000)
            saved = page.evaluate(
                "() => (window.__calls.find(c => c[0]==='saveBookmarks') || [])[1]")
            check("saved one bookmark", isinstance(saved, list) and len(saved) == 1)
            check("name Peak", saved and saved[0].get("name") == "Peak")
            check("list shows Peak", page.locator("#pb-bookmark-list").inner_text().find("Peak") >= 0)
            check("rename control present",
                  page.locator("#pb-bookmark-list button:text('Rename')").count() == 1)

            page.once("dialog", lambda d: d.accept("Climax"))
            page.click("#pb-bookmark-list button:text('Rename')")
            page.wait_for_function(
                "() => (window.__bookmarks[0] || {}).name === 'Climax'",
                timeout=3000)
            renamed = page.evaluate("() => window.__bookmarks[0]")
            check("rename persists Climax",
                  renamed and renamed.get("name") == "Climax", str(renamed))
            check("list shows Climax",
                  page.locator("#pb-bookmark-list").inner_text().find("Climax") >= 0)

            page.click("#pb-bookmark-list button:text('Remove')")
            page.wait_for_function(
                "() => window.__bookmarks.length === 0", timeout=3000)
            check("remove clears", page.evaluate("() => window.__bookmarks.length") == 0)
            browser.close()
    finally:
        shutdown()
        harness.unlink(missing_ok=True)
    return check.report()


if __name__ == "__main__":
    sys.exit(main())
