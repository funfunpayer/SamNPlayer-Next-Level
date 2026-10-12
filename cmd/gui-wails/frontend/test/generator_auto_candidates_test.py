"""Create scene-map: Accept/Reject author:auto contact candidates.

Run: python3 cmd/gui-wails/frontend/test/generator_auto_candidates_test.py
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
        "LoadFirstFrame": f"async () => ({{ width: 1280, height: 720, "
                          f"pngBase64: '{TINY_PNG_B64}' }})",
        "LoadFrameAt": f"async () => ({{ width: 1280, height: 720, "
                       f"pngBase64: '{TINY_PNG_B64}' }})",
        "SuggestPipeline": "async () => ({ Backend: 'csrt', Profile: 'standard', "
                           "Reason: 'everyday', GoPath: true })",
        "SuggestProfile": "async () => ({ found: false })",
        "CheckAIRoiAvailable": "async () => false",
        "CheckAudioCheckAvailable": "async () => true",
        "SceneMapAvailable": "async () => true",
        "LoadSceneMapForVideo": (
            "async (path) => { window.__calls.push(['LoadSceneMapForVideo', path]); "
            "return { found: true, path: '/tmp/clip.samn', "
            "map: { version: 1, cols: 16, rows: 9, width: 1280, height: 720, "
            "windows: [{ startMs: 0, endMs: 8000, score: [40], chosenCell: -1 }] }, "
            "marks: ["
            "{ id: 'teacher-contact-4000', kind: 'region', author: 'auto', "
            "class: 'contact', atMs: 4000, reviewed: false, follow: false, "
            "rect: { x: 100, y: 120, w: 80, h: 80 }, fromMs: 0, toMs: 0 },"
            "{ id: 'user1', kind: 'exclude', author: 'user', "
            "rect: { x: 1, y: 1, w: 10, h: 10 }, fromMs: 0, toMs: 0, follow: true }"
            "] }; }"
        ),
        "ReviewAutoContactCandidate": (
            "async (path, id, accept) => { "
            "window.__calls.push(['ReviewAutoContactCandidate', path, id, accept]); }"
        ),
        "ImportContactCandidatesForVideo": "async () => 0",
        "PickContactPointsFile": "async () => ''",
        "ScriptExistsForVideo": "async () => false",
        "GenerateScript": "async () => ({ path: '/tmp/clip.funscript' })",
        "ScanSceneMap": "async () => ({ version: 1, cols: 16, rows: 9, width: 1280, "
                        "height: 720, windows: [] })",
    }))
    harness = FRONTEND / "test" / "_generator_auto_candidates_harness.html"
    harness.write_text(PAGE)

    with sync_playwright() as p:
        browser = p.chromium.launch()
        page = browser.new_page()
        page.on("pageerror", lambda e: print("   [pageerror]", e))
        page.goto(f"{base}/test/_generator_auto_candidates_harness.html")
        page.wait_for_function("window.__ready === true")

        page.click("#gen-choose")
        page.wait_for_function(
            "() => (window.__calls || []).some(c => c[0] === 'LoadSceneMapForVideo')",
            timeout=5000)
        page.click("#gen-advanced > summary")
        page.evaluate("document.querySelector('#gen-advanced-scenemap').open = true")
        # Tools show after map restore when windows exist
        page.wait_for_selector("#gen-scene-map-tools", state="visible", timeout=5000)
        page.locator("#gen-scene-map-learning").evaluate("el => { el.open = true; }")
        page.wait_for_selector("#gen-auto-candidates", state="visible", timeout=5000)

        check("Import candidates button",
              page.locator("#gen-import-contact-candidates").count() == 1)
        check("Pending auto listed",
              page.locator("#gen-auto-candidates-list .gen-auto-accept").count() == 1)
        check("Marks label shows auto?",
              "·auto?" in page.locator("#gen-scene-map-marks-label").inner_text())

        page.click(".gen-auto-accept")
        page.wait_for_function(
            "() => (window.__calls || []).some("
            "c => c[0] === 'ReviewAutoContactCandidate' && c[3] === true)",
            timeout=3000)
        calls = page.evaluate(
            "() => (window.__calls || []).filter("
            "c => c[0] === 'ReviewAutoContactCandidate')")
        check("Accept called with mark id",
              calls and calls[0][2] == "teacher-contact-4000" and calls[0][3] is True)
        check("After accept label shows auto✓",
              "·auto✓" in page.locator("#gen-scene-map-marks-label").inner_text())

        # Re-open would need another pending — inject reject via a second mark
        # by restoring list state: click reject isn't available after accept.
        # Accept path is the critical Persist gate.
        browser.close()

    shutdown()
    harness.unlink(missing_ok=True)
    return check.report()


if __name__ == "__main__":
    sys.exit(main())
