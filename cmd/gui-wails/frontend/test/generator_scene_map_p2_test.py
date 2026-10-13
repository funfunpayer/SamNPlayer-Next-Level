"""Scene-map P2 — heatmap tools + exclude mark on preview.

After Show scene map, Advanced tools appear. Painting an exclude mark
stores a window-scoped mark and does not call GenerateScript.

Run: python3 cmd/gui-wails/frontend/test/generator_scene_map_p2_test.py
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

# Non-trivial score grid so overlay path runs (16x9 = 144 cells)
SCORE = ",".join(["40"] * 144)


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
            f"windows: [{{ startMs: 0, endMs: 8000, tempoHz: 1.0, "
            f"score: [{SCORE}], chosenCell: -1 }}] }}; }}"
        ),
        "ScriptExistsForVideo": "async () => false",
        "GenerateScript": (
            "async (opts) => { window.__calls.push(['GenerateScript', opts]); "
            "return { path: '/tmp/clip.funscript' }; }"
        ),
    }))
    harness = FRONTEND / "test" / "_generator_scene_map_p2_harness.html"
    harness.write_text(PAGE)

    with sync_playwright() as p:
        browser = p.chromium.launch()
        page = browser.new_page()
        page.on("dialog", lambda d: d.dismiss())
        page.on("pageerror", lambda e: print("   [pageerror]", e))
        page.goto(f"{base}/test/_generator_scene_map_p2_harness.html")
        page.wait_for_function("window.__ready === true")

        page.click("#gen-choose")
        page.wait_for_function(
            "document.querySelector('#gen-scene-map') && "
            "!document.querySelector('#gen-scene-map').disabled",
            timeout=5000)
        page.click("#gen-advanced > summary")
        page.evaluate("document.querySelector('#gen-advanced-scenemap').open = true")
        page.click("#gen-scene-map")
        page.wait_for_selector("#gen-scene-map-tools", state="visible", timeout=5000)

        check("P2 tools visible after scan",
              page.locator("#gen-scene-map-tools").is_visible())
        check("Heatmap overlay on by default",
              page.locator("#gen-scene-map-overlay").is_checked() is True)

        page.click("#gen-scene-map-mark")
        page.wait_for_function(
            "() => (document.querySelector('#gen-status')?.textContent || '')"
            ".includes('Scene map mark')",
            timeout=5000)
        canvas = page.locator("#roi-canvas")
        page.wait_for_function(
            "() => { const c = document.querySelector('#roi-canvas');"
            " return c && c.width > 10 && c.height > 10; }",
            timeout=5000)
        # Advanced panel opens below the preview; scroll canvas into view so
        # Playwright mouse coords hit the canvas (not off-screen).
        canvas.scroll_into_view_if_needed()
        box = canvas.bounding_box()
        page.mouse.move(box["x"] + box["width"] * 0.2, box["y"] + box["height"] * 0.2)
        page.mouse.down()
        page.mouse.move(box["x"] + box["width"] * 0.5, box["y"] + box["height"] * 0.5, steps=5)
        page.mouse.up()
        page.wait_for_function(
            "() => (document.querySelector('#gen-scene-map-marks-label')"
            "?.textContent || '').includes('ignore')",
            timeout=5000)
        check("Exclude mark listed",
              "ignore@" in (page.locator("#gen-scene-map-marks-label").inner_text() or ""))
        gen_calls = page.evaluate(
            "() => (window.__calls || []).filter(c => c[0] === 'GenerateScript')")
        check("Paint mark does not Generate", len(gen_calls) == 0)

        browser.close()

    shutdown()
    harness.unlink(missing_ok=True)
    return check.report()


if __name__ == "__main__":
    sys.exit(main())
