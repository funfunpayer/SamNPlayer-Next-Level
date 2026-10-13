"""V1 target Sound Curve: the video's loudness written as the Vibration curve
(smoothed, thinned), shown as a lane, undoable.

Run: python3 cmd/gui-wails/frontend/test/pb_sound_curve_test.py
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


def main():
    check = Checker()
    base, shutdown = serve(
        "window.__axes = { general: Array.from({ length: 200 }, (_, i) => ({ at: i * 500, pos: i % 2 ? 10 : 90 })), vibration: [], suction: [] };\n"
        + app_stub({
            "PickFunscriptFile": "async () => '/tmp/t.samn'",
            "LoadFunscript": "async () => ({ path: '/tmp/t.samn', actionCount: 200, durationMs: 100000, videoPath: '', hasVideo: false })",
            "GetScriptCurve": "async () => window.__axes.general.map(a => ({ atMs: a.at, pos: a.pos }))",
            "GetScriptAxisActions": "async (axis) => (window.__axes[axis || 'general'] || []).map(a => ({ ...a }))",
            "SaveScriptAxisActions": "async (axis, acts) => { window.__axes[axis] = acts.map(a => ({ at: a.at, pos: a.pos })); }",
            "GetHeatmap": "async n => Array.from({ length: n }, (_, i) => ({ atMs: i * 1000, intensity: 0.5 }))",
            "GetMarker": "async () => null",
            "VideoFileURL": "async () => ''",
            "GetOMarkers": "async () => []",
            # 100 s of sound: quiet first half, loud second half.
            "GetAudioLevelEnvelope": "async () => Array.from({ length: 2000 }, (_, i) => i < 1000 ? 0.1 : 0.8)",
        }))
    harness = pathlib.Path(__file__).resolve().parent / "_pb_sound_curve_harness.html"
    harness.write_text(PAGE)
    try:
        with sync_playwright() as p:
            b = p.chromium.launch()
            page = b.new_page(viewport={"width": 1280, "height": 900})
            errors = []
            page.on("pageerror", lambda e: errors.append(str(e)))
            page.on("dialog", lambda d: d.accept())
            page.goto(f"{base}/test/_pb_sound_curve_harness.html")
            page.wait_for_function("window.__ready === true")
            page.click("#pb-choose")
            page.wait_for_function("document.querySelector('#tab-playback').dataset.pbState === 'loaded'", timeout=5000)
            check("disabled without sound", page.is_disabled("#pb-aa-write"))
            page.evaluate("document.querySelector('#pb-video-stage').classList.add('has-video')")
            page.wait_for_function("!document.querySelector('#pb-aa-write').disabled", timeout=5000)
            check("enabled once the sound is read", True)
            page.click("#pb-aa-write")
            page.wait_for_function("window.__axes.vibration.length > 1", timeout=3000)
            vib = page.evaluate("window.__axes.vibration")
            check("curve follows the loudness (10 → 80)", vib[0]["pos"] == 10 and vib[-1]["pos"] == 80, str(vib[:2] + vib[-2:]))
            check("thinned (not one point per 50 ms)", 2 < len(vib) < 200, str(len(vib)))
            check("covers the script length", vib[-1]["at"] <= 100000 and vib[-1]["at"] >= 95000, str(vib[-1]))
            page.wait_for_function("(document.querySelector('#pb-axis-lanes').getAttribute('aria-label') || '').includes('Vibration')", timeout=3000)
            check("Vibration lane appears", True)
            page.wait_for_function("(document.querySelector('#pb-undo').title || '').includes('sound curve')", timeout=3000)
            page.evaluate("document.querySelector('#pb-undo').click()")
            page.wait_for_function("window.__axes.vibration.length === 0", timeout=3000)
            check("Undo removes it again", True)
            check("no page errors", errors == [], str(errors[:3]))
            b.close()
    finally:
        shutdown()
        harness.unlink(missing_ok=True)
    return check.report()


if __name__ == "__main__":
    sys.exit(main())
