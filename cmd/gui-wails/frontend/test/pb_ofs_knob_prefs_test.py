"""Play OFS / Feel knobs persist via settings (MakeVib-style wiring).

Acceptance:
1. Cap / HL / Scale / FPS-Snap / O-marker Intensity / Feel opacity / Contact
   Strength restore from GetSettings (incl. Strength 0 via finiteOr).
2. Changing those knobs calls SetSetting with the matching playback.* keys.
3. Defaults in stub match HTML defaults when fields are present.
4. BPM number is not persisted (blank = auto).

Run: python3 cmd/gui-wails/frontend/test/pb_ofs_knob_prefs_test.py
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

SETTINGS = (
    "async () => ({ playbackMock: false, playbackSync: 'independent', "
    "playbackTickMs: 50, playbackMaxSpeed: 0.6, playbackSmoothing: 0.3, "
    "playbackSoftStartMs: 500, playbackEOEnabled: true, "
    "playbackEOMin: 0.1, playbackEOHoldS: 10, playbackEORestoreMs: 500, "
    "playbackVideoPlayAutostart: true, playbackTrajectoryOverlay: false, "
    "playbackCapIntensity: 250, playbackSpeedHL: false, "
    "playbackSpeedHLThresh: 300, playbackScaleFactor: 1.2, "
    "playbackScaleSoftEdges: true, playbackFpsSnap: 30, "
    "playbackBpmGrid: true, playbackOMarkerIntensity: 0.25, "
    "playbackFeelHeatbands: false, playbackFeelHeatbandsOpacity: 0.4, "
    "playbackContactIntensity: 0 })"
)


def main():
    check = Checker()
    base, shutdown = serve(app_stub({
        "GetSettings": SETTINGS,
        "SetSetting": (
            "async (k, v) => { window.__calls.push(['SetSetting', k, v]); }"
        ),
    }))
    harness = TEST_DIR / "_pb_ofs_knob_prefs_harness.html"
    harness.write_text(PAGE)
    try:
        with sync_playwright() as p:
            browser = p.chromium.launch()
            page = browser.new_page()
            page.on("pageerror", lambda e: print("   [pageerror]", e))
            page.goto(f"{base}/test/_pb_ofs_knob_prefs_harness.html")
            page.wait_for_function("window.__ready === true")
            page.wait_for_timeout(120)

            check("cap restored 250",
                  page.locator("#pb-cap-intensity").input_value() == "250")
            check("cap label 250",
                  page.locator("#pb-cap-intensity-val").inner_text() == "250")
            check("speed HL off",
                  page.locator("#pb-speed-hl").is_checked() is False)
            check("HL thresh 300",
                  page.locator("#pb-speed-hl-thresh").input_value() == "300")
            check("scale 1.2",
                  page.locator("#pb-scale-factor").input_value() == "1.2")
            check("soft edges on",
                  page.locator("#pb-scale-soft-edges").is_checked() is True)
            check("fps snap 30",
                  page.locator("#pb-fps-snap").input_value() == "30")
            check("fps label 30",
                  page.locator("#pb-fps-snap-val").inner_text() == "30")
            check("bpm grid on",
                  page.locator("#pb-bpm-grid").is_checked() is True)
            check("omarker intensity 0.25",
                  page.locator("#pb-omarker-intensity").input_value() == "0.25")
            check("feel heatbands off",
                  page.locator("#pb-seg-heatbands").is_checked() is False)
            check("feel opacity 0.4",
                  page.locator("#pb-seg-heatbands-opacity").input_value() == "0.4")
            page.evaluate(
                "document.querySelector('#pb-contact-tune-details') && "
                "(document.querySelector('#pb-contact-tune-details').open = true)")
            check("contact strength 0 preserved",
                  page.locator("#pb-contact-intensity").input_value() == "0")
            check("contact strength label 0.00",
                  page.locator("#pb-contact-intensity-val").inner_text() == "0.00")

            page.evaluate("window.__calls = []")
            page.evaluate("""() => {
              const el = document.querySelector('#pb-cap-intensity');
              el.value = '500';
              el.dispatchEvent(new Event('change', { bubbles: true }));
            }""")
            page.wait_for_timeout(40)
            calls = page.evaluate(
                "window.__calls.filter(c => c[0]==='SetSetting' && c[1]==='playback.cap_intensity')")
            check("cap SetSetting", len(calls) == 1 and calls[0][2] == 500)

            page.evaluate("""() => {
              const el = document.querySelector('#pb-fps-snap');
              el.value = '0';
              el.dispatchEvent(new Event('change', { bubbles: true }));
            }""")
            page.wait_for_timeout(40)
            fps = page.evaluate(
                "window.__calls.filter(c => c[0]==='SetSetting' && c[1]==='playback.fps_snap').pop()")
            check("fps snap 0 SetSetting", fps is not None and fps[2] == 0)

            page.evaluate("""() => {
              const el = document.querySelector('#pb-contact-intensity');
              el.value = '0';
              el.dispatchEvent(new Event('change', { bubbles: true }));
            }""")
            page.wait_for_timeout(40)
            ci = page.evaluate(
                "window.__calls.filter(c => c[0]==='SetSetting' && c[1]==='playback.contact_intensity').pop()")
            check("contact intensity 0 SetSetting", ci is not None and ci[2] == 0)

            page.evaluate("""() => {
              const el = document.querySelector('#pb-omarker-intensity');
              el.value = '0.75';
              el.dispatchEvent(new Event('change', { bubbles: true }));
            }""")
            page.wait_for_timeout(40)
            om = page.evaluate(
                "window.__calls.filter(c => c[0]==='SetSetting' && c[1]==='playback.omarker_intensity').pop()")
            check("omarker SetSetting", om is not None and abs(om[2] - 0.75) < 1e-6)

            browser.close()
    finally:
        harness.unlink(missing_ok=True)
        shutdown()
    return check.report()


if __name__ == "__main__":
    sys.exit(main())
