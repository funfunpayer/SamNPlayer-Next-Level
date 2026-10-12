"""Settings AI server: Test AI server button probes CheckAIServerAvailable.

Run: python3 cmd/gui-wails/frontend/test/settings_ai_server_test.py
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
        "SetSetting": "async (key, value) => { window.__calls = window.__calls || []; window.__calls.push([key, value]); }",
        "ReportExists": "async () => false",
        "GetCacheInfo": "async () => ({files:0, humanSize:'0 B', path:'/tmp', entries:0, bytes:0})",
        "CurrentVersion": "async () => '0.0.0-test'",
        "GetRuntimeHealth": "async () => ({ok:true,deps:[],dirsCreated:[],resources:{}})",
        "EnsureVideoTools": "async () => {}",
        "CheckForUpdate": "async () => ({available:false})",
        "GetHardwareInfo": "async () => 'ok'",
        "CheckAIRoiAvailable": "async () => false",
        "CheckAIServerAvailable": "async () => { window.__aiServerChecked = true; return true; }",
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

    harness = pathlib.Path(__file__).resolve().parent / "_settings_ai_server_harness.html"
    harness.write_text(PAGE)

    with sync_playwright() as p:
        browser = p.chromium.launch()
        page = browser.new_page()
        page.on("pageerror", lambda e: print("   [pageerror]", e))
        page.goto(f"{base}/test/_settings_ai_server_harness.html")
        page.wait_for_function("window.__ready === true")
        page.locator("#st-optional-ai").evaluate("el => { el.open = true; }")

        check("Test AI server button", page.locator("#st-ai-server-check").count() == 1)
        check("AI server status line", page.locator("#st-ai-server-status").count() == 1)

        page.fill("#st-ai-base-url", "http://127.0.0.1:8080")
        page.click("#st-ai-server-check")
        page.wait_for_function("window.__aiServerChecked === true", timeout=3000)
        status = page.locator("#st-ai-server-status").inner_text()
        check("reachable status text", "Reachable" in status)

        browser.close()

    shutdown()
    harness.unlink(missing_ok=True)
    return check.report()


if __name__ == "__main__":
    sys.exit(main())
