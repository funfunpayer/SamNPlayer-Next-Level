"""Scene-map P4 follow-up — restore marks from companion .samn on Create load.

When a video has a companion .samn with sceneMap.marks, opening it in Create
restores sceneMapMarks (and map windows) so Play↔Create keeps annotations.
Also keeps M5 reviewed/confidence on restore (P5c GUI round-trip follow-up).

Run: python3 cmd/gui-wails/frontend/test/generator_scene_map_load_test.py
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

SCORE = ",".join(["40"] * 16)


def main():
    check = Checker()
    base, shutdown = serve(app_stub({
        "PickVideoFile": "async () => '/tmp/clip.mp4'",
        "LoadFirstFrame": f"async () => ({{ width: 640, height: 360, "
                          f"pngBase64: '{TINY_PNG_B64}' }})",
        "LoadFrameAt": f"async () => ({{ width: 640, height: 360, "
                       f"pngBase64: '{TINY_PNG_B64}' }})",
        "SuggestPipeline": "async () => ({ Backend: 'csrt', Profile: 'standard', "
                           "Reason: 'everyday', GoPath: true })",
        "SuggestProfile": "async () => ({ found: false })",
        "CheckAIRoiAvailable": "async () => true",
        "CheckAudioCheckAvailable": "async () => true",
        "SceneMapAvailable": "async () => true",
        "ScanSceneMap": "async () => ({ version: 1, cols: 4, rows: 4, "
                        "width: 640, height: 360, windows: [] })",
        "LoadSceneMapForVideo": (
            "async (path) => { window.__calls.push(['LoadSceneMapForVideo', path]); "
            "return { found: true, path: '/tmp/clip.samn', "
            "map: { version: 1, cols: 4, rows: 4, width: 640, height: 360, "
            f"windows: [{{ startMs: 0, endMs: 8000, tempoHz: 1, score: [{SCORE}], "
            f"chosenCell: -1 }}] }}, "
            "marks: ["
            "{ id: 'm3', kind: 'exclude', follow: true, "
            "rect: { X: 10, Y: 20, W: 30, H: 40 }, "
            "fromMs: 0, toMs: 8000, "
            "path: [{ ms: 0, rect: { x: 10, y: 20, w: 30, h: 40 } }, "
            "{ ms: 4000, rect: { x: 80, y: 40, w: 30, h: 40 } }] },"
            "{ id: 'auto1', kind: 'region', author: 'auto', class: 'tip', "
            "confidence: 0.91, reviewed: true, follow: false, "
            "rect: { x: 50, y: 60, w: 40, h: 40 }, fromMs: 0, toMs: 8000 }"
            "] }; }"
        ),
        "ScriptExistsForVideo": "async () => false",
        "GenerateScript": (
            "async (opts) => { window.__calls.push(['GenerateScript', opts]); "
            "return { path: '/tmp/clip.funscript' }; }"
        ),
    }))
    harness = FRONTEND / "test" / "_generator_scene_map_load_harness.html"
    harness.write_text(PAGE)

    with sync_playwright() as p:
        browser = p.chromium.launch()
        page = browser.new_page()
        page.on("dialog", lambda d: d.dismiss())
        page.on("pageerror", lambda e: print("   [pageerror]", e))
        page.goto(f"{base}/test/_generator_scene_map_load_harness.html")
        page.wait_for_function("window.__ready === true")

        page.click("#gen-choose")
        page.wait_for_function(
            "() => (window.__calls || []).some(c => c[0] === 'LoadSceneMapForVideo')",
            timeout=5000)
        calls = page.evaluate(
            "() => (window.__calls || []).filter(c => c[0] === 'LoadSceneMapForVideo')")
        check("LoadSceneMapForVideo called on video open", len(calls) == 1)
        check("LoadSceneMapForVideo path is loaded video",
              calls[0][1] == "/tmp/clip.mp4")

        # Open Advanced — marks label / tools live inside <details>.
        page.click("#gen-advanced > summary")
        page.evaluate("document.querySelector('#gen-advanced-scenemap').open = true")
        page.wait_for_function(
            "() => (document.querySelector('#gen-scene-map-marks-label')"
            "?.textContent || '').includes('ignore')",
            timeout=5000)
        label = page.evaluate(
            "() => document.querySelector('#gen-scene-map-marks-label')?.textContent || ''")
        check("Restored exclude mark listed", "ignore@" in label)
        check("Restored Follow flag shown", "→follow" in label)
        check("Restored reviewed region listed", "region:tip@" in label)

        status = page.evaluate(
            "() => document.querySelector('#gen-scene-map-status')?.textContent || ''")
        check("Status mentions restored marks",
              "Restored" in status and "mark" in status)
        check("Status counts both restored marks", "2 scene-map mark" in status)

        page.wait_for_selector("#gen-scene-map-tools", state="visible", timeout=5000)
        check("Map tools visible after restore (no re-scan)",
              page.locator("#gen-scene-map-tools").is_visible())

        browser.close()

    shutdown()
    harness.unlink(missing_ok=True)
    return check.report()


if __name__ == "__main__":
    sys.exit(main())
