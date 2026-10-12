"""Skript ohne Video: laden und allein abspielen.

Prüft, dass ein Funscript ohne Neben-Video die no-video-Stage zeigt und
dass „Abspielen“ StartPlayback mit useVideoSync=false aufruft — also die
eigene Uhr, nicht Video-Sync.

Ausführen: python3 cmd/gui-wails/frontend/test/pb_script_alone_test.py
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
        "PickFunscriptFile": "async () => '/tmp/alone.funscript'",
        "LoadFunscript": "async () => ({ path: '/tmp/alone.funscript', actionCount: 5, "
                         "durationMs: 8000, videoPath: '', hasVideo: false })",
        "GetScriptCurve": "async () => [{ atMs: 0, pos: 10 }, { atMs: 8000, pos: 90 }]",
        "GetHeatmap": "async n => Array.from({ length: n }, (_, i) => ({ atMs: i * 100, intensity: 0.4 }))",
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
        "ClearPlaybackVideo": "async () => {}",
        "StartPlayback": "async opts => { window.__calls.push(['StartPlayback', opts]); }",
        "StopPlayback": "async () => { window.__calls.push(['StopPlayback']); }",
    }))

    harness = pathlib.Path(__file__).resolve().parent / "_pb_script_alone_harness.html"
    harness.write_text(PAGE)

    with sync_playwright() as p:
        browser = p.chromium.launch()
        page = browser.new_page()
        page.on("pageerror", lambda e: print("   [pageerror]", e))
        page.goto(f"{base}/test/_pb_script_alone_harness.html")
        page.wait_for_function("window.__ready === true")

        page.click("#pb-choose")
        page.wait_for_function(
            "document.querySelector('#pb-video-stage').classList.contains('no-video')",
            timeout=5000)
        check("ohne Video: Stage hat no-video",
              page.locator("#pb-video-stage").evaluate("e => e.classList.contains('no-video')"))
        check("ohne Video: Hinweis sichtbar",
              "Feel without film" in page.locator("#pb-novideo").inner_text())
        check("ohne Video: Abspielen-Knopf in der Stage",
              page.locator("#pb-play-novideo").count() == 1)

        page.click("#pb-play-novideo")
        page.wait_for_function(
            "window.__calls.some(c => c[0] === 'StartPlayback')", timeout=5000)
        opts = page.evaluate(
            "window.__calls.filter(c => c[0] === 'StartPlayback').pop()[1]")
        check("StartPlayback ohne Video-Sync", opts.get("useVideoSync") is False, str(opts))
        check("Transport-Knopf wird während des Laufs zu Pause",
              page.locator("#pb-play").evaluate("e => e.dataset.state === 'pause' && e.getAttribute('aria-label') === 'Pause'"))

        page.click("#pb-stop")
        page.wait_for_function(
            "window.__calls.some(c => c[0] === 'StopPlayback')", timeout=5000)
        check("Stop beendet Skript-allein-Lauf", True)

        browser.close()

    shutdown()
    harness.unlink(missing_ok=True)
    return check.report()


if __name__ == "__main__":
    sys.exit(main())
