"""Play O-marker secondary Intensity: range + live readout.

Acceptance:
1. #pb-omarker-intensity is a range input.
2. Default 0.5 with matching #pb-omarker-intensity-val.
3. Input updates the readout.
4. Row stays hidden until kind=secondary.

Run: python3 cmd/gui-wails/frontend/test/pb_omarker_intensity_knob_test.py
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
    }))
    harness = TEST_DIR / "_pb_omarker_intensity_knob_harness.html"
    harness.write_text(PAGE)
    try:
        with sync_playwright() as p:
            browser = p.chromium.launch()
            page = browser.new_page()
            page.on("pageerror", lambda e: print("   [pageerror]", e))
            page.goto(f"{base}/test/_pb_omarker_intensity_knob_harness.html")
            page.wait_for_function("window.__ready === true")
            page.wait_for_timeout(80)

            check("intensity is range",
                  page.locator("#pb-omarker-intensity").evaluate("e => e.type") == "range")
            check("default 0.5",
                  page.locator("#pb-omarker-intensity").input_value() == "0.5")
            check("label default 0.5",
                  page.locator("#pb-omarker-intensity-val").inner_text() == "0.5")
            check("row hidden for primary",
                  page.locator("#pb-omarker-intensity-row").evaluate(
                      "e => e.style.display") == "none")

            # Add-row is hidden until a script loads; force visibility for knob UX.
            page.evaluate("""() => {
              document.querySelector('#pb-omarker-add-row').style.display = 'flex';
              const kind = document.querySelector('#pb-omarker-kind');
              kind.value = 'secondary';
              kind.dispatchEvent(new Event('change', { bubbles: true }));
            }""")
            page.wait_for_timeout(40)
            check("row visible for secondary",
                  page.locator("#pb-omarker-intensity-row").evaluate(
                      "e => e.style.display") == "flex")

            page.evaluate("""() => {
              const h = document.querySelector('#pb-omarker-intensity');
              h.value = '0.75';
              h.dispatchEvent(new Event('input', { bubbles: true }));
            }""")
            page.wait_for_timeout(40)
            check("readout follows",
                  page.locator("#pb-omarker-intensity-val").inner_text() == "0.75")

            browser.close()
    finally:
        shutdown()
        harness.unlink(missing_ok=True)
    return check.report()


if __name__ == "__main__":
    sys.exit(main())
