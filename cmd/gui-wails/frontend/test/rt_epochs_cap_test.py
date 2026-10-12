"""AI Train: Epochs input is capped at 500 before RunRoiModelTraining.

Run: python3 cmd/gui-wails/frontend/test/rt_epochs_cap_test.py
"""

import json
import pathlib
import sys

from playwright.sync_api import sync_playwright

from _harness import Checker, app_stub, serve

TINY_PNG_B64 = ("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNk"
                "+A8AAQUBAScY42YAAAAASUVORK5CYII=")

SUMMARY = {
    "datasetDir": "/data",
    "hasDataYaml": True,
    "trainImages": 2,
    "readyToTrain": True,
    "classes": [{"className": "glans", "classId": 0, "trainCount": 2, "valCount": 0}],
}

PAGE = """<!doctype html><html><body><div id="root"></div>
<script type="module">
  import { initRoiTraining } from '/src/roi_training.js';
  initRoiTraining(document.querySelector('#root'));
  window.__ready = true;
</script></body></html>"""


def main():
    check = Checker()
    base, shutdown = serve(app_stub({
        "PickVideoFile": "async () => '/tmp/video.mp4'",
        "LoadFirstFrame": f"async () => ({{ width: 640, height: 360, "
                          f"pngBase64: '{TINY_PNG_B64}' }})",
        "LoadFrameAt": f"async () => ({{ width: 640, height: 360, "
                       f"pngBase64: '{TINY_PNG_B64}' }})",
        "BootstrapRoiTrainingRegions":
            "async () => new Promise(() => { window.__bootStarted = true; })",
        "ListRoiTrainingSamples": "async () => []",
        "GetRoiTrainingSampleImage": f"async () => '{TINY_PNG_B64}'",
        "DiscardRoiTrainingSample": "async () => {}",
        "GetRoiDatasetSummary": f"async () => ({json.dumps(SUMMARY)})",
        "RunRoiModelTraining":
            "async (epochs, device) => { window.__epochs = epochs; return new Promise(() => {}); }",
        "CancelRoiTraining":
            "async () => { window.__cancelCalls = (window.__cancelCalls || 0) + 1; return true; }",
        "GetSettings": "async () => ({ roiDatasetDir: '/data', defaultRoiDatasetDir: '/data' })",
        "CheckRoiTrainingAvailable": "async () => true",
        "CheckRoiTrainingStatus": "async () => ({ python: true, opencv: true, "
                                  "ultralytics: true, detail: 'Ready' })",
        "InstallRoiTrainingDeps": "async () => {}",
        "GetMotionProfileModelStatus": "async () => ({ readyToTrain: false, modelAvailable: false })",
        "TrainMotionProfileModel": "async () => ({})",
        "UpdateRoiTrainingSample": "async () => {}",
        "ListRoiTrainingDevices": "async () => ([{id:'auto',label:'Automatic',available:true},"
                                  "{id:'cpu',label:'CPU',available:true}])",
        "SetSetting": "async () => {}",
        "AddRoiStillTrainingSample": "async () => 'still'",
        "PickImageFile": "async () => ''",
    }))

    harness = pathlib.Path(__file__).resolve().parent / "_rt_epochs_cap_harness.html"
    harness.write_text(PAGE)

    with sync_playwright() as p:
        browser = p.chromium.launch()
        page = browser.new_page()
        page.on("pageerror", lambda e: print("   [pageerror]", e))
        page.goto(f"{base}/test/_rt_epochs_cap_harness.html")
        page.wait_for_function("window.__ready === true")

        check("epochs input has max=500", page.get_attribute("#rt-epochs", "max") == "500")
        page.fill("#rt-epochs", "10000")
        page.evaluate("() => { document.querySelector('#rt-train').disabled = false; }")
        page.click("#rt-train")
        page.wait_for_function("window.__epochs !== undefined", timeout=5000)
        check("RunRoiModelTraining gets 500, not 10000", page.evaluate("window.__epochs") == 500)
        check("field shows the capped value", page.input_value("#rt-epochs") == "500")

        browser.close()

    shutdown()
    harness.unlink(missing_ok=True)
    return check.report()


if __name__ == "__main__":
    sys.exit(main())
