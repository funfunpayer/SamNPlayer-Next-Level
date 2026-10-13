"""Play: "Write into script" asks, calls BakeScriptOffset and resets the offset field.

Writes the file, so it asks first; Cancel must change nothing.

Run: python3 cmd/gui-wails/frontend/test/pb_offset_bake_test.py
"""

import pathlib
import sys

from playwright.sync_api import sync_playwright

from _harness import Checker, app_stub, serve

PAGE = """<!doctype html><html><body><div id="root"></div>
<script type="module">
  import { initPlayback } from '/src/playback.js';
  window.__pb = initPlayback(document.querySelector('#root'));
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
        "SetSetting": "async (key, value) => { window.__calls = window.__calls || []; window.__calls.push([key, value]); }",
        "BakeScriptOffset": "async () => { window.__bake = (window.__bake || 0) + 1; return {shiftedMs: 250, dropped: 0}; }",
        "SetScriptOffset": "async () => {}",
    }))
    harness = pathlib.Path(__file__).resolve().parent / "_pb_offset_bake_harness.html"
    harness.write_text(PAGE)
    try:
        with sync_playwright() as p:
            browser = p.chromium.launch()
            page = browser.new_page()
            page.on("pageerror", lambda e: print("   [pageerror]", e))
            page.goto(f"{base}/test/_pb_offset_bake_harness.html")
            page.wait_for_function("window.__ready === true")
            check("Write into script button present", page.locator("#pb-offset-bake").count() == 1)
            page.evaluate("""() => {
              document.querySelector('#pb-loaded').style.display = 'block';
              document.querySelector('#pb-offset-row').style.display = 'flex';
              document.querySelector('#pb-offset').value = '250';
            }""")
            page.once("dialog", lambda d: d.dismiss())
            page.click("#pb-offset-bake")
            page.wait_for_timeout(300)
            check("cancel in the dialog writes nothing", page.evaluate("window.__bake || 0") == 0)
            page.once("dialog", lambda d: d.accept())
            page.click("#pb-offset-bake")
            page.wait_for_function("(window.__bake || 0) === 1", timeout=3000)
            check("BakeScriptOffset called once after OK", page.evaluate("window.__bake") == 1)
            page.wait_for_function("document.querySelector('#pb-offset').value === '0'", timeout=3000)
            check("offset field back to 0", page.input_value("#pb-offset") == "0")
            browser.close()
    finally:
        shutdown()
        harness.unlink(missing_ok=True)
    return check.report()


if __name__ == "__main__":
    sys.exit(main())
