"""First start: EULA dialog covers the app until Accept; Decline quits.

Run: python3 cmd/gui-wails/frontend/test/eula_gate_test.py
"""

import pathlib
import sys

from playwright.sync_api import sync_playwright

from _harness import Checker, app_stub, serve

PAGE = """<!doctype html><html><body><div id="root"></div>
<script type="module">
  import { ensureEULAAccepted } from '/src/legal.js';
  window.__gate = ensureEULAAccepted().then(() => { window.__passed = true; });
  window.__ready = true;
</script></body></html>"""


def run(check, base, accept):
    with sync_playwright() as p:
        browser = p.chromium.launch()
        page = browser.new_page()
        page.on("pageerror", lambda e: print("   [pageerror]", e))
        page.goto(f"{base}/test/_eula_gate_harness.html")
        page.wait_for_function("window.__ready === true")
        page.wait_for_selector(".legal-overlay", timeout=3000)
        check("dialog shows the EULA text", "FREEWARE TERMS" in page.inner_text(".legal-text"))
        if accept:
            page.click("#eula-accept")
            page.wait_for_function("window.__passed === true", timeout=3000)
            check("AcceptEULA got the shown version", page.evaluate("window.__accepted") == "2026-10-10")
            check("dialog gone after accept", page.locator(".legal-overlay").count() == 0)
        else:
            page.click("#eula-decline")
            page.wait_for_function("window.__declined === true", timeout=3000)
            check("Decline calls DeclineEULA (quit)", page.evaluate("window.__declined") is True)
            check("dialog stays (app covered) after decline", page.locator(".legal-overlay").count() == 1)
            check("gate not passed", page.evaluate("window.__passed") is None)
        browser.close()


def main():
    check = Checker()
    base, shutdown = serve(app_stub({
        "GetEULA": "async () => ({version:'2026-10-10', text:'FREEWARE TERMS', accepted:false})",
        "AcceptEULA": "async (v) => { window.__accepted = v; }",
        "DeclineEULA": "async () => { window.__declined = true; }",
    }))
    harness = pathlib.Path(__file__).resolve().parent / "_eula_gate_harness.html"
    harness.write_text(PAGE)
    try:
        run(check, base, accept=True)
        run(check, base, accept=False)
    finally:
        shutdown()
        harness.unlink(missing_ok=True)
    return check.report()


if __name__ == "__main__":
    sys.exit(main())
