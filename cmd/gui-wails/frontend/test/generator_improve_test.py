"""Review improve panel: trim / fill gaps / audio toggles after generate.

Run: python3 cmd/gui-wails/frontend/test/generator_improve_test.py
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
            "return { path: req.path, beforeCount: 10, afterCount: 14, trimmed: true, "
            "gapsFilled: 1, pointsAdded: 4, windowsHealed: heal ? 2 : 0, "
            "speechHoldMs: 1500, "
            "audioSegments: ["
            "  { label: 'holding', start_ms: 0, end_ms: 1500, speech_hold: true },"
            "  { label: 'gentle', start_ms: 1500, end_ms: 4000 },"
            "  { label: 'intense', start_ms: 4000, end_ms: 7000 },"
            "  { label: 'climax', start_ms: 7000, end_ms: 9000 }"
            "], "
            "message: heal "
            "? 'trimmed start/end · healed 2 tracking gap(s) · filled 1 gap(s) (+4 points total)' "
            ": 'trimmed start/end · filled 1 gap(s)' }; }"
        ),
        "GetScriptChapterMarks": "async () => []",
        "SaveScriptChapterMarks": (
            "async (chs) => { window.__calls.push(['SaveScriptChapterMarks', chs]); }"
        ),
    }))
    harness = FRONTEND / "test" / "_generator_improve_harness.html"
    harness.write_text(PAGE)

    with sync_playwright() as p:
        browser = p.chromium.launch()
        page = browser.new_page()
        page.on("dialog", lambda d: d.dismiss())
        page.on("pageerror", lambda e: print("   [pageerror]", e))
        page.goto(f"{base}/test/_generator_improve_harness.html")
        page.wait_for_function("window.__ready === true")

        # Improve panel hidden until generate done.
        check("Improve panel hidden at start",
              page.locator("#gen-improve").evaluate("e => e.style.display === 'none'"))

        # AI second opinion / Advanced audio row removed from visible Advanced.
        check("AI quality not a visible Advanced checkbox",
              page.locator("label[for='gen-ai-quality']").count() == 0)
        check("Audio check not a visible Advanced checkbox",
              page.locator("label[for='gen-audio-check']").count() == 0)
        check("Flow downscale control gone from GUI",
              page.locator("#gen-flow-downscale").count() == 0)
        check("Scene memory collapsed under power-user",
              page.locator("#gen-power-user").count() == 1
              and page.locator("#gen-label-scene").count() == 1)

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

        # Auto fill-gaps runs on generate:done before Play load.
        page.wait_for_function(
            "() => (window.__calls || []).some(c => c[0] === 'ImproveGeneratedScript')",
            timeout=5000)
        auto = page.evaluate(
            "() => (window.__calls || []).filter(c => c[0] === 'ImproveGeneratedScript')")
        check("Auto fill-gaps after Generate",
              auto and auto[0][1].get("fillGaps") is True, str(auto))
        page.wait_for_function(
            "() => (document.querySelector('#gen-improve-status') || {}).textContent "
            "&& document.querySelector('#gen-improve-status').textContent.includes('After generate')",
            timeout=5000)
        check("Improve status mentions After generate",
              "After generate" in page.locator("#gen-improve-status").inner_text())

        check("Improve panel shown after generate",
              page.locator("#gen-improve").evaluate("e => e.style.display !== 'none'"))
        check("Fill gaps on by default",
              page.locator("#gen-improve-fill").is_checked())
        check("Heal tracking gaps on by default",
              page.locator("#gen-improve-heal").is_checked())
        check("Audio check on by default when ffmpeg ok",
              page.locator("#gen-improve-audio").is_checked())
        check("Auto Improve passes healTrackingGaps",
              auto and auto[0][1].get("healTrackingGaps") is True, str(auto))

        page.fill("#gen-improve-start", "1")
        page.fill("#gen-improve-end", "40")
        page.evaluate("window.__calls = (window.__calls || []).filter(c => c[0] !== 'ImproveGeneratedScript')")
        page.click("#gen-improve-apply")
        page.wait_for_function(
            "() => (window.__calls || []).some(c => c[0] === 'ImproveGeneratedScript')",
            timeout=5000)
        calls = page.evaluate(
            "() => (window.__calls || []).filter(c => c[0] === 'ImproveGeneratedScript')")
        check("ImproveGeneratedScript called from button", len(calls) == 1, str(calls))
        req = calls[0][1] if calls else {}
        check("Trim start passed", req.get("startSec") == 1, str(req))
        check("Trim end passed", req.get("endSec") == 40, str(req))
        check("Fill gaps passed", req.get("fillGaps") is True, str(req))
        check("Heal tracking gaps passed", req.get("healTrackingGaps") is True, str(req))
        check("Audio check passed", req.get("audioCheck") is True, str(req))

        page.wait_for_function(
            "() => (document.querySelector('#gen-status') || {}).textContent "
            "&& document.querySelector('#gen-status').textContent.includes('Improved:')",
            timeout=5000)
        status = page.locator("#gen-status").inner_text()
        check("Manual Improve status mentions healed", "healed 2" in status, status)
        check("Manual Improve status does not say fill for heal points",
              "+4 pts" in status and "fill)" not in status and " fill" not in status, status)

        # Speech-Hold / Feel strip after Improve with audioSegments.
        page.wait_for_function(
            "() => { const p = document.querySelector('#gen-audio-segments'); "
            "return p && !p.hidden; }",
            timeout=5000)
        check("Audio segments panel visible after Improve",
              page.locator("#gen-audio-segments").evaluate("e => !e.hidden"))
        check("Create Feel filters details present",
              page.locator("#gen-feel-filters-details").count() == 1)
        page.evaluate(
            "document.querySelector('#gen-feel-filters-details').open = true")
        segs = page.locator(".gen-audio-seg")
        check("Four segment blocks rendered", segs.count() == 4, str(segs.count()))
        check("Hold filter default on", page.locator("#gen-seg-f-holding").is_checked())
        page.uncheck("#gen-seg-f-gentle")
        page.uncheck("#gen-seg-f-intense")
        page.uncheck("#gen-seg-f-climax")
        check("Filter leaves Hold only", page.locator(".gen-audio-seg").count() == 1,
              str(page.locator(".gen-audio-seg").count()))
        page.check("#gen-seg-f-gentle")
        page.check("#gen-seg-f-intense")
        page.check("#gen-seg-f-climax")
        page.evaluate("window.__calls = (window.__calls || []).filter(c => c[0] !== 'SaveScriptChapterMarks')")
        page.click("#gen-audio-seg-chapters")
        page.wait_for_function(
            "() => (window.__calls || []).some(c => c[0] === 'SaveScriptChapterMarks')",
            timeout=5000)
        chCalls = page.evaluate(
            "() => (window.__calls || []).filter(c => c[0] === 'SaveScriptChapterMarks')")
        check("Add visible as chapters called SaveScriptChapterMarks",
              len(chCalls) == 1, str(chCalls))
        chapters = chCalls[0][1] if chCalls else []
        check("Chapters mapped from visible segments",
              isinstance(chapters, list) and len(chapters) == 4, str(chapters))

        page.wait_for_function("window.__playLoads && window.__playLoads.length > 0", timeout=3000)
        loads = page.evaluate("window.__playLoads")
        check("Improve/Generate reloads Play for editor",
              loads and any(l[1].get("review") is True for l in loads),
              str(loads))

        browser.close()

    shutdown()
    harness.unlink(missing_ok=True)
    return check.report()


if __name__ == "__main__":
    sys.exit(main())
