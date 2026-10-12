"""Settings + Create expose in-app user handbook.

Run: python3 cmd/gui-wails/frontend/test/handbook_gui_test.py
"""

import pathlib
import sys

from playwright.sync_api import sync_playwright

from _harness import Checker, app_stub, serve

PAGE_SETTINGS = """<!doctype html><html><body><div id="root"></div>
<script type="module">
  import { initSettings } from '/src/settings.js';
  initSettings(document.querySelector('#root'));
  window.__ready = true;
</script></body></html>"""

PAGE_CREATE = """<!doctype html><html><body><div id="root"></div>
<script type="module">
  import { initGenerator } from '/src/generator.js';
  initGenerator(document.querySelector('#root'), { loadScriptPath: () => {} });
  window.__ready = true;
</script></body></html>"""

TINY_PNG_B64 = ("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNk"
                "+A8AAQUBAScY42YAAAAASUVORK5CYII=")

TEST_DIR = pathlib.Path(__file__).resolve().parent


def main():
    check = Checker()
    settings_stub = app_stub({
        "GetSettings": "async () => ({updateCheckOnStartup:false,deviceConnectTest:false,logLevel:'info',logPath:'',reportPath:'',defaultReportPath:'',clearCacheOnExit:false,aiRoiModelPath:'',aiPreferredClasses:'',aiBaseUrl:'',collectLearningData:false})",
        "GetLicenseStatus": """async () => ({
          present:false, licensed:false, effective:true, enforcement:false,
          state:'none', message:'No license.'
        })""",
        "DeleteSceneMapLearningData": "async () => {}",
        "SetSetting": "async () => {}",
        "ReportExists": "async () => false",
        "GetCacheInfo": "async () => ({files:0, humanSize:'0 B', path:'/tmp', entries:0, bytes:0})",
        "CurrentVersion": "async () => '0.0.0-test'",
        "GetRuntimeHealth": "async () => ({ok:true,deps:[],dirsCreated:[],resources:{}})",
        "EnsureVideoTools": "async () => {}",
        "CheckForUpdate": "async () => ({available:false})",
        "GetHardwareInfo": "async () => 'ok'",
        "CheckAIRoiAvailable": "async () => false",
        "OpenLogFolder": "async () => {}",
        "PickReportPath": "async () => ''",
        "ReportSummary": "async () => ''",
        "QualityModelInfo": "async () => ({trained:false})",
        "TrainQualityModel": "async () => {}",
        "ClearCache": "async () => ({path:'',entries:0,bytes:0})",
        "ApplyUpdate": "async () => {}",
        "ImportLicenseText": "async () => ({})",
        "ImportLicenseFile": "async () => ({})",
        "ClearLicense": "async () => ({})",
    })

    harness_s = TEST_DIR / "_handbook_settings_harness.html"
    harness_s.write_text(PAGE_SETTINGS)
    base, shutdown = serve(settings_stub)
    try:
        with sync_playwright() as p:
            browser = p.chromium.launch()
            page = browser.new_page()
            page.on("pageerror", lambda e: print("   [pageerror]", e))
            page.goto(f"{base}/test/_handbook_settings_harness.html")
            page.wait_for_function("window.__ready === true")
            check("Settings has Open user handbook",
                  page.locator("#st-open-handbook").count() == 1)
            page.click("#st-open-handbook")
            check("Handbook overlay opens from Settings",
                  page.locator(".handbook-overlay").count() == 1)
            check("Handbook title visible",
                  page.locator(".handbook-panel h2").inner_text() == "User handbook")
            check("Handbook mentions Use contact points",
                  page.locator(".handbook-panel").inner_text().find(
                      "Use contact points") >= 0)
            page.click(".handbook-close")
            check("Handbook closes",
                  page.locator(".handbook-overlay").count() == 0)
            browser.close()
    finally:
        shutdown()
        harness_s.unlink(missing_ok=True)

    create_stub = app_stub({
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
            "async () => ({ available: false, reason: 'No model', stage: 'S1' })"
        ),
        "DraftAIScript": "async () => { throw new Error('n/a'); }",
        "KeepAIScriptDraft": "async () => { throw new Error('n/a'); }",
        "ExportAIScriptImitation": "async () => ({ path: '', message: '' })",
    })
    harness_c = TEST_DIR / "_handbook_create_harness.html"
    harness_c.write_text(PAGE_CREATE)
    base, shutdown = serve(create_stub)
    try:
        with sync_playwright() as p:
            browser = p.chromium.launch()
            page = browser.new_page()
            page.on("pageerror", lambda e: print("   [pageerror]", e))
            page.goto(f"{base}/test/_handbook_create_harness.html")
            page.wait_for_function("window.__ready === true")
            check("Create has User handbook button",
                  page.locator("#gen-open-handbook").count() == 1)
            page.click("#gen-open-handbook")
            check("Handbook opens from Create",
                  page.locator(".handbook-overlay").count() == 1)
            page.keyboard.press("Escape")
            check("Escape closes handbook",
                  page.locator(".handbook-overlay").count() == 0)
            browser.close()
    finally:
        shutdown()
        harness_c.unlink(missing_ok=True)

    return check.report()


if __name__ == "__main__":
    sys.exit(main())
