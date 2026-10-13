"""Stroke-preview Stage B GUI tip (BF-3).

When Generate emits STROKE_PREVIEW "enabling …" progress lines,
#gen-preview-steer-tip shows a short tip. Advanced checkboxes are NOT
flipped (sticky PerSceneROI forced Python + MIL on Windows portable — #338).
Go already applied opts for the current run.

Run: python3 cmd/gui-wails/frontend/test/generator_stroke_preview_steer_test.py
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
        "PickVideoFile": "async () => '/tmp/clip.mp4'",
        "LoadFirstFrame": f"async () => ({{ width: 1280, height: 720, "
                          f"pngBase64: '{TINY_PNG_B64}' }})",
        "LoadFrameAt": f"async () => ({{ width: 1280, height: 720, "
                       f"pngBase64: '{TINY_PNG_B64}' }})",
        "SuggestPipeline": "async () => ({ Backend: 'csrt', Profile: 'standard', "
                           "Reason: 'everyday', GoPath: true })",
        "SuggestProfile": "async () => ({ found: false })",
        "CheckAIRoiAvailable": "async () => true",
        "CheckAudioCheckAvailable": "async () => true",
        "ScriptExistsForVideo": "async () => false",
    }))
    harness = FRONTEND / "test" / "_generator_stroke_preview_steer_harness.html"
    harness.write_text(PAGE)

    with sync_playwright() as p:
        browser = p.chromium.launch()
        page = browser.new_page()
        page.on("dialog", lambda d: d.dismiss())
        page.on("pageerror", lambda e: print("   [pageerror]", e))
        page.goto(f"{base}/test/_generator_stroke_preview_steer_harness.html")
        page.wait_for_function("window.__ready === true")

        tip = page.locator("#gen-preview-steer-tip")
        check("Steer tip element present", tip.count() == 1, f"count={tip.count()}")
        check("Re-find OFF by default",
              page.locator("#gen-perscene").is_checked() is False)

        page.evaluate(
            """() => {
              window.__triggerEvent('generate:progress',
                'STROKE_PREVIEW: high cut rate (5.2/min) — enabling “Re-find region after each cut”');
            }"""
        )
        page.wait_for_timeout(50)
        check("Re-find checkbox stays OFF (no sticky) after cut-rate steer tip",
              page.locator("#gen-perscene").is_checked() is False)
        check("Tip mentions Re-find",
              "Re-find" in (tip.text_content() or ""), tip.text_content())

        page.evaluate(
            """() => {
              const box = document.querySelector('#gen-camcomp');
              if (box) box.checked = false;
              window.__triggerEvent('generate:progress',
                'STROKE_PREVIEW: pan-like energy share=61% — enabling camera motion compensation');
            }"""
        )
        page.wait_for_timeout(50)
        check("Camera compensation stays OFF (no sticky) after pan steer tip",
              page.locator("#gen-camcomp").is_checked() is False)
        check("Tip mentions camera",
              "camera" in (tip.text_content() or "").lower(), tip.text_content())

        browser.close()

    shutdown()
    harness.unlink(missing_ok=True)
    return check.report()


if __name__ == "__main__":
    sys.exit(main())
