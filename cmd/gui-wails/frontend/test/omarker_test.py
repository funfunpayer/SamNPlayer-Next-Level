"""Regressionstest für den O-Marker-Editor (docs/NEXT.md Priorität 7).

O-Marker sind authored data, die im Skript selbst gespeichert werden -
anders als die bestehende Markierung (weiße Umrandung), die nur lokal für
die automatische Extended-O-Erkennung beim Abspielen dient. Geprüft wird:
Abschnitt ziehen -> als O-Marker (primär/sekundär) übernehmen -> Liste
zeigt ihn -> SaveOMarkers bekommt den richtigen Wert -> Entfernen räumt
wieder auf.

Ausführen:  python3 cmd/gui-wails/frontend/test/omarker_test.py
"""

import pathlib
import sys

from playwright.sync_api import sync_playwright

from _harness import Checker, app_stub, serve

PAGE = """<!doctype html><html><body><div id="root"></div>
<script type="module">
  import { initPlayback } from '/src/playback.js';
  window.__calls = [];
  window.__pb = initPlayback(document.querySelector('#root'));
  window.__ready = true;
</script></body></html>"""


def main():
    check = Checker()
    base, shutdown = serve(app_stub({
        "PickFunscriptFile": "async () => '/tmp/test.funscript'",
        "LoadFunscript": "async () => ({ path: '/tmp/test.funscript', actionCount: 2, "
                         "durationMs: 100000, videoPath: '', hasVideo: false })",
        "GetScriptCurve": "async () => [{ atMs: 0, pos: 0 }, { atMs: 100000, pos: 100 }]",
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
        "GetScriptOffset": "async () => 0",
        "GetOMarkers": "async () => []",
        "SaveOMarkers": "async (path, markers) => { window.__calls.push(['saveOMarkers', markers]); }",
    }))

    harness = pathlib.Path(__file__).resolve().parent / "_omarker_harness.html"
    harness.write_text(PAGE)

    with sync_playwright() as p:
        browser = p.chromium.launch()
        page = browser.new_page()
        page.on("dialog", lambda d: d.dismiss())
        page.on("pageerror", lambda e: print("   [pageerror]", e))
        page.goto(f"{base}/test/_omarker_harness.html")
        page.wait_for_function("window.__ready === true")

        check("vor dem Laden: Hinweis/Add-Zeile verborgen",
              page.locator("#pb-omarker-hint").evaluate("e => e.style.display") == "none"
              and page.locator("#pb-omarker-add-row").evaluate("e => e.style.display") == "none")

        page.click("#pb-choose")
        page.wait_for_function(
            "document.querySelector('#pb-omarker-add-row').style.display === 'flex'", timeout=5000)
        page.locator("#pb-omarker-details").evaluate("el => { el.open = true }")
        check("nach dem Laden: Add-Zeile sichtbar", True)
        check("ohne Live-Markierung: Übernehmen-Knopf gesperrt",
              page.locator("#pb-omarker-add").is_disabled())
        check("Sekundär-Intensität zunächst verborgen (Kind=primär)",
              page.locator("#pb-omarker-intensity-row").evaluate("e => e.style.display") == "none")

        # Abschnitt auf der Heatmap-Leiste ziehen (10%-40% der Breite =
        # 10s-40s bei 100s Gesamtdauer).
        page.locator("#pb-heatmap").scroll_into_view_if_needed()  # layout may push it below the fold
        box = page.locator("#pb-heatmap").bounding_box()
        y = box["y"] + box["height"] / 2
        x_start = box["x"] + box["width"] * 0.10
        x_end = box["x"] + box["width"] * 0.40
        page.mouse.move(x_start, y)
        page.mouse.down()
        page.mouse.move(x_end, y, steps=5)
        page.mouse.up()

        check("nach dem Ziehen: Übernehmen-Knopf frei",
              not page.locator("#pb-omarker-add").is_disabled())

        # Primären Marker übernehmen (Standardauswahl).
        page.click("#pb-omarker-add")
        page.wait_for_function("window.__calls.some(c => c[0] === 'saveOMarkers')", timeout=5000)
        saved = page.evaluate("window.__calls.filter(c => c[0] === 'saveOMarkers').pop()[1]")
        check("primärer Marker wird mit korrektem Kind gespeichert",
              len(saved) == 1 and saved[0]["kind"] == "primary" and saved[0]["intensity"] == 1.0,
              str(saved))
        check("primärer Marker liegt im gezogenen Bereich (~10s-40s)",
              8000 <= saved[0]["startMs"] <= 12000 and 38000 <= saved[0]["endMs"] <= 42000,
              str(saved))
        check("Liste zeigt den neuen Marker",
              "Primary" in page.locator("#pb-omarker-list").inner_text())

        # Sekundären Marker mit eigener Intensität hinzufügen - Kind
        # umschalten muss die Intensitätszeile einblenden.
        page.select_option("#pb-omarker-kind", "secondary")
        check("Sekundär-Intensität sichtbar nach Kindwechsel",
              page.locator("#pb-omarker-intensity-row").evaluate("e => e.style.display") == "flex")
        page.evaluate("""() => {
          const el = document.querySelector('#pb-omarker-intensity');
          el.value = '0.3';
          el.dispatchEvent(new Event('input', { bubbles: true }));
          el.dispatchEvent(new Event('change', { bubbles: true }));
        }""")
        page.click("#pb-omarker-add")
        page.wait_for_function("window.__calls.filter(c => c[0] === 'saveOMarkers').length === 2",
                               timeout=5000)
        saved2 = page.evaluate("window.__calls.filter(c => c[0] === 'saveOMarkers').pop()[1]")
        check("zweiter (sekundärer) Marker kommt mit eigener Intensität an",
              len(saved2) == 2 and saved2[1]["kind"] == "secondary"
              and abs(saved2[1]["intensity"] - 0.3) < 1e-6,
              str(saved2))

        # Ersten Marker wieder entfernen.
        page.locator("#pb-omarker-list button", has_text="Remove").first.click()
        page.wait_for_function("window.__calls.filter(c => c[0] === 'saveOMarkers').length === 3",
                               timeout=5000)
        saved3 = page.evaluate("window.__calls.filter(c => c[0] === 'saveOMarkers').pop()[1]")
        check("Entfernen lässt nur den verbleibenden (sekundären) Marker übrig",
              len(saved3) == 1 and saved3[0]["kind"] == "secondary", str(saved3))

        browser.close()

    shutdown()
    harness.unlink(missing_ok=True)
    return check.report()


if __name__ == "__main__":
    sys.exit(main())
