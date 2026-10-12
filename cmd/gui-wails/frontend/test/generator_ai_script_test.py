"""Create Advanced: AIWrite S1 Export classical run discoverability.

S0: draft stays disabled. After a mocked Create finish, export enables and
status/status-line point at Advanced → Export classical run (GUI load).

Run: python3 cmd/gui-wails/frontend/test/generator_ai_script_test.py
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
        "ScriptExistsForVideo": "async () => false",
        "SceneMapAvailable": "async () => false",
        "AIScriptWriterStatus": (
            "async () => ({ available: false, reason: "
            "'No AI draft model yet. Export classical good runs (Advanced → Export classical run) to build a local imitation library, then draft becomes available. Everyday Create stays CSRT.', "
            "stage: 'S1', sampleCount: 0 })"
        ),
        "DraftAIScript": (
            "async () => { throw new Error('aiscript: not available'); }"
        ),
        "KeepAIScriptDraft": (
            "async () => { throw new Error('aiscript: no draft'); }"
        ),
        "GenerateScript": (
            "async (opts) => { window.__calls.push(['GenerateScript', opts]); "
            "setTimeout(() => window.__triggerEvent && window.__triggerEvent("
            "'generate:done', { path: '/tmp/clip.samn', qualityScore: 0.8, "
            "qualityPassed: true, seq: 1 }), 0); }"
        ),
        "ExportAIScriptImitation": (
            "async () => ({ message: 'Saved training sample under ai_script_imitation' })"
        ),
    }))
    harness = FRONTEND / "test" / "_generator_ai_script_harness.html"
    harness.write_text(PAGE)

    with sync_playwright() as p:
        browser = p.chromium.launch()
        page = browser.new_page()
        page.on("dialog", lambda d: d.dismiss())
        page.on("pageerror", lambda e: print("   [pageerror]", e))
        page.goto(f"{base}/test/_generator_ai_script_harness.html")
        page.wait_for_function("window.__ready === true")

        check("AI draft button in DOM", page.locator("#gen-ai-script-draft").count() == 1)
        check("Export classical in DOM", page.locator("#gen-ai-script-export").count() == 1)
        check("Keep draft in DOM", page.locator("#gen-ai-script-keep").count() == 1)
        check("Discard in DOM", page.locator("#gen-ai-script-discard").count() == 1)
        check("Keep hidden before draft", page.locator("#gen-ai-script-keep").is_hidden())

        page.click("#gen-choose")
        page.wait_for_function(
            "!document.querySelector('#gen-step-run').hidden",
            timeout=5000)

        page.locator("#gen-advanced").evaluate("e => { e.open = true; }")
        page.locator("#gen-advanced-ai-draft").evaluate("e => { e.open = true; }")
        page.wait_for_selector("#gen-ai-script-draft", state="visible", timeout=5000)
        check("draft disabled in S0", page.is_disabled("#gen-ai-script-draft"))
        check("export disabled before Create", page.is_disabled("#gen-ai-script-export"))
        status = page.locator("#gen-ai-script-status").inner_text()
        check("status mentions export after Create",
              "Export classical" in status or "after Create" in status,
              status)

        page.wait_for_function(
            "document.querySelector('#gen-autoroi').disabled === false && "
            "!document.querySelector('#gen-roi-label').textContent.includes('No ')",
            timeout=5000)
        page.wait_for_function(
            "!document.querySelector('#gen-generate').disabled",
            timeout=5000)
        page.click("#gen-generate")
        page.wait_for_function(
            "() => { const s = document.querySelector('#gen-status')?.textContent || '';"
            " return s.includes('Export classical run'); }",
            timeout=8000)
        status_line = page.locator("#gen-status").inner_text()
        check("Create status points at Advanced Export",
              "Export classical run" in status_line and "Advanced" in status_line,
              status_line)
        check("export enabled after Create", not page.is_disabled("#gen-ai-script-export"))
        status2 = page.locator("#gen-ai-script-status").inner_text()
        check("status says export ready",
              "Export classical run ready" in status2,
              status2)

        browser.close()

    shutdown()
    harness.unlink(missing_ok=True)
    return check.report()


if __name__ == "__main__":
    sys.exit(main())
