"""Regressionstest für Offset-Korrektur, Abschnittswiederholung und Tastatur.

Diese drei gehören zusammen: Ein fremdes Skript passt fast nie exakt zum
eigenen Videoschnitt. Man markiert einen Abschnitt, lässt ihn wiederholen
und justiert per Taste nach, bis es sitzt. Fehlt eines davon, ist der
Arbeitsablauf nicht durchführbar - ohne Wiederholung müsste man nach jeder
Korrektur von Hand zurückspulen und hätte den Vergleich verloren.

Geprüft wird der Weg vom Bedienelement bis zum Aufruf ins Backend. Die
eigentliche Verschiebung passiert in Go, an genau EINER Stelle (siehe
ReportVideoPosition) - hier geht es darum, dass die Bedienung dort auch
ankommt.

Ausführen:  python3 cmd/gui-wails/frontend/test/offset_test.py
"""

import sys

from playwright.sync_api import sync_playwright

from _harness import Checker, app_stub, serve

PAGE = """<!doctype html><html><body><div id="root"></div>
<script type="module">
  import { initPlayback } from '/src/playback.js';
  window.__pb = initPlayback(document.querySelector('#root'));
  window.__ready = true;
</script></body></html>"""


def main():
    check = Checker()
    curve_js = ("const CURVE = [];\n"
                "for (let i = 0; i < 400; i++) CURVE.push({ atMs: i * 250, pos: i % 2 ? 5 : 95 });\n")
    base, shutdown = serve(curve_js + app_stub({
        "PickFunscriptFile": "async () => '/tmp/test.funscript'",
        "LoadFunscript": "async () => ({ path: '/tmp/test.funscript', actionCount: CURVE.length, "
                         "durationMs: 100000, videoPath: '', hasVideo: false })",
        "GetScriptCurve": "async () => CURVE.slice()",
        "GetHeatmap": "async n => Array.from({ length: n }, (_, i) => "
                      "({ atMs: i * 1000, intensity: 0.5 }))",
        "GetMarker": "async () => null",
        "VideoFileURL": "async () => ''",
        "GetSpeedHighlights": "async () => []",
        "ExportScriptHeatmapPNG": "async () => '/tmp/h.png'",
        "SavePlaybackProject": "async () => '/tmp/p.snp.json'",
        "EditCapSpeedRange": "async () => {}",
        "EditDeleteRange": "async () => {}",
        "SnapTimeMs": "async (t, fps) => t",
        "GetScriptOffset": "async () => 250",
        "SetScriptOffset": "async (ms) => { window.__calls.push(['offset', ms]); }",
    }))

    import pathlib
    harness = pathlib.Path(__file__).resolve().parent / "_offset_harness.html"
    harness.write_text(PAGE)

    with sync_playwright() as p:
        browser = p.chromium.launch()
        page = browser.new_page()
        page.on("dialog", lambda d: d.dismiss())
        page.on("pageerror", lambda e: print("   [pageerror]", e))
        page.goto(f"{base}/test/_offset_harness.html")
        page.wait_for_function("window.__ready === true")

        check("vor dem Laden: Offset-Zeile verborgen",
              page.locator("#pb-offset-row").evaluate("e => e.style.display") == "none")

        page.click("#pb-choose")
        page.wait_for_function("document.querySelector('#pb-offset-row').style.display === 'flex'",
                               timeout=5000)
        check("nach dem Laden: Offset-Zeile sichtbar", True)

        # Der gespeicherte Offset des Skripts muss übernommen werden - sonst
        # müsste er nach jedem Neuladen erneut gesucht werden.
        page.wait_for_function("document.querySelector('#pb-offset').value === '250'",
                               timeout=5000)
        check("gespeicherter Offset wird beim Laden übernommen",
              page.locator("#pb-offset").input_value() == "250")

        page.click("#pb-offset-plus")
        page.wait_for_function("window.__calls.some(c => c[0] === 'offset')")
        last = page.evaluate("window.__calls.filter(c => c[0] === 'offset').pop()")
        check("Plus-Knopf verschiebt um +50", last[1] == 300, str(last))

        page.click("#pb-offset-minus")
        page.click("#pb-offset-minus")
        last = page.evaluate("window.__calls.filter(c => c[0] === 'offset').pop()")
        check("Minus-Knopf verschiebt zurück", last[1] == 200, str(last))

        page.click("#pb-offset-reset")
        last = page.evaluate("window.__calls.filter(c => c[0] === 'offset').pop()")
        check("Zurücksetzen ergibt 0", last[1] == 0, str(last))

        # Tastatur: Feinschritt von 10ms, damit sich im Hören nachjustieren
        # lässt, ohne über das Ziel hinauszuschießen.
        page.keyboard.press("+")
        last = page.evaluate("window.__calls.filter(c => c[0] === 'offset').pop()")
        check("Taste + verschiebt um 10ms", last[1] == 10, str(last))
        page.keyboard.press("-")
        page.keyboard.press("-")
        last = page.evaluate("window.__calls.filter(c => c[0] === 'offset').pop()")
        check("Taste − verschiebt zurück", last[1] == -10, str(last))

        # Der Wert darf nicht unbegrenzt wachsen: ein Offset von Minuten
        # wäre keine Korrektur mehr, sondern ein Fehler.
        page.eval_on_selector("#pb-offset",
                              "e => { e.value = 99999; e.dispatchEvent(new Event('change')); }")
        last = page.evaluate("window.__calls.filter(c => c[0] === 'offset').pop()")
        check("Offset wird auf 10 Sekunden begrenzt", last[1] == 10000, str(last))

        # Wiederholung
        loop = page.locator("#pb-loop")
        check("Wiederholung ist zunächst aus", not loop.is_checked())
        page.keyboard.press("l")
        check("Taste L schaltet die Wiederholung um", loop.is_checked())
        page.keyboard.press("l")
        check("und wieder zurück", not loop.is_checked())

        # In Eingabefeldern dürfen die Kürzel NICHT greifen - sonst kann man
        # keine Zahl mehr eintippen.
        before = page.evaluate("window.__calls.length")
        page.click("#pb-offset")
        page.keyboard.press("+")
        check("Tastenkürzel greifen nicht im Eingabefeld",
              page.evaluate("window.__calls.length") == before)

        browser.close()

    shutdown()
    harness.unlink(missing_ok=True)
    return check.report()


if __name__ == "__main__":
    sys.exit(main())
