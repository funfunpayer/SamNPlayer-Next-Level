"""Play OFS tools: Load project + Scale range must be wired in the GUI.

Backend already had LoadPlaybackProject / EditScaleRange; Save project and
speed-cap/delete were visible — load + scale were missing from the surface
(Owner: every shipped function must also load in the GUI).

Run: python3 cmd/gui-wails/frontend/test/pb_project_load_test.py
"""

import pathlib
import sys

from playwright.sync_api import sync_playwright

from _harness import Checker, app_stub, serve

PAGE = """<!doctype html><html><body><div id="root"></div>
<script type="module">
  import { initPlayback } from '/src/playback.js';
  window.__pb = initPlayback(document.querySelector('#root'));
  window.__ready = true;
</script></body></html>"""


def main():
    check = Checker()
    base, shutdown = serve(app_stub({
        "PickFunscriptFile": "async () => '/tmp/clip.samn'",
        "LoadFunscript": "async () => ({ path: '/tmp/clip.samn', actionCount: 5, "
                         "durationMs: 10000, videoPath: '', hasVideo: false })",
        "GetScriptCurve": "async () => [{ atMs: 0, pos: 0 }, { atMs: 10000, pos: 100 }]",
        "GetHeatmap": "async n => Array.from({ length: n }, (_, i) => ({ atMs: i * 100, intensity: 0.5 }))",
        "GetMarker": "async () => null",
        "VideoFileURL": "async () => ''",
        "GetSpeedHighlights": "async () => []",
        "ExportScriptHeatmapPNG": "async () => '/tmp/h.png'",
        "SavePlaybackProject": "async () => '/tmp/clip.snp.json'",
        "PickPlaybackProject": "async () => '/tmp/clip.snp.json'",
        "LoadPlaybackProject": (
            "async (path) => { window.__calls.push(['LoadPlaybackProject', path]); "
            "return { version: 1, scriptPath: '/tmp/clip.samn', videoPath: '', "
            "offsetMs: 120, seekMs: 2500, loopMarker: { startMs: 1000, endMs: 4000 } }; }"
        ),
        "SetScriptOffset": "async (ms) => { window.__calls.push(['SetScriptOffset', ms]); }",
        "GetScriptOffset": "async () => 0",
        "EditCapSpeedRange": (
            "async (axis, a, b, maxI) => { "
            "window.__calls.push(['EditCapSpeedRange', axis, a, b, maxI]); }"
        ),
        "EditDeleteRange": "async () => {}",
        "EditScaleRange": (
            "async (axis, a, b, f, soft) => { "
            "window.__calls.push(['EditScaleRange', axis, a, b, f, soft]); }"
        ),
        "SnapTimeMs": "async (t, fps) => t",
        "GetOMarkers": "async () => []",
        "SaveOMarkers": "async () => {}",
        "AnalyzeScript": "async () => ({ summary: 'ok' })",
        "ScriptChapters": "async () => []",
        "GetScriptActions": "async () => [{ at: 0, pos: 0 }, { at: 10000, pos: 100 }]",
        "GetScriptAxisActions": "async () => [{ at: 0, pos: 0 }, { at: 10000, pos: 100 }]",
        "GetStrengthPresets": "async () => ({ presets: [], active: '' })",
    }))

    harness = pathlib.Path(__file__).resolve().parent / "_pb_project_load_harness.html"
    harness.write_text(PAGE)

    with sync_playwright() as p:
        browser = p.chromium.launch()
        page = browser.new_page()
        page.on("pageerror", lambda e: print("   [pageerror]", e))
        page.goto(f"{base}/test/_pb_project_load_harness.html")
        page.wait_for_function("window.__ready === true")

        page.click("#pb-choose")
        page.wait_for_function(
            "() => document.querySelector('#pb-ofs-row').style.display === 'flex'",
            timeout=5000)
        check("curve edit details present", page.locator("#pb-ofs-edit-details").count() == 1)
        check("curve edit closed until selection",
              page.locator("#pb-ofs-edit-details").evaluate("el => !el.open"))
        check("omarker panel present collapsed",
              page.locator("#pb-omarker-details").count() == 1
              and page.locator("#pb-omarker-details").evaluate("el => !el.open"))
        page.locator("#pb-ofs-edit-details").evaluate("el => { el.open = true }")
        page.evaluate(
            "document.querySelector('#pb-project-details').open = true")

        check("Load project button present", page.locator("#pb-project-load").count() == 1)
        check("Project details present", page.locator("#pb-project-details").count() == 1)
        check("Scale range button present", page.locator("#pb-scale-range").count() == 1)
        check("Scale factor slider present", page.locator("#pb-scale-factor").count() == 1)
        check("Cap intensity slider present", page.locator("#pb-cap-intensity").count() == 1)
        check("Cap intensity default 400",
              page.locator("#pb-cap-intensity").input_value() == "400")

        page.click("#pb-project-load")
        page.wait_for_function(
            "() => (window.__calls || []).some(c => c[0] === 'LoadPlaybackProject')",
            timeout=5000)
        check("LoadPlaybackProject called",
              page.evaluate(
                  "() => (window.__calls || []).some(c => c[0]==='LoadPlaybackProject'"
                  " && c[1]==='/tmp/clip.snp.json')"))
        check("offset restored from project",
              page.evaluate(
                  "() => (window.__calls || []).some(c => c[0]==='SetScriptOffset' && c[1]===120)"))
        check("loop marker restored",
              page.locator("#pb-marker-label").inner_text().strip() != ""
              or page.evaluate(
                  "() => document.querySelector('#pb-marker-hint').style.display !== 'none'"))

        # Heatmap drag is heavy; drive selection via exposed path: click scale
        # without marker should warn; set marker through project already loaded.
        page.click("#pb-scale-range")
        page.wait_for_function(
            "() => (window.__calls || []).some(c => c[0] === 'EditScaleRange')",
            timeout=5000)
        check("EditScaleRange ×0.8 on general",
              page.evaluate(
                  "() => (window.__calls || []).some(c => c[0]==='EditScaleRange'"
                  " && c[1]==='general' && c[2]===1000 && c[3]===4000"
                  " && c[4]===0.8 && c[5]===false)"))

        page.fill("#pb-cap-intensity", "275")
        page.click("#pb-cap-speed")
        page.wait_for_function(
            "() => (window.__calls || []).some(c => c[0] === 'EditCapSpeedRange')",
            timeout=5000)
        check("EditCapSpeedRange uses Cap slider (275)",
              page.evaluate(
                  "() => (window.__calls || []).some(c => c[0]==='EditCapSpeedRange'"
                  " && c[1]==='general' && c[2]===1000 && c[3]===4000"
                  " && c[4]===275)"))

        browser.close()

    shutdown()
    harness.unlink(missing_ok=True)
    return check.report()


if __name__ == "__main__":
    sys.exit(main())
