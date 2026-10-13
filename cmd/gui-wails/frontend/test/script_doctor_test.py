"""Regressionstest für den "Skript prüfen (Script Doctor)"-Knopf in der
Wiedergabe.

Bindung und Go/Python-Kette existieren (generator.ScriptQuality,
generate_funscript.py --script-quality, siehe script_quality_test.py dort),
hier wird nur geprüft, dass die Oberfläche das Ergebnis tatsächlich zeigt:
bestanden -> grüner Rahmen mit Prozentwert, nicht bestanden -> roter Rahmen
mit Warnungen, und in beiden Fällen der Hinweis, dass die Schätzung ohne
Video auskommt (trackingbasierte Prüfungen fehlen dort).

Ausführen:  python3 cmd/gui-wails/frontend/test/script_doctor_test.py
"""

import pathlib
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
    base, shutdown = serve(app_stub({
        "PickFunscriptFile": "async () => '/tmp/test.funscript'",
        "LoadFunscript": "async () => ({ path: '/tmp/test.funscript', actionCount: 20, "
                         "durationMs: 10000, videoPath: '', hasVideo: false })",
        "GetScriptCurve": "async () => [{ atMs: 0, pos: 20 }, { atMs: 10000, pos: 80 }]",
        "GetHeatmap": "async n => Array.from({ length: n }, (_, i) => ({ atMs: i * 100, intensity: 0.5 }))",
        "GetMarker": "async () => null",
        "VideoFileURL": "async () => ''",
        "GetSpeedHighlights": "async () => []",
        "ExportScriptHeatmapPNG": "async () => '/tmp/h.png'",
        "SavePlaybackProject": "async () => '/tmp/p.snp.json'",
        "EditCapSpeedRange": "async () => {}",
        "EditDeleteRange": "async () => {}",
        "SnapTimeMs": "async (t, fps) => t",
        "GetScriptOffset": "async () => 0",
        "GetOMarkers": "async () => []",
        "SaveOMarkers": "async () => {}",
        "ScriptQuality": "async () => { if (window.__qualityError) throw new Error(window.__qualityError); "
                        "return window.__quality; }",
    }))

    harness = pathlib.Path(__file__).resolve().parent / "_script_doctor_harness.html"
    harness.write_text(PAGE)

    with sync_playwright() as p:
        browser = p.chromium.launch()
        page = browser.new_page()
        page.on("pageerror", lambda e: print("   [pageerror]", e))
        page.goto(f"{base}/test/_script_doctor_harness.html")
        page.wait_for_function("window.__ready === true")

        check("vor dem Laden: Knopf verborgen",
              page.locator("#pb-script-doctor-row").evaluate("e => e.style.display") == "none")

        page.click("#pb-choose")
        page.wait_for_function(
            "document.querySelector('#pb-script-doctor-row').style.display === 'flex'", timeout=5000)
        check("quality details present", page.locator("#pb-quality-details").count() == 1)
        check("quality details closed by default",
              page.locator("#pb-quality-details").evaluate("el => !el.open"))
        page.locator("#pb-quality-details").evaluate("el => { el.open = true }")
        check("nach dem Laden: Knopf sichtbar", True)

        # Bestanden.
        page.evaluate("window.__quality = { score: 0.9, passed: true, warnings: [], "
                      "estimatedFromScriptOnly: true }")
        page.click("#pb-script-doctor")
        page.wait_for_function(
            "document.querySelector('#pb-script-doctor-result').textContent.includes('90%')",
            timeout=5000)
        check("check opens quality details",
              page.locator("#pb-quality-details").evaluate("el => el.open"))
        result = page.locator("#pb-script-doctor-result")
        check("bestanden: Prozentwert sichtbar", "90%" in result.inner_text())
        check("passed: 'within normal' in text", "within normal" in result.inner_text())
        check("passed: estimate-without-video note",
              "without video" in result.inner_text())
        border = result.evaluate("e => getComputedStyle(e).borderColor")
        check("bestanden: grüner Rahmen (nicht rot)", "220, 77, 77" not in border, border)

        # Nicht bestanden, mit Warnungen.
        page.evaluate("window.__quality = { score: 0.3, passed: false, "
                      "warnings: ['3 Positionswerte außerhalb 0-100'], "
                      "estimatedFromScriptOnly: true }")
        page.click("#pb-script-doctor")
        page.wait_for_function(
            "document.querySelector('#pb-script-doctor-result').textContent.includes('30%')",
            timeout=5000)
        text = result.inner_text()
        check("nicht bestanden: Prozentwert sichtbar", "30%" in text)
        check("failed: 'review recommended' in text", "review recommended" in text)
        check("nicht bestanden: Warnung wird angezeigt",
              "außerhalb 0-100" in text, text)

        # Fehler bei der Prüfung darf nicht stillschweigend verschwinden.
        page.evaluate("window.__qualityError = 'no script path'")
        page.click("#pb-script-doctor")
        page.wait_for_function(
            "document.querySelector('#pb-script-doctor-status').textContent.includes('no script path')",
            timeout=5000)
        check("Fehler bei der Prüfung wird angezeigt",
              "no script path" in page.locator("#pb-script-doctor-status").inner_text())

        browser.close()

    shutdown()
    harness.unlink(missing_ok=True)
    return check.report()


if __name__ == "__main__":
    sys.exit(main())
