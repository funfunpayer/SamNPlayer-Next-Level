"""AI Train Cancel buttons call CancelRoiTraining.

Run: python3 cmd/gui-wails/frontend/test/roi_cancel_training_test.py
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
            "async () => new Promise(() => { window.__trainStarted = true; })",
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

    harness = pathlib.Path(__file__).resolve().parent / "_roi_cancel_training_harness.html"
    harness.write_text(PAGE)

    with sync_playwright() as p:
        browser = p.chromium.launch()
        page = browser.new_page()
        page.on("pageerror", lambda e: print("   [pageerror]", e))
        page.goto(f"{base}/test/_roi_cancel_training_harness.html")
        page.wait_for_function("window.__ready === true")

        check("cancel-run button present", page.locator("#rt-cancel-run").count() == 1)
        check("cancel-train button present", page.locator("#rt-cancel-train").count() == 1)
        check("cancel-run hidden initially", page.is_hidden("#rt-cancel-run"))
        check("cancel-train hidden initially", page.is_hidden("#rt-cancel-train"))

        # Enable train and start a hanging run so Cancel appears.
        page.evaluate("() => { document.querySelector('#rt-train').disabled = false; }")
        page.click("#rt-train")
        page.wait_for_function("window.__trainStarted === true", timeout=5000)
        check("cancel-train visible while training", page.is_visible("#rt-cancel-train"))
        check("cancel-run stays hidden during train", page.is_hidden("#rt-cancel-run"))
        check("bootstrap disabled during train",
              page.locator("#rt-bootstrap").is_disabled() is True)

        page.click("#rt-cancel-train")
        page.wait_for_function("() => (window.__cancelCalls || 0) >= 1", timeout=3000)
        check("CancelRoiTraining called",
              page.evaluate("() => (window.__cancelCalls || 0) >= 1"))

        browser.close()

    shutdown()
    harness.unlink(missing_ok=True)
    return check.report()


if __name__ == "__main__":
    sys.exit(main())
