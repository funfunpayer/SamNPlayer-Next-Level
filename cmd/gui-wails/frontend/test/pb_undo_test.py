"""V1 W2 edit history: curve dots and range tools record the axis before /
after; Undo writes "before" back, Redo writes "after"; Ctrl+Z / Ctrl+Y.

Run: python3 cmd/gui-wails/frontend/test/pb_undo_test.py
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
        "window.__axes = { general: [{ at: 0, pos: 0 }, { at: 50000, pos: 50 }, { at: 100000, pos: 100 }] };\n"
        "window.__saves = 0;\n" + app_stub({
            "PickFunscriptFile": "async () => '/tmp/test.samn'",
            "LoadFunscript": "async () => ({ path: '/tmp/test.samn', actionCount: window.__axes.general.length, durationMs: 100000, videoPath: '', hasVideo: false })",
            "GetScriptCurve": "async () => window.__axes.general.map(a => ({ atMs: a.at, pos: a.pos }))",
            "GetScriptAxisActions": "async (axis) => (window.__axes[axis || 'general'] || []).map(a => ({ ...a }))",
            "SaveScriptAxisActions": "async (axis, acts) => { window.__saves++; window.__axes[axis] = acts.map(a => ({ at: a.at, pos: a.pos })); }",
            "EditDeleteRange": "async (axis, a, b) => { window.__axes[axis] = window.__axes[axis].filter(x => x.at < a || x.at > b); }",
            "GetHeatmap": "async n => Array.from({ length: n }, (_, i) => ({ atMs: i * 1000, intensity: 0.5 }))",
            "GetMarker": "async () => null",
            "VideoFileURL": "async () => ''",
            "GetOMarkers": "async () => []",
            "SnapTimeMs": "async (t) => t",
        }))
    harness = pathlib.Path(__file__).resolve().parent / "_pb_undo_harness.html"
    harness.write_text(PAGE)
    count = "window.__axes.general.length"
    try:
        with sync_playwright() as p:
            b = p.chromium.launch()
            page = b.new_page(viewport={"width": 1280, "height": 900})
            errors = []
            page.on("pageerror", lambda e: errors.append(str(e)))
            page.goto(f"{base}/test/_pb_undo_harness.html")
            page.wait_for_function("window.__ready === true")
            page.click("#pb-choose")
            page.wait_for_function("document.querySelector('#pb-curve-edit-row').style.display === 'flex'", timeout=5000)
            check("Undo / Redo disabled at start", page.is_disabled("#pb-undo") and page.is_disabled("#pb-redo"))
            page.check("#pb-curve-edit")
            page.wait_for_timeout(200)
            page.locator("#pb-curve").scroll_into_view_if_needed()
            box = page.locator("#pb-curve").bounding_box()
            # Click an empty spot → a new dot is saved.
            page.mouse.move(box["x"] + box["width"] * 0.25, box["y"] + box["height"] * 0.2)
            page.mouse.down()
            page.mouse.up()
            page.wait_for_function(f"{count} === 4", timeout=3000)
            page.wait_for_function("!document.querySelector('#pb-undo').disabled", timeout=3000)
            check("new dot recorded → Undo enabled", "curve dots" in (page.get_attribute("#pb-undo", "title") or ""))
            page.click("#pb-undo")
            page.wait_for_function(f"{count} === 3", timeout=3000)
            check("Undo restores the 3 dots", page.evaluate("window.__axes.general.map(a => a.at)") == [0, 50000, 100000])
            check("Redo enabled after Undo", page.is_enabled("#pb-redo"))
            page.click("#pb-redo")
            page.wait_for_function(f"{count} === 4", timeout=3000)
            check("Redo brings the dot back", True)
            check("editor buffer follows (4 dots shown)", page.evaluate("document.querySelector('#root').dataset.pbUndo") == "1")

            # Keyboard: Ctrl+Z / Ctrl+Y.
            page.click("#root")  # focus the page, not an input
            page.keyboard.press("Control+z")
            page.wait_for_function(f"{count} === 3", timeout=3000)
            check("Ctrl+Z undoes", True)
            page.keyboard.press("Control+y")
            page.wait_for_function(f"{count} === 4", timeout=3000)
            check("Ctrl+Y redoes", True)

            # Range tool: mark 40–60 s on the heatmap, delete it, undo.
            page.locator("#pb-heatmap").scroll_into_view_if_needed()
            hb = page.locator("#pb-heatmap").bounding_box()
            page.mouse.move(hb["x"] + hb["width"] * 0.40, hb["y"] + hb["height"] / 2)
            page.mouse.down()
            page.mouse.move(hb["x"] + hb["width"] * 0.60, hb["y"] + hb["height"] / 2, steps=6)
            page.mouse.up()
            page.wait_for_function("!document.querySelector('#pb-del-range').disabled", timeout=3000)
            page.evaluate("document.querySelector('#pb-del-range').click()")
            page.wait_for_function("!window.__axes.general.some(a => a.at === 50000)", timeout=3000)
            page.wait_for_function("(document.querySelector('#pb-undo').title || '').includes('delete range')", timeout=3000)
            check("range delete recorded", True)
            page.click("#pb-undo")
            page.wait_for_function("window.__axes.general.some(a => a.at === 50000)", timeout=3000)
            check("Undo restores the deleted range", True)
            # A new edit after Undo clears Redo.
            page.click("#pb-undo")
            page.wait_for_function(f"{count} === 3", timeout=3000)
            page.mouse.move(box["x"] + box["width"] * 0.75, box["y"] + box["height"] * 0.2)
            page.mouse.down()
            page.mouse.up()
            page.wait_for_function(f"{count} === 4", timeout=3000)
            page.wait_for_timeout(200)
            check("new edit clears Redo", page.is_disabled("#pb-redo"))

            # Loading a script resets the history.
            page.click("#pb-choose")
            page.wait_for_function("document.querySelector('#pb-undo').disabled", timeout=3000)
            check("new load resets the history", page.is_disabled("#pb-redo"))
            check("no page errors", errors == [], str(errors[:3]))
            b.close()
    finally:
        shutdown()
        harness.unlink(missing_ok=True)
    return check.report()


if __name__ == "__main__":
    sys.exit(main())
