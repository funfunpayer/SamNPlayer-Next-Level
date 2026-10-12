"""Golden-clip benchmark Cancel calls CancelGoldenClipBenchmark.

Run: python3 cmd/gui-wails/frontend/test/benchmark_cancel_test.py
"""

import pathlib
import sys

from playwright.sync_api import sync_playwright

from _harness import Checker, app_stub, serve

FRONTEND = pathlib.Path(__file__).resolve().parents[1]

PAGE = """<!doctype html><html><body><div id="root"></div>
<script type="module">
  import { initBenchmark } from '/src/benchmark.js';
  initBenchmark(document.querySelector('#root'));
  window.__ready = true;
</script></body></html>"""


def main():
    check = Checker()
    base, shutdown = serve(app_stub({
        "GetSettings": "async () => ({})",
        "GetBenchmarkHistory": "async () => []",
        "RunGoldenClipBenchmark":
            "async (path) => { window.__benchStarted = path; "
            "return new Promise(() => {}); }",
        "CancelGoldenClipBenchmark":
            "async () => { window.__cancelCalls = (window.__cancelCalls || 0) + 1; return true; }",
        "PickBenchmarkManifest": "async () => '/tmp/manifest.json'",
        "PickVideoFile": "async () => ''",
        "PickFunscriptFile": "async () => ''",
        "ScoreScriptPair": "async () => ({})",
        "AppendBenchmarkPairLabel": "async () => ''",
        "SuggestBenchmarkPairBesideVideo": "async () => ({})",
        "ExportBenchmarkClip": "async () => ({})",
        "PickBenchmarkClipOutput": "async () => ''",
        "SuggestBenchmarkClipOutput": "async () => ''",
        "ParseBenchmarkClipTime": "async () => 0",
        "SetSetting": "async () => {}",
    }))

    harness = FRONTEND / "test" / "_benchmark_cancel_harness.html"
    harness.write_text(PAGE)

    with sync_playwright() as p:
        browser = p.chromium.launch()
        page = browser.new_page()
        page.on("pageerror", lambda e: print("   [pageerror]", e))
        page.goto(f"{base}/test/_benchmark_cancel_harness.html")
        page.wait_for_function("window.__ready === true")

        check("cancel button present", page.locator("#bm-cancel").count() == 1)
        check("cancel hidden initially", page.is_hidden("#bm-cancel"))
        check("progress wrap hidden initially",
              page.locator("#bm-progress-wrap").evaluate("el => el.style.display === 'none'"))

        page.click("#bm-pick")
        page.wait_for_function(
            "document.querySelector('#bm-manifest').value.includes('manifest.json')",
            timeout=3000)
        check("run enabled with manifest", not page.locator("#bm-run").is_disabled())

        page.click("#bm-run")
        page.wait_for_function("window.__benchStarted === '/tmp/manifest.json'", timeout=5000)
        check("cancel visible while running", page.is_visible("#bm-cancel"))
        check("progress wrap shown while running",
              page.locator("#bm-progress-wrap").evaluate("el => el.style.display !== 'none'"))

        page.click("#bm-cancel")
        page.wait_for_function("() => (window.__cancelCalls || 0) >= 1", timeout=3000)
        check("CancelGoldenClipBenchmark called",
              page.evaluate("() => (window.__cancelCalls || 0) >= 1"))

        # Simulate backend done with cancel error → warn path, Cancel hides.
        page.evaluate(
            "() => window.__triggerEvent('benchmark:done', "
            "{ error: 'context canceled' })")
        page.wait_for_function(
            "() => document.querySelector('#bm-cancel').hidden === true",
            timeout=3000)
        check("cancel hidden after done", page.is_hidden("#bm-cancel"))
        status = page.locator("#bm-status").inner_text()
        check("cancel treated as warn status", "cancelled" in status.lower()
              or "canceled" in status.lower() or "abgebrochen" in status.lower())

        browser.close()

    shutdown()
    harness.unlink(missing_ok=True)
    return check.report()


if __name__ == "__main__":
    sys.exit(main())
