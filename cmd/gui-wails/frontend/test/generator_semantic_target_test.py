"""Strict semantic AI target selection must never auto-apply or fall back."""

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
        "PickVideoFile": "async () => '/tmp/semantic.mp4'",
        "LoadFirstFrame": f"async () => ({{ width:1280, height:720, pngBase64:'{TINY_PNG_B64}' }})",
        "LoadFrameAt": f"async () => ({{ width:1280, height:720, pngBase64:'{TINY_PNG_B64}' }})",
        "CheckAIRoiAvailable": "async () => true",
        "CheckAudioCheckAvailable": "async () => true",
        "SuggestProfile": "async () => ({found:false})",
        "SuggestPipeline": "async () => ({Backend:'csrt',Profile:'standard',Reason:'test',GoPath:true})",
        "GenerateScript": "async (opts) => { window.__calls.push(['generate', opts]); }",
        "CancelROIDetection": "async () => { window.__calls.push(['cancel-roi']); }",
        "DetectExpectedTipROI": (
            "async (path, expected, timeSec, requestId) => { "
            "window.__calls.push(['strict-tip', path, expected, timeSec, requestId]); "
            "if (window.__holdStrict) return; const fail = !!window.__strictFail; "
            "setTimeout(() => window.__triggerEvent("
            "'generate:tip-detection', fail ? "
            "{requestId,videoPath:path,expectedClass:expected,match:false,status:'target_not_detected',"
            "errorCode:'target_not_detected',error:'not found'} : "
            "{requestId,videoPath:path,expectedClass:expected,matchedClass:expected,match:true,status:'matched',"
            "confidence:0.91,x:210,y:120,w:70,h:60,sampleIndex:25}), 0); }"
        ),
    }))
    harness = FRONTEND / "test" / "_generator_semantic_target_harness.html"
    harness.write_text(PAGE)

    try:
        with sync_playwright() as p:
            browser = p.chromium.launch()
            page = browser.new_page()
            page.on("pageerror", lambda e: print("   [pageerror]", e))
            page.goto(f"{base}/test/{harness.name}")
            page.wait_for_function("window.__ready === true")
            page.click("#gen-choose")
            page.wait_for_function(
                "document.querySelector('#gen-autoroi').disabled === false && "
                "!document.querySelector('#gen-roi-label').textContent.includes('No ')",
                timeout=5000)
            initial_label = page.locator("#gen-roi-label").inner_text()

            page.wait_for_function("!document.querySelector('#gen-ai-roi').disabled", timeout=5000)
            page.locator("#gen-mark-ai-details").evaluate("el => { el.open = true }")
            page.check("#gen-ai-roi")
            check("Strict target selector starts empty",
                  page.locator("#gen-ai-target-class").input_value() == "")
            page.click("#gen-autoroi")
            check("AI find without expected class does not call detector",
                  page.evaluate("window.__calls.filter(c => c[0] === 'strict-tip').length") == 0)
            check("Missing expected class is explained",
                  "choose the expected" in page.locator("#gen-status").inner_text().lower())

            page.select_option("#gen-ai-target-class", "glans")
            page.click("#gen-autoroi")
            page.wait_for_selector("#gen-ai-target-status button:text('Apply target')", timeout=5000)
            call = page.evaluate("window.__calls.find(c => c[0] === 'strict-tip')")
            check("Strict detector receives the selected canonical class",
                  call == ["strict-tip", "/tmp/semantic.mp4", "glans", 0, 1], str(call))
            check("Matching AI result is still only a proposal",
                  page.locator("#gen-roi-label").inner_text() == initial_label)
            check("Proposal shows matched class and confidence",
                  "Glans" in page.locator("#gen-ai-target-status").inner_text()
                  and "91%" in page.locator("#gen-ai-target-status").inner_text())

            page.click("#gen-generate")
            check("Unreviewed semantic proposal cannot start generation",
                  page.evaluate("window.__calls.filter(c => c[0] === 'generate').length") == 0)
            check("Create explains that Apply or Dismiss is required",
                  "apply target" in page.locator("#gen-status").inner_text().lower()
                  and "dismiss" in page.locator("#gen-status").inner_text().lower())

            page.click("#gen-ai-target-status button:text('Apply target')")
            check("Apply copies the semantic target into the active ROI",
                  "x=210" in page.locator("#gen-roi-label").inner_text())
            check("Apply carries the confirmed class into generation metadata",
                  page.locator("#gen-region-class").input_value() == "glans")

            page.evaluate("window.__strictFail = true")
            page.select_option("#gen-ai-target-class", "nipples")
            page.click("#gen-autoroi")
            page.wait_for_function(
                "document.querySelector('#gen-status').textContent.includes('Existing region kept')",
                timeout=5000)
            check("Wrong/missing target never overwrites the confirmed ROI",
                  "x=210" in page.locator("#gen-roi-label").inner_text())
            check("Strict failure offers manual recovery instead of class fallback",
                  "mark" in page.locator("#gen-status").inner_text().lower()
                  and page.locator("#gen-ai-target-status button").count() == 0)

            page.evaluate("window.__strictFail = false; window.__holdStrict = true")
            page.select_option("#gen-ai-target-class", "vagina")
            page.click("#gen-autoroi")
            page.click("#gen-generate")
            check("Running semantic detection blocks generation",
                  page.evaluate("window.__calls.filter(c => c[0] === 'generate').length") == 0
                  and "wait for" in page.locator("#gen-status").inner_text().lower())
            page.select_option("#gen-ai-target-class", "mouth")
            page.evaluate("window.__triggerEvent('generate:tip-detection', {"
                          "videoPath:'/tmp/semantic.mp4',expectedClass:'vagina',"
                          "matchedClass:'vagina',match:true,status:'matched',"
                          "confidence:0.99,x:500,y:400,w:90,h:80})")
            page.wait_for_timeout(50)
            check("Late result for a previously selected class is ignored",
                  page.locator("#gen-ai-target-status button").count() == 0
                  and "x=210" in page.locator("#gen-roi-label").inner_text())
            check("Changing class while detection runs restores Find controls",
                  page.locator("#gen-autoroi").is_enabled())
            check("Changing class cancels the superseded detector backend",
                  page.evaluate("window.__calls.filter(c => c[0] === 'cancel-roi').length") >= 1)
            page.evaluate("window.__holdStrict = false")
            page.fill("#gen-seek", "1.2")
            page.click("#gen-seek-btn")
            page.click("#gen-autoroi")
            page.wait_for_selector("#gen-ai-target-status button:text('Apply target')", timeout=5000)
            last_call = page.evaluate("window.__calls.filter(c => c[0] === 'strict-tip').slice(-1)[0]")
            check("Strict detector receives the currently displayed preview time",
                  last_call[2] == "mouth" and last_call[3] == 1.2, str(last_call))

            # Same video + same class: only the newest nonce may update result or progress.
            page.click("#gen-ai-target-status button:text('Dismiss')")
            page.evaluate("window.__holdStrict = true")
            page.click("#gen-autoroi")
            old_call = page.evaluate("window.__calls.filter(c => c[0] === 'strict-tip').slice(-1)[0]")
            page.fill("#gen-seek", "1.3")
            page.click("#gen-seek-btn")
            page.click("#gen-autoroi")
            new_call = page.evaluate("window.__calls.filter(c => c[0] === 'strict-tip').slice(-1)[0]")
            page.evaluate("([oldId]) => {"
                          "window.__triggerEvent('generate:tip-progress', {requestId:oldId,"
                          "videoPath:'/tmp/semantic.mp4',line:'STALE PROGRESS'});"
                          "window.__triggerEvent('generate:tip-percent', {requestId:oldId,"
                          "videoPath:'/tmp/semantic.mp4',percent:77});"
                          "window.__triggerEvent('generate:tip-detection', {requestId:oldId,"
                          "videoPath:'/tmp/semantic.mp4',expectedClass:'mouth',matchedClass:'mouth',"
                          "match:true,status:'matched',confidence:0.99,x:600,y:400,w:90,h:80});"
                          "}", [old_call[4]])
            page.wait_for_timeout(50)
            check("Old same-class result and progress nonce are ignored",
                  "STALE PROGRESS" not in page.locator("#gen-status").inner_text()
                  and page.locator("#gen-ai-target-status button").count() == 0
                  and page.locator("#gen-progress-bar").get_attribute("style").find("77%") < 0)
            page.evaluate("([newId]) => window.__triggerEvent('generate:tip-detection', {"
                          "requestId:newId,videoPath:'/tmp/semantic.mp4',expectedClass:'mouth',"
                          "matchedClass:'mouth',match:true,status:'matched',confidence:0.88,"
                          "x:310,y:210,w:75,h:65})", [new_call[4]])
            page.wait_for_selector("#gen-ai-target-status button:text('Apply target')", timeout=5000)
            check("Newest same-class nonce is accepted",
                  "88%" in page.locator("#gen-ai-target-status").inner_text())
            browser.close()
    finally:
        shutdown()
        harness.unlink(missing_ok=True)
    return check.report()


if __name__ == "__main__":
    sys.exit(main())
