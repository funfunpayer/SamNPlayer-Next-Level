"""Create Feel + Expert knobs persist via settings.

Acceptance:
1. Span / Curve / Smooth / Peakdist / Prominence / RDP / Max-speed / Adaptive
   restore from GetSettings (incl. Smooth/Peakdist/Prominence/RDP/Max-speed 0).
2. Changing those knobs calls SetSetting with generator.* keys.
3. Restored Curve sets userTouched so soft-default does not overwrite.

Run: python3 cmd/gui-wails/frontend/test/create_feel_expert_prefs_test.py
"""

import pathlib
import sys

from playwright.sync_api import sync_playwright

from _harness import Checker, app_stub, serve

PAGE = """<!doctype html><html><body><div id="root"></div>
<script type="module">
  import { initGenerator } from '/src/generator.js';
  window.__calls = [];
  initGenerator(document.querySelector('#root'), { loadScriptPath: async () => {} });
  window.__ready = true;
</script></body></html>"""

TEST_DIR = pathlib.Path(__file__).resolve().parent

TINY_PNG_B64 = ("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNk"
                "+A8AAQUBAScY42YAAAAASUVORK5CYII=")

SETTINGS = (
    "async () => ({ applyAISetupAutomatically: false, "
    "genContactVibrationSpan: 55, genContactVibrationCurve: 'peak', "
    "genSmoothWindow: 0, genMinPeakDistanceMs: 0, genPeakProminence: 0.4, "
    "genRDPTolerance: 0, genMaxSpeed: 0, genAdaptiveKeyframe: false })"
)


def main():
    check = Checker()
    base, shutdown = serve(app_stub({
        "GetSettings": SETTINGS,
        "SetSetting": (
            "async (k, v) => { window.__calls.push(['SetSetting', k, v]); }"
        ),
        "PickVideoFile": "async () => '/tmp/clip.mp4'",
        "LoadFirstFrame": f"async () => ({{ width: 640, height: 360, "
                          f"pngBase64: '{TINY_PNG_B64}' }})",
        "LoadFrameAt": f"async () => ({{ width: 640, height: 360, "
                       f"pngBase64: '{TINY_PNG_B64}' }})",
        "SuggestPipeline": "async () => ({ Backend: 'csrt', Profile: 'standard', "
                           "Reason: 'test', GoPath: true })",
        "SuggestProfile": "async () => ({ found: false })",
        "CheckAIRoiAvailable": "async () => false",
        "CheckAudioCheckAvailable": "async () => true",
        "ScriptExistsForVideo": "async () => false",
        "PreviewPostprocess": (
            "async (req) => { window.__calls.push(['PreviewPostprocess', req]); "
            "return { keyframeCount: 4, peakCount: 2, valleyCount: 2, meanHz: 1.0, "
            "hint: 'ok', sample: [{atMs:0,pos:10},{atMs:1000,pos:90}], "
            "rawSample: [{atMs:0,pos:10},{atMs:1000,pos:90}], peaks: [] }; }"
        ),
        "PreviewContactVibration": (
            "async (req) => ({ activePct: 40, peakVib: 0.5, "
            "sample: [{atMs:0,pos:20,vib:0},{atMs:500,pos:80,vib:0.5}] })"
        ),
    }))
    harness = TEST_DIR / "_create_feel_expert_prefs_harness.html"
    harness.write_text(PAGE)
    try:
        with sync_playwright() as p:
            browser = p.chromium.launch()
            page = browser.new_page()
            page.on("pageerror", lambda e: print("   [pageerror]", e))
            page.goto(f"{base}/test/_create_feel_expert_prefs_harness.html")
            page.wait_for_function("window.__ready === true")
            page.wait_for_timeout(150)

            check("span restored 55",
                  page.locator("#gen-contact-span").input_value() == "55")
            check("curve restored peak",
                  page.locator("#gen-contact-curve").input_value() == "peak")
            check("curve userTouched",
                  page.locator("#gen-contact-curve").evaluate(
                      "e => e.dataset.userTouched === '1'"))
            check("smooth 0 preserved",
                  page.locator("#gen-smooth").input_value() == "0")
            check("peakdist 0 preserved",
                  page.locator("#gen-peakdist").input_value() == "0")
            check("prominence 0.4",
                  page.locator("#gen-prominence").input_value() == "0.4")
            check("rdp 0",
                  page.locator("#gen-rdp").input_value() == "0")
            check("maxspeed 0",
                  page.locator("#gen-maxspeed").input_value() == "0")
            check("adaptive off",
                  page.locator("#gen-adaptive").is_checked() is False)

            page.evaluate("window.__calls = []")
            page.evaluate("""() => {
              const el = document.querySelector('#gen-contact-span');
              el.value = '85';
              el.dispatchEvent(new Event('change', { bubbles: true }));
            }""")
            page.wait_for_timeout(40)
            span = page.evaluate(
                "window.__calls.filter(c => c[0]==='SetSetting' "
                "&& c[1]==='generator.contact_vibration_span').pop()")
            check("span SetSetting", span is not None and span[2] == 85)

            page.evaluate("""() => {
              const el = document.querySelector('#gen-smooth');
              el.value = '0';
              el.dispatchEvent(new Event('change', { bubbles: true }));
            }""")
            page.wait_for_timeout(40)
            sm = page.evaluate(
                "window.__calls.filter(c => c[0]==='SetSetting' "
                "&& c[1]==='generator.smooth_window').pop()")
            check("smooth 0 SetSetting", sm is not None and sm[2] == 0)

            page.evaluate("""() => {
              const el = document.querySelector('#gen-contact-curve');
              el.value = 'soft';
              el.dispatchEvent(new Event('change', { bubbles: true }));
            }""")
            page.wait_for_timeout(40)
            cv = page.evaluate(
                "window.__calls.filter(c => c[0]==='SetSetting' "
                "&& c[1]==='generator.contact_vibration_curve').pop()")
            check("curve SetSetting", cv is not None and cv[2] == "soft")

            browser.close()
    finally:
        harness.unlink(missing_ok=True)
        shutdown()
    return check.report()


if __name__ == "__main__":
    sys.exit(main())
