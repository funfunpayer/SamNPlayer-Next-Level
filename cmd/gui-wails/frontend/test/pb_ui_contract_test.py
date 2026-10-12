"""Play UI contract (#475): layout hooks, state attributes, theme tokens,
keyboard on axis chips/lanes, canvases redraw when the layout resizes them.

The layout (CSS) is free to change; these hooks are what it may rely on —
see docs/PLAY_UI_CONTRACT.md.

Run: python3 cmd/gui-wails/frontend/test/pb_ui_contract_test.py
"""

import pathlib
import sys

from playwright.sync_api import sync_playwright

from _harness import Checker, app_stub, serve

PAGE = """<!doctype html><html><head><link rel="stylesheet" href="/src/style.css?real"></head>
<body data-tab="playback"><div id="tab-playback"></div>
<script type="module">
  import { initPlayback } from '/src/playback.js';
  window.__pb = initPlayback(document.querySelector('#tab-playback'));
  window.__ready = true;
</script></body></html>"""

REGIONS = ["head", "empty", "media", "video", "transport", "timeline", "legend", "curve",
           "lanes", "heatmap", "zoom", "playlist", "tools", "tool-group"]

PIXELS_OF = """
([sel, rgb]) => {
  const c = document.querySelector(sel);
  const d = c.getContext('2d').getImageData(0, 0, c.width, c.height).data;
  let n = 0;
  for (let i = 0; i < d.length; i += 4)
    if (d[i + 3] > 200 && Math.abs(d[i] - rgb[0]) < 14 && Math.abs(d[i + 1] - rgb[1]) < 14 && Math.abs(d[i + 2] - rgb[2]) < 14) n++;
  return n;
}
"""


def main():
    check = Checker()
    data_js = ("const STROKE = []; const VIB = []; const SUC = [];\n"
               "for (let i = 0; i < 200; i++) {\n"
               "  STROKE.push({ atMs: i * 500, pos: i % 2 ? 10 : 90 });\n"
               "  VIB.push({ at: i * 500, pos: 40 + (i % 5) * 10 });\n"
               "  SUC.push({ at: i * 500, pos: 70 - (i % 4) * 10 });\n"
               "}\n"
               "window.__axes = { general: STROKE.map(p => ({ at: p.atMs, pos: p.pos })), vibration: VIB, suction: SUC };\n")
    base, shutdown = serve(data_js + app_stub({
        "PickFunscriptFile": "async () => '/tmp/t.samn'",
        "LoadFunscript": "async () => ({ path: '/tmp/t.samn', actionCount: STROKE.length, durationMs: 100000, videoPath: '', hasVideo: false })",
        "GetScriptCurve": "async () => STROKE.slice()",
        "GetScriptAxisActions": "async (axis) => (window.__axes[axis || 'general'] || null)",
        "GetHeatmap": "async n => Array.from({ length: n }, (_, i) => ({ atMs: i * 1000, intensity: 0.5 }))",
        "GetMarker": "async () => null",
        "VideoFileURL": "async () => ''",
    }))
    harness = pathlib.Path(__file__).resolve().parent / "_pb_ui_contract_harness.html"
    harness.write_text(PAGE)
    try:
        with sync_playwright() as p:
            browser = p.chromium.launch()
            page = browser.new_page(viewport={"width": 1366, "height": 768})
            page.on("dialog", lambda d: d.dismiss())
            page.on("pageerror", lambda e: print("   [pageerror]", e))
            page.goto(f"{base}/test/_pb_ui_contract_harness.html")
            page.wait_for_function("window.__ready === true")
            root = "document.querySelector('#tab-playback')"

            for r in REGIONS:
                check(f"region '{r}' present",
                      page.evaluate(f"!!document.querySelector('[data-pb-region=\"{r}\"]')"))
            check("state starts empty", page.evaluate(f"{root}.dataset.pbState") == "empty")
            check("tools start open", page.evaluate(f"{root}.dataset.pbTools") == "open")
            check("no video yet", page.evaluate(f"{root}.dataset.pbVideo") == "false")
            order = page.evaluate("""() => ['video','transport','timeline'].map(r =>
                [...document.querySelectorAll('[data-pb-region]')].indexOf(document.querySelector(`[data-pb-region="${r}"]`)))""")
            check("DOM order video → transport → timeline (tab order)", order == sorted(order), str(order))

            page.click("#pb-choose")
            page.wait_for_function(f"{root}.dataset.pbState === 'loaded'", timeout=5000)
            page.wait_for_function("!document.querySelector('#pb-axis-lanes').hidden", timeout=5000)
            check("state loaded after choosing a script", True)
            page.click("#pb-tools-toggle")
            check("tools folded state", page.evaluate(f"{root}.dataset.pbTools") == "folded")
            page.click("#pb-tools-toggle")

            # Theme tokens: the layout recolours the stroke via CSS only.
            page.evaluate(f"{root}.style.setProperty('--axis-stroke', '#ff0000')")
            page.evaluate("window.dispatchEvent(new Event('resize'))")
            page.wait_for_timeout(300)
            check("--axis-stroke recolours the curve", page.evaluate(PIXELS_OF, ["#pb-curve", [255, 0, 0]]) > 150)
            page.evaluate(f"{root}.style.setProperty('--axis-vibration', '#00ff00')")
            page.evaluate("window.dispatchEvent(new Event('resize'))")
            page.wait_for_timeout(300)
            check("--axis-vibration recolours the lane", page.evaluate(PIXELS_OF, ["#pb-axis-lanes", [0, 255, 0]]) > 100)

            # Resize by layout only (no window resize): canvas follows its CSS size.
            page.evaluate("document.querySelector('#pb-curve').style.height = '120px'")
            page.wait_for_function("(() => { const c = document.querySelector('#pb-curve'); return c.clientHeight < 130 && c.height === Math.round(c.clientHeight * devicePixelRatio); })()", timeout=3000)
            check("curve backing store follows CSS height (ResizeObserver)", True)

            # Keyboard: arrows on chips select the axis.
            page.focus('.pb-axis-chip[data-axis="general"]')
            page.keyboard.press("ArrowRight")
            page.wait_for_function("document.querySelector('#pb-axis').value === 'vibration'", timeout=5000)
            check("→ on chips selects next axis", page.evaluate("document.activeElement.dataset.axis") == "vibration")
            page.keyboard.press("End")
            page.wait_for_function("document.querySelector('#pb-axis').value === 'suction'", timeout=5000)
            check("End selects last axis", True)
            page.focus("#pb-axis-lanes")
            page.keyboard.press("ArrowDown")
            page.wait_for_function("document.querySelector('#pb-axis').value === 'vibration'", timeout=5000)
            check("↓ on lanes cycles the channel", True)
            check("lanes have an aria-label",
                  "Vibration" in (page.get_attribute("#pb-axis-lanes", "aria-label") or ""))
            browser.close()
    finally:
        shutdown()
        harness.unlink(missing_ok=True)
    return check.report()


if __name__ == "__main__":
    sys.exit(main())
