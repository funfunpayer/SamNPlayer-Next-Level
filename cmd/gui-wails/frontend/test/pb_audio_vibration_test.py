"""Play panel: "Vibration from video audio only" checkbox + saveSetting key.

Same setting as Settings → Play (playback.audio_vibration). Off by default.

Run: python3 cmd/gui-wails/frontend/test/pb_audio_vibration_test.py
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
    }))
    harness = pathlib.Path(__file__).resolve().parent / "_pb_audio_vibration_harness.html"
    harness.write_text(PAGE)
    try:
        with sync_playwright() as p:
            browser = p.chromium.launch()
            page = browser.new_page()
            page.on("pageerror", lambda e: print("   [pageerror]", e))
            page.goto(f"{base}/test/_pb_audio_vibration_harness.html")
            page.wait_for_function("window.__ready === true")
            check("Play panel has the audio-vibration checkbox",
                  page.locator("#pb-audio-vibration").count() == 1)
            check("checkbox is off by default", page.is_checked("#pb-audio-vibration") is False)
            label = page.locator("label[for='pb-audio-vibration']")
            check("label text", "Vibration from video audio only" in (label.inner_text() or ""))
            help_text = label.get_attribute("data-help") or ""
            check("help mentions suction stays on the script",
                  "Suction still follows the script" in help_text)
            # The row lives in the collapsed "Playback & Extended-O" details
            # of the loaded-script panel (hidden until a script loads): show
            # both, as after loading a script and opening the section.
            page.evaluate("""() => {
              document.querySelector('#pb-loaded').style.display = 'block';
              document.querySelector('#pb-audio-vibration').closest('details').open = true;
            }""")
            page.check("#pb-audio-vibration")
            page.wait_for_function(
                "() => (window.__calls || []).some(c => c[0]==='playback.audio_vibration' && c[1]===true)",
                timeout=3000)
            check("SetSetting playback.audio_vibration true",
                  page.evaluate("() => (window.__calls || []).some(c => c[0]==='playback.audio_vibration' && c[1]===true)"))
            browser.close()
    finally:
        shutdown()
        harness.unlink(missing_ok=True)
    return check.report()


if __name__ == "__main__":
    sys.exit(main())
