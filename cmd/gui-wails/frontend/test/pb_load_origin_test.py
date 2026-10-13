"""S6 Library: the Player reports where a loaded script came from —
Generator results (opts.review) are 'generated', everything else 'imported'.

Run: python3 cmd/gui-wails/frontend/test/pb_load_origin_test.py
"""

import pathlib
import sys

from playwright.sync_api import sync_playwright

from _harness import Checker, app_stub, serve

PAGE = """<!doctype html><html><head></head>
<body><div id="tab-playback"></div>
<script type="module">
  import { initPlayback } from '/src/playback.js';
  window.__events = [];
  window.addEventListener('pb:loaded', e => window.__events.push(e.detail));
  window.__pb = initPlayback(document.querySelector('#tab-playback'));
  window.__ready = true;
</script></body></html>"""


def main():
    check = Checker()
    base, shutdown = serve(app_stub({
        "PickFunscriptFile": "async () => '/s/picked.samn'",
        "LoadFunscript": "async (p) => ({ path: p, actionCount: 2, durationMs: 10000, videoPath: '', hasVideo: false })",
        "GetScriptCurve": "async () => [{ atMs: 0, pos: 0 }, { atMs: 10000, pos: 100 }]",
        "GetHeatmap": "async n => []",
        "GetMarker": "async () => null",
        "VideoFileURL": "async () => ''",
    }))
    harness = pathlib.Path(__file__).resolve().parent / "_pb_load_origin_harness.html"
    harness.write_text(PAGE)
    try:
        with sync_playwright() as p:
            b = p.chromium.launch()
            page = b.new_page()
            page.on("pageerror", lambda e: print("   [pageerror]", e))
            page.goto(f"{base}/test/_pb_load_origin_harness.html")
            page.wait_for_function("window.__ready === true")
            page.click("#pb-choose")
            page.wait_for_function("window.__events.length === 1", timeout=5000)
            check("picker load → imported", page.evaluate("window.__events[0]") == {"path": "/s/picked.samn", "origin": "imported"})
            page.evaluate("window.__pb.loadScriptPath('/out/gen.samn', { review: true }).catch(() => {})")  # no top bar here
            page.wait_for_function("window.__events.length === 2", timeout=5000)
            check("Generator load (review) → generated", page.evaluate("window.__events[1]") == {"path": "/out/gen.samn", "origin": "generated"})
            b.close()
    finally:
        shutdown()
        harness.unlink(missing_ok=True)
    return check.report()


if __name__ == "__main__":
    sys.exit(main())
