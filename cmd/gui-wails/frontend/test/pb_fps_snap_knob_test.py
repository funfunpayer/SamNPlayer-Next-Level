"""Play FPS-Snap: range + live readout (0 = off).

Acceptance:
1. #pb-fps-snap is a range input (0–60, default 0).
2. #pb-fps-snap-val shows "off" at 0 and the fps when >0.
3. Input updates the readout.

Run: python3 cmd/gui-wails/frontend/test/pb_fps_snap_knob_test.py
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
    harness = TEST_DIR / "_pb_fps_snap_knob_harness.html"
    harness.write_text(PAGE)
    try:
        with sync_playwright() as p:
            browser = p.chromium.launch()
            page = browser.new_page()
            page.on("pageerror", lambda e: print("   [pageerror]", e))
            page.goto(f"{base}/test/_pb_fps_snap_knob_harness.html")
            page.wait_for_function("window.__ready === true")
            page.wait_for_timeout(80)

            check("fps-snap is range",
                  page.locator("#pb-fps-snap").evaluate("e => e.type") == "range")
            check("default 0",
                  page.locator("#pb-fps-snap").input_value() == "0")
            check("label off at 0",
                  page.locator("#pb-fps-snap-val").inner_text() == "off")
            check("min 0",
                  page.locator("#pb-fps-snap").evaluate("e => e.min") == "0")
            check("max 60",
                  page.locator("#pb-fps-snap").evaluate("e => e.max") == "60")

            page.evaluate("""() => {
              const h = document.querySelector('#pb-fps-snap');
              h.value = '30';
              h.dispatchEvent(new Event('input', { bubbles: true }));
            }""")
            page.wait_for_timeout(40)
            check("readout follows 30",
                  page.locator("#pb-fps-snap-val").inner_text() == "30")

            browser.close()
    finally:
        shutdown()
        harness.unlink(missing_ok=True)
    return check.report()


if __name__ == "__main__":
    sys.exit(main())
