"""Regressionstest für den Multi-Achsen-Script-Editor im Trainings-Tab.

Der Editor lässt ein eigenes player.TrainingScript zusammenbauen statt nur
eines der eingebauten Presets zu wählen (siehe
docs/TRAINING_MODE_RESEARCH.md, "Follow-up design"). Geprüft wird hier,
mit einem gemockten Backend (kein echtes Gerät, kein echtes Dateisystem):

  * eine Phase mit zwei unabhängigen Achsen (Vibration/Suction) lässt sich
    bearbeiten, hinzufügen und entfernen,
  * Speichern schickt die erwartete Form (inkl. channel-Feld je Achse) an
    SaveTrainingScript,
  * Löschen und Laden eines gespeicherten Scripts funktionieren.

Ausführen:  python3 cmd/gui-wails/frontend/test/training_editor_test.py
"""

import pathlib
import sys

from playwright.sync_api import sync_playwright

from _harness import Checker, app_stub, serve

FRONTEND = pathlib.Path(__file__).resolve().parents[1]

PAGE = """<!doctype html><html><body><div id="root"></div>
<script type="module">
  import { initTraining } from '/src/training.js';
  initTraining(document.querySelector('#root'));
  window.__ready = true;
</script></body></html>"""

SAVED_SCRIPT = {
    "name": "Saved One",
    "description": "a saved custom script",
    "progressionPerCycle": 0,
    "phases": [{
        "name": "Loaded phase", "repeatCycles": 2, "restMs": 500,
        "vibration": {"channel": "vibration", "startLevel": 0.1, "peakLevel": 0.4, "endLevel": 0.1,
                      "rampUpMs": 500, "holdMs": 500, "rampDownMs": 500, "randomJitterFraction": 0},
        "suction": {"channel": "suction", "startLevel": 0.0, "peakLevel": 0.3, "endLevel": 0.0,
                    "rampUpMs": 500, "holdMs": 500, "rampDownMs": 500, "randomJitterFraction": 0},
    }],
}


def main():
    check = Checker()
    base, shutdown = serve(app_stub({
        "StartTraining": "async () => {}",
        "TrainingHistory": "async () => []",
        "ListTrainingScripts": ("async () => [{name: 'Saved One', description: 'a saved custom script', "
                                 "custom: true}]"),
        "TrainingScriptPreview": "async () => ({ vibration: [], suction: [], totalMs: 0, phaseMarkers: [] })",
        "PreviewTrainingScriptDraft": "async () => ({ vibration: [], suction: [], totalMs: 1000, phaseMarkers: [] })",
        "SaveTrainingScript": ("async (req) => { window.__calls.push(['save', req]); "
                                "return { name: req.name, description: req.description, custom: true }; }"),
        "DeleteTrainingScript": "async (name) => { window.__calls.push(['delete', name]); }",
        "LoadTrainingScriptForEditing": f"async (name) => ({SAVED_SCRIPT!r})".replace("'", '"'),
    }))

    harness = FRONTEND / "test" / "_training_editor_harness.html"
    harness.write_text(PAGE)

    with sync_playwright() as p:
        browser = p.chromium.launch()
        page = browser.new_page()
        page.on("pageerror", lambda e: print("   [pageerror]", e))
        page.goto(f"{base}/test/_training_editor_harness.html")
        page.wait_for_function("window.__ready === true")

        # <details> öffnen - Playwright kann auf versteckten Inhalt nicht klicken.
        page.evaluate("document.querySelector('#tr-editor').open = true")

        check("ein Phasen-Block ist vorhanden",
              page.locator(".tr-editor-phase").count() == 1)
        check("Vibration-Achse ist standardmäßig aktiv",
              page.locator('[data-axis="vibration"][data-field="enabled"]').is_checked())
        check("Suction-Achse ist standardmäßig inaktiv",
              not page.locator('[data-axis="suction"][data-field="enabled"]').is_checked())
        check("Suction-Felder sind gesperrt, solange die Achse aus ist",
              page.locator('[data-axis="suction"][data-field="peakLevel"]').is_disabled())

        # Suction-Achse aktivieren -> Felder müssen freigeschaltet werden.
        page.locator('[data-axis="suction"][data-field="enabled"]').check()
        check("Suction-Felder werden nach Aktivierung freigeschaltet",
              not page.locator('[data-axis="suction"][data-field="peakLevel"]').is_disabled())

        page.fill('.tr-editor-phase [data-field="name"]', "Custom phase")
        page.fill('[data-field="repeatCycles"]', "4")
        page.fill('[data-axis="vibration"][data-field="peakLevel"]', "0.9")
        page.fill('[data-axis="suction"][data-field="peakLevel"]', "0.5")

        # Phase hinzufügen und wieder entfernen.
        page.click("#tr-editor-add-phase")
        page.wait_for_function('document.querySelectorAll(".tr-editor-phase").length === 2')
        check("Add phase legt eine zweite Phase an",
              page.locator(".tr-editor-phase").count() == 2)
        page.locator('.tr-editor-phase[data-phase-index="1"] [data-action="remove-phase"]').click()
        page.wait_for_function('document.querySelectorAll(".tr-editor-phase").length === 1')
        check("Remove phase entfernt sie wieder",
              page.locator(".tr-editor-phase").count() == 1)
        check("Remove-Knopf ist gesperrt, wenn nur eine Phase übrig ist",
              page.locator('[data-action="remove-phase"]').is_disabled())

        page.fill("#tr-editor-name", "My Multi-Axis Script")
        page.fill("#tr-editor-description", "vibration + suction together")

        page.click("#tr-editor-save")
        page.wait_for_function("window.__calls.some(c => c[0] === 'save')")
        saved = page.evaluate("window.__calls.find(c => c[0] === 'save')")[1]
        check("gespeicherter Name kommt an", saved["name"] == "My Multi-Axis Script", str(saved))
        check("gespeicherte Beschreibung kommt an",
              saved["description"] == "vibration + suction together", str(saved))
        phase = saved["phases"][0]
        check("Phasenname kommt an", phase["name"] == "Custom phase", str(phase))
        check("Repeats kommen an", phase["repeatCycles"] == 4, str(phase))
        check("Vibration-Achse trägt channel=vibration",
              phase["vibration"]["channel"] == "vibration", str(phase))
        check("Vibration-Höhepunkt kommt an", phase["vibration"]["peakLevel"] == 0.9, str(phase))
        check("aktivierte Suction-Achse wird NICHT als null geschickt",
              phase["suction"] is not None, str(phase))
        check("Suction-Achse trägt channel=suction",
              phase["suction"]["channel"] == "suction", str(phase))
        check("Suction-Höhepunkt kommt an", phase["suction"]["peakLevel"] == 0.5, str(phase))

        check("Status meldet den gespeicherten Namen",
              "My Multi-Axis Script" in page.locator("#tr-editor-status").inner_text())
        check("Delete-Knopf wird nach dem Speichern freigeschaltet",
              not page.locator("#tr-editor-delete").is_disabled())

        page.click("#tr-editor-delete")
        page.wait_for_function("window.__calls.some(c => c[0] === 'delete')")
        deleted_name = page.evaluate("window.__calls.find(c => c[0] === 'delete')")[1]
        check("Delete schickt den zuletzt gespeicherten Namen",
              deleted_name == "My Multi-Axis Script", str(deleted_name))

        # Ein zuvor gespeichertes Script laden.
        page.select_option("#tr-editor-load", "Saved One")
        page.wait_for_function(
            "document.querySelector('#tr-editor-name').value === 'Saved One'")
        check("geladenes Script füllt den Namen",
              page.locator("#tr-editor-name").input_value() == "Saved One")
        check("geladenes Script füllt die Beschreibung",
              page.locator("#tr-editor-description").input_value() == "a saved custom script")
        check("geladene Phase übernimmt den Namen",
              page.locator('.tr-editor-phase [data-field="name"]').input_value() == "Loaded phase")
        check("geladene Suction-Achse ist aktiv (war im gespeicherten Script gesetzt)",
              page.locator('[data-axis="suction"][data-field="enabled"]').is_checked())
        check("Delete-Knopf ist für ein geladenes Script freigeschaltet",
              not page.locator("#tr-editor-delete").is_disabled())

        browser.close()

    shutdown()
    harness.unlink(missing_ok=True)
    return check.report()


if __name__ == "__main__":
    sys.exit(main())
