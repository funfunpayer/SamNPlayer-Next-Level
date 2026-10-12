"""Edition switch: Free build hides [data-suite] controls, suite shows them.

Run: python3 cmd/gui-wails/frontend/test/edition_switch_test.py
"""

import pathlib
import sys

from playwright.sync_api import sync_playwright

from _harness import Checker, app_stub, serve

PAGE = """<!doctype html><html><head><link rel="stylesheet" href="/src/style.css?real"></head><body><div id="root"></div>
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
        "GetEULA": "async () => ({version:'2026-10-10', text:'EULA TEXT freeware', accepted:true})",
        "GetThirdPartyNotices": "async () => 'NOTICES FFmpeg GPL'",
        "GetDonateLinks": "async () => [{name:'Patreon', url:'https://www.patreon.com/x'}, {name:'PayPal', url:'https://paypal.me/x'}]",
        "OpenDonateLink": "async (u) => { window.__opened = u; }",
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

    harness = pathlib.Path(__file__).resolve().parent / "_edition_switch_harness.html"
    harness.write_text(PAGE)

    with sync_playwright() as p:
        browser = p.chromium.launch()
        page = browser.new_page()
        page.on("pageerror", lambda e: print("   [pageerror]", e))
        page.goto(f"{base}/test/_edition_switch_harness.html")
        page.wait_for_function("window.__ready === true")

        # Open every section first so "hidden" can only come from the edition rule.
        page.evaluate("document.querySelectorAll('details').forEach(d => d.open = true)")
        check("real stylesheet loaded (?real bypasses the harness stub)",
              page.evaluate("[...document.styleSheets].some(s => (s.href||'').includes('style.css'))"))
        page.evaluate("document.documentElement.dataset.edition = 'free'")
        check("Free: Learn from judgments hidden", page.is_hidden("#st-model-train"))
        check("Free: learning data hidden", page.is_hidden("#st-collect-learning"))
        check("Free: Open AI training hidden", page.is_hidden("#st-open-ai-train"))
        check("Free: normal settings still visible", page.is_visible("#st-show-eula"))
        page.evaluate("document.documentElement.dataset.edition = 'suite'")
        check("suite: Learn from judgments visible", page.is_visible("#st-model-train"))
        check("suite: Open AI training visible", page.is_visible("#st-open-ai-train"))

        browser.close()

    shutdown()
    harness.unlink(missing_ok=True)
    return check.report()


if __name__ == "__main__":
    sys.exit(main())
