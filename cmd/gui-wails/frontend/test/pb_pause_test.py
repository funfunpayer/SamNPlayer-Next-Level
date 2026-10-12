"""Play / Pause (Owner 11 Oct): #pb-play toggles Play ⇄ Pause; Pause keeps
the position and Resume starts there (startAtMs without video); Stop resets.

Run: python3 cmd/gui-wails/frontend/test/pb_pause_test.py
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
    base, shutdown = serve("window.__starts = []; window.__stops = 0;\n" + app_stub({
        "PickFunscriptFile": "async () => '/tmp/t.samn'",
        "LoadFunscript": "async () => ({ path: '/tmp/t.samn', actionCount: 3, durationMs: 100000, videoPath: '', hasVideo: false })",
        "GetScriptCurve": "async () => [{ atMs: 0, pos: 0 }, { atMs: 50000, pos: 50 }, { atMs: 100000, pos: 100 }]",
        "GetHeatmap": "async n => Array.from({ length: n }, (_, i) => ({ atMs: i * 1000, intensity: 0.5 }))",
        "GetMarker": "async () => null",
        "VideoFileURL": "async () => ''",
        "SnapTimeMs": "async (t) => t",
        "StartPlayback": "async (o) => { window.__starts.push(o); }",
        "StopPlayback": "async () => { window.__stops++; }",
    }))
    harness = pathlib.Path(__file__).resolve().parent / "_pb_pause_harness.html"
    harness.write_text(PAGE)
    st = "document.querySelector('#pb-play').dataset.state"
    label = "document.querySelector('#pb-play').getAttribute('aria-label')"
    try:
        with sync_playwright() as p:
            b = p.chromium.launch()
            page = b.new_page(viewport={"width": 1280, "height": 900})
            errors = []
            page.on("pageerror", lambda e: errors.append(str(e)))
            page.goto(f"{base}/test/_pb_pause_harness.html")
            page.wait_for_function("window.__ready === true")
            page.click("#pb-choose")
            page.wait_for_function("document.querySelector('#root').dataset.pbState === 'loaded'", timeout=5000)
            check("starts as Play", page.evaluate(st) == "play" and page.evaluate(label) == "Play")
            page.click("#pb-play")
            page.wait_for_function("window.__starts.length === 1", timeout=3000)
            check("Play starts from 0", page.evaluate("window.__starts[0].startAtMs") == 0)
            check("button becomes Pause", page.evaluate(st) == "pause" and page.evaluate(label) == "Pause")
            check("Pause button stays clickable", page.is_enabled("#pb-play"))
            # Playback moved on to 42 s.
            page.evaluate("window.__pb.seekMs(42000)")
            page.click("#pb-play")
            page.wait_for_function("window.__stops === 1", timeout=3000)
            check("Pause stops the device", True)
            check("then shows Resume", page.evaluate(st) == "play" and page.evaluate(label) == "Resume")
            check("paused state on the root", page.evaluate("document.querySelector('#root').dataset.pbPaused") == "true")
            check("Stop stays available while paused", page.is_enabled("#pb-stop"))
            page.keyboard.press("Space")
            page.wait_for_function("window.__starts.length === 2", timeout=3000)
            check("Space resumes at the paused position", page.evaluate("window.__starts[1].startAtMs") == 42000,
                  str(page.evaluate("window.__starts[1]")))
            page.keyboard.press("Space")
            page.wait_for_function("window.__stops === 2", timeout=3000)
            page.click("#pb-stop")
            page.wait_for_function(f"{label} === 'Play'", timeout=3000)
            check("Stop resets: label Play, not paused", page.evaluate("document.querySelector('#root').dataset.pbPaused") == "false")
            page.click("#pb-play")
            page.wait_for_function("window.__starts.length === 3", timeout=3000)
            check("after Stop, Play starts from 0 again", page.evaluate("window.__starts[2].startAtMs") == 0)
            check("no page errors", errors == [], str(errors[:3]))
            b.close()
    finally:
        shutdown()
        harness.unlink(missing_ok=True)
    return check.report()


if __name__ == "__main__":
    sys.exit(main())
