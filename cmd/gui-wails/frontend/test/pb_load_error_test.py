"""Play: LoadFunscript failure surfaces in the log (no silent unhandled rejection).

Run: python3 cmd/gui-wails/frontend/test/pb_load_error_test.py
"""

import pathlib
import sys

from playwright.sync_api import sync_playwright

from _harness import Checker, app_stub, serve

FRONTEND = pathlib.Path(__file__).resolve().parent.parent

PAGE = """<!doctype html><html><body><div id="root"></div>
<script type="module">
  import { initPlayback } from '/src/playback.js';
  window.__pb = initPlayback(document.querySelector('#root'));
  window.__ready = true;
</script></body></html>"""


def main():
    check = Checker()
    base, shutdown = serve(app_stub({
        "PickFunscriptFile": "async () => '/tmp/missing.funscript'",
        "LoadFunscript": "async () => { throw new Error('file not found'); }",
        "GetSettings": (
            "async () => ({ playbackMock: true, playbackSync: 'video', "
            "playbackTickMs: 50, playbackMaxSpeed: 0.6, playbackSmoothing: 0, "
            "playbackSoftStartMs: 0, playbackEOEnabled: false, playbackEOMin: 0.1, "
            "playbackEOHoldS: 10, playbackEORestoreMs: 500, "
            "playbackVideoPlayAutostart: false, playbackTrajectoryOverlay: false })"
        ),
        "SetSetting": "async () => {}",
    }))
    harness = FRONTEND / "test" / "_pb_load_error_harness.html"
    harness.write_text(PAGE)

    with sync_playwright() as p:
        browser = p.chromium.launch()
        page = browser.new_page()
        page.on("pageerror", lambda e: print("   [pageerror]", e))
        page.goto(f"{base}/test/_pb_load_error_harness.html")
        page.wait_for_function("window.__ready === true")

        page.click("#pb-choose")
        page.wait_for_function(
            "() => (document.querySelector('#pb-log')?.textContent || '')"
            ".toLowerCase().includes('could not load')",
            timeout=4000)
        log = page.locator("#pb-log").inner_text().lower()
        check("load error shown in pb-log", "could not load" in log)
        check("script path still empty/default",
              "no emotion script" in page.locator("#pb-script-path").inner_text().lower()
              or page.locator("#pb-script-path").inner_text() == "No Emotion Script selected")

        browser.close()

    shutdown()
    harness.unlink(missing_ok=True)
    return check.report()


if __name__ == "__main__":
    sys.exit(main())
