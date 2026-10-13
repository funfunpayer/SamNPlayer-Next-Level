"""FunGen-like Everyday Generate: clip → auto tip → CSRT; optional vibe / AI.

Acceptance from docs/EVERYDAY_GENERATE.md:
1. Video → Generate without painting a box (auto-ROI ran).
2. Path is CSRT Stroke, not 4-zone, unless opted in.
3. AI off by default; Contact vib on by default.
4. Zone 2 optional for where vibration should feel.

Run: python3 cmd/gui-wails/frontend/test/generator_everyday_test.py
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
    calls = {"AutoDetectROI": 0, "GenerateScript": 0}
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
        # Count GenerateScript invocations via window.__calls (harness prepends).
        "GenerateScript": (
            "async (opts) => { window.__calls.push(['GenerateScript', opts]); "
            "setTimeout(() => window.__triggerEvent && window.__triggerEvent("
            "'generate:done', { path: '/tmp/clip.funscript' }), 0); "
            "return { path: '/tmp/clip.funscript' }; }"
        ),
    }))
    harness = FRONTEND / "test" / "_generator_everyday_harness.html"
    harness.write_text(PAGE)

    with sync_playwright() as p:
        browser = p.chromium.launch()
        page = browser.new_page()
        page.on("dialog", lambda d: d.dismiss())
        page.on("pageerror", lambda e: print("   [pageerror]", e))
        page.goto(f"{base}/test/_generator_everyday_harness.html")
        page.wait_for_function("window.__ready === true")

        check("AI checkbox off by default",
              page.locator("#gen-ai-roi").is_checked() is False)
        check("Contact vibration on by default",
              page.locator("#gen-contact-vibration").is_checked())
        check("Trajectory capture checkbox present (feel Stage A soft-on)",
              page.locator("#gen-capture-trajectory").count() == 1)
        check("Trajectory capture soft-on with Contact vib",
              page.locator("#gen-capture-trajectory").is_checked())

        page.click("#gen-choose")
        page.wait_for_function(
            "document.querySelector('#gen-autoroi').disabled === false && "
            "!document.querySelector('#gen-roi-label').textContent.includes('No ')",
            timeout=5000)

        check("No hand-painted box required — auto tip present",
              page.eval_on_selector("#gen-generate", "e => e.disabled") is False)
        check("First choice backend CSRT",
              page.locator("#gen-backend").input_value() == "csrt")
        check("First choice profile Stroke",
              page.locator("#gen-profile").input_value() == "standard")
        check("4-zone not selected",
              page.locator("#gen-backend").input_value() != "region_fusion_auto")

        # Optional: where it vibrates (Zone 2) — Generate stays ready.
        page.locator("#roi-canvas").scroll_into_view_if_needed()
        box = page.locator("#roi-canvas").bounding_box()
        page.keyboard.down("Shift")
        page.mouse.move(box["x"] + 40, box["y"] + 40)
        page.mouse.down()
        page.mouse.move(box["x"] + 140, box["y"] + 140, steps=4)
        page.mouse.up()
        page.keyboard.up("Shift")
        page.wait_for_function(
            "() => { const l = document.querySelector('#gen-roi2-label');"
            " return l && !l.textContent.startsWith('No '); }",
            timeout=5000)
        check("Zone 2 optional for vibe location",
              "No contact" not in page.locator("#gen-roi2-label").inner_text())
        check("Generate still ready with Zone 2",
              page.locator("#gen-generate").is_enabled())

        # AI optional toggle does not block Generate.
        page.wait_for_function(
            "document.querySelector('#gen-ai-roi').disabled === false", timeout=5000)
        page.locator("#gen-mark-ai-details").evaluate("el => { el.open = true }")
        page.check("#gen-ai-roi")
        check("AI region can be turned on",
              page.locator("#gen-ai-roi").is_checked())
        check("Generate still ready with AI on",
              page.locator("#gen-generate").is_enabled())

        # Tip path is soft-on with Contact vib (Feel Stage A); already checked above.

        page.click("#gen-generate")
        page.wait_for_function(
            "() => (window.__calls || []).some(c => c[0] === 'GenerateScript')",
            timeout=5000)
        calls = page.evaluate(
            "() => (window.__calls || []).filter(c => c[0] === 'GenerateScript')")
        check("GenerateScript called once", len(calls) == 1, str(calls))
        opts = calls[0][1] if calls else {}
        check("Generate uses CSRT", opts.get("backend") == "csrt", str(opts))
        check("Generate uses Stroke profile",
              opts.get("profile") == "standard", str(opts))
        check("Contact vib passed through",
              opts.get("contactVibration") is True, str(opts))
        check("Contact area (roi2) persisted on Stroke+Contact",
              opts.get("w2", 0) > 0 and opts.get("h2", 0) > 0, str(opts))
        check("Trajectory capture soft-on reaches GenerateScript payload",
              opts.get("captureTrajectory") is True, str(opts))

        browser.close()

    shutdown()
    harness.unlink(missing_ok=True)
    return check.report()


if __name__ == "__main__":
    sys.exit(main())
