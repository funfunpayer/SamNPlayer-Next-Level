"""Regressionstest für die Preset-Script-Auswahl im Trainings-Tab.

Neu (siehe docs/TRAINING_MODE_RESEARCH.md, "Follow-up design: multi-phase,
per-channel scripts"): ein Dropdown mit den Scripts aus
player.BuiltinTrainingScripts() (ListTrainingScripts), das bei Auswahl
1) das alte Technik/Kanal-Formular versteckt (das Script bringt seine
eigenen Kurven mit), 2) eine Vorschau-Kurve zeichnet (TrainingScriptPreview),
und 3) StartTraining mit {scriptName} statt {technique, channel, ...} füttert.
"Custom" (leerer Wert) muss weiterhin exakt das alte Verhalten haben.

Ausführen:  python3 cmd/gui-wails/frontend/test/training_script_test.py
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

SCRIPTS = [
    {"name": "vibration-wave-suction-focus", "description": "Wave then suction focus."},
    {"name": "tissue-massage", "description": "Cupping-inspired suction-only."},
]

PREVIEW = {
    # Vier Punkte (Start/Höhepunkt/Höhepunkt/Ende) statt nur zwei - eine
    # echte Rampe-Halten-Rampe-Form mit einer inneren Ecke, damit die
    # Kurven-Rundung (roundedPathD, training.js) etwas zu glätten hat.
    "vibration": [{"atMs": 0, "level": 0.2}, {"atMs": 300, "level": 0.8},
                  {"atMs": 700, "level": 0.8}, {"atMs": 1000, "level": 0.2}],
    "suction": [{"atMs": 0, "level": 0.0}, {"atMs": 1000, "level": 0.0}],
    "totalMs": 1000,
    "phaseMarkers": [{"atMs": 0, "name": "Vibration wave"}],
}


def main():
    check = Checker()
    base, shutdown = serve(app_stub({
        "StartTraining": "async (req) => { window.__calls.push(['start', req]); }",
        "StopTraining": "async () => {}",
        "StopTrainingCycle": "async () => {}",
        "ReportArousal": "async () => {}",
        "TrainingHistory": "async () => []",
        "ListTrainingScripts": f"async () => {SCRIPTS!r}".replace("'", '"'),
        # Klammern um das Objekt-Literal: "async () => {...}" würde die
        # geschweiften Klammern als Funktionsrumpf statt als Objekt lesen
        # ("vibration": würde als ungültiges Label geparst).
        "TrainingScriptPreview": f"async () => ({PREVIEW!r})".replace("'", '"'),
    }))

    harness = FRONTEND / "test" / "_training_script_harness.html"
    harness.write_text(PAGE)

    with sync_playwright() as p:
        browser = p.chromium.launch()
        page = browser.new_page()
        page.on("pageerror", lambda e: print("   [pageerror]", e))
        page.goto(f"{base}/test/_training_script_harness.html")
        page.wait_for_function("window.__ready === true")

        page.wait_for_function("document.querySelector('#tr-script').options.length === 3")
        check("Dropdown listet Custom + beide Scripts",
              page.locator("#tr-script option").count() == 3,
              str(page.locator("#tr-script option").count()))

        check("vor Auswahl: manuelles Formular sichtbar",
              page.locator("#tr-manual-fields").evaluate("e => getComputedStyle(e).display") != "none")
        check("vor Auswahl: keine Vorschau",
              page.locator("#tr-script-preview").evaluate("e => getComputedStyle(e).display") == "none")

        page.select_option("#tr-script", "vibration-wave-suction-focus")
        page.wait_for_function(
            "getComputedStyle(document.querySelector('#tr-script-preview')).display !== 'none'")
        check("nach Auswahl: manuelles Formular versteckt",
              page.locator("#tr-manual-fields").evaluate("e => getComputedStyle(e).display") == "none")
        check("nach Auswahl: Vorschau zeigt eine SVG-Kurve",
              page.locator("#tr-script-preview svg").count() == 1)
        check("Beschreibung wird angezeigt",
              "Wave then suction focus" in page.locator("#tr-script-description").inner_text())

        # Die Vibrationskurve hat eine innere Ecke (Rampe->Halten->Rampe,
        # siehe PREVIEW oben) - roundedPathD muss sie mit einer
        # quadratischen Bezier ("Q") abrunden statt sie scharfkantig
        # ("L" an "L") stehen zu lassen.
        vib_path_d = page.locator("#tr-script-preview svg path").first.get_attribute("d")
        check("Vibrationskurve hat eine abgerundete Ecke (Q-Befehl)",
              "Q" in vib_path_d, vib_path_d)
        # Die Sog-Kurve hat nur Start/Ende (zwei Punkte, keine innere Ecke) -
        # da darf nichts gerundet werden, es gibt nichts zu runden.
        suc_path_d = page.locator("#tr-script-preview svg path").nth(1).get_attribute("d")
        check("Sog-Kurve (nur 2 Punkte) bleibt eine gerade Linie",
              "Q" not in suc_path_d and "L" in suc_path_d, suc_path_d)

        page.click("#tr-start")
        page.wait_for_function("window.__calls.some(c => c[0] === 'start')")
        started = page.evaluate("window.__calls.find(c => c[0] === 'start')")[1]
        check("StartTraining bekommt scriptName", started.get("scriptName") == "vibration-wave-suction-focus",
              str(started))
        check("StartTraining bekommt KEIN technique-Feld bei Script-Auswahl",
              "technique" not in started, str(started))

        page.evaluate("""window.__triggerEvent('training:scriptCycle', {
          phaseIndex: 0, phaseName: 'Vibration wave', phasesTotal: 2,
          repeatIndex: 0, repeatsTotal: 3, vibrationPeak: 0.8, suctionPeak: 0,
          restMs: 4000, stoppedByUser: false, arousalBefore: 10,
          peakFactorApplied: 0.7, restFactorApplied: 1.5,
        })""")
        page.wait_for_function(
            "document.querySelector('#tr-cycle-label').textContent.includes('Vibration wave')")
        check("Zyklus-Anzeige nennt Phase und Wiederholung",
              "1/3" in page.locator("#tr-cycle-label").inner_text(),
              page.locator("#tr-cycle-label").inner_text())
        check("Höhepunkt-Anzeige nennt Vibration in Prozent",
              "80%" in page.locator("#tr-peak-label").inner_text(),
              page.locator("#tr-peak-label").inner_text())
        effect = page.locator("#tr-feedback-effect").inner_text()
        check("Feedback-Effekt nennt beide Kanäle",
              "both channels" in effect, effect)
        check("Feedback-Effekt zeigt die gedämpfte Richtung (Höhepunkt runter)",
              "-30%" in effect, effect)
        check("Feedback-Effekt zeigt die verlängerte Pause",
              "+50%" in effect, effect)

        # Zurück auf "Custom" muss das alte Formular wiederherstellen.
        page.select_option("#tr-script", "")
        page.wait_for_function(
            "getComputedStyle(document.querySelector('#tr-manual-fields')).display !== 'none'")
        check("Custom stellt das manuelle Formular wieder her",
              page.locator("#tr-manual-fields").evaluate("e => getComputedStyle(e).display") != "none")

        browser.close()

    shutdown()
    harness.unlink(missing_ok=True)
    return check.report()


if __name__ == "__main__":
    sys.exit(main())
