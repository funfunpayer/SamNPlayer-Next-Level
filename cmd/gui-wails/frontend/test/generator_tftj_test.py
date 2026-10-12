"""Contact-first Generate: no Tf/Tj profile; Zone 2 optional; Contact vib on.

Ausführen:  python3 cmd/gui-wails/frontend/test/generator_tftj_test.py
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
        "SuggestPipeline": "async (w,h,w2,h2) => ({ Backend: 'csrt', Profile: 'standard', Reason: 'test', GoPath: true })",
        "CheckAIRoiAvailable": "async () => false",
        "CheckAudioCheckAvailable": "async () => false",
    }))
    harness = FRONTEND / "test" / "_generator_tftj_harness.html"
    harness.write_text(PAGE)

    with sync_playwright() as p:
        browser = p.chromium.launch()
        page = browser.new_page()
        page.on("dialog", lambda d: d.dismiss())
        page.on("pageerror", lambda e: print("   [pageerror]", e))
        page.goto(f"{base}/test/_generator_tftj_harness.html")
        page.wait_for_function("window.__ready === true")

        options = page.eval_on_selector_all("#gen-profile option", "els => els.map(e => e.value)")
        check("No Tf/Tj in product profile dropdown",
              options == ["standard", "weich", "autotune"] and "tf" not in options and "tj" not in options,
              str(options))

        page.click("#gen-choose")
        page.wait_for_function(
            "document.querySelector('#gen-autoroi').disabled === false && "
            "!document.querySelector('#gen-roi-label').textContent.includes('No ')",
            timeout=5000)

        page.locator("#roi-canvas").scroll_into_view_if_needed()
        box = page.locator("#roi-canvas").bounding_box()

        # Manual tip correction still allowed after auto-find.
        page.mouse.move(box["x"] + 40, box["y"] + 40)
        page.mouse.down()
        page.mouse.move(box["x"] + 120, box["y"] + 120, steps=5)
        page.mouse.up()
        page.wait_for_timeout(100)
        check("1. Region gesetzt", True)
        check("Profil bleibt 'standard' nach nur einer Region",
              page.locator("#gen-profile").input_value() == "standard",
              page.locator("#gen-profile").input_value())
        check("Generate enabled with tip only (no Zone 2)",
              page.eval_on_selector("#gen-generate", "e => e.disabled") is False)

        # Zone 2: optional — must NOT switch profile to tf.
        page.keyboard.down("Shift")
        page.mouse.move(box["x"] + 200, box["y"] + 40)
        page.mouse.down()
        page.mouse.move(box["x"] + 280, box["y"] + 120, steps=5)
        page.mouse.up()
        page.keyboard.up("Shift")

        page.wait_for_timeout(200)
        check("Profil bleibt standard nach Zone 2",
              page.locator("#gen-profile").input_value() == "standard",
              page.locator("#gen-profile").input_value())
        check("Zone 2 ist gesetzt",
              "No contact" not in page.locator("#gen-roi2-label").inner_text(),
              page.locator("#gen-roi2-label").inner_text())
        check("Kontakt-Vibration-Zeile sichtbar",
              page.locator("#gen-contact-vibration-row").evaluate("e => e.style.display") == "flex")
        check("Kontakt-Vibration standardmäßig aktiv",
              page.locator("#gen-contact-vibration").is_checked())
        check("Kontakt-Vibration-Optionen sichtbar",
              page.locator("#gen-contact-vibration-opts").evaluate("e => e.style.display") == "block")
        check("Default-Kurve soft (wie Berührung)",
              page.locator("#gen-contact-curve").input_value() == "soft")

        # Optional Zone 2 must not block Advanced → Expert → Re-find region after each cut
        # (that gate is only for real Tf/Tj two-point distance).
        page.click("#gen-advanced > summary")
        page.click("#gen-advanced-expert > summary")
        page.wait_for_selector("#gen-perscene", state="visible", timeout=5000)
        check("Re-find region stays enabled with Stroke Zone 2",
              page.locator("#gen-perscene").is_disabled() is False)
        page.locator("#gen-perscene").check()
        check("Re-find region can be checked with Zone 2 marked",
              page.locator("#gen-perscene").is_checked())

        browser.close()

    shutdown()
    harness.unlink(missing_ok=True)

    return check.report()


if __name__ == "__main__":
    sys.exit(main())
