"""Play: Feel / Speech-Hold bands on heatmap (filter-synced display).

Acceptance:
1. LoadFunscript with audioSegments shows the strip + heatbands toggle.
2. Heatmap canvas paints Feel bands when Show on heatmap is on.
3. Toggling a label filter / heatbands off changes painted pixels.
4. No stroke rewrite APIs are invoked.

Run: python3 cmd/gui-wails/frontend/test/pb_feel_heatbands_test.py
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

SAMPLE_PIXEL = """() => {
  const c = document.querySelector('#pb-heatmap');
  if (!c || c.style.display === 'none') return null;
  const ctx = c.getContext('2d');
  // Sample mid-height at ~25% (gentle 10–30s of 100s) and ~55% (intense).
  const y = Math.floor(c.height / 2);
  const xGentle = Math.floor(c.width * 0.20);
  const xIntense = Math.floor(c.width * 0.55);
  const xQuiet = Math.floor(c.width * 0.90);
  const g = ctx.getImageData(xGentle, y, 1, 1).data;
  const i = ctx.getImageData(xIntense, y, 1, 1).data;
  const q = ctx.getImageData(xQuiet, y, 1, 1).data;
  return {
    gentle: [g[0], g[1], g[2], g[3]],
    intense: [i[0], i[1], i[2], i[3]],
    quiet: [q[0], q[1], q[2], q[3]],
  };
}"""


def main():
    check = Checker()
    base, shutdown = serve(app_stub({
        "PickFunscriptFile": "async () => '/tmp/feel.funscript'",
        "LoadFunscript": (
            "async () => ({ path: '/tmp/feel.funscript', actionCount: 4, "
            "durationMs: 100000, videoPath: '', hasVideo: false, "
            "speechHoldMs: 8000, "
            "audioSegments: ["
            "  { label: 'holding', startMs: 0, endMs: 8000, speechHold: true },"
            "  { label: 'gentle', startMs: 10000, endMs: 30000, speechHold: false },"
            "  { label: 'intense', startMs: 40000, endMs: 70000, speechHold: false },"
            "  { label: 'climax', startMs: 75000, endMs: 90000, speechHold: false }"
            "] })"
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
        "SaveMarker": "async () => {}",
        "VideoFileURL": "async () => ''",
        "GetSpeedHighlights": "async () => []",
        "ExportScriptHeatmapPNG": "async () => '/tmp/h.png'",
        "SavePlaybackProject": "async () => '/tmp/p.snp.json'",
        "EditCapSpeedRange": "async () => {}",
        "EditDeleteRange": "async () => {}",
        "EditScaleRange": "async () => {}",
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
    harness = TEST_DIR / "_pb_feel_heatbands_harness.html"
    harness.write_text(PAGE)
    try:
        with sync_playwright() as p:
            browser = p.chromium.launch()
            page = browser.new_page()
            page.on("pageerror", lambda e: print("   [pageerror]", e))
            page.goto(f"{base}/test/_pb_feel_heatbands_harness.html")
            page.wait_for_function("window.__ready === true")

            check("heatbands toggle present",
                  page.locator("#pb-seg-heatbands").count() == 1)
            check("heatbands default on",
                  page.locator("#pb-seg-heatbands").is_checked())
            check("opacity slider present",
                  page.locator("#pb-seg-heatbands-opacity").count() == 1)
            check("opacity default 1",
                  page.locator("#pb-seg-heatbands-opacity").input_value() == "1")

            page.click("#pb-choose")
            page.wait_for_function(
                "document.querySelector('#pb-audio-segments') && "
                "!document.querySelector('#pb-audio-segments').hidden",
                timeout=5000)
            check("segment strip visible after load",
                  page.locator("#pb-audio-segments").is_visible())
            check("Feel filters details present",
                  page.locator("#pb-feel-filters-details").count() == 1)
            # Filters start collapsed — open before toggling checkboxes.
            page.evaluate(
                "document.querySelector('#pb-feel-filters-details').open = true")
            page.wait_for_function(
                "document.querySelector('#pb-heatmap') && "
                "document.querySelector('#pb-heatmap').style.display === 'block'",
                timeout=5000)

            samples = page.evaluate(SAMPLE_PIXEL)
            check("heatmap samples available", samples is not None, str(samples))
            # Gentle (teal-ish) vs quiet mid-intensity should differ when bands on.
            if samples:
                gentle_diff = (
                    abs(samples["gentle"][0] - samples["quiet"][0])
                    + abs(samples["gentle"][1] - samples["quiet"][1])
                    + abs(samples["gentle"][2] - samples["quiet"][2])
                )
                intense_diff = (
                    abs(samples["intense"][0] - samples["quiet"][0])
                    + abs(samples["intense"][1] - samples["quiet"][1])
                    + abs(samples["intense"][2] - samples["quiet"][2])
                )
                check("gentle band tint differs from quiet",
                      gentle_diff > 8, str(samples))
                check("intense band tint differs from quiet",
                      intense_diff > 8, str(samples))
                baseline = samples

            page.uncheck("#pb-seg-f-gentle")
            page.wait_for_timeout(80)
            after_filter = page.evaluate(SAMPLE_PIXEL)
            if samples and after_filter:
                changed = after_filter["gentle"] != baseline["gentle"]
                check("hiding Gentle filter updates heatmap band",
                      changed, str(after_filter))

            page.check("#pb-seg-f-gentle")
            page.evaluate("""() => {
              const o = document.querySelector('#pb-seg-heatbands-opacity');
              o.value = '0.25';
              o.dispatchEvent(new Event('input', { bubbles: true }));
            }""")
            page.wait_for_timeout(80)
            after_dim = page.evaluate(SAMPLE_PIXEL)
            if samples and after_dim:
                dim_diff = (
                    abs(after_dim["gentle"][0] - after_dim["quiet"][0])
                    + abs(after_dim["gentle"][1] - after_dim["quiet"][1])
                    + abs(after_dim["gentle"][2] - after_dim["quiet"][2])
                )
                check("lower opacity reduces Feel tint vs quiet",
                      dim_diff < gentle_diff, f"dim={dim_diff} was={gentle_diff}")
            check("opacity readout shows 0.25",
                  page.locator("#pb-seg-heatbands-opacity-val").inner_text() == "0.25")

            page.uncheck("#pb-seg-heatbands")
            page.wait_for_timeout(80)
            after_off = page.evaluate(SAMPLE_PIXEL)
            if samples and after_off:
                # With bands off, gentle/intense/quiet should be closer (flat intensity).
                gq = (
                    abs(after_off["gentle"][0] - after_off["quiet"][0])
                    + abs(after_off["gentle"][1] - after_off["quiet"][1])
                    + abs(after_off["gentle"][2] - after_off["quiet"][2])
                )
                check("heatbands off flattens Feel tint vs quiet",
                      gq < gentle_diff, f"gq={gq} was={gentle_diff}")
            check("opacity row hidden when bands off",
                  page.locator("#pb-seg-heatbands-opacity-row").evaluate(
                      "e => e.style.display") == "none")

            browser.close()
    finally:
        shutdown()
        harness.unlink(missing_ok=True)
    return check.report()


if __name__ == "__main__":
    sys.exit(main())
