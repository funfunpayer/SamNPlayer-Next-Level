"""Scene-map Advanced button (P1) — explicit scan only, never auto.

Owner § 6: ScanSceneMap starts from Advanced "Show scene map", not before
every Generate. This test guards the GUI half of P1:

1. Button present in Advanced, disabled until a video is loaded.
2. With video + SceneMapAvailable, click calls ScanSceneMap(path, 0).
3. Status shows window count; GenerateScript is never auto-triggered.

Run: python3 cmd/gui-wails/frontend/test/generator_scene_map_test.py
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
        "SceneMapAvailable": "async () => true",
        "ScanSceneMap": (
            "async (path, n) => { window.__calls.push(['ScanSceneMap', path, n]); "
            "return { version: 1, cols: 16, rows: 9, width: 1280, height: 720, "
            "windows: [{ startMs: 0, endMs: 8000, tempoHz: 1.0, score: [], "
            "chosenCell: -1 }] }; }"
        ),
        "ScriptExistsForVideo": "async () => false",
        "GenerateScript": (
            "async (opts) => { window.__calls.push(['GenerateScript', opts]); "
            "return { path: '/tmp/clip.funscript' }; }"
        ),
    }))
    harness = FRONTEND / "test" / "_generator_scene_map_harness.html"
    harness.write_text(PAGE)

    with sync_playwright() as p:
        browser = p.chromium.launch()
        page = browser.new_page()
        page.on("dialog", lambda d: d.dismiss())
        page.on("pageerror", lambda e: print("   [pageerror]", e))
        page.goto(f"{base}/test/_generator_scene_map_harness.html")
        page.wait_for_function("window.__ready === true")

        check("Scene-map button present",
              page.locator("#gen-scene-map").count() == 1)
        check("Scene-map disabled before video",
              page.locator("#gen-scene-map").is_disabled() is True)

        page.click("#gen-choose")
        page.wait_for_function(
            "document.querySelector('#gen-scene-map') && "
            "!document.querySelector('#gen-scene-map').disabled",
            timeout=5000)

        page.click("#gen-advanced > summary")
        page.evaluate("document.querySelector('#gen-advanced-scenemap').open = true")
        page.wait_for_selector("#gen-scene-map", state="visible", timeout=5000)
        page.click("#gen-scene-map")
        page.wait_for_function(
            "() => (window.__calls || []).some(c => c[0] === 'ScanSceneMap')",
            timeout=5000)

        calls = page.evaluate(
            "() => (window.__calls || []).filter(c => c[0] === 'ScanSceneMap')")
        check("ScanSceneMap called once", len(calls) == 1)
        check("ScanSceneMap path is loaded video",
              calls[0][1] == "/tmp/clip.mp4")
        check("ScanSceneMap n=0 (default windows)", calls[0][2] == 0)
        check("Status shows map ready",
              "Map ready" in (page.locator("#gen-scene-map-status").inner_text() or ""))
        gen_calls = page.evaluate(
            "() => (window.__calls || []).filter(c => c[0] === 'GenerateScript')")
        check("GenerateScript not auto-triggered by scan", len(gen_calls) == 0)

        browser.close()

    shutdown()
    harness.unlink(missing_ok=True)
    return check.report()


if __name__ == "__main__":
    sys.exit(main())
