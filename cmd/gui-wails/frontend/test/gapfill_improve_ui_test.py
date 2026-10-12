"""GapFill Improve UI: HealRhythm + RepairSpans (opt-in).

Acceptance:
1. Rhythm bridge checkbox exists, default off, disabled when Heal is off.
2. Repair this span row exists; From/To disabled until Repair checked.
3. Manual Improve passes healRhythm + repairSpans when enabled.
4. Auto Improve after Generate keeps healRhythm false / empty repairSpans.
5. Status surfaces rhythmBridged / spansRepaired when present.

Run: python3 cmd/gui-wails/frontend/test/gapfill_improve_ui_test.py
"""

import pathlib
import sys

from playwright.sync_api import sync_playwright

from _harness import Checker, app_stub, serve

FRONTEND = pathlib.Path(__file__).resolve().parents[1]

PAGE = """<!doctype html><html><body><div id="root"></div>
<script type="module">
  import { initGenerator } from '/src/generator.js';
  window.__playLoads = [];
  initGenerator(document.querySelector('#root'), {
    loadScriptPath: (path, opts) => { window.__playLoads.push([path, opts || {}]); },
  });
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
                           "Reason: 'test', GoPath: true })",
        "SuggestProfile": "async () => ({ found: false })",
        "CheckAIRoiAvailable": "async () => false",
        "CheckAudioCheckAvailable": "async () => true",
        "ScriptExistsForVideo": "async () => false",
        "GenerateScript": (
            "async (opts) => { window.__calls.push(['GenerateScript', opts]); "
            "setTimeout(() => window.__triggerEvent && window.__triggerEvent("
            "'generate:done', { path: '/tmp/clip.funscript', qualityScore: 0.9, "
            "qualityPassed: true, pipeline: 'go', tracking: 'csrt', backend: 'csrt' }), 0); "
            "return {}; }"
        ),
        "ImproveGeneratedScript": (
            "async (req) => { window.__calls.push(['ImproveGeneratedScript', req]); "
            "const heal = !!(req.healTrackingGaps || req.HealTrackingGaps); "
            "const rhythm = !!(req.healRhythm || req.HealRhythm); "
            "const spans = req.repairSpans || req.RepairSpans || []; "
            "const nSpan = Array.isArray(spans) ? spans.length : 0; "
            "return { path: req.path, beforeCount: 10, afterCount: 16, trimmed: false, "
            "gapsFilled: 0, pointsAdded: 6, windowsHealed: heal ? 1 : 0, "
            "rhythmBridged: (rhythm || nSpan) ? 1 : 0, "
            "spansRepaired: nSpan, "
            "message: 'healed 1 tracking gap(s) · repaired 1 span(s) · 1 rhythm bridge(s)' }; }"
        ),
    }))
    harness = FRONTEND / "test" / "_gapfill_improve_ui_harness.html"
    harness.write_text(PAGE)
    try:
        with sync_playwright() as p:
            browser = p.chromium.launch()
            page = browser.new_page()
            page.on("dialog", lambda d: d.dismiss())
            page.on("pageerror", lambda e: print("   [pageerror]", e))
            page.goto(f"{base}/test/_gapfill_improve_ui_harness.html")
            page.wait_for_function("window.__ready === true")

            check("rhythm control present",
                  page.locator("#gen-improve-heal-rhythm").count() == 1)
            check("repair control present",
                  page.locator("#gen-improve-repair").count() == 1)
            check("Improve more details present",
                  page.locator("#gen-improve-more-details").count() == 1)
            check("rhythm default off",
                  not page.locator("#gen-improve-heal-rhythm").is_checked())
            check("repair default off",
                  not page.locator("#gen-improve-repair").is_checked())

            page.click("#gen-choose")
            page.wait_for_function(
                "document.querySelector('#gen-autoroi').disabled === false && "
                "!document.querySelector('#gen-roi-label').textContent.includes('No ')",
                timeout=5000)
            page.click("#gen-generate")
            page.wait_for_function(
                "document.querySelector('#gen-improve') && "
                "document.querySelector('#gen-improve').style.display !== 'none'",
                timeout=5000)
            page.wait_for_function(
                "() => (window.__calls || []).some(c => c[0] === 'ImproveGeneratedScript')",
                timeout=5000)
            auto = page.evaluate(
                "() => (window.__calls || []).filter(c => c[0] === 'ImproveGeneratedScript')")
            areq = auto[0][1] if auto else {}
            check("auto Improve healRhythm false",
                  areq.get("healRhythm") is False, str(areq))
            check("auto Improve repairSpans empty",
                  areq.get("repairSpans") in ([], None) or
                  (isinstance(areq.get("repairSpans"), list) and
                   len(areq.get("repairSpans")) == 0),
                  str(areq))

            # Open More details before toggling Rhythm / Repair.
            page.evaluate(
                "document.querySelector('#gen-improve-more-details').open = true")

            # Heal on → rhythm enabled; heal off → rhythm disabled + unchecked.
            check("rhythm enabled while heal on",
                  page.locator("#gen-improve-heal-rhythm").is_enabled())
            page.uncheck("#gen-improve-heal")
            page.wait_for_timeout(40)
            check("rhythm disabled when heal off",
                  page.locator("#gen-improve-heal-rhythm").is_disabled())
            check("rhythm unchecked when heal off",
                  not page.locator("#gen-improve-heal-rhythm").is_checked())
            page.check("#gen-improve-heal")
            page.wait_for_timeout(40)
            page.check("#gen-improve-heal-rhythm")

            check("repair From disabled until repair on",
                  page.locator("#gen-improve-repair-start").is_disabled())
            page.check("#gen-improve-repair")
            page.wait_for_timeout(40)
            check("repair From enabled",
                  page.locator("#gen-improve-repair-start").is_enabled())
            page.fill("#gen-improve-repair-start", "2")
            page.fill("#gen-improve-repair-end", "5")

            page.evaluate(
                "window.__calls = (window.__calls || []).filter("
                "c => c[0] !== 'ImproveGeneratedScript')")
            page.click("#gen-improve-apply")
            page.wait_for_function(
                "() => (window.__calls || []).some(c => c[0] === 'ImproveGeneratedScript')",
                timeout=5000)
            calls = page.evaluate(
                "() => (window.__calls || []).filter(c => c[0] === 'ImproveGeneratedScript')")
            req = calls[0][1] if calls else {}
            check("manual healRhythm true",
                  req.get("healRhythm") is True, str(req))
            spans = req.get("repairSpans") or []
            check("manual repairSpans one window",
                  isinstance(spans, list) and len(spans) == 1, str(req))
            if spans:
                s0 = spans[0]
                start = s0.get("start_ms", s0.get("StartMs"))
                end = s0.get("end_ms", s0.get("EndMs"))
                check("repair span 2000–5000 ms",
                      start == 2000 and end == 5000, str(spans))

            page.wait_for_function(
                "() => (document.querySelector('#gen-status') || {}).textContent "
                "&& document.querySelector('#gen-status').textContent.includes('Improved:')",
                timeout=5000)
            status = page.locator("#gen-status").inner_text()
            check("status mentions repaired", "repaired 1" in status, status)
            check("status mentions rhythm", "rhythm" in status, status)

            browser.close()
    finally:
        shutdown()
        harness.unlink(missing_ok=True)
    return check.report()


if __name__ == "__main__":
    sys.exit(main())
