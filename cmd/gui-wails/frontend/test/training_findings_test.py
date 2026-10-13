"""Regressionstest für die Verbesserungsvorschläge aus
docs/TRAINING_MODE_RESEARCH.md, die nachträglich umgesetzt wurden:

  * Escape unterbricht den laufenden Zyklus von überall im Tab (B)
  * ein Vorschlag aus der History für die gerade gewählte Technik/das
    gewählte Script (D)
  * ein Hinweis, wenn ein Script-Preview randomisiert ist (E)
  * Script-Editor: Phasen verschieben/duplizieren (F)
  * History-Zeilen für Script-Sessions ohne "/script"-Technik-Suffix (G)
  * CSV-Export-Knopf für die History (H)

Ausführen:  python3 cmd/gui-wails/frontend/test/training_findings_test.py
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

HISTORY = [
    {"fileName": "a", "startedAt": "2026-09-22T10:00:00Z", "technique": "stopstart", "channel": "vibration",
     "cyclesCompleted": 5, "cyclesStoppedEarly": 0, "meanPeakIntensity": 0.8,
     "meanReachedPeakAfterMs": 4000, "meanArousalReported": 0, "arousalReportsCount": 0},
    {"fileName": "b", "startedAt": "2026-09-21T10:00:00Z", "technique": "stopstart", "channel": "vibration",
     "cyclesCompleted": 5, "cyclesStoppedEarly": 0, "meanPeakIntensity": 0.75,
     "meanReachedPeakAfterMs": 4000, "meanArousalReported": 0, "arousalReportsCount": 0},
    {"fileName": "c", "startedAt": "2026-09-20T10:00:00Z", "technique": "vibration-wave-suction-focus",
     "channel": "script", "cyclesCompleted": 6, "cyclesStoppedEarly": 3, "meanPeakIntensity": 0.6,
     "meanReachedPeakAfterMs": 0, "meanArousalReported": 0, "arousalReportsCount": 0},
]

PREVIEW_PLAIN = {"vibration": [], "suction": [], "totalMs": 0, "phaseMarkers": [], "hasRandomJitter": False}
PREVIEW_JITTER = {"vibration": [], "suction": [], "totalMs": 0, "phaseMarkers": [], "hasRandomJitter": True}


def main():
    check = Checker()
    calls_log = []

    base, shutdown = serve(app_stub({
        "StartTraining": "async () => {}",
        "StopTraining": "async () => {}",
        "StopTrainingCycle": "async () => { window.__calls.push(['stopCycle']); }",
        "TrainingHistory": f"async () => {HISTORY!r}".replace("'", '"').replace("False", "false").replace("True", "true"),
        "ExportTrainingHistoryCSV": "async () => { window.__calls.push(['export']); return '/tmp/training-history.csv'; }",
        "ListTrainingScripts": ("async () => [{name: 'variable', description: 'randomized wave', custom: false}, "
                                 "{name: 'vibration-wave-suction-focus', description: 'wave then focus', custom: false}]"),
        "TrainingScriptPreview": ("async (name) => (" +
                                   "name === 'variable' ? " +
                                   f"{PREVIEW_JITTER!r}".replace("'", '"').replace("False", "false").replace("True", "true") +
                                   " : " +
                                   f"{PREVIEW_PLAIN!r}".replace("'", '"').replace("False", "false").replace("True", "true") +
                                   ")"),
    }))

    harness = FRONTEND / "test" / "_training_findings_harness.html"
    harness.write_text(PAGE)

    with sync_playwright() as p:
        browser = p.chromium.launch()
        page = browser.new_page()
        page.on("pageerror", lambda e: print("   [pageerror]", e))
        page.goto(f"{base}/test/_training_findings_harness.html")
        page.wait_for_function("window.__ready === true")
        page.wait_for_function("document.querySelectorAll('#tr-history div').length === 3")

        # --- G: script-session history label ------------------------------
        lines = page.locator("#tr-history div").all_inner_texts()
        script_line = next((l for l in lines if "vibration wave suction focus" in l), None)
        check("Script-Session zeigt lesbaren Namen ohne '/script'-Suffix",
              script_line is not None and "/script" not in script_line, str(lines))
        check("Technik/Kanal-Session zeigt weiterhin Stop-Start/Vibration",
              any("Stop-Start/Vibration" in l for l in lines), str(lines))

        # --- D: history-based suggestion -----------------------------------
        page.select_option("#tr-technique", "stopstart")
        page.wait_for_function(
            "document.querySelector('#tr-history-suggestion').style.display !== 'none'")
        suggestion = page.locator("#tr-history-suggestion").inner_text()
        check("Vorschlag erscheint für saubere Sessions (stopstart)",
              "higher" in suggestion, suggestion)

        page.select_option("#tr-script", "vibration-wave-suction-focus")
        page.wait_for_function(
            "document.querySelector('#tr-history-suggestion').style.display !== 'none'")
        suggestion2 = page.locator("#tr-history-suggestion").inner_text()
        check("Vorschlag rät zum Zurückfahren nach Unterbrechungen (Script mit Stopps)",
              "ease off" in suggestion2, suggestion2)

        page.select_option("#tr-script", "")
        page.select_option("#tr-technique", "plateau")
        page.wait_for_function(
            "document.querySelector('#tr-history-suggestion').style.display === 'none'")
        check("kein Vorschlag ohne passende History (plateau, keine Daten)",
              page.locator("#tr-history-suggestion").inner_text() == "")

        # --- H: CSV export ---------------------------------------------------
        page.click("#tr-history-export")
        page.wait_for_function("window.__calls.some(c => c[0] === 'export')")
        check("Export-Knopf ruft ExportTrainingHistoryCSV auf",
              page.evaluate("window.__calls.some(c => c[0] === 'export')"))

        # --- E: randomized-script preview note -------------------------------
        page.select_option("#tr-script", "vibration-wave-suction-focus")
        page.wait_for_function(
            "getComputedStyle(document.querySelector('#tr-script-preview')).display !== 'none'")
        check("kein Zufalls-Hinweis für ein festes Script",
              page.locator("#tr-script-jitter-note").evaluate("e => getComputedStyle(e).display") == "none")

        page.select_option("#tr-script", "variable")
        page.wait_for_function(
            "getComputedStyle(document.querySelector('#tr-script-jitter-note')).display !== 'none'")
        check("Zufalls-Hinweis erscheint für ein Script mit Jitter",
              page.locator("#tr-script-jitter-note").evaluate("e => getComputedStyle(e).display") != "none")

        page.select_option("#tr-script", "")

        # --- B: global Escape stop -------------------------------------------
        page.click("#tr-start")
        page.wait_for_function("document.querySelector('#tr-pause').disabled === false")
        page.keyboard.press("Escape")
        page.wait_for_function("window.__calls.some(c => c[0] === 'stopCycle')")
        check("Escape löst StopTrainingCycle aus",
              page.evaluate("window.__calls.some(c => c[0] === 'stopCycle')"))
        check("Escape-Interrupt wird im Log vermerkt",
              "Escape" in page.locator("#tr-log").inner_text())

        # --- F: editor phase reorder / duplicate -----------------------------
        page.evaluate("document.querySelector('#tr-editor').open = true")
        page.click("#tr-editor-add-phase")
        page.wait_for_function("document.querySelectorAll('.tr-editor-phase').length === 2")
        page.fill('.tr-editor-phase[data-phase-index="0"] [data-field="name"]', "First")
        page.fill('.tr-editor-phase[data-phase-index="1"] [data-field="name"]', "Second")

        page.click('.tr-editor-phase[data-phase-index="0"] [data-action="move-phase-down"]')
        page.wait_for_function(
            "document.querySelector(`.tr-editor-phase[data-phase-index='0'] [data-field='name']`).value === 'Second'")
        names_after_move = [page.locator(f'.tr-editor-phase[data-phase-index="{i}"] [data-field="name"]').input_value()
                             for i in range(2)]
        check("move-phase-down vertauscht die Reihenfolge",
              names_after_move == ["Second", "First"], str(names_after_move))

        page.click('.tr-editor-phase[data-phase-index="0"] [data-action="duplicate-phase"]')
        page.wait_for_function("document.querySelectorAll('.tr-editor-phase').length === 3")
        dup_name = page.locator('.tr-editor-phase[data-phase-index="1"] [data-field="name"]').input_value()
        check("duplicate-phase legt eine Kopie direkt danach an",
              dup_name == "Second copy", dup_name)

        browser.close()

    shutdown()
    harness.unlink(missing_ok=True)
    return check.report()


if __name__ == "__main__":
    sys.exit(main())
