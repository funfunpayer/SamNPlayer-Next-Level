"""Create Tip→Partner snack guide + Partner fixed label.

Run: python3 cmd/gui-wails/frontend/test/generator_mark_snack_test.py
"""

import pathlib

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
        "GenerateScript": "async () => ({ path: '/tmp/clip.funscript' })",
    }))
    harness = FRONTEND / "test" / "_generator_mark_snack_harness.html"
    harness.write_text(PAGE)

    with sync_playwright() as p:
        browser = p.chromium.launch()
        page = browser.new_page()
        page.on("dialog", lambda d: d.dismiss())
        page.goto(f"{base}/test/_generator_mark_snack_harness.html")
        page.wait_for_function("window.__ready === true")

        page.click("#gen-choose")
        page.wait_for_function(
            "document.querySelector('#gen-autoroi').disabled === false",
            timeout=5000)

        check("Mark snack visible when Contact vib on",
              page.locator("#gen-mark-snack").is_visible())
        check("Snack has Tip / Partner / More steps",
              page.locator("#gen-mark-snack [data-snack='tip']").count() == 1
              and page.locator("#gen-mark-snack [data-snack='partner']").count() == 1
              and page.locator("#gen-mark-snack [data-snack='extra']").count() == 1)
        current = page.eval_on_selector_all(
            "#gen-mark-snack [data-snack]",
            "els => els.filter(e => e.classList.contains('is-current')).map(e => e.getAttribute('data-snack'))")
        check("Exactly one snack step is current",
              len(current) == 1 and current[0] in ("tip", "partner", "extra"))
        hint = page.locator("#gen-mark-snack-hint").inner_text().lower()
        check("Snack hint mentions tip + (stroke|contact|partner|find)",
              "tip" in hint and any(w in hint for w in ("stroke", "contact", "partner", "find")))
        check("Partner fixed (pixels) label present",
              "Partner fixed" in page.locator("#gen-roi2-fixed").evaluate(
                  "el => (el.closest('label') || el.parentElement).textContent"))
        check("AI assist starts closed",
              page.locator("#gen-mark-ai-details").evaluate("el => !el.open"))
        check("Mark options starts closed",
              page.locator("#gen-mark-options-details").evaluate("el => !el.open"))
        check("Partner fixed lives under Mark options",
              page.locator("#gen-mark-options-details #gen-roi2-fixed").count() == 1)
        check("AI detections live under AI assist",
              page.locator("#gen-mark-ai-details #gen-ai-detections").count() == 1)

        page.uncheck("#gen-contact-vibration")
        page.wait_for_timeout(50)
        check("Snack hidden when Contact vib off",
              page.locator("#gen-mark-snack").is_hidden())

        page.check("#gen-contact-vibration")
        page.wait_for_timeout(50)
        check("Snack returns when vib on again",
              page.locator("#gen-mark-snack").is_visible())

        browser.close()
    shutdown()
    harness.unlink(missing_ok=True)
    return check.report()


if __name__ == "__main__":
    raise SystemExit(main())
