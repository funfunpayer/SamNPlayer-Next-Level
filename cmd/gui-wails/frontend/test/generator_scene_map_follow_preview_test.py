"""S1 — Create preview Follow Path at scrub from restored .samn.

After LoadSceneMapForVideo restores an Ignore mark with Follow Path samples,
Create Time/Frame scrub draws the mark at the nearest Path rect (not the
static paint Rect). Heatmap-off still shows marks.

Run: python3 cmd/gui-wails/frontend/test/generator_scene_map_follow_preview_test.py
"""

import json
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

# Solid white 1×1 so black Ignore fill (rgba 0,0,0,0.35) is measurable.
TINY_PNG_B64 = ("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAIAAACQd1PeAAAADElEQVR42mP4"
                "//8/AAX+Av4zEpUUAAAAAElFTkSuQmCC")

SCORE = ",".join(["40"] * 16)

# Static paint rect (top-left) vs Follow Path at 10s (far right).
MARK = {
    "id": "m1",
    "kind": "exclude",
    "follow": True,
    "rect": {"X": 10, "Y": 20, "W": 40, "H": 40},
    "fromMs": 0,
    "toMs": 0,
    "path": [
        {"ms": 0, "rect": {"x": 10, "y": 20, "w": 40, "h": 40}},
        {"ms": 10000, "rect": {"x": 500, "y": 200, "w": 40, "h": 40}},
    ],
}


def _luma_at(page, native_x, native_y):
    """Average luma near a native-pixel point on #roi-canvas (0–255)."""
    return page.evaluate(
        """([nx, ny]) => {
          const c = document.querySelector('#roi-canvas');
          if (!c || !c.width) return -1;
          const ctx = c.getContext('2d');
          // native coords → canvas (same mapping as drawNativeRect)
          const sx = c.width / 640, sy = c.height / 360;
          const x = Math.round((nx + 20) * sx);
          const y = Math.round((ny + 20) * sy);
          const d = ctx.getImageData(Math.max(0, x - 2), Math.max(0, y - 2), 5, 5).data;
          let s = 0, n = 0;
          for (let i = 0; i < d.length; i += 4) {
            s += 0.299 * d[i] + 0.587 * d[i + 1] + 0.114 * d[i + 2];
            n++;
          }
          return n ? s / n : -1;
        }""",
        [native_x, native_y],
    )


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
            f"windows: [{{ startMs: 0, endMs: 20000, tempoHz: 1, score: [{SCORE}], "
            f"chosenCell: -1 }}] }}, "
            f"marks: [{json.dumps(MARK)}] }}; }}"
        ),
        "ScriptExistsForVideo": "async () => false",
        "GenerateScript": (
            "async (opts) => { window.__calls.push(['GenerateScript', opts]); "
            "return { path: '/tmp/clip.funscript' }; }"
        ),
    }))
    harness = FRONTEND / "test" / "_generator_scene_map_follow_preview_harness.html"
    harness.write_text(PAGE)

    with sync_playwright() as p:
        browser = p.chromium.launch()
        page = browser.new_page()
        page.on("dialog", lambda d: d.dismiss())
        page.on("pageerror", lambda e: print("   [pageerror]", e))
        page.goto(f"{base}/test/_generator_scene_map_follow_preview_harness.html")
        page.wait_for_function("window.__ready === true")

        page.click("#gen-choose")
        page.wait_for_function(
            "() => (window.__calls || []).some(c => c[0] === 'LoadSceneMapForVideo')",
            timeout=5000)
        page.wait_for_function(
            "() => { const c = document.querySelector('#roi-canvas');"
            " return c && c.width > 10 && c.height > 10; }",
            timeout=5000)

        page.click("#gen-advanced > summary")
        page.evaluate("document.querySelector('#gen-advanced-scenemap').open = true")
        page.wait_for_function(
            "() => (document.querySelector('#gen-scene-map-marks-label')"
            "?.textContent || '').includes('→follow')",
            timeout=5000)
        label = page.evaluate(
            "() => document.querySelector('#gen-scene-map-marks-label')?.textContent || ''")
        check("Restored Follow ignore listed", "ignore@" in label and "→follow" in label)

        # t=0 → Path sample near static top-left
        luma_static_t0 = _luma_at(page, 10, 20)
        luma_path_t0 = _luma_at(page, 500, 200)
        check("At t=0 Ignore drawn near Path[0] (darker)",
              luma_static_t0 >= 0 and luma_static_t0 < luma_path_t0 - 8)

        # Scrub to 10s → mark should jump to Path[10s]
        page.fill("#gen-seek", "10")
        page.click("#gen-seek-btn")
        page.wait_for_function(
            "() => (document.querySelector('#gen-status')?.textContent || '')"
            ".includes('Frame at 10')",
            timeout=5000)
        # Allow redraw after frame load
        page.wait_for_timeout(200)

        luma_static_t10 = _luma_at(page, 10, 20)
        luma_path_t10 = _luma_at(page, 500, 200)
        check("At t=10s Ignore drawn near Path[10s] (darker)",
              luma_path_t10 >= 0 and luma_path_t10 < luma_static_t10 - 8)

        # Heatmap off — marks still drawn (help text: marks stay)
        page.uncheck("#gen-scene-map-overlay")
        page.wait_for_timeout(100)
        luma_after = _luma_at(page, 500, 200)
        check("Heatmap off still draws Follow Ignore at scrub",
              luma_after >= 0 and luma_after < 200)

        browser.close()

    shutdown()
    harness.unlink(missing_ok=True)
    return check.report()


if __name__ == "__main__":
    sys.exit(main())
