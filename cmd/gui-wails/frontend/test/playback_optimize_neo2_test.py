"""Play: Optimize for Neo 2 on an imported community funscript.

Run: python3 cmd/gui-wails/frontend/test/playback_optimize_neo2_test.py
"""

import pathlib
import sys

from playwright.sync_api import sync_playwright

from _harness import Checker, app_stub, serve

FRONTEND = pathlib.Path(__file__).resolve().parents[1]

PAGE = """<!doctype html><html><body><div id="root"></div>
<script type="module">
  import { initPlayback } from '/src/playback.js';
  window.__pb = initPlayback(document.querySelector('#root'));
  window.__ready = true;
</script></body></html>"""


def main():
    check = Checker()
    base, shutdown = serve(app_stub({
        "PickFunscriptFile": "async () => '/tmp/imported.funscript'",
        "LoadFunscript": (
            "async () => ({ path: '/tmp/imported.funscript', actionCount: 4, "
            "durationMs: 5000, videoPath: '', hasVideo: false, "
            "nativeFormat: false, hasNeoAxes: false, contactVibration: false, "
            "profile: 'standard', playbackSource: 'recipe' })"
        ),
        "GetScriptCurve": "async () => [{ atMs: 0, pos: 0 }, { atMs: 5000, pos: 100 }]",
        "GetScriptAxisActions": "async () => [{ at: 0, pos: 0 }, { at: 5000, pos: 100 }]",
        "GetScriptActions": "async () => [{ at: 0, pos: 0 }, { at: 5000, pos: 100 }]",
        "GetHeatmap": "async () => []",
        "GetSpeedHighlights": "async () => []",
        "GetMarker": "async () => null",
        "GetOMarkers": "async () => []",
        "GetScriptOffset": "async () => 0",
        "GetStrengthPresets": "async () => ({ presets: [], active: '' })",
        "AnalyzeScript": "async () => ({})",
        "OptimizeLoadedForNeo2": (
            "async (fill) => { window.__calls.push(['OptimizeLoadedForNeo2', fill]); "
            "return { path: '/tmp/imported.samn', message: 'Neo 2 ready · axes baked', "
            "baked: true, contactOn: true, gapsFilled: 1, pointsAdded: 3, hasNeoAxes: true }; }"
        ),
        "BakeNeoAxesOnLoaded": "async () => '/tmp/imported.samn'",
        "SaveLoadedAsSamn": "async () => '/tmp/imported.samn'",
        "SaveScriptAxisActions": "async () => {}",
    }))
    harness = FRONTEND / "test" / "_playback_optimize_neo2_harness.html"
    harness.write_text(PAGE)

    with sync_playwright() as p:
        browser = p.chromium.launch()
        page = browser.new_page()
        page.on("pageerror", lambda e: print("   [pageerror]", e))
        page.goto(f"{base}/test/_playback_optimize_neo2_harness.html")
        page.wait_for_function("window.__ready === true")

        page.click("#pb-choose")
        page.wait_for_function(
            "document.querySelector('#pb-optimize-neo2') && "
            "!document.querySelector('#pb-axis-row').hidden && "
            "document.querySelector('#pb-axis-row').style.display !== 'none'",
            timeout=5000)
        check("Optimize for Neo 2 button visible",
              page.locator("#pb-optimize-neo2").count() == 1)

        page.click("#pb-optimize-neo2")
        page.wait_for_function(
            "() => (window.__calls || []).some(c => c[0] === 'OptimizeLoadedForNeo2')",
            timeout=5000)
        calls = page.evaluate(
            "() => (window.__calls || []).filter(c => c[0] === 'OptimizeLoadedForNeo2')")
        check("OptimizeLoadedForNeo2 called with fillGaps",
              calls and calls[0][1] is True, str(calls))
        page.wait_for_function(
            "document.querySelector('#pb-optimize-neo2-status') && "
            "document.querySelector('#pb-optimize-neo2-status').textContent.includes('Neo 2')",
            timeout=5000)
        check("Status shows Neo 2 ready",
              "Neo 2" in page.locator("#pb-optimize-neo2-status").inner_text())

        browser.close()

    shutdown()
    harness.unlink(missing_ok=True)
    return check.report()


if __name__ == "__main__":
    sys.exit(main())
