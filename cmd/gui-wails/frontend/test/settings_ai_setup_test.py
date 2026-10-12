"""Settings: Check AI setup + Install buttons.

Run: python3 cmd/gui-wails/frontend/test/settings_ai_setup_test.py
"""

import pathlib
import sys

from playwright.sync_api import sync_playwright

from _harness import Checker, app_stub, serve

PAGE = """<!doctype html><html><body><div id="root"></div>
<script type="module">
  import { initSettings } from '/src/settings.js';
  initSettings(document.querySelector('#root'));
  window.__ready = true;
</script></body></html>"""


def main():
    check = Checker()
    base, shutdown = serve(app_stub({
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
        "CheckAIServerAvailable": "async () => false",
        "CheckAISetup": """async () => {
          window.__aiSetupChecked = true;
          return { gpus: [{ name: 'Test GPU', vram_gb: 16 }],
            packages: { nudenet: true }, servers: {}, next_steps: ['Install train'] };
        }""",
        "InstallAISetup": "async (p) => { window.__aiSetupInstall = p; }",
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
        "PluginsDir": "async () => '/tmp/plugins'",
        "ListInstalledPlugins": "async () => ([])",
        "OpenPluginsFolder": "async () => {}",
        "InstallPluginPack": "async () => ({})",
    }))

    harness = pathlib.Path(__file__).resolve().parent / "_settings_ai_setup_harness.html"
    harness.write_text(PAGE)

    with sync_playwright() as p:
        browser = p.chromium.launch()
        page = browser.new_page()
        page.on("pageerror", lambda e: print("   [pageerror]", e))
        page.goto(f"{base}/test/_settings_ai_setup_harness.html")
        page.wait_for_function("window.__ready === true")
        page.locator("#st-optional-ai").evaluate("el => { el.open = true; }")

        check("Check AI setup button", page.locator("#st-ai-setup-check").count() == 1)
        check("Install teachers button", page.locator("#st-ai-setup-teachers").count() == 1)
        page.click("#st-ai-setup-check")
        page.wait_for_function("window.__aiSetupChecked === true", timeout=3000)
        out = page.locator("#st-ai-setup-out").inner_text()
        check("Report shows GPU", "Test GPU" in out)
        check("Report shows next steps", "Install train" in out)

        page.click("#st-ai-setup-teachers")
        page.wait_for_function("window.__aiSetupInstall === 'teachers'", timeout=3000)
        check("Install teachers profile",
              page.evaluate("() => window.__aiSetupInstall") == "teachers")

        browser.close()

    shutdown()
    harness.unlink(missing_ok=True)
    return check.report()


if __name__ == "__main__":
    sys.exit(main())
