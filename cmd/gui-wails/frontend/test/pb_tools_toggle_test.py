"""Play layout: the tools column collapses (button or T key) for a bigger video,
the choice is remembered, and the info sidebar is hidden on the Play tab.

Run: python3 cmd/gui-wails/frontend/test/pb_tools_toggle_test.py
"""

import pathlib
import sys

from playwright.sync_api import sync_playwright

from _harness import Checker, app_stub, serve

PAGE = """<!doctype html><html><head><link rel="stylesheet" href="/src/style.css?real"></head>
<body data-tab="playback"><aside id="sidebar">info</aside><div id="tab-playback"></div>
<input id="other-input" />
<script type="module">
  import { initPlayback } from '/src/playback.js';
  window.__pb = initPlayback(document.querySelector('#tab-playback'));
  window.__ready = true;
</script></body></html>"""


def main():
    check = Checker()
    base, shutdown = serve(app_stub({
        "GetSettings": "async () => ({playbackAudioVibration:false,playbackMock:false,"
                       "playbackSync:'video',playbackTickMs:50,playbackMaxSpeed:0.6,"
                       "playbackSmoothing:0.3,playbackSoftStartMs:0,playbackEOEnabled:false,"
                       "playbackEOMin:0.1,playbackEOHoldS:2,playbackEORestoreMs:400,"
                       "playbackVideoPlayAutostart:true})",
        "SetSetting": "async () => {}",
    }))
    harness = pathlib.Path(__file__).resolve().parent / "_pb_tools_toggle_harness.html"
    harness.write_text(PAGE)
    try:
        with sync_playwright() as p:
            browser = p.chromium.launch()
            page = browser.new_page(viewport={"width": 1440, "height": 900})
            page.on("pageerror", lambda e: print("   [pageerror]", e))
            page.goto(f"{base}/test/_pb_tools_toggle_harness.html")
            page.wait_for_function("window.__ready === true")
            page.evaluate("localStorage.removeItem('pb.toolsCollapsed')")
            check("info sidebar hidden on the Play tab", page.is_hidden("#sidebar"))
            page.evaluate("document.body.dataset.tab = 'generator'")
            check("info sidebar visible on other tabs", page.is_visible("#sidebar"))
            page.evaluate("document.body.dataset.tab = 'playback'")

            page.evaluate("document.querySelector('#pb-loaded').hidden = false")
            root = "document.querySelector('#tab-playback').classList.contains('pb-tools-collapsed')"
            check("tools start expanded", not page.evaluate(root))
            page.click("#pb-tools-toggle")
            check("button collapses the tools", page.evaluate(root))
            check("collapse is remembered",
                  page.evaluate("localStorage.getItem('pb.toolsCollapsed')") == "1")
            check("button stays reachable when collapsed", page.is_visible("#pb-tools-toggle"))
            page.keyboard.press("t")
            check("T key expands again", not page.evaluate(root))
            page.focus("#other-input")
            page.keyboard.press("t")
            check("T typed into an input does not toggle", not page.evaluate(root))
            page.evaluate("document.activeElement.blur(); document.body.dataset.tab = 'generator'")
            page.keyboard.press("t")
            check("T on another tab does not toggle", not page.evaluate(root))
            page.evaluate("document.body.dataset.tab = 'playback'")
            page.keyboard.press("t")
            check("T on Play collapses", page.evaluate(root))

            page.reload()
            page.wait_for_function("window.__ready === true")
            check("collapsed state restored after reload", page.evaluate(root))
            page.evaluate("localStorage.removeItem('pb.toolsCollapsed')")
            browser.close()
    finally:
        shutdown()
        harness.unlink(missing_ok=True)
    return check.report()


if __name__ == "__main__":
    sys.exit(main())
