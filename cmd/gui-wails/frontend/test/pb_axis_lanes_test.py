"""Play multi-axis view: colour-coded vibration / suction lanes under the curve,
legend chips that switch the shown axis, main curve in the axis colour.

Run: python3 cmd/gui-wails/frontend/test/pb_axis_lanes_test.py
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
    data_js = ("const STROKE = []; const VIB = []; const SUC = [];\n"
               "for (let i = 0; i < 200; i++) {\n"
               "  STROKE.push({ atMs: i * 500, pos: i % 2 ? 10 : 90 });\n"
               "  VIB.push({ at: i * 500, pos: Math.round(50 + 45 * Math.sin(i / 6)) });\n"
               "  SUC.push({ at: i * 500, pos: Math.round(50 + 45 * Math.cos(i / 9)) });\n"
               "}\n"
               "window.__axes = { general: STROKE.map(p => ({ at: p.atMs, pos: p.pos })), vibration: VIB, suction: SUC };\n")
    base, shutdown = serve(data_js + app_stub({
        "PickFunscriptFile": "async () => '/tmp/test.samn'",
        "LoadFunscript": "async () => ({ path: '/tmp/test.samn', actionCount: STROKE.length, "
                         "durationMs: 100000, videoPath: '', hasVideo: false })",
        "GetScriptCurve": "async () => STROKE.slice()",
        "GetScriptAxisActions": "async (axis) => (window.__axes[axis || 'general'] || null)",
        "GetHeatmap": "async n => Array.from({ length: n }, (_, i) => ({ atMs: i * 1000, intensity: 0.5 }))",
        "GetMarker": "async () => null",
        "VideoFileURL": "async () => ''",
    }))
    harness = pathlib.Path(__file__).resolve().parent / "_pb_axis_lanes_harness.html"
    harness.write_text(PAGE)
    try:
        with sync_playwright() as p:
            browser = p.chromium.launch()
            page = browser.new_page(viewport={"width": 1280, "height": 900})
            page.on("dialog", lambda d: d.dismiss())
            page.on("pageerror", lambda e: print("   [pageerror]", e))
            page.goto(f"{base}/test/_pb_axis_lanes_harness.html")
            page.wait_for_function("window.__ready === true")
            check("lanes hidden before a script is loaded", page.is_hidden("#pb-axis-lanes"))

            page.click("#pb-choose")
            page.wait_for_function("!document.querySelector('#pb-axis-lanes').hidden", timeout=5000)
            check("lanes visible for a script with feel channels", page.is_visible("#pb-axis-lanes"))
            STROKE = page.evaluate(TOKEN_RGB, "--axis-stroke")
            VIB = page.evaluate(TOKEN_RGB, "--axis-vibration")
            SUC = page.evaluate(TOKEN_RGB, "--axis-suction")
            check("theme defines three distinct channel colours", None not in (STROKE, VIB, SUC)
                  and len({tuple(STROKE), tuple(VIB), tuple(SUC)}) == 3, str((STROKE, VIB, SUC)))
            check("legend visible", page.is_visible("#pb-axis-legend"))
            check("two lanes → 68 px tall",
                  page.evaluate("document.querySelector('#pb-axis-lanes').getBoundingClientRect().height") == 68)
            check("vibration lane drawn in its token colour", page.evaluate(PIXELS_OF, ["#pb-axis-lanes", VIB]) > 200)
            check("suction lane drawn in its token colour", page.evaluate(PIXELS_OF, ["#pb-axis-lanes", SUC]) > 200)
            check("main curve in the stroke colour", page.evaluate(PIXELS_OF, ["#pb-curve", STROKE]) > 200)
            check("Stroke chip active",
                  page.get_attribute('.pb-axis-chip[data-axis="general"]', "aria-pressed") == "true")

            page.click('.pb-axis-chip[data-axis="suction"]')
            page.wait_for_function(
                "document.querySelector('.pb-axis-chip[data-axis=\"suction\"]').getAttribute('aria-pressed') === 'true'",
                timeout=5000)
            check("chip switches the axis select", page.input_value("#pb-axis") == "suction")
            check("main curve now in the suction colour", page.evaluate(PIXELS_OF, ["#pb-curve", SUC]) > 200)

            box = page.locator("#pb-axis-lanes").bounding_box()
            page.mouse.click(box["x"] + box["width"] / 2, box["y"] + box["height"] * 0.25)
            page.wait_for_function("document.querySelector('#pb-axis').value === 'vibration'", timeout=5000)
            page.wait_for_function(
                "document.querySelector('.pb-axis-chip[data-axis=\"vibration\"]').getAttribute('aria-pressed') === 'true'",
                timeout=5000)
            check("clicking the top lane selects vibration", page.input_value("#pb-axis") == "vibration")
            check("main curve now in the vibration colour", page.evaluate(PIXELS_OF, ["#pb-curve", VIB]) > 200)

            # A plain funscript without feel channels: no lanes, no legend.
            page.evaluate("window.__axes.vibration = null; window.__axes.suction = []")
            page.select_option("#pb-axis", "general")
            page.wait_for_function("document.querySelector('#pb-axis-lanes').hidden", timeout=5000)
            check("no feel channels → lanes hidden", page.is_hidden("#pb-axis-lanes"))
            check("no feel channels → legend hidden", page.is_hidden("#pb-axis-legend"))
            browser.close()
    finally:
        shutdown()
        harness.unlink(missing_ok=True)
    return check.report()


if __name__ == "__main__":
    sys.exit(main())
