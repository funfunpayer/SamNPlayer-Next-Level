"""Regressionstest für den Intensitätsmesser (EIN Ring, EINE volle Bahn
für beide Kanäle) im Trainings-Tab.

Muss für BEIDE Ereignisquellen funktionieren: die einfache Technik/Kanal-
Form ("training:cycle", ein einzelner peakIntensity-Wert - der Kanal
kommt aus dem eigenen Formularfeld, nicht aus dem Event) und die
Multi-Phasen-Scripts ("training:scriptCycle", zwei getrennte Werte). Und
er muss beim Sessionende wieder auf 0 zurückgehen, sonst zeigt er nach
"Stop" weiter die letzte Intensität an.

Ausführen:  python3 cmd/gui-wails/frontend/test/training_intensity_meter_test.py
"""

import pathlib
import sys

from playwright.sync_api import sync_playwright

from _harness import Checker, app_stub, serve

FRONTEND = pathlib.Path(__file__).resolve().parents[1]

# style.css geladen (mit "?raw=1", weil der Harness den bloßen Pfad
# "/src/style.css" auf ein leeres JS-Modul umbiegt - für main.js's
# CSS-Import gedacht, siehe _harness.py). Andere Trainings-Tests brauchen
# das nicht (sie prüfen nur DOM-Klassen/-Attribute), aber die Puls-/
# Kontraktions-Animationen unten sind selbst CSS-Keyframes - ohne
# geladenes Stylesheet bliebe computed transform immer "none".
PAGE = """<!doctype html><html><head><link rel="stylesheet" href="/src/style.css?raw=1"></head>
<body><div id="root"></div>
<script type="module">
  import { initTraining } from '/src/training.js';
  initTraining(document.querySelector('#root'));
  window.__ready = true;
</script></body></html>"""


def main():
    check = Checker()
    base, shutdown = serve(app_stub({
        "StartTraining": "async () => {}",
        "StopTraining": "async () => {}",
        "TrainingHistory": "async () => []",
        "ListTrainingScripts": "async () => []",
    }))

    harness = FRONTEND / "test" / "_training_meter_harness.html"
    harness.write_text(PAGE)

    def ring_value(page, which):
        return page.locator(f"#tr-ring-value-{which}").inner_text()

    def ring_pulsing(page, which):
        """Das Glanz-Glühen einer einzelnen Bahn - eigene Klasse auf dem
        jeweiligen Kreis, unabhängig von der Kontraktion des ganzen Rings."""
        return page.locator(f"#tr-ring-{which}").evaluate("e => e.classList.contains('tr-pulsing')")

    def wrap_suck_pulsing(page):
        """Der GANZE Ring bewegt sich (kleiner/größer) nur für Sog - siehe
        updateRingSuction. Eine eigene Klasse, getrennt von der
        Vibrations-Bahn-eigenen tr-pulsing-Klasse oben."""
        return page.locator("#tr-ring-wrap").evaluate("e => e.classList.contains('tr-suck-pulsing')")

    def ring_fill_fraction(page, which):
        """Wie weit der Fortschrittsbogen gefüllt ist, aus stroke-dashoffset
        errechnet (0 = leer, 1 = voll) - die eigentliche visuelle Aussage
        des Rings, nicht nur der Textwert daneben."""
        return page.locator(f"#tr-ring-{which}").evaluate("""e => {
            const circumference = parseFloat(e.getAttribute('stroke-dasharray'));
            const offset = parseFloat(e.style.strokeDashoffset || e.getAttribute('stroke-dashoffset'));
            return 1 - (offset / circumference);
        }""")

    def sample_scales(page, selector, n, wait_ms):
        values = set()
        for _ in range(n):
            values.add(page.locator(selector).evaluate("e => getComputedStyle(e).transform"))
            page.wait_for_timeout(wait_ms)
        return values

    with sync_playwright() as p:
        browser = p.chromium.launch()
        page = browser.new_page()
        page.on("pageerror", lambda e: print("   [pageerror]", e))
        page.goto(f"{base}/test/_training_meter_harness.html")
        page.wait_for_function("window.__ready === true")

        check("EIN gemeinsames Ring-Widget im DOM (kein zweites)",
              page.locator("#tr-ring-wrap").count() == 1)
        check("vor dem Start: Vibrationsring zeigt 0%", ring_value(page, "vibration") == "0%")
        check("vor dem Start: Sog-Ring zeigt 0%", ring_value(page, "suction") == "0%")
        check("vor dem Start: Vibrationsbahn ist VOLL (Intensität = Bewegung, nicht Füllanteil)",
              ring_fill_fraction(page, "vibration") > 0.98,
              str(ring_fill_fraction(page, "vibration")))
        check("Legende nennt beide Achsen",
              "Vibration" in page.locator(".tr-ring-legend").inner_text()
              and "Suction" in page.locator(".tr-ring-legend").inner_text())

        # force=True siehe Kommentar beim zweiten #tr-start-Klick weiter
        # unten - der Ring animiert (Kippen/Glanz) schon ab dem ersten
        # Render, nicht erst nach einer Session.
        page.click("#tr-start", force=True)
        page.select_option("#tr-channel", "both")
        page.evaluate("window.__triggerEvent('training:cycle', "
                       "{ cycleIndex: 0, cyclesTotal: 3, peakIntensity: 0.7, holdMs: 1000 })")
        page.wait_for_function("document.querySelector('#tr-ring-value-vibration').textContent === '70%'")
        check("Kanal 'both': Vibrationsring zeigt die Intensität",
              ring_value(page, "vibration") == "70%", ring_value(page, "vibration"))
        check("Kanal 'both': Sog-Ring zeigt dieselbe Intensität",
              ring_value(page, "suction") == "70%", ring_value(page, "suction"))
        check("Kanal 'both': Vibrationsbahn bleibt VOLL (kein Füllanteil-Meter)",
              ring_fill_fraction(page, "vibration") > 0.98,
              str(ring_fill_fraction(page, "vibration")))
        check("laufende Session: Vibrationsbahn glüht", ring_pulsing(page, "vibration"))
        check("laufende Session: Sog-Bahn glüht", ring_pulsing(page, "suction"))

        # Vibration → Puls nach außen; Amplitude proportional zur Intensität.
        vib_amp = page.locator("#tr-ring-anim-vibration").evaluate(
            "e => getComputedStyle(e).getPropertyValue('--vib-amp')").strip()
        check("Vibration-Amplitude proportional zur Intensität (70% -> ~0.098)",
              vib_amp == "0.098", vib_amp)

        vib_scales = sample_scales(page, "#tr-ring-anim-vibration", 6, 100)
        check("Vibrationsbahn pulsiert tatsächlich (mehrere Skalierungswerte über Zeit)",
              len(vib_scales) > 1, str(vib_scales))
        check("Vibrationsbahn pulst zeitweise über volle Größe hinaus (nach außen)",
              any("1, 0, 0, 1" not in m for m in vib_scales), str(vib_scales))

        # Sog → ganzer Ring zusammen / auseinander.
        suc_amp = page.locator("#tr-ring-wrap").evaluate(
            "e => getComputedStyle(e).getPropertyValue('--suc-amp')").strip()
        check("Sog-Amplitude proportional zur Intensität (70% -> ~0.126)",
              suc_amp == "0.126", suc_amp)
        check("laufende Session: der GANZE Ring atmet (Sog aktiv)", wrap_suck_pulsing(page))

        wrap_scales = sample_scales(page, "#tr-ring-wrap", 8, 140)
        check("der ganze Ring bewegt sich tatsächlich (mehrere Skalierungswerte über Zeit)",
              len(wrap_scales) > 1, str(wrap_scales))
        check("der ganze Ring wird zeitweise kleiner/größer als Ruhemaß (Sog)",
              any("1, 0, 0, 1" not in m for m in wrap_scales), str(wrap_scales))

        page.select_option("#tr-channel", "vibration")
        page.evaluate("window.__triggerEvent('training:cycle', "
                       "{ cycleIndex: 1, cyclesTotal: 3, peakIntensity: 0.5, holdMs: 1000 })")
        page.wait_for_function("document.querySelector('#tr-ring-value-vibration').textContent === '50%'")
        check("Kanal 'vibration': nur der Vibrationsring bewegt sich",
              ring_value(page, "vibration") == "50%", ring_value(page, "vibration"))
        check("Kanal 'vibration': Sog-Ring geht auf 0% zurück",
              ring_value(page, "suction") == "0%", ring_value(page, "suction"))
        check("Kanal 'vibration': Sog-Bahn glüht nicht mehr",
              not ring_pulsing(page, "suction"))
        check("Kanal 'vibration': der ganze Ring atmet NICHT mehr (kein Sog aktiv)",
              not wrap_suck_pulsing(page))
        check("Kanal 'vibration': Vibrationsbahn pulsiert weiter (noch aktiv)",
              ring_pulsing(page, "vibration"))

        page.evaluate("window.__triggerEvent('training:done')")
        page.wait_for_function("document.querySelector('#tr-ring-value-vibration').textContent === '0%'")
        check("nach Sessionende: Vibrationsring zurückgesetzt",
              ring_value(page, "vibration") == "0%")
        check("nach Sessionende: Sog-Ring zurückgesetzt",
              ring_value(page, "suction") == "0%")
        check("nach Sessionende: keine Bahn glüht mehr", not ring_pulsing(page, "vibration"))
        check("nach Sessionende: der ganze Ring atmet nicht mehr", not wrap_suck_pulsing(page))

        page.click("#tr-start", force=True)
        page.evaluate("window.__triggerEvent('training:scriptCycle', {"
                       "phaseIndex: 0, phaseName: 'Wave', phasesTotal: 1,"
                       "repeatIndex: 0, repeatsTotal: 1, vibrationPeak: 0.3, suctionPeak: 0.9,"
                       "restMs: 1000, stoppedByUser: false, arousalBefore: 0 })")
        page.wait_for_function("document.querySelector('#tr-ring-value-suction').textContent === '90%'")
        check("Script-Event: Vibrationsring übernimmt vibrationPeak",
              ring_value(page, "vibration") == "30%", ring_value(page, "vibration"))
        check("Script-Event: Sog-Ring übernimmt suctionPeak",
              ring_value(page, "suction") == "90%", ring_value(page, "suction"))
        check("Script-Event: beide Bahnen glühen, wenn beide > 0",
              ring_pulsing(page, "vibration") and ring_pulsing(page, "suction"))
        check("Script-Event: der ganze Ring atmet (Sog aktiv)", wrap_suck_pulsing(page))
        vib_amp_s = float(page.locator("#tr-ring-anim-vibration").evaluate(
            "e => getComputedStyle(e).getPropertyValue('--vib-amp')").strip() or "0")
        suc_amp_s = float(page.locator("#tr-ring-wrap").evaluate(
            "e => getComputedStyle(e).getPropertyValue('--suc-amp')").strip() or "0")
        check("Script-Event: Sog-Amplitude stärker als Vibrations-Amplitude (0.9 > 0.3)",
              suc_amp_s > vib_amp_s,
              f"suction={suc_amp_s} vibration={vib_amp_s}")
        check("Script-Event: beide Bahnen bleiben VOLL",
              ring_fill_fraction(page, "vibration") > 0.98
              and ring_fill_fraction(page, "suction") > 0.98)

        # Live levels (training:levels) override stale cycle-peak summaries.
        page.evaluate("window.__triggerEvent('training:levels', {vibration:0.55, suction:0.25})")
        page.wait_for_function("document.querySelector('#tr-ring-value-vibration').textContent === '55%'")
        check("Live-Levels: Vibrationsring folgt training:levels",
              ring_value(page, "vibration") == "55%", ring_value(page, "vibration"))
        check("Live-Levels: Sog-Ring folgt training:levels",
              ring_value(page, "suction") == "25%", ring_value(page, "suction"))
        # Cycle peak must not overwrite live meter while levels are active
        page.evaluate("window.__triggerEvent('training:scriptCycle', {"
                      "phaseName:'X',phaseIndex:0,phasesTotal:1,repeatIndex:0,repeatsTotal:1,"
                      "vibrationPeak:0.9,suctionPeak:0.9,restMs:100})")
        page.wait_for_timeout(80)
        check("Live-Levels: Peak-Event überschreibt den Ring nicht",
              ring_value(page, "vibration") == "55%", ring_value(page, "vibration"))

        browser.close()

    shutdown()
    harness.unlink(missing_ok=True)
    return check.report()


if __name__ == "__main__":
    sys.exit(main())
