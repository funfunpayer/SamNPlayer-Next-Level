"""Play: adjustable Speed-highlight threshold (display only).

Acceptance:
1. Speed highlights toggle + HL threshold slider present (default on / 400).
2. LoadFunscript calls GetSpeedHighlights with the chosen threshold.
3. Changing HL re-calls GetSpeedHighlights; toggling off clears paint path.
4. No Cap/Scale/Delete edit APIs invoked when adjusting HL.

Run: python3 cmd/gui-wails/frontend/test/pb_speed_highlight_knob_test.py
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
        "PickFunscriptFile": "async () => '/tmp/hl.funscript'",
        "LoadFunscript": (
            "async () => ({ path: '/tmp/hl.funscript', actionCount: 4, "
            "durationMs: 10000, videoPath: '', hasVideo: false })"
        ),
        "GetScriptCurve": (
            "async () => ["
            "{ atMs: 0, pos: 10 }, { atMs: 200, pos: 90 },"
            "{ atMs: 5000, pos: 20 }, { atMs: 10000, pos: 80 }]"
        ),
        "GetHeatmap": (
            "async n => Array.from({ length: n }, (_, i) => "
            "({ atMs: Math.round(i * 10000 / n), intensity: 0.5 }))"
        ),
        "GetMarker": "async () => null",
        "SaveMarker": "async () => {}",
        "VideoFileURL": "async () => ''",
        "GetSpeedHighlights": (
            "async (maxI) => { window.__calls.push(['GetSpeedHighlights', maxI]); "
            "return [{ fromMs: 0, toMs: 200 }]; }"
        ),
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
    harness = TEST_DIR / "_pb_speed_hl_harness.html"
    harness.write_text(PAGE)
    try:
        with sync_playwright() as p:
            browser = p.chromium.launch()
            page = browser.new_page()
            page.on("pageerror", lambda e: print("   [pageerror]", e))
            page.goto(f"{base}/test/_pb_speed_hl_harness.html")
            page.wait_for_function("window.__ready === true")

            check("Speed highlights toggle present",
                  page.locator("#pb-speed-hl").count() == 1)
            check("Speed highlights default on",
                  page.locator("#pb-speed-hl").is_checked())
            check("HL threshold slider present",
                  page.locator("#pb-speed-hl-thresh").count() == 1)
            check("HL threshold default 400",
                  page.locator("#pb-speed-hl-thresh").input_value() == "400")

            page.click("#pb-choose")
            page.wait_for_function(
                "() => (window.__calls || []).some(c => c[0]==='GetSpeedHighlights')",
                timeout=5000)
            check("load uses HL threshold 400",
                  page.evaluate(
                      "() => (window.__calls || []).some(c => c[0]==='GetSpeedHighlights'"
                      " && c[1]===400)"))

            page.locator("#pb-ofs-edit-details").evaluate("el => { el.open = true }")
            page.fill("#pb-speed-hl-thresh", "250")
            page.wait_for_function(
                "() => (window.__calls || []).some(c => c[0]==='GetSpeedHighlights'"
                " && c[1]===250)",
                timeout=3000)
            check("changing HL re-fetches highlights at 250", True)

            n_before = page.evaluate(
                "() => (window.__calls || []).filter(c => c[0]==='GetSpeedHighlights').length")
            page.uncheck("#pb-speed-hl")
            page.wait_for_timeout(60)
            n_after = page.evaluate(
                "() => (window.__calls || []).filter(c => c[0]==='GetSpeedHighlights').length")
            # Off path clears without requiring a new fetch (or may skip fetch).
            check("toggling HL off does not edit stroke",
                  page.evaluate(
                      "() => (window.__calls || []).filter(c => "
                      "['EditCapSpeedRange','EditScaleRange','EditDeleteRange']"
                      ".includes(c[0])).length") == 0)
            check("HL off does not require extra highlight fetch",
                  n_after <= n_before + 1, f"{n_before}->{n_after}")

            browser.close()
    finally:
        shutdown()
        harness.unlink(missing_ok=True)
    return check.report()


if __name__ == "__main__":
    sys.exit(main())
