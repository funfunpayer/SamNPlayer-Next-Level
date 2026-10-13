"""Play curve/heatmap strip heights — taller strips for readability.

Run: python3 cmd/gui-wails/frontend/test/play_heatmap_size_test.py
"""

import pathlib

from playwright.sync_api import sync_playwright

from _harness import Checker, app_stub, serve

FRONTEND = pathlib.Path(__file__).resolve().parents[1]

PAGE = """<!doctype html><html><body><div id="root"></div>
<script type="module">
  import { initPlayback } from '/src/playback.js';
  window.__pb = initPlayback(document.querySelector('#root'));
  window.__ready = true;
</script></body></html>"""


def main():
    check = Checker()
    curve_js = ("const CURVE = [];\n"
                "for (let i = 0; i < 80; i++) CURVE.push({ atMs: i * 250, pos: i % 2 ? 5 : 95 });\n")
    base, shutdown = serve(curve_js + app_stub({
        "PickFunscriptFile": "async () => '/tmp/test.funscript'",
        "LoadFunscript": "async () => ({ path: '/tmp/test.funscript', actionCount: CURVE.length, "
                         "durationMs: 20000, videoPath: '', hasVideo: false })",
        "GetScriptCurve": "async () => CURVE.slice()",
        "GetHeatmap": "async n => Array.from({ length: n }, (_, i) => ({ atMs: i * 1000, intensity: 0.5 }))",
        "GetMarker": "async () => null",
        "VideoFileURL": "async () => ''",
    }))
    harness = FRONTEND / "test" / "_play_heatmap_size_harness.html"
    harness.write_text(PAGE)

    with sync_playwright() as p:
        browser = p.chromium.launch()
        page = browser.new_page(viewport={"width": 1100, "height": 900})
        page.on("dialog", lambda d: d.dismiss())
        page.goto(f"{base}/test/_play_heatmap_size_harness.html")
        page.wait_for_function("window.__ready === true")

        check("HTML curve height attr 176",
              page.locator("#pb-curve").get_attribute("height") == "176")
        check("HTML heatmap height attr 56",
              page.locator("#pb-heatmap").get_attribute("height") == "56")

        page.click("#pb-choose")
        page.wait_for_function(
            "document.querySelector('#pb-curve').style.display === 'block'",
            timeout=5000)
        check("CSS curve height ≥ 160px when visible",
              page.eval_on_selector("#pb-curve",
                  "e => parseFloat(getComputedStyle(e).height)") >= 160)
        check("CSS heatmap height ≥ 48px when visible",
              page.eval_on_selector("#pb-heatmap",
                  "e => parseFloat(getComputedStyle(e).height)") >= 48)
        check("After load: canvas logical height curve ≥ 176",
              page.eval_on_selector("#pb-curve", "e => e.height") >= 176)
        check("After load: canvas logical height heatmap ≥ 56",
              page.eval_on_selector("#pb-heatmap", "e => e.height") >= 56)

        browser.close()
    shutdown()
    harness.unlink(missing_ok=True)
    return check.report()


if __name__ == "__main__":
    raise SystemExit(main())
