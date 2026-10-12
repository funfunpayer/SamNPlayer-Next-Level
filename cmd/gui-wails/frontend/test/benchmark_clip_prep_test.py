"""Bench tab Clip-Prep In/Out cutter (GUI export + script fallback).

Run: python3 cmd/gui-wails/frontend/test/benchmark_clip_prep_test.py
"""

import pathlib
import sys

from playwright.sync_api import sync_playwright

from _harness import Checker, app_stub, serve

PAGE = """<!doctype html><html><body><div id="root"></div>
<script type="module">
  import { initBenchmark } from '/src/benchmark.js';
  initBenchmark(document.querySelector('#root'));
  window.__ready = true;
</script></body></html>"""


def main():
    check = Checker()
    base, shutdown = serve(app_stub({
        "GetSettings": "async () => ({ benchmarkManifestPath: '' })",
        "GetBenchmarkHistory": "async () => []",
        "PickBenchmarkManifest": "async () => ''",
        "PickFunscriptFile": "async () => ''",
        "PickVideoFile": "async () => '/tmp/long.mp4'",
        "SuggestBenchmarkPairBesideVideo": "async () => ({})",
        "ScoreScriptPair": "async () => ({})",
        "AppendBenchmarkPairLabel": "async () => ''",
        "RunGoldenClipBenchmark": "async () => {}",
        "SetSetting": "async () => {}",
        "ParseBenchmarkClipTime": "async (v) => {"
        "  if (String(v).includes(':')) {"
        "    const p = String(v).split(':').map(Number);"
        "    if (p.length === 2) return p[0]*60+p[1];"
        "    return p[0]*3600+p[1]*60+p[2];"
        "  }"
        "  return Number(v);"
        "}",
        "SuggestBenchmarkClipOutput":
            "async (src, start, end) => '/tmp/long_01-20-02-05.mp4'",
        "PickBenchmarkClipOutput": "async () => '/tmp/out_clip.mp4'",
        "ExportBenchmarkClip":
            "async (req) => ({ output: req.output, startSec: 80, endSec: 125,"
            " durationSec: 45, maxWidth: 1280 })",
    }))
    harness = pathlib.Path(__file__).resolve().parent / "_benchmark_clip_prep_harness.html"
    harness.write_text(PAGE)

    with sync_playwright() as p:
        browser = p.chromium.launch()
        page = browser.new_page()
        page.goto(f"{base}/test/_benchmark_clip_prep_harness.html")
        page.wait_for_function("window.__ready === true")

        check("Clip-Prep details present",
              page.locator("#bm-clip-prep").count() == 1)
        check("In field present",
              page.locator("#bm-clip-in").count() == 1)
        check("Out field present",
              page.locator("#bm-clip-out-t").count() == 1)
        check("Export button present",
              page.locator("#bm-clip-export").count() == 1)
        check("Export disabled until filled",
              page.locator("#bm-clip-export").is_disabled())
        check("Preset defaults to 720p",
              page.locator("#bm-clip-preset").input_value() == "720p")
        check("Copy command button present",
              page.locator("#bm-clip-prep-copy").count() == 1)
        check("Fallback command mentions cut_clip",
              "cut_clip" in (page.locator("#bm-clip-prep-cmd").text_content() or ""))
        check("Compare scripts heading still present",
              page.locator("text=Compare scripts").count() >= 1)
        check("Suggest beside video button present",
              page.locator("#bm-suggest-pair").count() == 1)

        page.click("#bm-clip-pick-src")
        page.wait_for_timeout(50)
        check("Source filled from browse",
              page.locator("#bm-clip-src").input_value() == "/tmp/long.mp4")
        check("Suggested output filled",
              "long_" in (page.locator("#bm-clip-dst").input_value() or ""))

        page.fill("#bm-clip-in", "01:20")
        page.fill("#bm-clip-out-t", "02:05")
        page.wait_for_timeout(40)
        check("Export enabled with In/Out",
              not page.locator("#bm-clip-export").is_disabled())

        page.click("#bm-clip-export")
        page.wait_for_timeout(80)
        status = page.locator("#bm-clip-export-status").inner_text() or ""
        check("Export status shows Done",
              "Done" in status)
        check("Use in Compare enabled after export",
              not page.locator("#bm-clip-use-compare").is_disabled())

        page.click("#bm-clip-use-compare")
        page.wait_for_timeout(30)
        check("Compare video filled from export",
              "long_" in (page.locator("#bm-video").input_value() or "")
              or page.locator("#bm-video").input_value().endswith(".mp4"))

        page.locator("#bm-clip-prep-scripts summary").click()
        page.wait_for_timeout(40)
        page.click("#bm-clip-prep-batch")
        page.wait_for_timeout(30)
        check("Batch command shows marks.json",
              "marks.json" in (page.locator("#bm-clip-prep-cmd").text_content() or ""))

        browser.close()
    shutdown()
    harness.unlink(missing_ok=True)
    return check.report()


if __name__ == "__main__":
    raise SystemExit(main())
