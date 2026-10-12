"""Bench tab: Suggest scripts beside short clip (Clip-Prep → Compare).

Run: python3 cmd/gui-wails/frontend/test/benchmark_suggest_pair_test.py
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
        "PickVideoFile": "async () => '/tmp/clips/hub_easy.mp4'",
        "PickFunscriptFile": "async () => ''",
        "SuggestBenchmarkPairBesideVideo": (
            "async (video) => { "
            "window.__calls.push(['SuggestBenchmarkPairBesideVideo', video]); "
            "return { video, "
            "reference: '/tmp/clips/hub_easy.funscript', "
            "candidate: '/tmp/clips/hub_easy__hub.funscript', "
            "note: 'Paired FunGen/ref + Everyday (__hub) beside the clip.' }; }"
        ),
        "ScoreScriptPair": "async () => ({})",
        "AppendBenchmarkPairLabel": "async () => ''",
    }))
    harness = FRONTEND / "test" / "_benchmark_suggest_pair_harness.html"
    harness.write_text(PAGE)

    with sync_playwright() as p:
        browser = p.chromium.launch()
        page = browser.new_page()
        page.on("pageerror", lambda e: print("   [pageerror]", e))
        page.goto(f"{base}/test/_benchmark_suggest_pair_harness.html")
        page.wait_for_function("window.__ready === true")

        check("Suggest disabled without video",
              page.locator("#bm-suggest-pair").is_disabled())

        page.click("#bm-pick-video")
        page.wait_for_function(
            "document.querySelector('#bm-video').value.includes('hub_easy.mp4')",
            timeout=3000)
        check("Suggest enabled with video",
              not page.locator("#bm-suggest-pair").is_disabled())

        page.click("#bm-suggest-pair")
        page.wait_for_function(
            "() => (window.__calls || []).some(c => c[0] === 'SuggestBenchmarkPairBesideVideo')",
            timeout=5000)
        calls = page.evaluate(
            "() => (window.__calls || []).filter(c => c[0] === 'SuggestBenchmarkPairBesideVideo')")
        check("SuggestBenchmarkPairBesideVideo called", len(calls) == 1, str(calls))
        check("Reference filled",
              "hub_easy.funscript" in page.locator("#bm-ref").input_value()
              and "__hub" not in page.locator("#bm-ref").input_value())
        check("Candidate filled as __hub",
              "hub_easy__hub.funscript" in page.locator("#bm-cand").input_value())
        check("Score enabled after suggest",
              not page.locator("#bm-score").is_disabled())
        check("Status mentions paired",
              "Paired" in (page.locator("#bm-suggest-status").inner_text() or ""))

        browser.close()
    shutdown()
    harness.unlink(missing_ok=True)
    return check.report()


if __name__ == "__main__":
    raise SystemExit(main())
