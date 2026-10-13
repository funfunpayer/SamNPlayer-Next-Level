"""GUI: Everyday CSRT (auto tip). 4-zone removed from product Generate GUI.

Ausführen:  python3 cmd/gui-wails/frontend/test/generator_backend_test.py
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
        "SuggestPipeline": "async (w,h,w2,h2) => ({ Backend: 'csrt', Profile: w2>0?'tf':'standard', Reason: 'test', GoPath: true })",
        "CheckAIRoiAvailable": "async () => false",
        "CheckAudioCheckAvailable": "async () => false",
    }))
    harness = FRONTEND / "test" / "_generator_backend_harness.html"
    harness.write_text(PAGE)

    with sync_playwright() as p:
        browser = p.chromium.launch()
        page = browser.new_page()
        page.on("dialog", lambda d: d.dismiss())
        page.on("pageerror", lambda e: print("   [pageerror]", e))
        page.goto(f"{base}/test/_generator_backend_harness.html")
        page.wait_for_function("window.__ready === true")

        options = page.eval_on_selector_all("#gen-backend option", "els => els.map(e => e.value)")
        check("Dropdown is CSRT-only (1-Zone product)",
              options == ["csrt"], str(options))
        check("4-zone not in Generate GUI",
              "region_fusion_auto" not in options, str(options))
        check("Weak research backends not in GUI",
              "flow" not in options and "grid_lk" not in options
              and "region_fusion" not in options,
              str(options))
        check("4-zone Everyday button removed",
              page.locator("#gen-nomark").count() == 0)

        check("Generate disabled without video",
              page.eval_on_selector("#gen-generate", "e => e.disabled") is True)

        page.click("#gen-choose")
        page.wait_for_function(
            "document.querySelector('#gen-autoroi').disabled === false && "
            "!document.querySelector('#gen-roi-label').textContent.includes('No ')",
            timeout=5000)

        check("Everyday auto-find enables Generate on CSRT",
              page.eval_on_selector("#gen-generate", "e => e.disabled") is False)
        check("Backend stays CSRT after auto-find",
              page.locator("#gen-backend").input_value() == "csrt")

        browser.close()
    shutdown()
    harness.unlink(missing_ok=True)
    return check.report()


if __name__ == "__main__":
    sys.exit(main())
