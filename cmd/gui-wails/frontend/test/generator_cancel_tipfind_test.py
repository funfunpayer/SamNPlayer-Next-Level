"""Create Cancel during tip-find-then-Create unlocks the UI.

When Create starts without a tip, Generate auto-finds ROI then continues.
Cancel in that phase must call CancelROIDetection + clear generating so
Create is not stuck locked waiting for generate:done.

Run: python3 cmd/gui-wails/frontend/test/generator_cancel_tipfind_test.py
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
        # Never complete — Choose + Create tip-find stay in flight until Cancel.
        "AutoDetectROI": (
            "async (path) => { "
            "window.__calls.push(['AutoDetectROI', path]); }"
        ),
        "CancelROIDetection": (
            "async () => { window.__calls.push(['CancelROIDetection']); }"
        ),
        "CancelGenerate": (
            "async () => { window.__calls.push(['CancelGenerate']); }"
        ),
        "GenerateScript": (
            "async (opts) => { window.__calls.push(['GenerateScript', opts]); }"
        ),
    }))
    harness = FRONTEND / "test" / "_generator_cancel_tipfind_harness.html"
    harness.write_text(PAGE)
    try:
        with sync_playwright() as p:
            browser = p.chromium.launch()
            page = browser.new_page()
            page.on("dialog", lambda d: d.dismiss())
            page.on("pageerror", lambda e: print("   [pageerror]", e))
            page.goto(f"{base}/test/_generator_cancel_tipfind_harness.html")
            page.wait_for_function("window.__ready === true")

            page.click("#gen-choose")
            # Choose starts auto-find (hangs); Create stays disabled until tip —
            # Everyday still allows Create with no tip once we unlock the button
            # and click (roi still null → tip-find-then-Create).
            page.wait_for_function(
                "() => (window.__calls || []).some(c => c[0] === 'AutoDetectROI')",
                timeout=5000)
            page.wait_for_selector("#gen-generate", timeout=3000)

            page.evaluate("() => { window.__calls = []; }")
            # Force-enable Create (loadVideo left it disabled waiting for tip).
            page.evaluate("""() => {
              const btn = document.querySelector('#gen-generate');
              btn.disabled = false;
              btn.click();
            }""")
            page.wait_for_function(
                "() => (window.__calls || []).some(c => c[0] === 'AutoDetectROI')",
                timeout=3000)
            page.wait_for_function(
                "() => document.querySelector('#gen-cancel') "
                "&& !document.querySelector('#gen-cancel').disabled "
                "&& document.querySelector('#gen-generate').disabled",
                timeout=3000)
            status = page.locator("#gen-status").inner_text()
            check("Status shows tip-find-then-Create",
                  "finding" in status.lower() or "no tip" in status.lower(),
                  status)

            page.click("#gen-cancel")
            page.wait_for_timeout(80)
            calls = page.evaluate("() => window.__calls || []")
            check("CancelROIDetection called",
                  any(c[0] == "CancelROIDetection" for c in calls), str(calls))
            check("CancelGenerate called",
                  any(c[0] == "CancelGenerate" for c in calls), str(calls))
            check("Cancel re-enables Create",
                  page.locator("#gen-generate").is_disabled() is False)
            check("Cancel re-enables Find tip",
                  page.locator("#gen-autoroi").is_disabled() is False)
            check("Cancel disables Cancel button again",
                  page.locator("#gen-cancel").is_disabled() is True)
            check("GenerateScript not started after Cancel",
                  not any(c[0] == "GenerateScript" for c in calls), str(calls))
            browser.close()
    finally:
        shutdown()
        harness.unlink(missing_ok=True)
    return check.report()


if __name__ == "__main__":
    sys.exit(main())
