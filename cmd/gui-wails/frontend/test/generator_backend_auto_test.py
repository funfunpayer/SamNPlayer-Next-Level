"""Everyday: choose video → auto tip ROI → CSRT Generate ready.

Ausführen:  python3 cmd/gui-wails/frontend/test/generator_backend_auto_test.py
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
        "PickVideoFile": "async () => '/tmp/video.mp4'",
        "LoadFirstFrame": f"async () => ({{ width: 640, height: 360, "
                          f"pngBase64: '{TINY_PNG_B64}' }})",
        "LoadFrameAt": f"async () => ({{ width: 640, height: 360, "
                       f"pngBase64: '{TINY_PNG_B64}' }})",
        "SuggestPipeline": "async () => ({ Backend: 'csrt', Profile: 'standard', Reason: 'test', GoPath: true })",
        "CheckAIRoiAvailable": "async () => false",
        "CheckAudioCheckAvailable": "async () => false",
    }))
    harness = FRONTEND / "test" / "_generator_backend_auto_harness.html"
    harness.write_text(PAGE)

    with sync_playwright() as p:
        browser = p.chromium.launch()
        page = browser.new_page()
        page.on("dialog", lambda d: d.dismiss())
        page.on("pageerror", lambda e: print("   [pageerror]", e))
        page.goto(f"{base}/test/_generator_backend_auto_harness.html")
        page.wait_for_function("window.__ready === true")

        options = page.eval_on_selector_all("#gen-backend option", "els => els.map(e => e.value)")
        check("CSRT-only dropdown (1-Zone)",
              options == ["csrt"], str(options))
        check("4-zone not in Generate GUI",
              "region_fusion_auto" not in options, str(options))

        check("Generate disabled without video",
              page.eval_on_selector("#gen-generate", "e => e.disabled") is True)

        page.click("#gen-choose")
        page.wait_for_function(
            "document.querySelector('#gen-autoroi').disabled === false && "
            "!document.querySelector('#gen-roi-label').textContent.includes('No ')",
            timeout=5000)

        check("Everyday auto-find sets tip ROI",
              "No region" not in page.locator("#gen-roi-label").inner_text(),
              page.locator("#gen-roi-label").inner_text())
        check("CSRT + auto tip enables Generate",
              page.eval_on_selector("#gen-generate", "e => e.disabled") is False)
        check("Profile is Stroke (standard)",
              page.locator("#gen-profile").input_value() == "standard")
        check("AI region off by default",
              page.locator("#gen-ai-roi").is_checked() is False)

        browser.close()

    shutdown()
    harness.unlink(missing_ok=True)

    return check.report()


if __name__ == "__main__":
    sys.exit(main())
