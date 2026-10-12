"""VLM1 contact-points Advanced toggle wires into GenerateScript payload.

Guards:
1. Checkbox present, OFF/disabled until Rhythm-robust signal is on.
2. With rhythm + contact points + path → contactPointsFile in payload.
3. Rhythm off → contactPointsFile empty (no silent send).
4. Verify-with-engine OFF by default; ON → contactVerifyK 1.5 only with path.
5. Verify cleared when rhythm / Use contact points off → contactVerifyK 0.

Run: python3 cmd/gui-wails/frontend/test/generator_contact_points_test.py
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
        "PickContactPointsFile": "async () => '/tmp/clip.contact.json'",
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
        "GenerateScript": (
            "async (opts) => { window.__calls.push(['GenerateScript', opts]); "
            "setTimeout(() => window.__triggerEvent && window.__triggerEvent("
            "'generate:done', { path: '/tmp/clip.funscript' }), 0); "
            "return { path: '/tmp/clip.funscript' }; }"
        ),
    }))
    harness = FRONTEND / "test" / "_generator_contact_points_harness.html"
    harness.write_text(PAGE)

    with sync_playwright() as p:
        browser = p.chromium.launch()
        page = browser.new_page()
        page.on("dialog", lambda d: d.dismiss())
        page.on("pageerror", lambda e: print("   [pageerror]", e))
        page.goto(f"{base}/test/_generator_contact_points_harness.html")
        page.wait_for_function("window.__ready === true")

        check("Contact-points checkbox present",
              page.locator("#gen-contact-points").count() == 1)
        check("Contact-verify checkbox present",
              page.locator("#gen-contact-verify").count() == 1)
        check("Contact-points disabled while rhythm off",
              page.locator("#gen-contact-points").is_disabled())
        check("Contact-points OFF by default",
              page.locator("#gen-contact-points").is_checked() is False)
        check("Contact-verify hidden while Use off",
              page.locator("#gen-contact-verify-row").is_hidden())

        page.click("#gen-choose")
        page.wait_for_function(
            "document.querySelector('#gen-autoroi').disabled === false && "
            "!document.querySelector('#gen-roi-label').textContent.includes('No ')",
            timeout=5000)
        page.click("#gen-advanced > summary")
        page.evaluate("document.querySelector('#gen-advanced-antidrift').open = true")
        page.wait_for_selector("#gen-rhythm-grid", state="visible", timeout=5000)

        page.check("#gen-rhythm-grid")
        check("Contact-points enabled after rhythm on",
              page.locator("#gen-contact-points").is_disabled() is False)
        page.check("#gen-contact-points")
        page.wait_for_selector("#gen-contact-points-row", state="visible", timeout=3000)
        page.wait_for_selector("#gen-contact-verify-row", state="visible", timeout=3000)
        check("Contact-verify OFF by default",
              page.locator("#gen-contact-verify").is_checked() is False)
        page.click("#gen-contact-points-pick")
        page.wait_for_function(
            "document.querySelector('#gen-contact-points-path').value.includes('contact')",
            timeout=3000)

        page.click("#gen-generate")
        page.wait_for_function(
            "() => (window.__calls || []).filter(c => c[0] === 'GenerateScript')"
            ".length === 1",
            timeout=5000)
        opts = page.evaluate(
            "() => (window.__calls || []).filter(c => c[0] === 'GenerateScript')[0][1]")
        check("Payload includes contactPointsFile path",
              opts.get("contactPointsFile") == "/tmp/clip.contact.json",
              str(opts.get("contactPointsFile")))
        check("Payload keeps rhythmGrid true",
              opts.get("rhythmGrid") is True)
        check("Verify off -> contactVerifyK 0",
              not opts.get("contactVerifyK"),
              str(opts.get("contactVerifyK")))

        page.wait_for_function(
            "document.querySelector('#gen-generate').disabled === false", timeout=5000)
        page.check("#gen-contact-verify")
        page.click("#gen-generate")
        page.wait_for_function(
            "() => (window.__calls || []).filter(c => c[0] === 'GenerateScript')"
            ".length === 2",
            timeout=5000)
        opts_v = page.evaluate(
            "() => (window.__calls || []).filter(c => c[0] === 'GenerateScript')[1][1]")
        check("Verify on -> contactVerifyK 1.5",
              opts_v.get("contactVerifyK") == 1.5,
              str(opts_v.get("contactVerifyK")))
        check("Verify on still sends contactPointsFile",
              opts_v.get("contactPointsFile") == "/tmp/clip.contact.json")

        page.wait_for_function(
            "document.querySelector('#gen-generate').disabled === false", timeout=5000)
        page.uncheck("#gen-rhythm-grid")
        check("Contact-points cleared when rhythm off",
              page.locator("#gen-contact-points").is_checked() is False)
        check("Contact-verify cleared when rhythm off",
              page.locator("#gen-contact-verify").is_checked() is False)
        page.click("#gen-generate")
        page.wait_for_function(
            "() => (window.__calls || []).filter(c => c[0] === 'GenerateScript')"
            ".length === 3",
            timeout=5000)
        opts2 = page.evaluate(
            "() => (window.__calls || []).filter(c => c[0] === 'GenerateScript')[2][1]")
        check("Rhythm off -> contactPointsFile empty",
              not opts2.get("contactPointsFile"),
              str(opts2.get("contactPointsFile")))
        check("Rhythm off -> contactVerifyK 0",
              not opts2.get("contactVerifyK"),
              str(opts2.get("contactVerifyK")))

        browser.close()

    shutdown()
    harness.unlink(missing_ok=True)
    return check.report()


if __name__ == "__main__":
    sys.exit(main())
