"""C7: Play load failure offers Choose Emotion Script retry.

A failed LoadFunscript must not leave only prose in #pb-log. The status
names the failure and a button re-runs the picker. Everyday CSRT unchanged.

Run: python3 cmd/gui-wails/frontend/test/playback_load_retry_test.py
"""

import pathlib

from playwright.sync_api import sync_playwright

from _harness import Checker, app_stub, serve

FRONTEND = pathlib.Path(__file__).resolve().parents[1]

PAGE = """<!doctype html><html><body><div id="root"></div>
<script type="module">
  import { initPlayback } from '/src/playback.js';
  initPlayback(document.querySelector('#root'));
  window.__ready = true;
</script></body></html>"""


def main():
    check = Checker()
    base, shutdown = serve(app_stub({
        "PickFunscriptFile": (
            "async () => { window.__calls.push(['PickFunscriptFile']); "
            "return '/tmp/missing.funscript'; }"
        ),
        "LoadFunscript": (
            "async (path) => { window.__calls.push(['LoadFunscript', path]); "
            "throw new Error('not a funscript'); }"
        ),
        "GetSettings": "async () => ({})",
        "GetHeatmap": "async () => []",
        "GetScriptCurve": "async () => []",
        "GetMarker": "async () => null",
    }))
    harness = FRONTEND / "test" / "_playback_load_retry_harness.html"
    harness.write_text(PAGE)
    try:
        with sync_playwright() as p:
            browser = p.chromium.launch()
            page = browser.new_page()
            page.on("dialog", lambda d: d.dismiss())
            page.on("pageerror", lambda e: print("   [pageerror]", e))
            page.goto(f"{base}/test/_playback_load_retry_harness.html")
            page.wait_for_function("window.__ready === true")

            page.click("#pb-choose-empty")
            page.wait_for_function(
                "() => (window.__calls || []).some(c => c[0] === 'LoadFunscript')",
                timeout=5000)
            log = page.locator("#pb-log")
            check("load failure names the script",
                  "Could not load script" in (log.inner_text() or ""),
                  log.inner_text())
            retry = page.locator("#pb-log .pb-choose-retry")
            check("Choose Emotion Script retry is in the status",
                  retry.count() == 1 and "Choose Emotion Script" in (retry.inner_text() or ""),
                  retry.inner_text() if retry.count() else "missing")

            page.evaluate("() => { window.__calls = []; document.querySelector('#pb-log .pb-choose-retry').click(); }")
            page.wait_for_function(
                "() => (window.__calls || []).some(c => c[0] === 'PickFunscriptFile')",
                timeout=5000)
            check("retry opens the script picker again",
                  any(c[0] == "PickFunscriptFile" for c in page.evaluate("window.__calls")),
                  str(page.evaluate("window.__calls")))
            browser.close()
    finally:
        shutdown()
        harness.unlink(missing_ok=True)
    check.report()


if __name__ == "__main__":
    main()
