"""Training display residuals (ChatGPT #185 / #177 verify).

1. Pending RAF levels after training:done must not revive the meter.
2. Late training:levels after done ignored; new session still receives levels.
3. StartTraining that emits done before resolve must leave session stopped.

Run: python3 cmd/gui-wails/frontend/test/training_lifecycle_test.py
"""

import pathlib
import sys

from playwright.sync_api import sync_playwright

from _harness import Checker, app_stub, serve

FRONTEND = pathlib.Path(__file__).resolve().parents[1]

PAGE = """<!doctype html><html><body><div id="root"></div>
<script type="module">
  import { initTraining } from '/src/training.js';
  initTraining(document.querySelector('#root'));
  window.__ready = true;
</script></body></html>"""


def ring_value(page, channel):
    return page.locator(f"#tr-ring-value-{channel}").inner_text()


def main():
    check = Checker()
    base, shutdown = serve(app_stub({
        # Emit done before the StartTraining promise resolves (start race).
        "StartTraining": (
            "async () => { "
            "window.__triggerEvent('training:done'); "
            "await new Promise(r => setTimeout(r, 30)); }"
        ),
        "StopTraining": "async () => {}",
        "StopTrainingCycle": "async () => {}",
        "ReportArousal": "async () => ({})",
        "TrainingHistory": "async () => []",
        "ExportTrainingHistoryCSV": "async () => ''",
        "ListTrainingScripts": "async () => []",
        "PreviewTrainingScript": "async () => ({})",
        "SaveCustomTrainingScript": "async () => ({})",
        "DeleteCustomTrainingScript": "async () => {}",
        "LoadCustomTrainingScript": "async () => ({})",
    }))
    harness = FRONTEND / "test" / "_training_lifecycle_harness.html"
    harness.write_text(PAGE)

    with sync_playwright() as p:
        browser = p.chromium.launch()
        page = browser.new_page()
        page.on("pageerror", lambda e: print("   [pageerror]", e))
        page.goto(f"{base}/test/_training_lifecycle_harness.html")
        page.wait_for_function("window.__ready === true")

        # --- Start race: done before StartTraining resolves ---
        page.click("#tr-start", force=True)
        page.wait_for_timeout(80)
        check("Start race: session stays stopped (Start enabled)",
              page.locator("#tr-start").is_enabled())
        check("Start race: Stop stays disabled",
              page.locator("#tr-stop").is_disabled())
        check("Start race: rings idle after done-before-resolve",
              ring_value(page, "vibration") == "0%"
              and ring_value(page, "suction") == "0%")

        # --- Pending RAF after done must not revive display ---
        # Use a StartTraining that does NOT auto-done so we can control events.
        browser.close()

    shutdown()
    harness.unlink(missing_ok=True)

    # Second harness: controllable Start + mocked RAF queue.
    base, shutdown = serve(app_stub({
        "StartTraining": "async () => {}",
        "StopTraining": "async () => {}",
        "StopTrainingCycle": "async () => {}",
        "ReportArousal": "async () => ({})",
        "TrainingHistory": "async () => []",
        "ExportTrainingHistoryCSV": "async () => ''",
        "ListTrainingScripts": "async () => []",
        "PreviewTrainingScript": "async () => ({})",
        "SaveCustomTrainingScript": "async () => ({})",
        "DeleteCustomTrainingScript": "async () => {}",
        "LoadCustomTrainingScript": "async () => ({})",
    }))
    harness.write_text(PAGE)

    with sync_playwright() as p:
        browser = p.chromium.launch()
        page = browser.new_page()
        page.on("pageerror", lambda e: print("   [pageerror]", e))
        page.goto(f"{base}/test/_training_lifecycle_harness.html")
        page.wait_for_function("window.__ready === true")

        # Install controlled RAF before start.
        page.evaluate("""() => {
          window.__rafQueue = [];
          window.requestAnimationFrame = (cb) => {
            window.__rafQueue.push(cb);
            return window.__rafQueue.length;
          };
          window.cancelAnimationFrame = (id) => {
            const i = id - 1;
            if (i >= 0 && i < window.__rafQueue.length) window.__rafQueue[i] = null;
          };
          window.__flushRaf = () => {
            const q = window.__rafQueue.splice(0);
            for (const cb of q) { if (cb) cb(performance.now()); }
          };
        }""")

        page.click("#tr-start", force=True)
        page.wait_for_function(
            "document.querySelector('#tr-start').disabled === true", timeout=3000)

        page.evaluate(
            "window.__triggerEvent('training:levels', {vibration:0.8, suction:0.6})")
        # Do NOT flush RAF yet — done first, then flush (ChatGPT repro).
        page.evaluate("window.__triggerEvent('training:done')")
        page.wait_for_function(
            "document.querySelector('#tr-start').disabled === false", timeout=3000)
        page.evaluate("window.__flushRaf()")
        page.wait_for_timeout(50)
        check("Pending RAF after done: vibration stays 0%",
              ring_value(page, "vibration") == "0%", ring_value(page, "vibration"))
        check("Pending RAF after done: suction stays 0%",
              ring_value(page, "suction") == "0%", ring_value(page, "suction"))

        # Late levels after done must not revive.
        page.evaluate(
            "window.__triggerEvent('training:levels', {vibration:0.9, suction:0.9})")
        page.evaluate("window.__flushRaf()")
        page.wait_for_timeout(50)
        check("Late levels after done ignored",
              ring_value(page, "vibration") == "0%"
              and ring_value(page, "suction") == "0%")

        # New session still receives levels.
        page.click("#tr-start", force=True)
        page.wait_for_function(
            "document.querySelector('#tr-start').disabled === true", timeout=3000)
        page.evaluate(
            "window.__triggerEvent('training:levels', {vibration:0.4, suction:0.2})")
        page.evaluate("window.__flushRaf()")
        page.wait_for_function(
            "document.querySelector('#tr-ring-value-vibration').textContent === '40%'",
            timeout=3000)
        check("New session receives live levels",
              ring_value(page, "vibration") == "40%"
              and ring_value(page, "suction") == "20%")

        browser.close()

    shutdown()
    harness.unlink(missing_ok=True)
    return check.report()


if __name__ == "__main__":
    sys.exit(main())
