"""Play: "From audio" fills BPM from EstimatePlaybackAudioBPM and turns the grid on.

Display only; never rewrites the stroke.

Run: python3 cmd/gui-wails/frontend/test/pb_bpm_auto_test.py
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
        "EstimatePlaybackAudioBPM": "async () => { window.__bpmCalls = (window.__bpmCalls || 0) + 1; return 123; }",
    }))
    harness = pathlib.Path(__file__).resolve().parent / "_pb_bpm_auto_harness.html"
    harness.write_text(PAGE)
    try:
        with sync_playwright() as p:
            browser = p.chromium.launch()
            page = browser.new_page()
            page.on("pageerror", lambda e: print("   [pageerror]", e))
            page.goto(f"{base}/test/_pb_bpm_auto_harness.html")
            page.wait_for_function("window.__ready === true")
            check("From audio button present", page.locator("#pb-bpm-auto").count() == 1)
            page.evaluate("""() => {
              // As after loading a script: loaded panel + OFS edit section shown.
              document.querySelector('#pb-loaded').style.display = 'block';
              document.querySelector('#pb-ofs-edit-details').style.display = 'block';
              let n = document.querySelector('#pb-bpm-auto');
              while (n) { if (n.tagName === 'DETAILS') n.open = true; n = n.parentElement; }
            }""")
            page.click("#pb-bpm-auto")
            page.wait_for_function("document.querySelector('#pb-bpm').value === '123'", timeout=3000)
            check("BPM field filled from audio", page.input_value("#pb-bpm") == "123")
            check("BPM grid switched on", page.is_checked("#pb-bpm-grid"))
            check("status says from audio", "123 BPM" in page.inner_text("#pb-bpm-auto-status"))
            browser.close()
    finally:
        shutdown()
        harness.unlink(missing_ok=True)
    return check.report()


if __name__ == "__main__":
    sys.exit(main())
