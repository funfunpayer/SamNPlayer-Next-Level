"""Regressionstest für die Quality-Doctor-Anzeige im Generator-Tab.

Lädt das echte src/generator.js in Headless-Chromium und ersetzt nur die
wailsjs-Bindings durch Stubs, damit kein laufender Wails-/Go-Prozess nötig
ist. window.__triggerEvent(name, data) simuliert ein Event, das sonst von
Go käme.

Hintergrund: die Anzeige hat das Urteil früher selbst aus qualityScore >= 0.5
berechnet und damit die harten Ausschlusskriterien des Quality Doctor
ignoriert (z.B. >40% der Videolänge ohne Tracking-Daten). Ein Skript mit
Score 0.63, das die Prüfung nicht bestanden hat, wurde als "within normal"
angezeigt. Dieser Test hält fest, dass das Urteil aus quality_passed kommt.

Ausführen:  python3 cmd/gui-wails/frontend/test/quality_display_test.py
Braucht:    pip install playwright  (Chromium muss installiert sein)
"""

import pathlib
import sys

from playwright.sync_api import sync_playwright

from _harness import app_stub, serve

FRONTEND = pathlib.Path(__file__).resolve().parents[1]

# Stubs für die beiden von generator.js importierten wailsjs-Module. Nur die
# Namen müssen existieren; aufgerufen wird davon im Test nichts.




PAGE = """<!doctype html><html><body><div id="root"></div>
<script type="module">
  import { initGenerator } from '/src/generator.js';
  initGenerator(document.querySelector('#root'), { loadScriptPath: () => {} });
  window.__ready = true;
</script></body></html>"""


def read_verdict(page, payload):
    page.evaluate("d => window.__triggerEvent('generate:done', d)", payload)
    box = page.locator("#gen-quality")
    return box.inner_text(), box.evaluate("e => e.style.display")


def main():
    base, shutdown = serve(app_stub())

    (FRONTEND / "test" / "_harness.html").write_text(PAGE)

    failures = []

    def check(name, cond, detail=""):
        print(("  OK   " if cond else "  FAIL ") + name + (f"  [{detail}]" if not cond else ""))
        if not cond:
            failures.append(name)

    with sync_playwright() as p:
        browser = p.chromium.launch()
        page = browser.new_page()
        page.on("dialog", lambda d: d.dismiss())
        page.goto(f"{base}/test/_harness.html")
        page.wait_for_function("window.__ready === true")

        # 1. Der eigentliche Regressionsfall: Score über 0.5, Prüfung aber
        #    wegen harter Ausschlusskriterien nicht bestanden.
        text, _ = read_verdict(page, {
            "path": "/tmp/a.funscript", "qualityScore": 0.63, "qualityPassed": False,
            "qualityWarnings": ["58% der Videolänge ohne Tracking-Daten"],
        })
        check("Score 0.63 + passed=false -> 'review recommended'", "review recommended" in text, text)
        check("Warnung wird angezeigt", "58%" in text, text)

        # 2. Sauberes Ergebnis bleibt sauber.
        text, _ = read_verdict(page, {
            "path": "/tmp/b.funscript", "qualityScore": 1.0, "qualityPassed": True,
            "qualityWarnings": [],
        })
        check("Score 1.0 + passed=true -> 'within normal'", "within normal" in text, text)

        # 3. Niedriger Score bleibt auffällig, auch wenn passed fehlt.
        text, _ = read_verdict(page, {
            "path": "/tmp/c.funscript", "qualityScore": 0.2, "qualityWarnings": [],
        })
        check("Score 0.2 without passed -> 'review recommended'", "review recommended" in text, text)

        # 4. Rückwärtskompatibilität: ältere Skripte ohne quality_passed
        #    werden weiter nach dem Score beurteilt.
        text, _ = read_verdict(page, {
            "path": "/tmp/d.funscript", "qualityScore": 0.8, "qualityWarnings": [],
        })
        check("Score 0.8 without passed -> 'within normal' (legacy)", "within normal" in text, text)

        # 5. Fortschrittsbalken: erscheint bei Prozentmeldungen, verschwindet
        #    wenn das Ergebnis da ist.
        page.evaluate("window.__triggerEvent('generate:percent', 0)")
        check("Balken erscheint bei 0 %",
              page.locator("#gen-progress-wrap").evaluate("e => e.style.display") == "block")
        page.evaluate("window.__triggerEvent('generate:progress', 'Go-native CSRT pipeline')")
        check("Verlaufs-Log enthält Progress-Zeile",
              "Go-native" in page.locator("#gen-log").inner_text(),
              page.locator("#gen-log").inner_text())
        page.evaluate("window.__triggerEvent('generate:percent', 42)")
        check("Balken auf 42 %",
              page.locator("#gen-progress-bar").evaluate("e => e.style.width") == "42%",
              page.locator("#gen-progress-bar").evaluate("e => e.style.width"))
        check("Prozenttext sichtbar", "42" in page.locator("#gen-progress-text").inner_text(),
              page.locator("#gen-progress-text").inner_text())
        page.evaluate("window.__triggerEvent('generate:percent', -1)")
        check("unknown total length -> note instead of percent",
              "could not determine" in page.locator("#gen-progress-text").inner_text(),
              page.locator("#gen-progress-text").inner_text())
        page.evaluate("window.__triggerEvent('generate:percent', 80)")
        read_verdict(page, {"path": "/tmp/f.funscript", "qualityScore": 1.0, "qualityPassed": True})
        check("Balken nach Abschluss ausgeblendet",
              page.locator("#gen-progress-wrap").evaluate("e => e.style.display") == "none",
              page.locator("#gen-progress-wrap").evaluate("e => e.style.display"))

        # 6. Ohne Quality-Daten bleibt die Box unsichtbar.
        _, display = read_verdict(page, {"path": "/tmp/e.funscript"})
        check("ohne qualityScore -> Box ausgeblendet", display == "none", display)

        browser.close()

    shutdown()
    (FRONTEND / "test" / "_harness.html").unlink(missing_ok=True)

    print(("FEHLGESCHLAGEN: " + ", ".join(failures)) if failures else "Alle Prüfungen bestanden.")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
