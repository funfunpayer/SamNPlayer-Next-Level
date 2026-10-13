"""O-Zone-Vorschlag und Polaritaetsknoepfe in der Playback-UI."""
import pathlib
import sys
from playwright.sync_api import sync_playwright
from _harness import Checker, app_stub, serve

PAGE = """<!doctype html><html><body><div id=\"root\"></div>
<script type=\"module\">
  import { initPlayback } from '/src/playback.js';
  import { enhancePlaybackOZone } from '/src/ozone_ui.js';
  window.__pb = initPlayback(document.querySelector('#root'));
  enhancePlaybackOZone(document.querySelector('#root'));
  window.__ready = true;
</script></body></html>"""

def main():
    check = Checker()
    base, shutdown = serve(app_stub({
        "PickFunscriptFile": "async () => '/tmp/test.funscript'",
        "LoadFunscript": "async () => ({ path: '/tmp/test.funscript', actionCount: 20, durationMs: 10000, videoPath: '', hasVideo: false })",
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
        "SaveOMarkers": "async (path, markers) => { window.__calls.push(['saveOMarkers', markers]); }",
        "ApplySuggestedOZone": "async () => { window.__calls.push(['applyOzone']); return { ok: true, startMs: 8500, endMs: 9300, reason: 'test' }; }",
        "SuggestPolarity": "async () => ({ suggestInvert: false, firstHalfMean: 55, reason: 'keine klare Richtung' })",
    }))
    harness = pathlib.Path(__file__).resolve().parent / "_ozone_harness.html"
    harness.write_text(PAGE)
    with sync_playwright() as p:
        browser = p.chromium.launch()
        page = browser.new_page()
        page.on("dialog", lambda d: d.dismiss())
        page.goto(f"{base}/test/_ozone_harness.html")
        page.wait_for_function("window.__ready === true")
        page.click("#pb-choose")
        page.wait_for_function("document.querySelector('#pb-omarker-add-row').style.display === 'flex'", timeout=5000)
        page.locator("#pb-omarker-details").evaluate("el => { el.open = true }")
        check("O-Zone-Knopf existiert", page.locator("#pb-ozone-suggest").count() == 1)
        page.click("#pb-ozone-suggest")
        page.wait_for_function("window.__calls.some(c => c[0] === 'applyOzone')", timeout=5000)
        check("ApplySuggestedOZone wird aufgerufen", True)
        check("Status zeigt Begruendung", "test" in page.locator("#pb-ozone-status").inner_text())
        page.click("#pb-polarity-invert")
        page.wait_for_timeout(200)
        txt = page.locator("#pb-ozone-status").inner_text().lower()
        check("Polaritaet ohne Invert zeigt Begruendung", "richtung" in txt)
        # Taste O: setzt primaeren O-Marker ab currentPosMs (0 ohne Video)
        page.keyboard.press("o")
        page.wait_for_function("window.__calls.some(c => c[0] === 'saveOMarkers')", timeout=5000)
        saved = page.evaluate("window.__calls.filter(c => c[0] === 'saveOMarkers').pop()[1]")
        check("Taste O speichert O-Marker", isinstance(saved, list) and len(saved) >= 1)
        check("Taste O primary 4s", saved[-1].get("kind") == "primary" and saved[-1].get("endMs", 0) - saved[-1].get("startMs", 0) == 4000)
        browser.close()
    shutdown()
    harness.unlink(missing_ok=True)
    return check.report()

if __name__ == "__main__":
    sys.exit(main())
