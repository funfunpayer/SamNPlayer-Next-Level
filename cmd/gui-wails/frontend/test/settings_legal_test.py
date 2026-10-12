"""Settings: licence section (EULA + notices) and donation buttons; no licence-key UI.

Run: python3 cmd/gui-wails/frontend/test/settings_legal_test.py
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
        "GetEULA": "async () => ({version:'2026-10-10', text:'EULA TEXT freeware', accepted:true})",
        "GetThirdPartyNotices": "async () => 'NOTICES FFmpeg GPL'",
        "GetDonateLinks": "async () => [{name:'Patreon', url:'https://www.patreon.com/x'}, {name:'PayPal', url:'https://paypal.me/x'}]",
        "OpenDonateLink": "async (u) => { window.__opened = u; }",
        "GetAboutInfo": "async () => ({version:'v0.5.47', edition:'free', eulaVersion:'2026-10-11', partnerName:'Sex for him', partnerUrl:''})",
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

    harness = pathlib.Path(__file__).resolve().parent / "_settings_legal_harness.html"
    harness.write_text(PAGE)

    with sync_playwright() as p:
        browser = p.chromium.launch()
        page = browser.new_page()
        page.on("pageerror", lambda e: print("   [pageerror]", e))
        page.goto(f"{base}/test/_settings_legal_harness.html")
        page.wait_for_function("window.__ready === true")

        check("no licence-key UI left", page.locator("#st-license-paste").count() == 0
              and page.locator("#st-license-import-text").count() == 0)
        page.wait_for_function("document.querySelectorAll('#st-donate-buttons button').length === 2", timeout=3000)
        check("two donation buttons", page.locator("#st-donate-buttons button").count() == 2)
        page.click("#st-donate-buttons button >> nth=1")
        page.wait_for_function("window.__opened !== undefined", timeout=3000)
        check("PayPal button opens the PayPal link", page.evaluate("window.__opened") == "https://paypal.me/x")
        page.click("#st-show-eula")
        check("EULA dialog shows the text", "EULA TEXT freeware" in page.inner_text(".legal-text"))
        page.click("#legal-close")
        check("EULA dialog closes", page.locator(".legal-overlay").count() == 0)
        page.click("#st-show-notices")
        check("notices dialog shows FFmpeg", "FFmpeg" in page.inner_text(".legal-text"))
        page.click("#legal-close")
        page.wait_for_function("document.querySelector('#st-about-version').textContent.includes('v0.5.47')", timeout=3000)
        check("About shows version + free edition",
              "Free edition" in page.inner_text("#st-about-version"))
        check("About partner text says coming soon without URL",
              "coming soon" in page.inner_text("#st-about-partner-text"))
        check("partner button disabled without URL", page.is_disabled("#st-about-partner"))
        check("partner graphic present", page.locator(".st-about-partner img").count() == 1)

        browser.close()

    shutdown()
    harness.unlink(missing_ok=True)
    return check.report()


if __name__ == "__main__":
    sys.exit(main())
