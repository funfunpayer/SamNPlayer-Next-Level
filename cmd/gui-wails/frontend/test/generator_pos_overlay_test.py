"""Generator: FunGen-like 0–100 gauge over video after Generate.

Run: python3 cmd/gui-wails/frontend/test/generator_pos_overlay_test.py
"""

import pathlib
import sys

from playwright.sync_api import sync_playwright

from _harness import Checker, app_stub, serve

FRONTEND = pathlib.Path(__file__).resolve().parents[1]

PAGE = """<!doctype html><html><body><div id="root"></div>
<script type="module">
  import { initGenerator } from '/src/generator.js';
  const playback = {
    loadScriptPath: async (path) => {
      window.__playLoads = window.__playLoads || [];
      window.__playLoads.push(path);
    },
  };
  initGenerator(document.querySelector('#root'), playback);
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
            "'generate:done', { path: '/tmp/clip.funscript', qualityScore: 0.8, "
            "qualityPassed: true, pipeline: 'go', tracking: 'csrt', "
            "backend: 'csrt' }), 0); "
            "return { path: '/tmp/clip.funscript' }; }"
        ),
        "ImproveGeneratedScript": (
            "async (req) => { window.__calls.push(['ImproveGeneratedScript', req]); "
            "return { path: '/tmp/clip.samn', beforeCount: 4, afterCount: 8, "
            "gapsFilled: 1, pointsAdded: 4, message: 'filled 1 gap(s)' }; }"
        ),
        "GetScriptCurve": (
            "async () => [{ atMs: 0, pos: 10 }, { atMs: 2000, pos: 90 }, "
            "{ atMs: 4000, pos: 20 }]"
        ),
    }))
    harness = FRONTEND / "test" / "_generator_pos_overlay_harness.html"
    harness.write_text(PAGE)

    with sync_playwright() as p:
        browser = p.chromium.launch()
        page = browser.new_page()
        page.on("pageerror", lambda e: print("   [pageerror]", e))
        page.goto(f"{base}/test/_generator_pos_overlay_harness.html")
        page.wait_for_function("window.__ready === true")

        check("Toggle present", page.locator("#gen-pos-overlay-toggle").count() == 1)
        check("Overlay hidden before generate",
              page.locator("#gen-pos-overlay").evaluate("e => !!e.hidden"))

        page.click("#gen-choose")
        page.wait_for_function(
            "document.querySelector('#gen-generate') && "
            "!document.querySelector('#gen-generate').disabled",
            timeout=8000)
        page.click("#gen-generate")
        page.wait_for_function(
            "() => (window.__playLoads || []).length > 0",
            timeout=8000)
        page.wait_for_function(
            "() => { const e = document.querySelector('#gen-pos-overlay'); "
            "return e && !e.hidden; }",
            timeout=5000)
        check("Overlay visible after generate",
              page.locator("#gen-pos-overlay").evaluate("e => !e.hidden"))
        val = page.locator("#gen-pos-value").inner_text().strip()
        check("Value is numeric 0–100", val.isdigit() and 0 <= int(val) <= 100, val)

        page.uncheck("#gen-pos-overlay-toggle")
        page.wait_for_timeout(50)
        check("Toggle hides overlay",
              page.locator("#gen-pos-overlay").evaluate("e => e.hidden"))
        page.check("#gen-pos-overlay-toggle")
        page.wait_for_timeout(50)
        check("Toggle shows overlay again",
              page.locator("#gen-pos-overlay").evaluate("e => !e.hidden"))

        page.fill("#gen-seek", "2")
        page.click("#gen-seek-btn")
        page.wait_for_timeout(200)
        val2 = page.locator("#gen-pos-value").inner_text().strip()
        check("Seek updates gauge toward peak",
              val2.isdigit() and int(val2) >= 70, f"at 2s got {val2}")

        browser.close()

    shutdown()
    harness.unlink(missing_ok=True)
    return check.report()


if __name__ == "__main__":
    sys.exit(main())
