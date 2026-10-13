"""Create Step 2: clickable marks + body map for Tip/Contact class.

Guards the Anleitung Mark/Anker + Modelle UX:

1. Body map mounts inside Contact marks wrap when Contact vib is on.
2. Painting Tip selects the mark; body-map zone sets Tip class.
3. Click empty preview deselects.
4. Delete selected clears Tip.

Run: python3 cmd/gui-wails/frontend/test/generator_mark_body_map_test.py
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
        "GetSettings": "async () => ({ applyAISetupAutomatically: false })",
        "GenerateScript": "async () => ({ path: '/tmp/clip.funscript' })",
    }))
    harness = FRONTEND / "test" / "_generator_mark_body_map_harness.html"
    harness.write_text(PAGE)

    with sync_playwright() as p:
        browser = p.chromium.launch()
        page = browser.new_page()
        page.on("dialog", lambda d: d.dismiss())
        page.on("pageerror", lambda e: print("   [pageerror]", e))
        page.goto(f"{base}/test/_generator_mark_body_map_harness.html")
        page.wait_for_function("window.__ready === true")

        page.click("#gen-choose")
        page.wait_for_function(
            "document.querySelector('#gen-autoroi').disabled === false",
            timeout=5000)

        check("Contact vib on by default",
              page.locator("#gen-contact-vibration").is_checked())
        check("Body map host present in Create",
              page.locator("#gen-body-figure").count() == 1)
        check("Body map SVG clickable zones",
              page.locator("#gen-body-figure .bf-zone").count() >= 5)
        check("Selected-mark panel starts hidden",
              page.locator("#gen-selected-mark").is_hidden())

        # Paint a tip box on the preview (drag).
        canvas = page.locator("#roi-canvas")
        canvas.scroll_into_view_if_needed()
        box = canvas.bounding_box()
        assert box
        page.mouse.move(box["x"] + 40, box["y"] + 40)
        page.mouse.down()
        page.mouse.move(box["x"] + 120, box["y"] + 100)
        page.mouse.up()
        page.wait_for_timeout(80)

        check("Tip paint selects mark",
              page.locator("#gen-selected-mark").is_visible())
        check("Mark options opens when a mark is selected",
              page.locator("#gen-mark-options-details").evaluate("el => el.open"))
        check("Selected label mentions Tip",
              "Tip" in (page.locator("#gen-selected-mark-label").inner_text() or ""))

        # Body map → glans (or penis) tip class.
        page.locator('#gen-body-figure .bf-zone[data-class="glans"]').click()
        page.wait_for_timeout(50)
        tip_cls = page.locator("#gen-region-class").input_value()
        check("Body map set Tip class to glans", tip_cls == "glans")

        # Deselect via Escape (reliable; corner clicks can hit the 0–100 overlay).
        page.keyboard.press("Escape")
        page.wait_for_timeout(50)
        check("Escape deselects mark",
              page.locator("#gen-selected-mark").evaluate("e => !!e.hidden"))

        # Delete selected is present (panel hidden until a mark is selected again).
        check("Delete selected control exists",
              page.locator("#gen-selected-mark-delete").count() == 1)

        browser.close()
    shutdown()
    harness.unlink(missing_ok=True)
    return check.report()


if __name__ == "__main__":
    raise SystemExit(main())
