"""Regressionstest: ScriptChapters() wird tatsächlich in der Wiedergabe
angezeigt.

Die Bindung (motionx.Chapter, ScriptChapters) existierte bereits, wurde
aber von keiner Stelle im Frontend aufgerufen - eine Funktion mit Backend
und Test, aber ohne angeschlossene Oberfläche. Dieser Test hält fest, dass
das Laden eines Skripts die Kapitelliste in #pb-analysis anzeigt (neben der
schon bestehenden AnalyzeScript-Zusammenfassung, nicht anstelle davon).

Ausführen:  python3 cmd/gui-wails/frontend/test/chapters_display_test.py
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
        "LoadFunscript": "async () => ({ path: '/tmp/test.funscript', actionCount: 5, "
                         "durationMs: 10000, videoPath: '', hasVideo: false })",
        "GetScriptCurve": "async () => [{ atMs: 0, pos: 0 }, { atMs: 10000, pos: 100 }]",
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
        "AnalyzeScript": "async () => ({ summary: '10 seconds: steady 80%' })",
        "ScriptChapters": "async () => [{ kind: 'pause', startMs: 0, endMs: 1000 }, "
                          "{ kind: 'crescendo', startMs: 1000, endMs: 10000 }]",
    }))

    harness = pathlib.Path(__file__).resolve().parent / "_chapters_harness.html"
    harness.write_text(PAGE)

    with sync_playwright() as p:
        browser = p.chromium.launch()
        page = browser.new_page()
        page.on("pageerror", lambda e: print("   [pageerror]", e))
        page.goto(f"{base}/test/_chapters_harness.html")
        page.wait_for_function("window.__ready === true")

        page.click("#pb-choose")
        page.wait_for_function(
            "document.querySelector('#pb-analysis') && "
            "document.querySelector('#pb-analysis').textContent.includes('Chapters')",
            timeout=5000)
        # Analysis lives under closed Script-check details (G4.C) — read textContent.
        page.locator("#pb-quality-details").evaluate("el => { el.open = true }")
        text = page.locator("#pb-analysis").evaluate("el => el.textContent || ''")
        check("analysis summary kept", "steady" in text, text)
        check("chapter list is appended", "Chapters:" in text, text)
        check("chapter names translated (Pause/Crescendo)",
              "Pause" in text and "Crescendo" in text, text)
        check("times formatted as m:ss", "0:00" in text and "0:10" in text, text)
        check("quality summary hints analysis ready",
              "summary ready" in page.locator("#pb-quality-details > summary").inner_text())

        browser.close()

    shutdown()
    harness.unlink(missing_ok=True)
    return check.report()


if __name__ == "__main__":
    sys.exit(main())
