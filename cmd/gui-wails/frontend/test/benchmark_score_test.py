"""Bench tab: Score vs FunGen reference (Rel38 #350 smoke).

Run: python3 cmd/gui-wails/frontend/test/benchmark_score_test.py
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
        "PickVideoFile": "async () => '/tmp/clip.mp4'",
        "SuggestBenchmarkPairBesideVideo": "async () => ({})",
        "PickFunscriptFile": (
            "async () => { "
            "window.__pickN = (window.__pickN || 0) + 1; "
            "return window.__pickN === 1 ? '/tmp/ref.funscript' : '/tmp/cand.funscript'; }"
        ),
        "ScoreScriptPair": (
            "async (ref, cand, video) => { "
            "window.__calls.push(['ScoreScriptPair', {ref, cand, video}]); "
            "return { kind: 'pair_score', label: 'good', passed: true, "
            "detail: 'gut: Motion Fidelity ok', "
            "fidelity: { r: 0.95, diagnosis: { verdict: 'ok' } }, "
            "quality: { score: 0.9, passed: true } }; }"
        ),
        "AppendBenchmarkPairLabel": (
            "async (score) => { "
            "window.__calls.push(['AppendBenchmarkPairLabel', score]); "
            "return '/tmp/benchmark_pair_labels.jsonl'; }"
        ),
    }))
    harness = FRONTEND / "test" / "_benchmark_score_harness.html"
    harness.write_text(PAGE)

    with sync_playwright() as p:
        browser = p.chromium.launch()
        page = browser.new_page()
        page.on("dialog", lambda d: d.dismiss())
        page.on("pageerror", lambda e: print("   [pageerror]", e))
        page.goto(f"{base}/test/_benchmark_score_harness.html")
        page.wait_for_function("window.__ready === true")

        check("Score disabled without scripts",
              page.locator("#bm-score").is_disabled())
        check("Save label disabled initially",
              page.locator("#bm-save-label").is_disabled())

        page.click("#bm-pick-ref")
        page.wait_for_function(
            "document.querySelector('#bm-ref').value.includes('ref.funscript')",
            timeout=3000)
        page.click("#bm-pick-cand")
        page.wait_for_function(
            "document.querySelector('#bm-cand').value.includes('cand.funscript')",
            timeout=3000)
        check("Score enabled with ref+candidate",
              not page.locator("#bm-score").is_disabled())

        page.click("#bm-score")
        page.wait_for_function(
            "() => (window.__calls || []).some(c => c[0] === 'ScoreScriptPair')",
            timeout=5000)
        calls = page.evaluate(
            "() => (window.__calls || []).filter(c => c[0] === 'ScoreScriptPair')")
        check("ScoreScriptPair called", len(calls) == 1, str(calls))
        req = calls[0][1] if calls else {}
        check("Reference path passed", req.get("ref") == "/tmp/ref.funscript", str(req))
        check("Candidate path passed", req.get("cand") == "/tmp/cand.funscript", str(req))

        page.wait_for_function(
            "document.querySelector('#bm-pair-status').textContent.includes('GUT')",
            timeout=5000)
        status = page.locator("#bm-pair-status").inner_text()
        check("Status shows GUT label", "GUT" in status, status)
        result = page.locator("#bm-pair-result").inner_text()
        check("Result shows GUT", "GUT" in result, result)
        check("Save label enabled after score",
              not page.locator("#bm-save-label").is_disabled())

        page.click("#bm-save-label")
        page.wait_for_function(
            "() => (window.__calls || []).some(c => c[0] === 'AppendBenchmarkPairLabel')",
            timeout=5000)
        check("Label saved path shown",
              "benchmark_pair_labels" in page.locator("#bm-pair-status").inner_text())

        browser.close()

    shutdown()
    harness.unlink(missing_ok=True)
    return check.report()


if __name__ == "__main__":
    sys.exit(main())
