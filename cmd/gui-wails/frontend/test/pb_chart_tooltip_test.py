"""Playback: heatmap/curve hover tooltips show time + value.

Ausführen: python3 cmd/gui-wails/frontend/test/pb_chart_tooltip_test.py
"""

import json
import pathlib
import sys

from playwright.sync_api import sync_playwright

from _harness import Checker, app_stub, serve

ROOT = pathlib.Path(__file__).resolve().parents[1]
CLIP = ROOT / "test" / "fixtures" / "clip7776_tf.funscript"

PAGE = """<!doctype html><html><body><div id="root"></div>
<script type="module">
  import { initPlayback } from '/src/playback.js';
  window.__pb = initPlayback(document.querySelector('#root'));
  window.__ready = true;
</script></body></html>"""


def main():
    check = Checker()
    raw = json.loads(CLIP.read_text())
    actions = raw["actions"]
    duration_ms = int(raw.get("metadata", {}).get("duration") or actions[-1]["at"])
    curve = [{"atMs": a["at"], "pos": a["pos"]} for a in actions]
    curve_js = json.dumps(curve)
    path = str(CLIP)

    base, shutdown = serve(app_stub({
        "PickFunscriptFile": f"async () => {json.dumps(path)}",
        "LoadFunscript": (
            f"async () => ({{ path: {json.dumps(path)}, actionCount: {len(actions)}, "
            f"durationMs: {duration_ms}, videoPath: '', hasVideo: false, "
            f"contactVibration: false }})"
        ),
        "GetScriptCurve": f"async () => {curve_js}",
        "GetHeatmap": "async n => Array.from({ length: n }, (_, i) => ({ atMs: i * 100, intensity: 0.42 }))",
        "GetMarker": "async () => null",
        "VideoFileURL": "async () => ''",
        "GetSpeedHighlights": "async () => []",
        "GetScriptOffset": "async () => 0",
        "GetOMarkers": "async () => []",
        "GetSettings": "async () => ({})",
    }))

    harness = pathlib.Path(__file__).resolve().parent / "_pb_chart_tooltip_harness.html"
    harness.write_text(PAGE)

    with sync_playwright() as p:
        browser = p.chromium.launch()
        page = browser.new_page()
        page.on("pageerror", lambda e: print("   [pageerror]", e))
        page.goto(f"{base}/test/_pb_chart_tooltip_harness.html")
        page.wait_for_function("window.__ready === true")

        page.click("#pb-choose")
        page.wait_for_function(
            "document.querySelector('#pb-curve') && "
            "getComputedStyle(document.querySelector('#pb-curve')).display !== 'none'",
            timeout=5000,
        )

        page.locator("#pb-curve").scroll_into_view_if_needed()  # layout may push it below the fold
        box = page.locator("#pb-curve").bounding_box()
        page.mouse.move(box["x"] + box["width"] * 0.25, box["y"] + box["height"] * 0.5)
        page.wait_for_function(
            "() => { const t = document.querySelector('#pb-chart-tooltip'); "
            "return t && !t.hidden && t.textContent.includes('·'); }",
            timeout=3000,
        )
        tip = page.locator("#pb-chart-tooltip").inner_text()
        check("curve tooltip visible with time and value", "·" in tip, tip)

        page.locator("#pb-heatmap").scroll_into_view_if_needed()  # layout may push it below the fold
        hbox = page.locator("#pb-heatmap").bounding_box()
        page.mouse.move(hbox["x"] + hbox["width"] * 0.5, hbox["y"] + hbox["height"] * 0.5)
        page.wait_for_function(
            "() => { const t = document.querySelector('#pb-chart-tooltip'); "
            "return t && !t.hidden && t.textContent.includes('%'); }",
            timeout=3000,
        )
        htip = page.locator("#pb-chart-tooltip").inner_text()
        check("heatmap tooltip shows intensity percent", "%" in htip, htip)

        page.mouse.move(0, 0)
        page.wait_for_function(
            "() => document.querySelector('#pb-chart-tooltip').hidden",
            timeout=3000,
        )
        check("tooltip hides on mouseleave", page.locator("#pb-chart-tooltip").is_hidden())

        browser.close()

    shutdown()
    harness.unlink(missing_ok=True)
    return check.report()


if __name__ == "__main__":
    sys.exit(main())
