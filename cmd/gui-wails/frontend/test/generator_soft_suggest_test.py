"""Soft profile suggestion on video load mounts Apply (parity with Suggest click).

Run: python3 cmd/gui-wails/frontend/test/generator_soft_suggest_test.py
"""

import pathlib
import sys

from playwright.sync_api import sync_playwright

from _harness import Checker, app_stub, serve

FRONTEND = pathlib.Path(__file__).resolve().parents[1]

PAGE = """<!doctype html><html><body><div id="root"></div>
<script type="module">
  import { initGenerator } from '/src/generator.js';
  initGenerator(document.querySelector('#root'), { loadScriptPath: () => {} });
  window.__ready = true;
</script></body></html>"""

TINY_PNG_B64 = ("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNk"
                "+A8AAQUBAScY42YAAAAASUVORK5CYII=")


def main():
    check = Checker()
    base, shutdown = serve(app_stub({
        "PickVideoFile": "async () => '/tmp/soft-suggest.mp4'",
        "LoadFirstFrame": f"async () => ({{ width: 640, height: 360, "
                          f"pngBase64: '{TINY_PNG_B64}' }})",
        "LoadFrameAt": f"async () => ({{ width: 640, height: 360, "
                       f"pngBase64: '{TINY_PNG_B64}' }})",
        "SuggestPipeline": "async () => ({ Backend: 'csrt', Profile: 'standard', "
                           "Reason: 'test', GoPath: true })",
        "SuggestProfile": "async () => ({ found: true, label: 'weich', "
                          "kind: 'local_model', confidence: 0.88 })",
        "CheckAIRoiAvailable": "async () => false",
        "CheckAudioCheckAvailable": "async () => false",
        "ScriptExistsForVideo": "async () => false",
    }))
    harness = FRONTEND / "test" / "_generator_soft_suggest_harness.html"
    harness.write_text(PAGE)

    with sync_playwright() as p:
        browser = p.chromium.launch()
        page = browser.new_page()
        page.on("dialog", lambda d: d.dismiss())
        page.on("pageerror", lambda e: print("   [pageerror]", e))
        page.goto(f"{base}/test/_generator_soft_suggest_harness.html")
        page.wait_for_function("window.__ready === true")

        page.click("#gen-choose")
        # Soft-suggest lands in #gen-suggest-status inside collapsed Advanced details.
        page.locator("#gen-power-user summary").click()
        page.wait_for_selector("#gen-suggest-status button:text('Apply')", timeout=5000)
        text = page.locator("#gen-suggest-status").inner_text()
        check("Soft-suggest shows English Soft (not weich)",
              "Soft" in text and "weich" not in text.lower(), text)
        check("Soft-suggest mounts Apply button",
              page.locator("#gen-suggest-status button:text('Apply')").count() == 1)
        check("Profile still Normal until Apply",
              page.locator("#gen-profile").input_value() == "standard")

        page.locator("#gen-suggest-status button:text('Apply')").click()
        check("Apply sets Soft (weich) profile",
              page.locator("#gen-profile").input_value() == "weich")
        check("Status confirms Soft applied",
              "Soft" in page.locator("#gen-suggest-status").inner_text()
              and "applied" in page.locator("#gen-suggest-status").inner_text().lower())

        browser.close()

    shutdown()
    harness.unlink(missing_ok=True)
    return check.report()


if __name__ == "__main__":
    sys.exit(main())
