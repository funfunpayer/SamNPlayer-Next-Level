"""Training: Peak / Plateau / Progression range+readout.

Acceptance:
1. Controls are range inputs with live value labels.
2. Defaults match settings (0.8 / 0.7 / 0.15).
3. Input updates readout.

Run: python3 cmd/gui-wails/frontend/test/tr_intensity_knobs_test.py
"""

import pathlib
import sys

from playwright.sync_api import sync_playwright

from _harness import Checker, app_stub, serve

PAGE = """<!doctype html><html><body><div id="root"></div>
<script type="module">
  import { initTraining } from '/src/training.js';
  initTraining(document.querySelector('#root'));
  window.__ready = true;
</script></body></html>"""

TEST_DIR = pathlib.Path(__file__).resolve().parent


def main():
    check = Checker()
    base, shutdown = serve(app_stub({
        "GetSettings": (
            "async () => ({ trainingMock: false, trainingTechnique: 'stopstart', "
            "trainingChannel: 'vibration', trainingCycles: 5, trainingRampUpMs: 8000, "
            "trainingHoldMs: 3000, trainingRestMs: 10000, "
            "trainingPeakIntensity: 0.8, trainingPlateauFraction: 0.7, "
            "trainingProgressionPerCycle: 0.15 })"
        ),
        "SaveSetting": "async () => {}",
    }))
    harness = TEST_DIR / "_tr_intensity_knobs_harness.html"
    harness.write_text(PAGE)
    try:
        with sync_playwright() as p:
            browser = p.chromium.launch()
            page = browser.new_page()
            page.on("pageerror", lambda e: print("   [pageerror]", e))
            page.goto(f"{base}/test/_tr_intensity_knobs_harness.html")
            page.wait_for_function("window.__ready === true")
            page.wait_for_timeout(80)

            for sel in ("#tr-peak", "#tr-plateaufrac", "#tr-progression"):
                check(f"{sel} is range",
                      page.locator(sel).evaluate("e => e.type") == "range")

            check("peak default 0.8",
                  page.locator("#tr-peak").input_value() == "0.8")
            check("peak label 0.8",
                  page.locator("#tr-peak-val").inner_text() == "0.8")
            check("plateau default 0.7",
                  page.locator("#tr-plateaufrac").input_value() == "0.7")
            check("progression default 0.15",
                  page.locator("#tr-progression").input_value() == "0.15")

            page.evaluate("""() => {
              const h = document.querySelector('#tr-peak');
              h.value = '0.55';
              h.dispatchEvent(new Event('input', { bubbles: true }));
            }""")
            page.wait_for_timeout(40)
            check("peak readout follows",
                  page.locator("#tr-peak-val").inner_text() == "0.55")

            browser.close()
    finally:
        shutdown()
        harness.unlink(missing_ok=True)
    return check.report()


if __name__ == "__main__":
    sys.exit(main())
