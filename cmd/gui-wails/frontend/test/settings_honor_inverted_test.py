"""Settings: honor OFS inverted checkbox + saveSetting key.

Run: python3 cmd/gui-wails/frontend/test/settings_honor_inverted_test.py
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
        "GetSettings": "async () => ({updateCheckOnStartup:false,deviceConnectTest:false,"
                       "playbackHonorInverted:true,logLevel:'info',logPath:'',reportPath:'',"
                       "defaultReportPath:'',clearCacheOnExit:false,aiRoiModelPath:'',"
                       "aiPreferredClasses:'',aiBaseUrl:'',collectLearningData:false})",
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
        "InstallPluginPack": "async () => {}",
        "OpenPluginsFolder": "async () => {}",
        "CheckAIServerAvailable": "async () => ({ok:false})",
        "CheckAISetup": "async () => ({ok:true})",
        "InstallAISetup": "async () => ({})",
        "CheckForPatch": "async () => ({available:false})",
        "ApplyPatch": "async () => {}",
    }))

    harness = pathlib.Path(__file__).resolve().parent / "_settings_honor_inverted_harness.html"
    harness.write_text(PAGE)

    with sync_playwright() as p:
        browser = p.chromium.launch()
        page = browser.new_page()
        page.on("pageerror", lambda e: print("   [pageerror]", e))
        page.goto(f"{base}/test/_settings_honor_inverted_harness.html")
        page.wait_for_function("window.__ready === true")

        check("honor-inverted checkbox present", page.locator("#st-honor-inverted").count() == 1)
        check("default honor inverted on", page.is_checked("#st-honor-inverted") is True)
        help_text = page.locator("label[for='st-honor-inverted']").get_attribute("data-help") or ""
        check("data-help mentions OFS inverted", "OpenFunscripter" in help_text and "inverted" in help_text)

        page.uncheck("#st-honor-inverted")
        page.wait_for_function(
            "() => (window.__calls || []).some(c => c[0]==='playback.honor_inverted' && c[1]===false)",
            timeout=3000)
        check("SetSetting playback.honor_inverted false",
              page.evaluate("() => (window.__calls || []).some(c => c[0]==='playback.honor_inverted' && c[1]===false)"))

        browser.close()

    shutdown()
    harness.unlink(missing_ok=True)
    return check.report()


if __name__ == "__main__":
    sys.exit(main())
