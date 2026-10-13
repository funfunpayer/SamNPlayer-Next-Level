"""Settings: Apply AI setup automatically checkbox (default off).

Run: python3 cmd/gui-wails/frontend/test/settings_apply_ai_setup_test.py
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
        "GetSettings": "async () => ({updateCheckOnStartup:false,deviceConnectTest:false,logLevel:'info',logPath:'',reportPath:'',defaultReportPath:'',clearCacheOnExit:false,aiRoiModelPath:'',aiPreferredClasses:'',aiBaseUrl:'',collectLearningData:false,applyAISetupAutomatically:false})",
        "GetLicenseStatus": """async () => ({
          present:false, licensed:false, effective:true, enforcement:false,
          state:'none', message:'No license.'
        })""",
        "SetSetting": "async (key, value) => { window.__calls = window.__calls || []; window.__calls.push([key, value]); }",
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
        "DeleteSceneMapLearningData": "async () => {}",
        "ListInstalledPlugins": "async () => []",
        "PluginsDir": "async () => '/tmp/plugins'",
    }))

    harness = pathlib.Path(__file__).resolve().parent / "_settings_apply_ai_setup_harness.html"
    harness.write_text(PAGE)

    with sync_playwright() as p:
        browser = p.chromium.launch()
        page = browser.new_page()
        page.on("pageerror", lambda e: print("   [pageerror]", e))
        page.goto(f"{base}/test/_settings_apply_ai_setup_harness.html")
        page.wait_for_function("window.__ready === true")
        page.locator("#st-optional-ai").evaluate("el => { el.open = true; }")

        check("Apply AI setup checkbox present",
              page.locator("#st-apply-ai-setup").count() == 1)
        check("Apply AI setup default off",
              not page.locator("#st-apply-ai-setup").is_checked())

        page.check("#st-apply-ai-setup")
        page.wait_for_timeout(50)
        calls = page.evaluate("() => window.__calls || []")
        hit = any(c[0] == "generator.applyAISetupAutomatically" and c[1] is True
                  for c in calls)
        check("Toggling on saves setting", hit)

        browser.close()
    shutdown()
    harness.unlink(missing_ok=True)
    return check.report()


if __name__ == "__main__":
    raise SystemExit(main())
