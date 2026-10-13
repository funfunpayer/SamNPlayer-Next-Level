"""Manual Find tip Cancel unlocks Find tip (not only tip-find-then-Create).

Run: python3 cmd/gui-wails/frontend/test/generator_manual_find_cancel_test.py
"""

import pathlib
import sys

from playwright.sync_api import sync_playwright

from _harness import Checker, app_stub, serve

FRONTEND = pathlib.Path(__file__).resolve().parents[1]

PAGE = """<!doctype html><html><body><div id="root"></div>
<script type="module">
  import { initGenerator } from '/src/generator.js';
  initGenerator(document.querySelector('#root'), { loadScriptPath: () => {} });
  window.__ready = true;
</script></body></html>"""

TINY_PNG_B64 = ("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNk"
                "+A8AAQUBAScY42YAAAAASUVORK5CYII=")


def main():
    check = Checker()
    base, shutdown = serve(app_stub({
        "PickVideoFile": "async () => '/tmp/clip.mp4'",
        "LoadFirstFrame": f"async () => ({{ width: 640, height: 360, "
                          f"pngBase64: '{TINY_PNG_B64}' }})",
        "LoadFrameAt": f"async () => ({{ width: 640, height: 360, "
                       f"pngBase64: '{TINY_PNG_B64}' }})",
        "SuggestPipeline": "async () => ({ Backend: 'csrt', Profile: 'standard', "
                           "Reason: 'everyday', GoPath: true })",
        "SuggestProfile": "async () => ({ found: false })",
        "CheckAIRoiAvailable": "async () => false",
        "CheckAudioCheckAvailable": "async () => true",
        "ScriptExistsForVideo": "async () => false",
        # First call (choose auto-find) completes; later Find tip hangs.
        "AutoDetectROI": (
            "async (path) => { "
            "window.__autoRoiN = (window.__autoRoiN || 0) + 1; "
            "window.__calls.push(['AutoDetectROI', path, window.__autoRoiN]); "
            "if (window.__autoRoiN === 1) { "
            "  setTimeout(() => window.__triggerEvent && window.__triggerEvent("
            "    'generate:autoroi', {x:40,y:40,w:120,h:120,engine:'auto',"
            "    videoPath: path, seq: 1}), 0); "
            "} "
            "}"
        ),
        "CancelROIDetection": (
            "async () => { window.__calls.push(['CancelROIDetection']); }"
        ),
        "CancelGenerate": (
            "async () => { window.__calls.push(['CancelGenerate']); }"
        ),
    }))
    harness = FRONTEND / "test" / "_generator_manual_find_cancel_harness.html"
    harness.write_text(PAGE)
    try:
        with sync_playwright() as p:
            browser = p.chromium.launch()
            page = browser.new_page()
            page.on("dialog", lambda d: d.dismiss())
            page.on("pageerror", lambda e: print("   [pageerror]", e))
            page.goto(f"{base}/test/_generator_manual_find_cancel_harness.html")
            page.wait_for_function("window.__ready === true")

            page.click("#gen-choose")
            page.wait_for_function(
                "document.querySelector('#gen-autoroi') && "
                "!document.querySelector('#gen-autoroi').disabled",
                timeout=5000)

            page.evaluate("() => { window.__calls = []; }")
            page.click("#gen-autoroi")
            page.wait_for_function(
                "() => (window.__calls || []).some(c => c[0] === 'AutoDetectROI')",
                timeout=3000)
            page.wait_for_function(
                "() => document.querySelector('#gen-cancel') "
                "&& !document.querySelector('#gen-cancel').disabled",
                timeout=3000)
            check("Cancel enabled during manual Find tip",
                  page.locator("#gen-cancel").is_disabled() is False)
            check("Find tip disabled while searching",
                  page.locator("#gen-autoroi").is_disabled() is True)

            page.click("#gen-cancel")
            page.wait_for_timeout(80)
            calls = page.evaluate("() => window.__calls || []")
            check("CancelROIDetection called",
                  any(c[0] == "CancelROIDetection" for c in calls), str(calls))
            check("Find tip re-enabled after Cancel",
                  page.locator("#gen-autoroi").is_disabled() is False)
            check("Cancel disabled again",
                  page.locator("#gen-cancel").is_disabled() is True)
            status = page.locator("#gen-status").inner_text().lower()
            check("Status says tip find canceled",
                  "cancel" in status, status)

            # Seek during classic Find must CancelROIDetection (not only AI).
            page.evaluate("() => { window.__calls = []; window.__autoRoiN = 1; }")
            page.click("#gen-autoroi")
            page.wait_for_function(
                "() => (window.__calls || []).some(c => c[0] === 'AutoDetectROI')",
                timeout=3000)
            page.fill("#gen-seek", "2")
            page.click("#gen-seek-btn")
            page.wait_for_timeout(120)
            calls2 = page.evaluate("() => window.__calls || []")
            check("Seek cancels classic Find via CancelROIDetection",
                  any(c[0] == "CancelROIDetection" for c in calls2), str(calls2))
            check("Find tip re-enabled after seek cancel",
                  page.locator("#gen-autoroi").is_disabled() is False)
            browser.close()
    finally:
        shutdown()
        harness.unlink(missing_ok=True)
    return check.report()


if __name__ == "__main__":
    sys.exit(main())
