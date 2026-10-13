"""Regressionstest für die Bedienelemente des Trainingsmodus.

Zwei Dinge, die leicht kaputtgehen und dann still nicht funktionieren:

  * "Jetzt unterbrechen" und die Rückmeldeskala dürfen nur bei laufender
    Session bedienbar sein. Ein Knopf, der ins Leere greift, ist schlimmer
    als keiner - gerade hier, wo man ihn im Ernstfall schnell trifft.

  * Die gemeldete Zahl muss unverändert ankommen. Eine stillschweigende
    Verschiebung um eins würde die Regelung verfälschen, ohne aufzufallen.

Ausführen:  python3 cmd/gui-wails/frontend/test/training_display_test.py
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


def main():
    base, shutdown = serve(app_stub({
        "StartTraining": "async () => { window.__calls.push(['start']); }",
        "StopTraining": "async () => { window.__calls.push(['stopSession']); }",
        "StopTrainingCycle": "async () => { window.__calls.push(['stopCycle']); }",
        "ReportArousal": "async (n) => { window.__calls.push(['arousal', n]); }",
    }))
    (FRONTEND / "test" / "_training_harness.html").write_text(PAGE)

    failures = []

    def check(name, cond, detail=""):
        print(("  OK   " if cond else "  FAIL ") + name + (f"  [{detail}]" if not cond else ""))
        if not cond:
            failures.append(name)

    with sync_playwright() as p:
        browser = p.chromium.launch()
        page = browser.new_page()
        page.on("dialog", lambda d: d.dismiss())
        page.on("pageerror", lambda e: print("   [pageerror]", e))
        page.goto(f"{base}/test/_training_harness.html")
        page.wait_for_function("window.__ready === true")

        disabled = lambda sel: page.locator(sel).evaluate("e => e.disabled")

        check("vor dem Start: Unterbrechen gesperrt", disabled("#tr-pause"))
        check("vor dem Start: Rückmeldung gesperrt", disabled("#tr-arousal"))

        page.click("#tr-start")
        page.wait_for_function("document.querySelector('#tr-pause').disabled === false",
                               timeout=5000)
        check("nach dem Start: Unterbrechen frei", not disabled("#tr-pause"))
        check("nach dem Start: Rückmeldung frei", not disabled("#tr-arousal"))

        check("Skala hat zehn Stufen",
              page.locator("#tr-arousal-buttons button").count() == 10,
              str(page.locator("#tr-arousal-buttons button").count()))
        check("Pixel-Paar-Stage ist da",
              page.locator("#tr-pixel-stage .tr-mosaic-stack").count() == 1)
        check("You/Partner-Labels",
              "Clip" in page.locator(".tr-pixel-labels").inner_text()
              and ("Curve" in page.locator(".tr-pixel-labels").inner_text()
                   or "Partner" in page.locator(".tr-pixel-labels").inner_text()
                   or "You" in page.locator(".tr-pixel-labels").inner_text()))
        check("Clip-Pixel-Strip geladen",
              page.locator("#tr-pixel-stage .tr-mosaic-clip").count() == 1)
        check("Intensitätsring sitzt im Motion-Card (nicht darunter gestapelt)",
              page.locator("#tr-pixel-stage #tr-ring-slot #tr-ring-wrap").count() == 1)
        check("kein zweites Ring-Widget außerhalb der Card",
              page.locator("#tr-ring-wrap").count() == 1)
        check("Zielstufe 7 markiert",
              page.locator("#tr-arousal-buttons button.is-target").count() == 1)

        page.click("#tr-pause")
        page.wait_for_function("window.__calls.some(c => c[0] === 'stopCycle')")
        check("Unterbrechen beendet NICHT die Session",
              not page.evaluate("window.__calls.some(c => c[0] === 'stopSession')"))

        page.locator("#tr-arousal-buttons button", has_text="9").first.click()
        page.wait_for_function("window.__calls.some(c => c[0] === 'arousal')")
        reported = page.evaluate("window.__calls.filter(c => c[0] === 'arousal').pop()")
        check("gemeldeter Wert kommt unverändert an", reported[1] == 9, str(reported))
        check("Rückmeldung wird bestätigt",
              "9" in page.locator("#tr-arousal-status").inner_text(),
              page.locator("#tr-arousal-status").inner_text())
        check("Pixel-Stage zeigt Feedback",
              "9" in page.locator("#tr-pixel-fb").inner_text(),
              page.locator("#tr-pixel-fb").inner_text())
        check("gewählter Feedback-Button markiert",
              page.locator("#tr-arousal-buttons button.is-picked").count() == 1)

        # Live cycle intensity moves the pair (closer + warmer).
        page.evaluate(
            "window.__triggerEvent('training:cycle', "
            "{cycleIndex:0,cyclesTotal:5,peakIntensity:0.8,holdMs:3000})")
        page.wait_for_function(
            "document.querySelector('#tr-pixel-pct')?.textContent.includes('80')")
        check("Zyklus-Intensität auf Pixel-Paar",
              "80%" in page.locator("#tr-pixel-pct").inner_text(),
              page.locator("#tr-pixel-pct").inner_text())

        page.evaluate("window.__triggerEvent('training:done')")
        page.wait_for_function("document.querySelector('#tr-arousal').disabled === true",
                               timeout=5000)
        check("nach Sessionende wieder gesperrt", disabled("#tr-arousal"))
        check("Pixel-Paar nach Ende idle",
              "Idle" in page.locator("#tr-pixel-pct").inner_text(),
              page.locator("#tr-pixel-pct").inner_text())

        browser.close()

    shutdown()
    (FRONTEND / "test" / "_training_harness.html").unlink(missing_ok=True)

    print(("FEHLGESCHLAGEN: " + ", ".join(failures)) if failures else "Alle Prüfungen bestanden.")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
