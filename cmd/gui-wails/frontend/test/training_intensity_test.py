"""Regressionstest für die Profil-Intensität im Trainings-Tab: ein
vorhandenes Profil (built-in oder eigenes) lässt sich jetzt insgesamt
sanfter/stärker fahren, ohne es neu zu bauen.

  * eine explizite Stufe (Gentle ×0.8 / Standard ×1 / Intense ×1.2)
  * ein optionaler automatischer Schubser aus der History DIESES Profils
    (vorher nur Vorschlagstext, siehe docs/TRAINING_MODE_RESEARCH.md
    Vorschlag A - jetzt tatsächlich angewendet, mit Opt-out-Checkbox)
  * die Plan-Vorschau zeigt die TATSÄCHLICH laufende (skalierte) Kurve,
    nicht die nominale
  * StartTraining bekommt den kombinierten Faktor mitgeschickt

Ausführen:  python3 cmd/gui-wails/frontend/test/training_intensity_test.py
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

# "gentle-target": letzte Session hatte Abbrüche -> History-Schubser <1.
# "boost-target": zwei saubere Sessions in Folge -> History-Schubser >1.
# "plain-target": keine passende History -> kein Schubser.
HISTORY = [
    {"fileName": "a", "startedAt": "2026-09-22T10:00:00Z", "technique": "gentle-target", "channel": "script",
     "cyclesCompleted": 4, "cyclesStoppedEarly": 2, "meanPeakIntensity": 0.7,
     "meanReachedPeakAfterMs": 0, "meanArousalReported": 0, "arousalReportsCount": 0},
    {"fileName": "b", "startedAt": "2026-09-21T10:00:00Z", "technique": "boost-target", "channel": "script",
     "cyclesCompleted": 6, "cyclesStoppedEarly": 0, "meanPeakIntensity": 0.6,
     "meanReachedPeakAfterMs": 0, "meanArousalReported": 0, "arousalReportsCount": 0},
    {"fileName": "c", "startedAt": "2026-09-20T10:00:00Z", "technique": "boost-target", "channel": "script",
     "cyclesCompleted": 6, "cyclesStoppedEarly": 0, "meanPeakIntensity": 0.6,
     "meanReachedPeakAfterMs": 0, "meanArousalReported": 0, "arousalReportsCount": 0},
]


def main():
    check = Checker()
    base, shutdown = serve(app_stub({
        "StartTraining": "async (req) => { window.__calls.push(['start', req]); }",
        "StopTraining": "async () => {}",
        "TrainingHistory": (f"async () => {HISTORY!r}"
                             .replace("'", '"').replace("False", "false").replace("True", "true")),
        "ListTrainingScripts": ("async () => ["
                                 "{name: 'gentle-target', description: 'has early stops', custom: false}, "
                                 "{name: 'boost-target', description: 'clean streak', custom: false}, "
                                 "{name: 'plain-target', description: 'no history', custom: false}]"),
        "TrainingScriptPreview": ("async (name, factor) => { window.__calls.push(['preview', name, factor]); "
                                   "const f = factor || 1; return { "
                                   "vibration: [{atMs:0, level:0.2*f}, {atMs:1000, level:0.8*f}], "
                                   "suction: [], totalMs: 1000, phaseMarkers: [{atMs:0, name:'p'}], "
                                   "hasRandomJitter: false }; }"),
    }))

    harness = FRONTEND / "test" / "_training_intensity_harness.html"
    harness.write_text(PAGE)

    with sync_playwright() as p:
        browser = p.chromium.launch()
        page = browser.new_page()
        page.on("pageerror", lambda e: print("   [pageerror]", e))
        page.goto(f"{base}/test/_training_intensity_harness.html")
        page.wait_for_function("window.__ready === true")
        page.wait_for_function("document.querySelectorAll('#tr-script option').length >= 4")

        def visible(selector):
            return page.locator(selector).evaluate("e => getComputedStyle(e).display") != "none"

        # --- kein Script gewählt: Intensitäts-Steuerung versteckt --------
        check("ohne Script: Intensitäts-Zeile versteckt", not visible("#tr-intensity-row"))
        check("ohne Script: Auto-Anpassen-Zeile versteckt", not visible("#tr-history-autoadjust-row"))

        # --- Script ohne passende History: Steuerung sichtbar, kein Hinweis
        page.select_option("#tr-script", "plain-target")
        page.wait_for_function("document.querySelector('#tr-script-preview').style.display !== 'none'")
        check("mit Script: Intensitäts-Zeile sichtbar", visible("#tr-intensity-row"))
        check("mit Script: Auto-Anpassen-Zeile sichtbar", visible("#tr-history-autoadjust-row"))
        check("ohne passende History: kein Intensitäts-Hinweis", not visible("#tr-intensity-note"))

        # Stufe auf "Intense" (×1.2) -> Vorschau lädt neu, Hinweis erscheint.
        page.select_option("#tr-intensity", "1.2")
        page.wait_for_function("document.querySelector('#tr-intensity-note').style.display !== 'none'")
        note = page.locator("#tr-intensity-note").inner_text()
        check("Stufen-Hinweis nennt 'Intense'", "Intense" in note, note)
        check("Stufen-Hinweis nennt den Faktor ×1.20", "×1.20" in note, note)

        # Die Vorschau muss die TATSÄCHLICH laufende (skalierte) Kurve
        # zeigen - der Mock bekommt den Faktor direkt mitgeschickt, statt
        # das nur aus dem gezeichneten Pfad zurückzurechnen.
        last_preview_call = page.evaluate("window.__calls.filter(c => c[0] === 'preview').pop()")
        check("Vorschau-Anfrage bekam den Faktor 1.2",
              abs(last_preview_call[2] - 1.2) < 0.001, last_preview_call)

        # --- Script mit Abbrüchen in der History: automatischer Schubser -
        page.select_option("#tr-intensity", "1")  # zurück auf Standard
        page.select_option("#tr-script", "gentle-target")
        page.wait_for_function("document.querySelector('#tr-intensity-note').style.display !== 'none'")
        note2 = page.locator("#tr-intensity-note").inner_text()
        check("History-Schubser (Abbrüche) senkt den Faktor auf ×0.85", "×0.85" in note2, note2)
        check("Hinweis nennt 'history nudge'", "history nudge" in note2, note2)

        # Auto-Anpassen abschalten -> zurück auf Faktor 1 (Standard-Stufe),
        # kein Hinweis mehr - der History-Schubser lässt sich abschalten.
        page.uncheck("#tr-history-autoadjust")
        page.wait_for_function("document.querySelector('#tr-intensity-note').style.display === 'none'")

        # --- sauberer Streak: Faktor ×1.1 ---------------------------------
        page.check("#tr-history-autoadjust")
        page.select_option("#tr-script", "boost-target")
        page.wait_for_function("document.querySelector('#tr-intensity-note').style.display !== 'none'")
        note3 = page.locator("#tr-intensity-note").inner_text()
        check("sauberer Streak hebt den Faktor auf ×1.10", "×1.10" in note3, note3)

        # --- StartTraining bekommt den kombinierten Faktor ----------------
        page.click("#tr-start")
        page.wait_for_function("window.__calls.some(c => c[0] === 'start')")
        start_req = page.evaluate("window.__calls.find(c => c[0] === 'start')[1]")
        check("StartTraining bekommt intensityFactor ~1.1",
              abs(start_req["intensityFactor"] - 1.1) < 0.001, start_req)
        check("StartTraining bekommt weiterhin den Script-Namen",
              start_req["scriptName"] == "boost-target", start_req)

        browser.close()

    shutdown()
    harness.unlink(missing_ok=True)
    return check.report()


if __name__ == "__main__":
    sys.exit(main())
