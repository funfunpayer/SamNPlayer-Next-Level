"""V1 target extras: Invert Curve (shown channel, range or whole, undoable),
±10 s skip buttons, sleep timer.

Run: python3 cmd/gui-wails/frontend/test/pb_edit_extras_test.py
"""

import pathlib
import sys

from playwright.sync_api import sync_playwright

from _harness import Checker, app_stub, serve

PAGE = """<!doctype html><html><head><link rel="stylesheet" href="/src/style.css?real"></head>
<body><div id="root"></div>
<script type="module">
  import { initPlayback } from '/src/playback.js';
  window.__pb = initPlayback(document.querySelector('#root'));
  window.__ready = true;
</script></body></html>"""


def main():
    check = Checker()
    base, shutdown = serve(
        "window.__axes = { general: [{ at: 0, pos: 10 }, { at: 50000, pos: 30 }, { at: 100000, pos: 90 }] };\n"
        "window.__starts = []; window.__stops = 0;\n" + app_stub({
            "PickFunscriptFile": "async () => '/tmp/t.samn'",
            "LoadFunscript": "async () => ({ path: '/tmp/t.samn', actionCount: 3, durationMs: 100000, videoPath: '', hasVideo: false })",
            "GetScriptCurve": "async () => window.__axes.general.map(a => ({ atMs: a.at, pos: a.pos }))",
            "GetScriptAxisActions": "async (axis) => (window.__axes[axis || 'general'] || []).map(a => ({ ...a }))",
            "SaveScriptAxisActions": "async (axis, acts) => { window.__axes[axis] = acts.map(a => ({ at: a.at, pos: a.pos })); }",
            "GetHeatmap": "async n => Array.from({ length: n }, (_, i) => ({ atMs: i * 1000, intensity: 0.5 }))",
            "GetMarker": "async () => null",
            "VideoFileURL": "async () => ''",
            "GetOMarkers": "async () => []",
            "SnapTimeMs": "async (t) => t",
            "StartPlayback": "async (o) => { window.__starts.push(o); }",
            "StopPlayback": "async () => { window.__stops++; }",
        }))
    harness = pathlib.Path(__file__).resolve().parent / "_pb_edit_extras_harness.html"
    harness.write_text(PAGE)
    pos = "JSON.stringify(window.__axes.general.map(a => a.pos))"
    try:
        with sync_playwright() as p:
            b = p.chromium.launch()
            page = b.new_page(viewport={"width": 1280, "height": 900})
            errors = []
            page.on("pageerror", lambda e: errors.append(str(e)))
            page.on("dialog", lambda d: d.accept())
            page.clock.install()
            page.goto(f"{base}/test/_pb_edit_extras_harness.html")
            page.wait_for_function("window.__ready === true")
            page.click("#pb-choose")
            page.wait_for_function("document.querySelector('#pb-curve-edit-row').style.display === 'flex'", timeout=5000)

            # Invert whole curve (no selection) → confirm → 100 − pos; Undo restores.
            page.click("#pb-invert-curve")
            page.wait_for_function(f"{pos} === '[90,70,10]'", timeout=3000)
            check("Invert without selection flips the whole curve", True)
            page.wait_for_function("(document.querySelector('#pb-undo').title || '').includes('invert curve')", timeout=3000)
            page.click("#pb-undo")
            page.wait_for_function(f"{pos} === '[10,30,90]'", timeout=3000)
            check("Undo restores it", True)

            # Invert a marked range only (40–60 s).
            page.locator("#pb-heatmap").scroll_into_view_if_needed()
            hb = page.locator("#pb-heatmap").bounding_box()
            page.mouse.move(hb["x"] + hb["width"] * 0.40, hb["y"] + hb["height"] / 2)
            page.mouse.down()
            page.mouse.move(hb["x"] + hb["width"] * 0.60, hb["y"] + hb["height"] / 2, steps=6)
            page.mouse.up()
            page.click("#pb-invert-curve")
            page.wait_for_function(f"{pos} === '[10,70,90]'", timeout=3000)
            check("Invert with a selection flips only the range", True)

            # ±10 s: pause at 30 s, back 10 s, resume starts at 20 s.
            page.click("#pb-play")
            page.wait_for_function("window.__starts.length === 1", timeout=3000)
            page.evaluate("window.__pb.seekMs(30000)")
            page.click("#pb-play")  # pause
            page.wait_for_function("window.__stops === 1", timeout=3000)
            page.click("#pb-back10")
            page.click("#pb-play")  # resume
            page.wait_for_function("window.__starts.length === 2", timeout=3000)
            check("Back 10 s then resume → 20 s", page.evaluate("window.__starts[1].startAtMs") == 20000,
                  str(page.evaluate("window.__starts[1].startAtMs")))
            page.click("#pb-play")  # pause
            page.wait_for_function("window.__stops === 2", timeout=3000)
            page.click("#pb-fwd10")
            page.click("#pb-fwd10")
            page.click("#pb-play")
            page.wait_for_function("window.__starts.length === 3", timeout=3000)
            check("Forward 2 × 10 s → 40 s", page.evaluate("window.__starts[2].startAtMs") == 40000)

            # Sleep timer: 15 min → playback stops after 15 min.
            page.select_option("#pb-sleep", "15")
            check("sleep timer state on the root", page.evaluate("document.querySelector('#root').dataset.pbSleep") == "15")
            stops = page.evaluate("window.__stops")
            page.clock.fast_forward("14:00")
            check("still playing after 14 min", page.evaluate("window.__stops") == stops)
            page.clock.fast_forward("01:05")
            page.wait_for_function(f"window.__stops === {stops + 1}", timeout=3000)
            check("sleep timer stops playback after 15 min", True)
            check("no page errors", errors == [], str(errors[:3]))
            b.close()
    finally:
        shutdown()
        harness.unlink(missing_ok=True)
    return check.report()


if __name__ == "__main__":
    sys.exit(main())
