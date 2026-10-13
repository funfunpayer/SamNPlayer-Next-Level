"""AI Train: Sampling + Box scale range+readout.

Acceptance:
1. #rt-sample-every and #rt-box-scale are range inputs.
2. Defaults 12 / 1.0 with matching labels.
3. Input updates readout.

Run: python3 cmd/gui-wails/frontend/test/rt_sample_knobs_test.py
"""

import pathlib
import sys

from playwright.sync_api import sync_playwright

from _harness import Checker, app_stub, serve

PAGE = """<!doctype html><html><body><div id="root"></div>
<script type="module">
  import { initRoiTraining } from '/src/roi_training.js';
  initRoiTraining(document.querySelector('#root'));
  window.__ready = true;
</script></body></html>"""

TEST_DIR = pathlib.Path(__file__).resolve().parent


def main():
    check = Checker()
    base, shutdown = serve(app_stub({
        "GetSettings": "async () => ({})",
        "SaveSetting": "async () => {}",
        "ListRoiTrainingDevices": "async () => []",
    }))
    harness = TEST_DIR / "_rt_sample_knobs_harness.html"
    harness.write_text(PAGE)
    try:
        with sync_playwright() as p:
            browser = p.chromium.launch()
            page = browser.new_page()
            page.on("pageerror", lambda e: print("   [pageerror]", e))
            page.goto(f"{base}/test/_rt_sample_knobs_harness.html")
            page.wait_for_function("window.__ready === true")
            page.wait_for_timeout(80)

            check("sample-every is range",
                  page.locator("#rt-sample-every").evaluate("e => e.type") == "range")
            check("box-scale is range",
                  page.locator("#rt-box-scale").evaluate("e => e.type") == "range")
            check("sample default 12",
                  page.locator("#rt-sample-every").input_value() == "12")
            check("sample label 12",
                  page.locator("#rt-sample-every-val").inner_text() == "12")
            check("box-scale default 1",
                  page.locator("#rt-box-scale").input_value() in ("1", "1.0"))
            check("box-scale label 1",
                  page.locator("#rt-box-scale-val").inner_text() == "1")

            page.evaluate("""() => {
              const h = document.querySelector('#rt-sample-every');
              h.value = '24';
              h.dispatchEvent(new Event('input', { bubbles: true }));
            }""")
            page.wait_for_timeout(40)
            check("sample readout follows",
                  page.locator("#rt-sample-every-val").inner_text() == "24")

            page.evaluate("""() => {
              const h = document.querySelector('#rt-box-scale');
              h.value = '1.2';
              h.dispatchEvent(new Event('input', { bubbles: true }));
            }""")
            page.wait_for_timeout(40)
            check("box-scale readout follows",
                  page.locator("#rt-box-scale-val").inner_text() == "1.2")

            browser.close()
    finally:
        shutdown()
        harness.unlink(missing_ok=True)
    return check.report()


if __name__ == "__main__":
    sys.exit(main())
