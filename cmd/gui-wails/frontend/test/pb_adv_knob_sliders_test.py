"""Play Advanced: Tick / Max-Speed / Smoothing / Soft-Start range+readout.

Acceptance:
1. Controls are range inputs with live value labels.
2. Defaults match settings defaults (50 / 0.6 / 0.3 / 500).
3. Dragging updates the readout (no Create / stroke APIs).

Run: python3 cmd/gui-wails/frontend/test/pb_adv_knob_sliders_test.py
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
        "GetSettings": (
            "async () => ({ playbackMock: false, playbackSync: 'independent', "
            "playbackTickMs: 50, playbackMaxSpeed: 0.6, playbackSmoothing: 0.3, "
            "playbackSoftStartMs: 500, playbackEOEnabled: true, "
            "playbackEOMin: 0.1, playbackEOHoldS: 10, playbackEORestoreMs: 500, "
            "playbackVideoPlayAutostart: true, playbackTrajectoryOverlay: false })"
        ),
        "SaveSetting": (
            "async (k, v) => { window.__calls.push(['SaveSetting', k, v]); }"
        ),
    }))
    harness = TEST_DIR / "_pb_adv_knob_sliders_harness.html"
    harness.write_text(PAGE)
    try:
        with sync_playwright() as p:
            browser = p.chromium.launch()
            page = browser.new_page()
            page.on("pageerror", lambda e: print("   [pageerror]", e))
            page.goto(f"{base}/test/_pb_adv_knob_sliders_harness.html")
            page.wait_for_function("window.__ready === true")
            page.wait_for_timeout(80)
            page.evaluate("document.querySelector('.pb-advanced').open = true")

            for sel in ("#pb-tick", "#pb-maxspeed", "#pb-smoothing", "#pb-softstart"):
                check(f"{sel} is range",
                      page.locator(sel).evaluate("e => e.type") == "range")

            check("tick default 50", page.locator("#pb-tick").input_value() == "50")
            check("tick label 50", page.locator("#pb-tick-val").inner_text() == "50")
            check("maxspeed default 0.6",
                  page.locator("#pb-maxspeed").input_value() == "0.6")
            check("maxspeed label 0.6",
                  page.locator("#pb-maxspeed-val").inner_text() == "0.6")
            check("smoothing default 0.3",
                  page.locator("#pb-smoothing").input_value() == "0.3")
            check("softstart default 500",
                  page.locator("#pb-softstart").input_value() == "500")
            check("softstart label 500",
                  page.locator("#pb-softstart-val").inner_text() == "500")

            page.evaluate("""() => {
              const t = document.querySelector('#pb-tick');
              t.value = '100';
              t.dispatchEvent(new Event('input', { bubbles: true }));
            }""")
            page.wait_for_timeout(40)
            check("tick readout follows",
                  page.locator("#pb-tick-val").inner_text() == "100")
            page.evaluate("""() => {
              const s = document.querySelector('#pb-smoothing');
              s.value = '0.55';
              s.dispatchEvent(new Event('input', { bubbles: true }));
            }""")
            page.wait_for_timeout(40)
            check("smoothing readout follows",
                  page.locator("#pb-smoothing-val").inner_text() in ("0.55", "0.6"),
                  page.locator("#pb-smoothing-val").inner_text())

            browser.close()
    finally:
        shutdown()
        harness.unlink(missing_ok=True)
    return check.report()


if __name__ == "__main__":
    sys.exit(main())
