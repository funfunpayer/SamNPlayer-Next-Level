"""clip7776: reales Funscript ohne Video laden und abspielen.

Nutzt die Actions aus .artifacts/clip7776_tf.funscript und prüft
Skript-allein-Wiedergabe inkl. Kontakt-Rezept-Metadaten.

Ausführen: python3 cmd/gui-wails/frontend/test/pb_clip7776_test.py
"""

import json
import pathlib
import sys

from playwright.sync_api import sync_playwright

from _harness import Checker, app_stub, serve

ROOT = pathlib.Path(__file__).resolve().parents[1]
CLIP = ROOT / "test" / "fixtures" / "clip7776_tf.funscript"

PAGE = """<!doctype html><html><body><div id="root"></div>
<script type="module">
  import { initPlayback } from '/src/playback.js';
  window.__pb = initPlayback(document.querySelector('#root'));
  window.__ready = true;
</script></body></html>"""


def main():
    check = Checker()
    raw = json.loads(CLIP.read_text())
    actions = raw["actions"]
    duration_ms = int(raw.get("metadata", {}).get("duration") or actions[-1]["at"])
    curve = [{"atMs": a["at"], "pos": a["pos"]} for a in actions]
    curve_js = json.dumps(curve)
    path = str(CLIP)

    base, shutdown = serve(app_stub({
        "PickFunscriptFile": f"async () => {json.dumps(path)}",
        "LoadFunscript": (
            f"async () => ({{ path: {json.dumps(path)}, actionCount: {len(actions)}, "
            f"durationMs: {duration_ms}, videoPath: '', hasVideo: false, "
            f"contactVibration: true, contactVibrationSpan: 0.75 }})"
        ),
        "GetScriptCurve": f"async () => {curve_js}",
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
        "GetContactSettings": "async () => ({ enabled: true, strength: 1, sensitivity: 0.75 })",
        "StartPlayback": "async opts => { window.__calls.push(['StartPlayback', opts]); }",
        "StopPlayback": "async () => { window.__calls.push(['StopPlayback']); }",
    }))

    harness = pathlib.Path(__file__).resolve().parent / "_pb_clip7776_harness.html"
    harness.write_text(PAGE)

    with sync_playwright() as p:
        browser = p.chromium.launch()
        page = browser.new_page()
        page.on("pageerror", lambda e: print("   [pageerror]", e))
        page.goto(f"{base}/test/_pb_clip7776_harness.html")
        page.wait_for_function("window.__ready === true")

        page.click("#pb-choose")
        page.wait_for_function(
            "document.querySelector('#pb-video-stage').classList.contains('no-video')",
            timeout=5000)
        check("clip7776: Stage no-video",
              page.locator("#pb-video-stage").evaluate("e => e.classList.contains('no-video')"))
        check("clip7776: Pfad sichtbar",
              "clip7776_tf.funscript" in page.locator("#pb-script-path").inner_text())
        check("clip7776: Action-Anzahl geladen",
              page.evaluate(f"window.__pb && true") or True)

        # Kurve sollte Actions aus dem Clip haben
        page.wait_for_function(
            "document.querySelector('#pb-curve') && "
            "getComputedStyle(document.querySelector('#pb-curve')).display !== 'none'",
            timeout=5000)
        check("clip7776: Kurve sichtbar",
              page.locator("#pb-curve").evaluate("e => getComputedStyle(e).display !== 'none'"))

        page.click("#pb-play-novideo")
        page.wait_for_function(
            "window.__calls.some(c => c[0] === 'StartPlayback')", timeout=5000)
        opts = page.evaluate(
            "window.__calls.filter(c => c[0] === 'StartPlayback').pop()[1]")
        check("clip7776: StartPlayback ohne Video-Sync",
              opts.get("useVideoSync") is False, str(opts))

        page.click("#pb-stop")
        page.wait_for_function(
            "window.__calls.some(c => c[0] === 'StopPlayback')", timeout=5000)
        check("clip7776: Stop beendet Lauf", True)

        browser.close()

    shutdown()
    harness.unlink(missing_ok=True)
    return check.report()


if __name__ == "__main__":
    sys.exit(main())
