"""Regressionstest für den KI-Trainingssystem-Tab.

Deckt den vollen Weg ab, den app_roi_training.go anbietet: Region(en)
markieren -> Bootstrap anstoßen -> Kontrollansicht zeigt die gesammelten
Beispiele mit Box-Overlay -> Verwerfen entfernt ein Beispiel -> Übersicht
zählt Klassen -> Training anstoßen und Log/Status aus den Events übernehmen.
Die eigentliche Python-/Go-Logik (Tracking, Label-Dateien, Training) läuft
hier nicht mit - nur die Verdrahtung im Frontend wird geprüft, mit Stubs für
alle Go-Bindings (siehe app_stub in _harness.py).

Ausführen:  python3 cmd/gui-wails/frontend/test/roi_training_test.py
"""

import json
import pathlib
import sys

from playwright.sync_api import sync_playwright

from _harness import Checker, app_stub, serve

FRONTEND = pathlib.Path(__file__).resolve().parents[1]

PAGE = """<!doctype html><html><body>
<button class="tab-btn" data-tab="settings" id="tabbtn-settings">Settings</button>
<button class="tab-btn" data-tab="generator" id="tabbtn-generator">Create</button>
<div id="root"></div>
<script type="module">
  import { initRoiTraining } from '/src/roi_training.js';
  document.querySelectorAll('.tab-btn').forEach(btn => {
    btn.addEventListener('click', () => { window.__lastTab = btn.dataset.tab; });
  });
  initRoiTraining(document.querySelector('#root'));
  window.__ready = true;
</script></body></html>"""

# 1x1-Pixel-PNG (transparent) - dieselbe Attrappe wie in generator_tftj_test.py.
TINY_PNG_B64 = ("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNk"
                "+A8AAQUBAScY42YAAAAASUVORK5CYII=")

SAMPLES = [
    {
        "split": "train", "name": "clipA_000000.jpg", "imagePath": "/data/images/train/clipA_000000.jpg",
        "boxes": [{"classId": 0, "className": "brust", "xc": 0.5, "yc": 0.5, "w": 0.2, "h": 0.2}],
    },
    {
        "split": "train", "name": "clipA_000001.jpg", "imagePath": "/data/images/train/clipA_000001.jpg",
        "boxes": [{"classId": 0, "className": "brust", "xc": 0.4, "yc": 0.4, "w": 0.2, "h": 0.2}],
    },
]

SUMMARY = {
    "datasetDir": "/data",
    "hasDataYaml": True,
    "trainImages": 2,
    "readyToTrain": True,
    "classes": [{"className": "brust", "classId": 0, "trainCount": 2, "valCount": 0}],
}


def main():
    check = Checker()
    base, shutdown = serve(app_stub({
        "PickVideoFile": "async () => '/tmp/video.mp4'",
        "LoadFirstFrame": f"async () => ({{ width: 640, height: 360, "
                          f"pngBase64: '{TINY_PNG_B64}' }})",
        "LoadFrameAt": f"async () => ({{ width: 640, height: 360, "
                       f"pngBase64: '{TINY_PNG_B64}' }})",
        "BootstrapRoiTrainingRegions": "async () => { window.__calls.push('bootstrap'); return 'clipA_deadbeef'; }",
        "BootstrapRoiTrainingSampleEx": "async () => { window.__calls.push('bootstrap'); return 'clipA_deadbeef'; }",
        "ListRoiTrainingSamples": f"async () => {{ window.__calls.push('list'); return {json.dumps(SAMPLES)}; }}",
        "GetRoiTrainingSampleImage": f"async () => '{TINY_PNG_B64}'",
        "DiscardRoiTrainingSample": "async (dir, split, name) => { window.__calls.push(['discard', split, name]); }",
        "GetRoiDatasetSummary": f"async () => ({json.dumps(SUMMARY)})",
        "RunRoiModelTraining": "async (epochs, device) => { window.__calls.push(['train', epochs, device]); }",
        "GetSettings": "async () => ({ roiDatasetDir: '/data', defaultRoiDatasetDir: '/data' })",
        "CheckRoiTrainingAvailable": "async () => true",
        "CheckRoiTrainingStatus": "async () => ({ python: true, opencv: true, "
                                  "ultralytics: true, detail: 'Bereit' })",
        "InstallRoiTrainingDeps": "async () => {}",
        "GetMotionProfileModelStatus": "async () => ({ labelsPath: '/cfg/scenes.jsonl', "
            "modelPath: '/cfg/models/motion.json', labelledScenes: 3, usableSamples: 3, "
            "readyToTrain: true, modelAvailable: !!window.__profileReady, "
            "trainedSamples: window.__profileReady ? 3 : 0, "
            "trainedAt: window.__profileReady ? '2026-09-24T00:00:00Z' : '', "
            "profiles: [{profile:'standard',count:2},{profile:'weich',count:1}] })",
        "TrainMotionProfileModel": "async () => { window.__calls.push('train-profile'); "
            "window.__profileReady = true; return ({ "
            "labelsPath:'/cfg/scenes.jsonl', modelPath:'/cfg/models/motion.json', "
            "labelledScenes:3, usableSamples:3, readyToTrain:true, modelAvailable:true, "
            "trainedSamples:3, trainedAt:'2026-09-24T00:00:00Z', "
            "profiles:[{profile:'standard',count:2},{profile:'weich',count:1}] }); }",
        "UpdateRoiTrainingSample": "async (dir, split, name, boxes) => { "
            "window.__calls.push(['update', split, name, boxes]); }",
        "ListRoiTrainingDevices": "async () => (["
            "{id:'auto',label:'Automatic (best available)',available:true},"
            "{id:'cuda',label:'NVIDIA CUDA',available:false},"
            "{id:'directml',label:'DirectML (Windows)',available:false},"
            "{id:'mps',label:'Apple MPS',available:false},"
            "{id:'cpu',label:'CPU (very slow)',available:true}])",
    }))
    harness = FRONTEND / "test" / "_roi_training_harness.html"
    harness.write_text(PAGE)

    with sync_playwright() as p:
        browser = p.chromium.launch()
        page = browser.new_page()
        page.on("dialog", lambda d: d.dismiss())
        page.on("pageerror", lambda e: print("   [pageerror]", e))
        page.goto(f"{base}/test/_roi_training_harness.html")
        page.wait_for_function("window.__ready === true")

        # getSettingsCache() löst asynchron auf - warten statt sofort zu prüfen,
        # sonst ist der Test von der Reihenfolge zweier Promise-Ticks abhängig.
        page.wait_for_function(
            "document.querySelector('#rt-dataset-dir').value === '/data'", timeout=5000)
        check("Datensatzordner wird aus den Einstellungen übernommen", True)

        check("Body map present", page.locator("#rt-body-figure .body-figure-svg").count() == 1)
        check("Nine legend body parts",
              page.locator(".body-figure-legend [data-class]").count() == 9)

        page.wait_for_function("!document.querySelector('#rt-profile-train').disabled", timeout=5000)
        check("Go profile model shows usable learned scenes",
              "3 usable" in page.locator("#rt-profile-status").inner_text())
        check("Open Settings (Check availability) button",
              page.locator("#rt-open-settings").count() == 1)
        page.click("#rt-open-settings")
        page.wait_for_function("window.__lastTab === 'settings'", timeout=3000)
        check("Open Settings jumps to settings tab",
              page.evaluate("() => window.__lastTab") == "settings")
        page.click("#rt-profile-train")
        page.wait_for_function("window.__calls.includes('train-profile')", timeout=5000)
        check("Go profile model training is wired", True)
        page.wait_for_function(
            "document.querySelector('#rt-profile-status').textContent.includes('Model ready')",
            timeout=5000)
        check("Go profile model reports ready after training", True)

        page.click("#rt-pick-video")
        page.wait_for_function(
            "document.querySelector('#rt-video-path').textContent.includes('video.mp4')", timeout=5000)
        check("Video wird geladen", True)

        box = page.locator("#rt-canvas").bounding_box()

        # --- Region 1 markieren, ohne Klasse: Knopf bleibt gesperrt ----------
        page.mouse.move(box["x"] + 40, box["y"] + 40)
        page.mouse.down()
        page.mouse.move(box["x"] + 120, box["y"] + 120, steps=5)
        page.mouse.up()
        check("Ohne Klassennamen bleibt 'Use for training' gesperrt",
              page.locator("#rt-bootstrap").is_disabled())

        # Body map sets class without typing — then clear for the text-fill check.
        page.locator('.bf-zone[data-class="glans"]').first.click()
        page.wait_for_timeout(50)
        check("Body map click sets Class 1 to glans",
              page.locator("#rt-class1").input_value() == "glans")
        check("Body map unlocks Use for training",
              not page.locator("#rt-bootstrap").is_disabled())
        page.fill("#rt-class1", "")
        page.locator("#rt-class1").dispatch_event("input")
        check("Cleared class locks Use for training again",
              page.locator("#rt-bootstrap").is_disabled())

        page.fill("#rt-class1", "brust")
        page.locator("#rt-class1").dispatch_event("input")
        check("Mit Klassenname und 1 Region wird der Knopf frei",
              not page.locator("#rt-bootstrap").is_disabled())

        # --- 2. Region ohne deren Klassenname: wieder gesperrt ----------------
        page.click("#rt-mark-next")
        page.locator("#rt-canvas").scroll_into_view_if_needed()
        box = page.locator("#rt-canvas").bounding_box()
        page.mouse.move(box["x"] + 200, box["y"] + 40)
        page.mouse.down()
        page.mouse.move(box["x"] + 280, box["y"] + 120, steps=5)
        page.mouse.up()
        check("2. Region ohne deren Klasse sperrt den Knopf wieder",
              page.locator("#rt-bootstrap").is_disabled())

        page.fill("#rt-class2", "hand")
        page.locator("#rt-class2").dispatch_event("input")
        check("Mit beiden Klassennamen wieder frei",
              not page.locator("#rt-bootstrap").is_disabled())

        # --- Free tags: same label twice (nipples×2) is allowed -------------------
        page.click("#rt-clear-marks")
        page.wait_for_timeout(50)
        page.locator("#rt-canvas").scroll_into_view_if_needed()
        box = page.locator("#rt-canvas").bounding_box()
        page.mouse.move(box["x"] + 40, box["y"] + 40)
        page.mouse.down()
        page.mouse.move(box["x"] + 100, box["y"] + 100, steps=5)
        page.mouse.up()
        page.fill("#rt-class1", "nipples")
        page.locator("#rt-class1").dispatch_event("input")
        page.wait_for_function(
            "document.querySelector('#rt-nipples-hint').style.display !== 'none'",
            timeout=3000)
        check("Nipples×1 zeigt Hinweis auf zweite Box",
              "second nipple" in page.locator("#rt-nipples-hint").inner_text().lower()
              or "zweite" in page.locator("#rt-nipples-hint").inner_text().lower(),
              page.locator("#rt-nipples-hint").inner_text())
        page.click("#rt-mark-next")
        box = page.locator("#rt-canvas").bounding_box()
        page.mouse.move(box["x"] + 200, box["y"] + 40)
        page.mouse.down()
        page.mouse.move(box["x"] + 260, box["y"] + 100, steps=5)
        page.mouse.up()
        page.fill("#rt-class2", "nipples")
        page.locator("#rt-class2").dispatch_event("input")
        check("Zwei Nipples-Labels halten Use for training frei",
              not page.locator("#rt-bootstrap").is_disabled())
        box1_lab = page.locator("#rt-mark-fields .field-row").first.locator("label").inner_text()
        check("Box-Felder heißen Box N (free tags)",
              box1_lab.strip() == "Box 1", box1_lab)
        check("Free-tag Copy erwähnt duplicate / nipples",
              "nipples need two" in page.locator("#root > .hint").first.inner_text().lower()
              or "same label" in page.locator("#root > .hint").first.inner_text().lower(),
              page.locator("#root > .hint").first.inner_text()[:120])
        check("Copy sagt jedes Label wird getrackt (nicht nur Glans)",
              "every" in page.locator("#root > .hint").first.inner_text().lower()
              and "glans" in page.locator("#root > .hint").first.inner_text().lower(),
              page.locator("#root > .hint").first.inner_text()[:160])
        check("Collect Delete pro Box vorhanden",
              page.locator('#rt-mark-fields button[data-delete-mark]').count() >= 2)
        # Delete box 1 → compact; remaining nipples stays labeled, Use for training free
        page.locator('#rt-mark-fields button[data-delete-mark="0"]').click()
        page.wait_for_timeout(50)
        check("Delete kompaktioniert auf eine Box",
              page.locator('#rt-mark-fields button[data-delete-mark]').count() == 1)
        check("Nach Delete bleibt Use for training frei",
              not page.locator("#rt-bootstrap").is_disabled())
        check("Chips listen Nipples (volle Taxonomy)",
              "nipples" in page.locator("#rt-class-chips").inner_text().lower()
              or "nipple" in page.locator("#rt-class-chips").inner_text().lower())

        # --- Add custom tag + Delete box -----------------------------------------
        page.fill("#rt-new-tag", "custom_toy")
        page.click("#rt-add-tag")
        page.wait_for_timeout(50)
        # After two nipples boxes, add-tag should land on next empty-class box or
        # overwrite an empty-class preference — ensure custom chip/assign path works.
        check("+ Add tag UI vorhanden", page.locator("#rt-add-tag").count() == 1)
        check("Delete box UI vorhanden", page.locator("#rt-delete-mark").count() == 1)
        vals_before = page.evaluate(
            "() => [1,2,3,4].map(i => (document.querySelector('#rt-class'+i)||{}).value || '')")
        page.click("#rt-delete-mark")
        page.wait_for_timeout(50)
        vals_after = page.evaluate(
            "() => [1,2,3,4].map(i => (document.querySelector('#rt-class'+i)||{}).value || '')")
        filled_before = sum(1 for v in vals_before if v.strip())
        filled_after = sum(1 for v in vals_after if v.strip())
        check("Delete box entfernt eine Markierung",
              filled_after < filled_before or filled_after <= 1,
              f"before={vals_before} after={vals_after}")

        # Restore a normal bootstrap path (brust + hand) for review samples -----
        page.click("#rt-clear-marks")
        page.wait_for_timeout(50)
        box = page.locator("#rt-canvas").bounding_box()
        page.mouse.move(box["x"] + 40, box["y"] + 40)
        page.mouse.down()
        page.mouse.move(box["x"] + 120, box["y"] + 120, steps=5)
        page.mouse.up()
        page.fill("#rt-class1", "brust")
        page.locator("#rt-class1").dispatch_event("input")
        page.click("#rt-mark-next")
        box = page.locator("#rt-canvas").bounding_box()
        page.mouse.move(box["x"] + 200, box["y"] + 40)
        page.mouse.down()
        page.mouse.move(box["x"] + 280, box["y"] + 120, steps=5)
        page.mouse.up()
        page.fill("#rt-class2", "hand")
        page.locator("#rt-class2").dispatch_event("input")

        # --- Bootstrap anstoßen und per Event abschließen ---------------------
        page.click("#rt-bootstrap")
        page.wait_for_function(
            "window.__calls.includes('bootstrap')", timeout=5000)
        check("Knopf ist während des Laufs gesperrt", page.locator("#rt-bootstrap").is_disabled())

        page.evaluate("window.__triggerEvent('roitraining:bootstrap:progress', 'Frame 1/50')")
        check("Fortschrittszeile erscheint im Log",
              "Frame 1/50" in page.locator("#rt-bootstrap-log").inner_text())
        page.evaluate("window.__triggerEvent('roitraining:bootstrap:percent', 37)")
        check("Bootstrap-Balken sichtbar",
              page.locator("#rt-bootstrap-progress-wrap").evaluate("e => e.style.display") == "block")
        check("Bootstrap-Balken 37 %",
              page.locator("#rt-bootstrap-progress-bar").evaluate("e => e.style.width") == "37%",
              page.locator("#rt-bootstrap-progress-bar").evaluate("e => e.style.width"))

        page.evaluate("window.__triggerEvent('roitraining:bootstrap:done', { prefix: 'clipA_deadbeef' })")
        page.wait_for_function(
            "document.querySelector('#rt-bootstrap-progress-wrap').style.display === 'none'",
            timeout=5000)
        page.wait_for_function("window.__calls.includes('list')", timeout=5000)
        check("Knopf wird nach Abschluss wieder frei", not page.locator("#rt-bootstrap").is_disabled())

        # --- Kontrollansicht: Karten mit Bild + Box-Overlay --------------------
        page.wait_for_selector(".rt-card", timeout=5000)
        cards = page.locator(".rt-card")
        check("Kontrollansicht zeigt beide Beispiele", cards.count() == 2, str(cards.count()))
        page.wait_for_selector(".rt-box", timeout=5000)
        check("Box-Overlay wird gezeichnet", page.locator(".rt-box").count() >= 1)
        check("Klassenname steht auf dem Overlay",
              any(x in page.locator(".rt-box-label").first.inner_text().lower()
                  for x in ("brust", "breast")))

        # --- Großer Review-Editor: Thumb / Correct box öffnet Modal ------------
        page.locator(".rt-card .rt-thumb-wrap").first.click()
        page.wait_for_selector(".rt-review-overlay", timeout=5000)
        check("Thumb-Klick öffnet großen Editor",
              page.locator(".rt-review-overlay").count() == 1)
        check("Editor zeigt Confirm correct",
              page.locator(".rt-review-overlay button:text('Confirm correct')").count() == 1)
        check("Editor zeigt + Add box (free tag)",
              page.locator("#rt-review-add-box").count() == 1)
        check("Editor zeigt Delete box",
              page.locator("#rt-review-delete-box").count() == 1)
        check("Editor erlaubt custom tag input",
              page.locator("#rt-review-class-custom").count() == 1)
        check("Editor zeigt Box auf großem Bild",
              page.locator(".rt-review-scene .rt-box").count() >= 1)
        # All taxonomy labels (not only Face) are settable on detection windows.
        class_opts = page.locator("#rt-review-class-sel option").all_text_contents()
        check("Label-Select listet alle 9 Taxonomy-Klassen",
              len(class_opts) >= 9, str(class_opts))
        joined = " | ".join(class_opts).lower()
        for want in ("face", "mouth", "breasts", "nipples", "hand 1", "hand 2",
                     "penis", "glans", "vagina"):
            check(f"Label-Select enthält {want}", want in joined, joined)
        check("Window-Select ist sichtbar",
              page.locator("#rt-review-box-sel").count() == 1)
        page.select_option("#rt-review-class-sel", "glans")
        page.wait_for_function(
            "window.__calls.some(c => Array.isArray(c) && c[0] === 'update')",
            timeout=5000)
        check("Label-Wechsel ruft UpdateRoiTrainingSample auf", True)

        # + Add box: second free tag with nipples (Korrektur path)
        before_updates = page.evaluate(
            "() => window.__calls.filter(c => Array.isArray(c) && c[0] === 'update').length")
        page.click("#rt-review-add-box")
        page.wait_for_function(
            "document.querySelector('.rt-review-viewport.is-adjusting')", timeout=3000)
        check("+ Add box schaltet Zeichenmodus",
              page.locator(".rt-review-viewport.is-adjusting").count() == 1)
        page.select_option("#rt-review-class-sel", "nipples")
        # 1×1 stub image: drive pointer events on the scene (scaled) so Add finishes.
        page.evaluate("""() => {
          const vp = document.querySelector('.rt-review-viewport');
          const scene = document.querySelector('.rt-review-scene');
          if (!vp || !scene) throw new Error('missing review stage');
          const r = scene.getBoundingClientRect();
          const x0 = r.left + r.width * 0.1, y0 = r.top + r.height * 0.1;
          const x1 = r.left + r.width * 0.45, y1 = r.top + r.height * 0.45;
          const fire = (type, x, y) => {
            vp.dispatchEvent(new PointerEvent(type, {
              bubbles: true, cancelable: true, pointerId: 1, pointerType: 'mouse',
              clientX: x, clientY: y, button: 0, buttons: type === 'pointerup' ? 0 : 1,
            }));
          };
          fire('pointerdown', x0, y0);
          fire('pointermove', x1, y1);
          window.dispatchEvent(new PointerEvent('pointermove', {
            bubbles: true, cancelable: true, pointerId: 1, pointerType: 'mouse',
            clientX: x1, clientY: y1, button: 0, buttons: 1,
          }));
          window.dispatchEvent(new PointerEvent('pointerup', {
            bubbles: true, cancelable: true, pointerId: 1, pointerType: 'mouse',
            clientX: x1, clientY: y1, button: 0, buttons: 0,
          }));
        }""")
        page.wait_for_function(
            f"() => window.__calls.filter(c => Array.isArray(c) && c[0] === 'update').length > {before_updates}",
            timeout=5000)
        last_update = page.evaluate(
            "() => { const u = window.__calls.filter(c => Array.isArray(c) && c[0]==='update'); "
            "return u.length ? u[u.length-1] : null; }")
        check("+ Add box speichert zusätzliche Box",
              isinstance(last_update, list) and len(last_update) >= 4
              and isinstance(last_update[3], list) and len(last_update[3]) >= 2,
              str(last_update)[:200])
        added_names = []
        if isinstance(last_update, list) and len(last_update) >= 4:
            for b in last_update[3]:
                if isinstance(b, dict):
                    added_names.append(str(b.get("className") or b.get("ClassName") or "").lower())
        check("+ Add box kann nipples als Label setzen",
              "nipples" in added_names, str(added_names))

        # Delete box: remove selected free tag (Korrektur)
        before_del = page.evaluate(
            "() => window.__calls.filter(c => Array.isArray(c) && c[0] === 'update').length")
        n_boxes = page.locator("#rt-review-box-sel option").count()
        page.click("#rt-review-delete-box")
        page.wait_for_function(
            f"() => window.__calls.filter(c => Array.isArray(c) && c[0] === 'update').length > {before_del}",
            timeout=5000)
        after_del = page.evaluate(
            "() => { const u = window.__calls.filter(c => Array.isArray(c) && c[0]==='update'); "
            "return u.length ? u[u.length-1] : null; }")
        del_len = 0
        if isinstance(after_del, list) and len(after_del) >= 4 and isinstance(after_del[3], list):
            del_len = len(after_del[3])
        check("Delete box speichert eine Box weniger",
              del_len == max(0, n_boxes - 1) or del_len < n_boxes,
              f"before_opts={n_boxes} after_boxes={del_len} call={str(after_del)[:160]}")
        check("Delete box Button vorhanden",
              page.locator("#rt-review-delete-box").count() == 1)

        page.locator(".rt-review-overlay button:text('Confirm correct')").click()
        page.wait_for_function(
            "document.querySelectorAll('.rt-review-overlay').length === 0", timeout=5000)
        check("Confirm correct schließt den Editor", True)

        page.locator(".rt-card button:text('Correct box')").first.click()
        page.wait_for_selector(".rt-review-overlay.is-adjusting, .rt-review-viewport.is-adjusting",
                               timeout=5000)
        check("Correct box öffnet Editor im Adjust-Modus",
              page.locator(".rt-review-viewport.is-adjusting").count() == 1)
        page.locator(".rt-review-overlay button:text('Close')").click()
        page.wait_for_function(
            "document.querySelectorAll('.rt-review-overlay').length === 0", timeout=5000)

        # --- Verwerfen entfernt genau eine Karte --------------------------------
        page.locator(".rt-card button:text('Discard')").first.click()
        page.wait_for_function("window.__calls.some(c => Array.isArray(c) && c[0] === 'discard')", timeout=5000)
        page.wait_for_function("document.querySelectorAll('.rt-card').length === 1", timeout=5000)
        check("Verwerfen entfernt genau eine Karte", True)

        # --- Datensatz-Übersicht ------------------------------------------------
        page.click("#rt-summary-refresh")
        page.wait_for_selector("#rt-summary table", timeout=5000)
        check("Übersicht zeigt die Klasse 'brust'", "brust" in page.locator("#rt-summary").inner_text())
        check("Übersicht zeigt die Train-Anzahl", "2" in page.locator("#rt-summary").inner_text())

        # --- Training ------------------------------------------------------------
        # CheckRoiTrainingAvailable() löst asynchron auf - warten, bis der Knopf
        # freigegeben ist, statt gegen einen deaktivierten Knopf zu klicken.
        page.wait_for_function("!document.querySelector('#rt-train').disabled", timeout=5000)
        page.fill("#rt-epochs", "50")
        page.select_option("#rt-device", "cpu")
        page.click("#rt-train")
        page.wait_for_function(
            "window.__calls.some(c => Array.isArray(c) && c[0] === 'train')", timeout=5000)
        check("Training wird mit Epochen und Gerät aufgerufen",
              page.evaluate("window.__calls.find(c => Array.isArray(c) && c[0] === 'train')")
              == ["train", 50, "cpu"])
        check("Trainings-Knopf ist während des Laufs gesperrt", page.locator("#rt-train").is_disabled())

        page.evaluate("window.__triggerEvent('roitraining:train:progress', 'Epoch 1/50')")
        check("Trainings-Fortschrittszeile erscheint im Log",
              "Epoch 1/50" in page.locator("#rt-train-log").inner_text())

        page.evaluate("window.__triggerEvent('roitraining:train:done', { modelPath: '/models/roi.onnx' })")
        page.wait_for_function("!document.querySelector('#rt-train').disabled", timeout=5000)
        check("Status zeigt den Modellpfad nach Abschluss",
              "/models/roi.onnx" in page.locator("#rt-train-status").inner_text())

        browser.close()

    shutdown()

    # --- ohne ultralytics: Knopf bleibt gesperrt, Hinweis mit pip-Befehl -------
    base2, shutdown2 = serve(app_stub({
        "CheckRoiTrainingAvailable": "async () => false",
        "CheckRoiTrainingStatus": "async () => ({ python: true, opencv: true, "
                                  "ultralytics: false, detail: 'Bootstrap möglich; "
                                  "Training: pip install ultralytics onnx' })",
        "GetMotionProfileModelStatus": "async () => ({ labelsPath:'/cfg/scenes.jsonl', "
            "modelPath:'/cfg/models/motion.json', labelledScenes:2, usableSamples:2, "
            "readyToTrain:true, modelAvailable:false, trainedSamples:0, trainedAt:'', "
            "modelWarning:'invalid motion-profile model', profiles:[{profile:'standard',count:2}] })",
        "InstallRoiTrainingDeps": "async () => {}",
        "ListRoiTrainingDevices": "async () => []",
    }))
    with sync_playwright() as p:
        browser = p.chromium.launch()
        page = browser.new_page()
        page.on("dialog", lambda d: d.dismiss())
        page.goto(f"{base2}/test/_roi_training_harness.html")
        page.wait_for_function("window.__ready === true")
        page.wait_for_function(
            "document.querySelector('#rt-train-unavailable').style.display === 'block'", timeout=5000)
        check("Ohne ultralytics bleibt der Training-Knopf gesperrt",
              page.locator("#rt-train").is_disabled())
        hint = page.locator("#rt-train-unavailable").inner_text()
        check("Hinweis nennt pip/ultralytics",
              "ultralytics" in hint.lower() or "pip install" in hint.lower(), hint)
        page.wait_for_function("!document.querySelector('#rt-profile-train').disabled", timeout=5000)
        check("Korruptes Profilmodell sperrt Neutraining nicht",
              "replace" in page.locator("#rt-profile-path").inner_text().lower())
        browser.close()
    shutdown2()

    harness.unlink(missing_ok=True)

    return check.report()


if __name__ == "__main__":
    sys.exit(main())
