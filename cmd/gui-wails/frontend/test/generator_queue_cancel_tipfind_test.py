"""Create queue Cancel during tip-find tears down the queue.

Tip-find-then-Create never emits generate:done until tracking starts.
Cancel in that phase must unlock Create AND clear queueRunning so
Clear/Run are not stuck forever.

Run: python3 cmd/gui-wails/frontend/test/generator_queue_cancel_tipfind_test.py
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
        # Hang forever so queue tip-find never reaches GenerateScript.
        "AutoDetectROI": (
            "async (path) => { window.__calls.push(['AutoDetectROI', path]); }"
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
    harness = FRONTEND / "test" / "_generator_queue_cancel_tipfind_harness.html"
    harness.write_text(PAGE)
    try:
        with sync_playwright() as p:
            browser = p.chromium.launch()
            page = browser.new_page()
            page.on("dialog", lambda d: d.dismiss())
            page.on("pageerror", lambda e: print("   [pageerror]", e))
            page.goto(f"{base}/test/_generator_queue_cancel_tipfind_harness.html")
            page.wait_for_function("window.__ready === true")

            page.evaluate("""window.dispatchEvent(new CustomEvent('drop:video', {
              detail: {
                path: '/v/a.mp4',
                paths: ['/v/a.mp4', '/v/b.mp4'],
                extraCount: 1,
              },
            }))""")
            page.wait_for_function(
                "document.querySelector('#gen-queue') && "
                "!document.querySelector('#gen-queue').hidden",
                timeout=5000)
            # Choose/drop auto-find hangs — Create queue still enabled once listed.
            page.wait_for_selector("#gen-queue-run:not([disabled])", timeout=5000)

            page.evaluate("() => { window.__calls = []; }")
            page.click("#gen-queue-run")
            page.wait_for_function(
                "() => (window.__calls || []).some(c => c[0] === 'AutoDetectROI')",
                timeout=5000)
            page.wait_for_function(
                "() => document.querySelector('#gen-cancel') "
                "&& !document.querySelector('#gen-cancel').disabled",
                timeout=3000)
            check("Clear disabled while queue tip-find runs",
                  page.locator("#gen-queue-clear").is_disabled() is True)

            page.click("#gen-cancel")
            page.wait_for_timeout(100)
            calls = page.evaluate("() => window.__calls || []")
            check("CancelROIDetection called",
                  any(c[0] == "CancelROIDetection" for c in calls), str(calls))
            check("CancelGenerate called",
                  any(c[0] == "CancelGenerate" for c in calls), str(calls))
            check("Cancel re-enables Create",
                  page.locator("#gen-generate").is_disabled() is False)
            check("Cancel re-enables queue Clear",
                  page.locator("#gen-queue-clear").is_disabled() is False)
            status = page.locator("#gen-status").inner_text().lower()
            check("Status says queue canceled",
                  "queue" in status and "cancel" in status, status)
            check("GenerateScript not started",
                  not any(c[0] == "GenerateScript" for c in calls), str(calls))
            browser.close()
    finally:
        shutdown()
        harness.unlink(missing_ok=True)
    return check.report()


if __name__ == "__main__":
    sys.exit(main())
