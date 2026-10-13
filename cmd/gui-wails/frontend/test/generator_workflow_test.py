"""Sequential Generate workflow: FunGen-like everyday path.

Contact-first: choose video → auto tip ROI → Generate ready.
Optional Zone 2 / 4-zone advanced. No Tf/Tj in product dropdown.

Run: python3 cmd/gui-wails/frontend/test/generator_workflow_test.py
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


def visible(page, sel):
    return page.locator(sel).evaluate("e => !e.hidden && e.getAttribute('hidden') === null")


def main():
    check = Checker()
    base, shutdown = serve(app_stub({
        "PickVideoFile": "async () => '/tmp/video.mp4'",
        "LoadFirstFrame": f"async () => ({{ width: 640, height: 360, "
                          f"pngBase64: '{TINY_PNG_B64}' }})",
        "LoadFrameAt": f"async () => ({{ width: 640, height: 360, "
                       f"pngBase64: '{TINY_PNG_B64}' }})",
        "SuggestPipeline": "async () => ({ Backend: 'csrt', Profile: 'standard', "
                           "Reason: 'test', GoPath: true })",
        "SuggestProfile": "async () => { window.__suggestCalls = (window.__suggestCalls || 0) + 1; "
                          "if (window.__suggestCalls === 1) return new Promise(resolve => { "
                          "window.__resolveAutoSuggestion = resolve; }); "
                          "return ({ found:true, label:'standard', kind:'local_model', confidence:0.9 }); }",
        "LabelSceneWithProfile": "async (path, label, profile) => { "
                                 "window.__calls.push(['label-profile', label, profile]); }",
        "CheckAIRoiAvailable": "async () => false",
        "CheckAudioCheckAvailable": "async () => false",
        "ScriptExistsForVideo": "async () => false",
    }))
    harness = FRONTEND / "test" / "_generator_workflow_harness.html"
    harness.write_text(PAGE)

    with sync_playwright() as p:
        browser = p.chromium.launch()
        page = browser.new_page()
        page.on("dialog", lambda d: d.dismiss())
        page.on("pageerror", lambda e: print("   [pageerror]", e))
        page.goto(f"{base}/test/_generator_workflow_harness.html")
        page.wait_for_function("window.__ready === true")

        check("Step strip present", page.locator("#gen-steps").count() == 1)
        check("Step 1 video visible at start", visible(page, "#gen-step-video"))
        check("Step 2 region hidden at start", not visible(page, "#gen-step-region"))
        check("Step 3 motion hidden at start", not visible(page, "#gen-step-motion"))
        check("Step 4 generate hidden at start", not visible(page, "#gen-step-run"))
        check("Step 5 review hidden at start", not visible(page, "#gen-step-result"))
        check("Current step is 1",
              page.locator('.gen-step-item[data-step="1"]').evaluate(
                  "e => e.classList.contains('is-current')"))

        page.click("#gen-choose")
        # Everyday: after video, region + motion + generate unlock (auto-find tip).
        page.wait_for_function(
            "document.querySelector('#gen-step-region') && "
            "!document.querySelector('#gen-step-region').hidden && "
            "!document.querySelector('#gen-step-motion').hidden && "
            "!document.querySelector('#gen-step-run').hidden",
            timeout=5000)
        check("After video: region step appears", visible(page, "#gen-step-region"))
        check("After video: motion step appears (everyday)", visible(page, "#gen-step-motion"))
        check("After video: generate appears (everyday)", visible(page, "#gen-step-run"))

        # Wait for auto-find tip (harness AutoDetectROI stub).
        page.wait_for_function(
            "document.querySelector('#gen-autoroi').disabled === false && "
            "!document.querySelector('#gen-roi-label').textContent.includes('No ')",
            timeout=5000)
        check("Auto tip ROI applied",
              "auto" in page.locator("#gen-roi-label").inner_text().lower()
              or "found" in page.locator("#gen-roi-label").inner_text().lower(),
              page.locator("#gen-roi-label").inner_text())
        check("Everyday backend is CSRT",
              page.locator("#gen-backend").input_value() == "csrt")
        check("Generate enabled after auto-find",
              page.locator("#gen-generate").is_enabled())

        # Remembered scenes now carry the confirmed Style into Go-model training.
        page.locator("#gen-power-user summary").click()
        page.fill("#gen-scene-label", "test scene")
        page.select_option("#gen-profile", "weich")
        page.click("#gen-label-scene")
        page.wait_for_function(
            "window.__calls.some(c => Array.isArray(c) && c[0] === 'label-profile')",
            timeout=5000)
        check("Scene memory stores selected style for training",
              page.evaluate("window.__calls.find(c => Array.isArray(c) && c[0] === 'label-profile')")
              == ["label-profile", "test scene", "weich"])

        # Manual result must not be overwritten by the older auto request.
        page.click("#gen-suggest-profile")
        page.wait_for_selector("#gen-suggest-status button:text('Apply')", timeout=5000)
        page.evaluate("window.__resolveAutoSuggestion({found:true,label:'autotune',kind:'measured',confidence:0.01})")
        page.wait_for_timeout(100)
        check("Older automatic suggestion cannot erase manual Apply",
              page.locator("#gen-suggest-status button:text('Apply')").count() == 1
              and "local" in page.locator("#gen-suggest-status").inner_text().lower())
        page.locator("#gen-power-user summary").click()

        # No Tf/Tj in product dropdown — Contact-first.
        profiles = page.eval_on_selector_all(
            "#gen-profile option", "els => els.map(e => e.value)")
        check("No tf profile option", "tf" not in profiles and "tj" not in profiles,
              str(profiles))

        # Optional Zone 2 must NOT hide Generate.
        page.locator("#roi-canvas").scroll_into_view_if_needed()
        box = page.locator("#roi-canvas").bounding_box()
        page.keyboard.down("Shift")
        page.mouse.move(box["x"] + 200, box["y"] + 40)
        page.mouse.down()
        page.mouse.move(box["x"] + 280, box["y"] + 120, steps=5)
        page.mouse.up()
        page.keyboard.up("Shift")
        page.wait_for_function(
            "() => { const l = document.querySelector('#gen-roi2-label');"
            " return l && !l.textContent.startsWith('No '); }",
            timeout=5000)
        check("After optional Zone 2: generate still visible",
              visible(page, "#gen-step-run"))
        check("Generate still enabled", page.locator("#gen-generate").is_enabled())
        check("Review still hidden until done", not visible(page, "#gen-step-result"))

        # 4-zone removed from product GUI (CLI-only).
        options = page.eval_on_selector_all(
            "#gen-backend option", "els => els.map(e => e.value)")
        check("4-zone not in Tracking method",
              "region_fusion_auto" not in options, str(options))
        check("Backend stays CSRT",
              page.locator("#gen-backend").input_value() == "csrt")
        check("Generate still enabled on CSRT",
              page.locator("#gen-generate").is_enabled())

        browser.close()

    shutdown()
    harness.unlink(missing_ok=True)
    return check.report()


if __name__ == "__main__":
    sys.exit(main())
