"""Settings → Open AI training deep-link.

Run: python3 cmd/gui-wails/frontend/test/settings_ai_train_link_test.py
"""

import pathlib
import sys

from playwright.sync_api import sync_playwright

from _harness import Checker, app_stub, serve

PAGE = """<!doctype html><html><body>
<button class="tab-btn" data-tab="settings">Settings</button>
<button class="tab-btn" data-tab="roi-training" id="tabbtn-roi">AI Train</button>
<div id="root"></div>
<script type="module">
  import { initSettings } from '/src/settings.js';
  document.querySelectorAll('.tab-btn').forEach(btn => {
    btn.addEventListener('click', () => {
      window.__lastTab = btn.dataset.tab;
      document.querySelectorAll('.tab-btn').forEach(b => b.classList.toggle('active', b === btn));
    });
  });
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
    }))

    harness = pathlib.Path(__file__).resolve().parent / "_settings_ai_train_link_harness.html"
    harness.write_text(PAGE)

    with sync_playwright() as p:
        browser = p.chromium.launch()
        page = browser.new_page()
        page.on("pageerror", lambda e: print("   [pageerror]", e))
        page.goto(f"{base}/test/_settings_ai_train_link_harness.html")
        page.wait_for_function("window.__ready === true")
        page.locator("#st-optional-ai").evaluate("el => { el.open = true; }")

        check("Open AI training button present",
              page.locator("#st-open-ai-train").count() == 1)
        page.click("#st-open-ai-train")
        page.wait_for_function("window.__lastTab === 'roi-training'", timeout=3000)
        check("Open AI training switches to roi-training tab",
              page.evaluate("() => window.__lastTab") == "roi-training")
        check("AI Train tab marked active",
              page.locator("#tabbtn-roi").evaluate("e => e.classList.contains('active')"))

        browser.close()

    shutdown()
    harness.unlink(missing_ok=True)
    return check.report()


if __name__ == "__main__":
    sys.exit(main())
