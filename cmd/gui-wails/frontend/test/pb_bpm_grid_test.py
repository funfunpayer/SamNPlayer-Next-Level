"""Play: optional BPM / tempo grid on the curve (display only).

Acceptance:
1. BPM grid checkbox + BPM input present in OFS row.
2. Enabling grid with BPM draws vertical beat lines (pixel sample differs).
3. LoadFunscript audioHz prefills placeholder (auto BPM).
4. No stroke-edit APIs invoked when toggling the grid.

Run: python3 cmd/gui-wails/frontend/test/pb_bpm_grid_test.py
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

SAMPLE_COL = """() => {
  const c = document.querySelector('#pb-curve');
  if (!c || c.width < 10) return null;
  const ctx = c.getContext('2d');
  // Sample a vertical column at ~25% width (should hit a beat line at 120 BPM / 10s).
  const x = Math.floor(c.width * 0.25);
  let sum = 0;
  for (let y = 2; y < c.height - 2; y += 2) {
    const p = ctx.getImageData(x, y, 1, 1).data;
    sum += p[0] + p[1] + p[2];
  }
  return sum;
}"""


def main():
    check = Checker()
    base, shutdown = serve(app_stub({
        "PickFunscriptFile": "async () => '/tmp/bpm.funscript'",
        "LoadFunscript": (
            "async () => ({ path: '/tmp/bpm.funscript', actionCount: 4, "
            "durationMs: 10000, videoPath: '', hasVideo: false, "
            "audioHz: 2.0, speechHoldMs: 0, audioSegments: [] })"
        ),
        "GetScriptCurve": (
            "async () => ["
            "{ atMs: 0, pos: 20 }, { atMs: 2500, pos: 80 },"
            "{ atMs: 5000, pos: 30 }, { atMs: 10000, pos: 70 }]"
        ),
        "GetHeatmap": (
            "async n => Array.from({ length: n }, (_, i) => "
            "({ atMs: Math.round(i * 10000 / n), intensity: 0.4 }))"
        ),
        "GetMarker": "async () => null",
        "SaveMarker": "async () => {}",
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
    harness = TEST_DIR / "_pb_bpm_grid_harness.html"
    harness.write_text(PAGE)
    try:
        with sync_playwright() as p:
            browser = p.chromium.launch()
            page = browser.new_page()
            page.on("pageerror", lambda e: print("   [pageerror]", e))
            page.goto(f"{base}/test/_pb_bpm_grid_harness.html")
            page.wait_for_function("window.__ready === true")

            check("BPM grid toggle present", page.locator("#pb-bpm-grid").count() == 1)
            check("BPM input present", page.locator("#pb-bpm").count() == 1)
            check("BPM grid default off", not page.locator("#pb-bpm-grid").is_checked())

            page.click("#pb-choose")
            page.wait_for_function(
                "() => document.querySelector('#pb-ofs-row').style.display === 'flex'",
                timeout=5000)
            page.wait_for_timeout(120)
            page.locator("#pb-ofs-edit-details").evaluate("el => { el.open = true }")

            ph = page.locator("#pb-bpm").get_attribute("placeholder") or ""
            check("audioHz prefills auto BPM placeholder (~120)",
                  "120" in ph or "auto" in ph.lower(), ph)

            before = page.evaluate(SAMPLE_COL)
            page.fill("#pb-bpm", "120")
            page.check("#pb-bpm-grid")
            page.wait_for_timeout(80)
            after = page.evaluate(SAMPLE_COL)
            check("enabling BPM grid changes curve pixels",
                  before is not None and after is not None and after != before,
                  f"before={before} after={after}")

            edits = page.evaluate(
                "() => (window.__calls || []).filter(c => "
                "['EditCapSpeedRange','EditScaleRange','EditDeleteRange'].includes(c[0])).length")
            check("toggling BPM grid does not edit stroke", edits == 0, str(edits))

            browser.close()
    finally:
        shutdown()
        harness.unlink(missing_ok=True)
    return check.report()


if __name__ == "__main__":
    sys.exit(main())
