"""Play Extended-O: Amplitude / Hold / Restore range+readout.

Acceptance:
1. Controls are range inputs with live value labels.
2. Defaults match settings (0.1 / 10 / 500).
3. Input updates readout.
4. Amplitude / Restore 0 is preserved (not coerced to fallback via ||).

Run: python3 cmd/gui-wails/frontend/test/pb_eo_knob_sliders_test.py
"""

import pathlib
import sys

from playwright.sync_api import sync_playwright

from _harness import Checker, app_stub, serve

PAGE = """<!doctype html><html><body><div id="root"></div>
<script type="module">
  import { initPlayback } from '/src/playback.js';
  window.__pb = initPlayback(document.querySelector('#root'));
  window.__ready = true;
</script></body></html>"""

TEST_DIR = pathlib.Path(__file__).resolve().parent


def main():
    check = Checker()
    base, shutdown = serve(app_stub({
        "GetSettings": (
            "async () => ({ playbackMock: false, playbackSync: 'independent', "
            "playbackTickMs: 50, playbackMaxSpeed: 0.6, playbackSmoothing: 0.3, "
            "playbackSoftStartMs: 500, playbackEOEnabled: true, "
            "playbackEOMin: 0.1, playbackEOHoldS: 10, playbackEORestoreMs: 500, "
            "playbackVideoPlayAutostart: true, playbackTrajectoryOverlay: false })"
        ),
        "SaveSetting": "async () => {}",
        "LoadFunscript": "async () => ({ durationMs: 5000, path: '/tmp/t.funscript' })",
        "GetHeatmap": "async () => ({ width: 4, height: 1, pngBase64: '' })",
        "GetScriptCurve": "async () => ({ points: [{at:0,pos:0},{at:1000,pos:100}] })",
        "GetOMarkers": "async () => []",
        "GetMarker": "async () => null",
        "GetPlaybackSource": "async () => ({ path: '/tmp/t.funscript' })",
        "StartPlayback": (
            "async (opts) => { window.__calls.push(['StartPlayback', opts]); }"
        ),
        "TriggerExtendedO": (
            "async (min, hold, restore) => { "
            "window.__calls.push(['TriggerExtendedO', min, hold, restore]); }"
        ),
        "StopPlayback": "async () => {}",
    }))
    harness = TEST_DIR / "_pb_eo_knob_sliders_harness.html"
    harness.write_text(PAGE)
    try:
        with sync_playwright() as p:
            browser = p.chromium.launch()
            page = browser.new_page()
            page.on("pageerror", lambda e: print("   [pageerror]", e))
            page.goto(f"{base}/test/_pb_eo_knob_sliders_harness.html")
            page.wait_for_function("window.__ready === true")
            page.wait_for_timeout(80)
            page.evaluate("document.querySelector('.pb-advanced').open = true")
            page.evaluate(
                "document.querySelector('#pb-eo-details').open = true")

            for sel in ("#pb-eo-min", "#pb-eo-hold", "#pb-eo-restore"):
                check(f"{sel} is range",
                      page.locator(sel).evaluate("e => e.type") == "range")
            check("Extended-O details present",
                  page.locator("#pb-eo-details").count() == 1)

            check("amplitude default 0.1",
                  page.locator("#pb-eo-min").input_value() == "0.1")
            check("amplitude label 0.1",
                  page.locator("#pb-eo-min-val").inner_text() == "0.1")
            check("hold default 10",
                  page.locator("#pb-eo-hold").input_value() == "10")
            check("restore default 500",
                  page.locator("#pb-eo-restore").input_value() == "500")

            page.evaluate("""() => {
              const h = document.querySelector('#pb-eo-hold');
              h.value = '25';
              h.dispatchEvent(new Event('input', { bubbles: true }));
            }""")
            page.wait_for_timeout(40)
            check("hold readout follows",
                  page.locator("#pb-eo-hold-val").inner_text() == "25")

            # Amplitude / Restore min=0 must not collapse via || fallback.
            page.evaluate("""() => {
              const a = document.querySelector('#pb-eo-min');
              a.value = '0';
              a.dispatchEvent(new Event('input', { bubbles: true }));
              const r = document.querySelector('#pb-eo-restore');
              r.value = '0';
              r.dispatchEvent(new Event('input', { bubbles: true }));
            }""")
            page.wait_for_timeout(40)
            check("amplitude 0 readout",
                  page.locator("#pb-eo-min-val").inner_text() == "0")
            check("restore 0 readout",
                  page.locator("#pb-eo-restore-val").inner_text() == "0")

            page.evaluate("""() => {
              window.__calls = [];
              const btn = document.querySelector('#pb-eo-trigger');
              btn.disabled = false;
              btn.click();
            }""")
            page.wait_for_function(
                "() => (window.__calls || []).some(c => c[0] === 'TriggerExtendedO')",
                timeout=3000)
            eo = page.evaluate(
                "() => (window.__calls || []).filter(c => c[0] === 'TriggerExtendedO')")
            check("TriggerExtendedO keeps amplitude 0",
                  eo and eo[0][1] == 0, str(eo))
            check("TriggerExtendedO keeps restore 0",
                  eo and eo[0][3] == 0, str(eo))

            browser.close()
    finally:
        shutdown()
        harness.unlink(missing_ok=True)
    return check.report()


if __name__ == "__main__":
    sys.exit(main())
