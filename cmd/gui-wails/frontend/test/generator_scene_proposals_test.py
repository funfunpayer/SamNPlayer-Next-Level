"""Create step 2: Scene2 proposals — Load / Apply primary / partner opt-in.

Run: python3 cmd/gui-wails/frontend/test/generator_scene_proposals_test.py
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

PROPOSAL = (
    "{ found: true, path: '/tmp/clip.scene.json', width: 1280, height: 720, "
    "count: 1, regionClass: 'mouth', "
    "proposal: { t_ms: 4000, start_ms: 0, end_ms: 8000, scene_type: 'blowjob', "
    "confidence: 0.8, "
    "primary: { x: 560, y: 400, w: 160, h: 80, score: 0.8, index: 1, class: 'mouth' }, "
    "partner: { x: 560, y: 480, w: 80, h: 240, score: 0.8, index: 2, class: 'penis' } } }"
)


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
        "SceneMapAvailable": "async () => false",
        "LoadSceneMapForVideo": "async () => ({ found: false })",
        "LoadSceneProposalsBesideVideo": (
            "async (path, atMs) => { "
            "window.__calls.push(['LoadSceneProposalsBesideVideo', path, atMs]); "
            f"return {PROPOSAL}; }}"
        ),
        "PickSceneProposalsFile": "async () => '/tmp/other.scene.json'",
        "LoadSceneProposalAt": (
            "async (path, atMs) => { "
            "window.__calls.push(['LoadSceneProposalAt', path, atMs]); "
            f"return {{ ...{PROPOSAL}, path }}; }}"
        ),
        "ScriptExistsForVideo": "async () => false",
        "GenerateScript": "async () => ({ path: '/tmp/clip.funscript' })",
    }))
    harness = FRONTEND / "test" / "_generator_scene_proposals_harness.html"
    harness.write_text(PAGE)

    with sync_playwright() as p:
        browser = p.chromium.launch()
        page = browser.new_page()
        page.on("pageerror", lambda e: print("   [pageerror]", e))
        page.goto(f"{base}/test/_generator_scene_proposals_harness.html")
        page.wait_for_function("window.__ready === true")

        page.click("#gen-choose")
        page.wait_for_function(
            "() => (window.__calls || []).some("
            "c => c[0] === 'LoadSceneProposalsBesideVideo')",
            timeout=5000)

        check("Scene type chip visible",
              page.locator("#gen-scene-type-chip").is_visible()
              and "Blowjob" in page.locator("#gen-scene-type-chip").inner_text())
        check("Apply as Tip shown",
              page.locator("#gen-scene-apply-primary").is_visible())
        check("Apply as contact shown (partner)",
              page.locator("#gen-scene-apply-partner").is_visible())

        # Before apply: no tip from scene (auto-find may have set one — clear by
        # checking Apply updates label to scene primary).
        page.click("#gen-scene-apply-primary")
        page.wait_for_function(
            "() => (document.querySelector('#gen-roi-label') || {}).textContent"
            "?.includes('scene primary')",
            timeout=3000)
        label = page.locator("#gen-roi-label").inner_text()
        check("Tip from scene primary",
              "scene primary" in label and "560" in label)
        status = page.locator("#gen-status").inner_text()
        check("Status says no silent ROI2",
              "partner not applied" in status.lower()
              or "no silent" in status.lower())

        page.click("#gen-scene-apply-partner")
        page.wait_for_function(
            "() => (document.querySelector('#gen-roi2-label') || {}).textContent"
            "?.includes('candidate') || "
            "(document.querySelector('#gen-roi2-label') || {}).textContent"
            "?.includes('560') || "
            "(document.querySelector('#gen-status') || {}).textContent"
            "?.includes('partner')",
            timeout=3000)
        check("Partner apply opted in",
              "partner" in page.locator("#gen-status").inner_text().lower()
              or "560" in page.locator("#gen-roi2-label").inner_text())

        page.click("#gen-scene-proposals-dismiss")
        check("Dismiss hides actions",
              not page.locator("#gen-scene-proposals-actions").is_visible())

        browser.close()

    shutdown()
    harness.unlink(missing_ok=True)
    return check.report()


if __name__ == "__main__":
    sys.exit(main())
