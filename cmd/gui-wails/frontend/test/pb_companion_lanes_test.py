"""D4: multi-axis companion files (clip.twist.funscript …) show as read-only
lanes in their own colour; clicking them does not switch the edited axis.

Run: python3 cmd/gui-wails/frontend/test/pb_companion_lanes_test.py
"""

import pathlib
import sys

from playwright.sync_api import sync_playwright

from _harness import Checker, app_stub, serve

PAGE = """<!doctype html><html><head><link rel="stylesheet" href="/src/style.css?real"></head>
<body><div id="tab-playback"></div>
<script type="module">
  import { initPlayback } from '/src/playback.js';
  window.__pb = initPlayback(document.querySelector('#tab-playback'));
  window.__ready = true;
</script></body></html>"""

# Dominant non-transparent colour channel of a canvas: 'teal' | 'amber' | 'lilac'.
PIXELS_OF = """
([sel, rgb]) => {
  const c = document.querySelector(sel);
  const d = c.getContext('2d').getImageData(0, 0, c.width, c.height).data;
  let n = 0;
  for (let i = 0; i < d.length; i += 4) {
    if (d[i + 3] > 200 && Math.abs(d[i] - rgb[0]) < 12 && Math.abs(d[i + 1] - rgb[1]) < 12
        && Math.abs(d[i + 2] - rgb[2]) < 12) n++;
  }
  return n;
}
"""
# Channel colours come from the theme tokens (--axis-*), not fixed values.
TOKEN_RGB = """
(name) => {
  const v = getComputedStyle(document.querySelector('#tab-playback')).getPropertyValue(name).trim();
  const m = v.match(/^#([0-9a-f]{2})([0-9a-f]{2})([0-9a-f]{2})$/i);
  return m ? [parseInt(m[1], 16), parseInt(m[2], 16), parseInt(m[3], 16)] : null;
}
"""


def main():
    check = Checker()
    data_js = ("const STROKE = []; const TW = []; const PI = [];\n"
               "for (let i = 0; i < 200; i++) {\n"
               "  STROKE.push({ atMs: i * 500, pos: i % 2 ? 10 : 90 });\n"
               "  TW.push({ at: i * 500, pos: Math.round(50 + 45 * Math.sin(i / 6)) });\n"
               "  PI.push({ at: i * 500, pos: Math.round(50 + 45 * Math.cos(i / 9)) });\n"
               "}\n"
               "window.__calls = [];\n"
               "window.__axes = { general: STROKE.map(p => ({ at: p.atMs, pos: p.pos })), twist: TW, pitch: PI };\n")
    base, shutdown = serve(data_js + app_stub({
        "PickFunscriptFile": "async () => '/tmp/clip.funscript'",
        "LoadFunscript": "async () => ({ path: '/tmp/clip.funscript', actionCount: STROKE.length, "
                         "durationMs: 100000, videoPath: '', hasVideo: false })",
        "GetScriptCurve": "async () => STROKE.slice()",
        "GetScriptAxisActions": "async (axis) => { window.__calls.push(axis); return window.__axes[axis || 'general'] || null; }",
        "GetHeatmap": "async n => Array.from({ length: n }, (_, i) => ({ atMs: i * 1000, intensity: 0.5 }))",
        "GetMarker": "async () => null",
        "VideoFileURL": "async () => ''",
    }))
    harness = pathlib.Path(__file__).resolve().parent / "_pb_companion_lanes_harness.html"
    harness.write_text(PAGE)
    try:
        with sync_playwright() as p:
            browser = p.chromium.launch()
            page = browser.new_page(viewport={"width": 1280, "height": 900})
            errors = []
            page.on("pageerror", lambda e: errors.append(str(e)))
            page.goto(f"{base}/test/_pb_companion_lanes_harness.html")
            page.wait_for_function("window.__ready === true")
            page.click("#pb-choose")
            page.wait_for_function("!document.querySelector('#pb-axis-lanes').hidden", timeout=5000)
            calls = page.evaluate("window.__calls")
            check("every companion axis is asked for", all(a in calls for a in ["surge", "sway", "twist", "roll", "pitch"]), str(calls))
            check("lanes: twist + pitch only (files that exist)",
                  page.get_attribute("#tab-playback", "data-pb-lanes") == "twist,pitch")
            check("two lanes → 68 px tall",
                  page.evaluate("document.querySelector('#pb-axis-lanes').getBoundingClientRect().height") == 68)
            TW = page.evaluate(TOKEN_RGB, "--axis-twist")
            PI = page.evaluate(TOKEN_RGB, "--axis-pitch")
            check("twist / pitch colour tokens", TW is not None and PI is not None and TW != PI, str((TW, PI)))
            check("twist lane in its colour", page.evaluate(PIXELS_OF, ["#pb-axis-lanes", TW]) > 200)
            check("pitch lane in its colour", page.evaluate(PIXELS_OF, ["#pb-axis-lanes", PI]) > 200)
            check("no edit legend for read-only lanes", page.is_hidden("#pb-axis-legend"))
            label = page.get_attribute("#pb-axis-lanes", "aria-label") or ""
            check("lane label names the axes", "Twist" in label and "Pitch" in label, label)
            box = page.locator("#pb-axis-lanes").bounding_box()
            page.mouse.click(box["x"] + box["width"] / 2, box["y"] + box["height"] * 0.25)
            page.wait_for_timeout(200)
            check("clicking a companion lane keeps the stroke axis", page.input_value("#pb-axis") in ("general", ""))
            check("no page errors", errors == [], str(errors[:3]))
            browser.close()
    finally:
        shutdown()
        harness.unlink(missing_ok=True)
    return check.report()


if __name__ == "__main__":
    sys.exit(main())
