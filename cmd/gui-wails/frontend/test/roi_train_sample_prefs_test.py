"""AI Train Sampling + Box scale persist via settings.

Acceptance:
1. every-N / box scale restore from GetSettings (defaults 12 / 1.0 when omitted).
2. Changing knobs calls SetSetting with generator.roi_sample_every / roi_box_scale.
3. Readouts follow restored values.

Run: python3 cmd/gui-wails/frontend/test/roi_train_sample_prefs_test.py
"""

import pathlib
import sys

from playwright.sync_api import sync_playwright

from _harness import Checker, app_stub, serve

PAGE = """<!doctype html><html><body><div id="root"></div>
<script type="module">
  import { initRoiTraining } from '/src/roi_training.js';
  window.__calls = [];
  initRoiTraining(document.querySelector('#root'));
  window.__ready = true;
</script></body></html>"""

TEST_DIR = pathlib.Path(__file__).resolve().parent

SETTINGS = (
    "async () => ({ roiDatasetDir: '', defaultRoiDatasetDir: '/tmp/roi', "
    "roiSampleEvery: 24, roiBoxScale: 1.25 })"
)


def main():
    check = Checker()
    base, shutdown = serve(app_stub({
        "GetSettings": SETTINGS,
        "SetSetting": (
            "async (k, v) => { window.__calls.push(['SetSetting', k, v]); }"
        ),
        "CheckRoiTrainingStatus": "async () => ({ ultralytics: false, detail: '' })",
        "ListTrainDevices": "async () => ({ devices: [] })",
        "GetRoiDatasetSummary": "async () => ({ readyToTrain: false })",
        "ListRoiClasses": "async () => []",
    }))
    harness = TEST_DIR / "_roi_train_sample_prefs_harness.html"
    harness.write_text(PAGE)
    try:
        with sync_playwright() as p:
            browser = p.chromium.launch()
            page = browser.new_page()
            page.on("pageerror", lambda e: print("   [pageerror]", e))
            page.goto(f"{base}/test/_roi_train_sample_prefs_harness.html")
            page.wait_for_function("window.__ready === true")
            page.wait_for_timeout(120)

            check("sample every restored 24",
                  page.locator("#rt-sample-every").input_value() == "24")
            check("sample every label 24",
                  page.locator("#rt-sample-every-val").inner_text() == "24")
            check("box scale restored 1.25",
                  page.locator("#rt-box-scale").input_value() == "1.25")
            check("box scale label 1.25",
                  page.locator("#rt-box-scale-val").inner_text() == "1.25")

            page.evaluate("window.__calls = []")
            page.evaluate("""() => {
              const el = document.querySelector('#rt-sample-every');
              el.value = '8';
              el.dispatchEvent(new Event('change', { bubbles: true }));
            }""")
            page.wait_for_timeout(40)
            every = page.evaluate(
                "window.__calls.filter(c => c[0]==='SetSetting' "
                "&& c[1]==='generator.roi_sample_every').pop()")
            check("sample every SetSetting", every is not None and every[2] == 8)

            page.evaluate("""() => {
              const el = document.querySelector('#rt-box-scale');
              el.value = '0.8';
              el.dispatchEvent(new Event('change', { bubbles: true }));
            }""")
            page.wait_for_timeout(40)
            scale = page.evaluate(
                "window.__calls.filter(c => c[0]==='SetSetting' "
                "&& c[1]==='generator.roi_box_scale').pop()")
            check("box scale SetSetting",
                  scale is not None and abs(scale[2] - 0.8) < 1e-6)

            browser.close()
    finally:
        harness.unlink(missing_ok=True)
        shutdown()
    return check.report()


if __name__ == "__main__":
    sys.exit(main())
