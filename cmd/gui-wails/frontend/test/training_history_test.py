"""Regressionstest für den Verlauf-Bereich im Trainings-Tab.

Sessions wurden schon immer mitgeschrieben (app_training.go: openSessionLog),
aber nie wieder gelesen - eine Funktion mit Backend und Test, aber ohne
angeschlossene Oberfläche (docs/NEXT.md "Later": "Training history across
multiple sessions"). Geprüft wird hier nur die Anzeige: leer -> Hinweistext,
gefüllt -> eine Zeile je Session mit Technik/Kanal/Zyklen/Ø-Spitze, und dass
eine abgeschlossene Session (training:done) den Verlauf neu lädt.

Ausführen:  python3 cmd/gui-wails/frontend/test/training_history_test.py
"""

import pathlib
import sys

from playwright.sync_api import sync_playwright

from _harness import Checker, app_stub, serve

PAGE = """<!doctype html><html><body><div id="root"></div>
<script type="module">
  import { initTraining } from '/src/training.js';
  window.__pb = initTraining(document.querySelector('#root'));
  window.__ready = true;
</script></body></html>"""


def main():
    check = Checker()
    base, shutdown = serve(app_stub({
        "TrainingHistory": "async () => window.__history || []",
        "StartTraining": "async () => {}",
    }))

    harness = pathlib.Path(__file__).resolve().parent / "_training_history_harness.html"
    harness.write_text(PAGE)

    with sync_playwright() as p:
        browser = p.chromium.launch()
        page = browser.new_page()
        page.on("pageerror", lambda e: print("   [pageerror]", e))
        page.goto(f"{base}/test/_training_history_harness.html")
        page.wait_for_function("window.__ready === true")

        page.wait_for_function(
            "document.querySelector('#tr-history').textContent.includes('No completed')", timeout=5000)
        check("ohne Sessions: Hinweistext statt leerer Liste",
              "No completed" in page.locator("#tr-history").inner_text())

        page.evaluate("""
          window.__history = [
            { fileName: 'a', startedAt: '2026-09-15T10:00:00Z', technique: 'stopstart',
              channel: 'vibration', cyclesCompleted: 5, cyclesStoppedEarly: 2,
              meanPeakIntensity: 0.75, meanReachedPeakAfterMs: 4000,
              meanArousalReported: 7.5, arousalReportsCount: 3 },
            { fileName: 'b', startedAt: '2026-09-10T10:00:00Z', technique: 'plateau',
              channel: 'both', cyclesCompleted: 3, cyclesStoppedEarly: 0,
              meanPeakIntensity: 0.6, meanReachedPeakAfterMs: 0,
              meanArousalReported: 0, arousalReportsCount: 0 },
          ];
        """)
        page.evaluate("window.__triggerEvent('training:done')")
        page.wait_for_function(
            "document.querySelector('#tr-history').textContent.includes('Stop-Start')",
            timeout=5000)
        lines = page.locator("#tr-history div").all_inner_texts()
        check("zwei Sessions als zwei Zeilen", len(lines) == 2, str(lines))
        first, second = lines[0], lines[1]
        check("Technik wird übersetzt angezeigt (Stop-Start)", "Stop-Start" in first, first)
        check("Zyklenzahl steht in der Zeile", "5 cycles" in first, first)
        check("Ø-Spitze als Prozent", "75%" in first, first)
        check("Unterbrechungszähler wird genannt", "2× interrupted" in first, first)
        check("Rückmeldungs-Durchschnitt wird genannt", "7.5" in first, first)
        check("zweite Session (Plateau) korrekt übersetzt", "Plateau" in second, second)
        check("Session ohne Rückmeldung zeigt keinen Durchschnitt",
              "avg feedback" not in second, second)
        check("Session ohne Unterbrechung zeigt keinen Zähler",
              "interrupted" not in second, second)

        browser.close()

    shutdown()
    harness.unlink(missing_ok=True)
    return check.report()


if __name__ == "__main__":
    sys.exit(main())
