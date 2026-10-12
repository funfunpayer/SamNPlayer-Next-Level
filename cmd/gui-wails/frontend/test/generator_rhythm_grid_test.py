"""Rhythm-grid Advanced toggle wires through to the GenerateScript payload.

Backend (#233): opt-in drift-robust stroke signal (`Options.RhythmGrid`,
GUI field `rhythmGrid`). This test guards the GUI half:

1. The Advanced checkbox exists and is OFF by default (no silent default).
2. With it OFF, `GenerateScript` receives `rhythmGrid: false` (negative check).
3. With it ON, `GenerateScript` receives `rhythmGrid: true`.

Run: python3 cmd/gui-wails/frontend/test/generator_rhythm_grid_test.py
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
        "GenerateScript": (
            "async (opts) => { window.__calls.push(['GenerateScript', opts]); "
            "setTimeout(() => window.__triggerEvent && window.__triggerEvent("
            "'generate:done', { path: '/tmp/clip.funscript' }), 0); "
            "return { path: '/tmp/clip.funscript' }; }"
        ),
    }))
    harness = FRONTEND / "test" / "_generator_rhythm_grid_harness.html"
    harness.write_text(PAGE)

    with sync_playwright() as p:
        browser = p.chromium.launch()
        page = browser.new_page()
        page.on("dialog", lambda d: d.dismiss())
        page.on("pageerror", lambda e: print("   [pageerror]", e))
        page.goto(f"{base}/test/_generator_rhythm_grid_harness.html")
        page.wait_for_function("window.__ready === true")

        # Present + OFF by default — works on the still-collapsed control.
        check("Rhythm-grid checkbox present",
              page.locator("#gen-rhythm-grid").count() == 1)
        check("Rhythm-grid OFF by default (no silent default)",
              page.locator("#gen-rhythm-grid").is_checked() is False)

        # Auto-find tip so Generate becomes enabled (no hand-painted box).
        page.click("#gen-choose")
        page.wait_for_function(
            "document.querySelector('#gen-autoroi').disabled === false && "
            "!document.querySelector('#gen-roi-label').textContent.includes('No ')",
            timeout=5000)

        # The toggle lives in the collapsed "Advanced settings" <details>.
        page.click("#gen-advanced > summary")
        page.evaluate("document.querySelector('#gen-advanced-antidrift').open = true")
        page.wait_for_selector("#gen-rhythm-grid", state="visible", timeout=5000)

        # Run 1: toggle OFF -> payload rhythmGrid false.
        page.click("#gen-generate")
        page.wait_for_function(
            "() => (window.__calls || []).filter(c => c[0] === 'GenerateScript')"
            ".length === 1",
            timeout=5000)

        # Run 2: toggle ON -> payload rhythmGrid true.
        page.wait_for_function(
            "document.querySelector('#gen-generate').disabled === false", timeout=5000)
        page.check("#gen-rhythm-grid")
        check("Rhythm-grid can be turned on",
              page.locator("#gen-rhythm-grid").is_checked())
        page.click("#gen-generate")
        page.wait_for_function(
            "() => (window.__calls || []).filter(c => c[0] === 'GenerateScript')"
            ".length === 2",
            timeout=5000)

        calls = page.evaluate(
            "() => (window.__calls || []).filter(c => c[0] === 'GenerateScript')")
        off_opts = calls[0][1] if len(calls) > 0 else {}
        on_opts = calls[1][1] if len(calls) > 1 else {}
        check("Toggle OFF -> rhythmGrid false in payload",
              off_opts.get("rhythmGrid") is False, str(off_opts.get("rhythmGrid")))
        check("Toggle ON -> rhythmGrid true in payload",
              on_opts.get("rhythmGrid") is True, str(on_opts.get("rhythmGrid")))

        browser.close()

    shutdown()
    harness.unlink(missing_ok=True)
    return check.report()


if __name__ == "__main__":
    sys.exit(main())
