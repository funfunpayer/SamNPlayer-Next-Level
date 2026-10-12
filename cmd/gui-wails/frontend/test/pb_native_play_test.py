"""Regressionstest: natives Video-Play (Browser-eigene <video controls>)
startet die Funscript-Wiedergabe automatisch mit.

Vorher waren das native Video-Play und der App-eigene "Abspielen"-Knopf
(#pb-play) zwei völlig getrennte Aktionen - Klick auf die native
Video-Steuerung spielte nur das Video ab, ohne Funscript/Gerät zu starten.
Geprüft mit einem echten, per ffmpeg erzeugten Video (VP9/WebM, siehe
curve_editor_video_sync_test.py für die Begründung): natives Play löst
StartPlayback aus, OHNE das Video auf 0 zurückzuspulen (anders als #pb-play
bei aktivem Video-Sync - ein Sprung auf 0 wäre beim Weiterschauen ein
sichtbarer Bug). Ein zweites 'play'-Event während bereits laufender
Wiedergabe (z.B. video:resume nach Extended-O) darf StartPlayback NICHT ein
zweites Mal auslösen - das wäre sonst eine Rückkopplungsschleife, weil
play() bei aktivem Video-Sync selbst videoEl.play() aufruft.

Ausführen: python3 cmd/gui-wails/frontend/test/pb_native_play_test.py
"""

import pathlib
import subprocess
import sys

from playwright.sync_api import sync_playwright

from _harness import Checker, app_stub, serve

FRONTEND = pathlib.Path(__file__).resolve().parent.parent
CLIP_DURATION_S = 6

PAGE = """<!doctype html><html><body><div id="root"></div>
<script type="module">
  import { initPlayback } from '/src/playback.js';
  window.__pb = initPlayback(document.querySelector('#root'));
  window.__ready = true;
</script></body></html>"""


def make_clip(path):
    subprocess.run([
        "ffmpeg", "-f", "lavfi", "-i", f"testsrc=duration={CLIP_DURATION_S}:size=320x240:rate=10",
        "-c:v", "libvpx-vp9", "-crf", "30", "-b:v", "0", "-y", str(path),
    ], check=True, capture_output=True)


def main():
    check = Checker()
    clip_path = pathlib.Path(__file__).resolve().parent / "_native_play_test_clip.webm"
    make_clip(clip_path)

    duration_ms = CLIP_DURATION_S * 1000
    base, shutdown = serve(app_stub({
        "PickFunscriptFile": "async () => '/tmp/test.funscript'",
        "LoadFunscript": f"async () => ({{ path: '/tmp/test.funscript', actionCount: 3, "
                         f"durationMs: {duration_ms}, videoPath: '/tmp/test.webm', hasVideo: true }})",
        "GetScriptCurve": f"async () => [{{ atMs: 0, pos: 0 }}, {{ atMs: {duration_ms}, pos: 100 }}]",
        "GetHeatmap": "async n => Array.from({ length: n }, (_, i) => ({ atMs: i * 100, intensity: 0.5 }))",
        "GetMarker": "async () => null",
        "VideoFileURL": "async () => '/test/_native_play_test_clip.webm'",
        "GetSpeedHighlights": "async () => []",
        "ExportScriptHeatmapPNG": "async () => '/tmp/h.png'",
        "SavePlaybackProject": "async () => '/tmp/p.snp.json'",
        "EditCapSpeedRange": "async () => {}",
        "EditDeleteRange": "async () => {}",
        "SnapTimeMs": "async (t, fps) => t",
        "GetScriptOffset": "async () => 0",
        "GetOMarkers": "async () => []",
        "SaveOMarkers": "async () => {}",
        "StartPlayback": "async opts => { window.__calls.push(['StartPlayback', opts]); }",
        "StopPlayback": "async () => { window.__calls.push(['StopPlayback']); }",
    }))

    harness = FRONTEND / "test" / "_native_play_harness.html"
    harness.write_text(PAGE)

    with sync_playwright() as p:
        browser = p.chromium.launch()
        page = browser.new_page()
        page.on("pageerror", lambda e: print("   [pageerror]", e))
        page.goto(f"{base}/test/_native_play_harness.html")
        page.wait_for_function("window.__ready === true")

        page.click("#pb-choose")
        page.wait_for_function("document.querySelector('#pb-video').readyState >= 1", timeout=10000)

        start_calls = "window.__calls.filter(c => c[0] === 'StartPlayback').length"
        check("StartPlayback noch nicht aufgerufen, bevor irgendetwas abgespielt wurde",
              page.evaluate(start_calls) == 0)

        # An eine ferne Stelle vorspulen, damit ein Sprung zurück auf 0
        # beim nativen Play eindeutig erkennbar wäre.
        page.evaluate("document.querySelector('#pb-video').currentTime = 2.5")
        page.wait_for_timeout(100)

        page.evaluate("""() => {
            const v = document.querySelector('#pb-video');
            v.muted = true;  // Headless-Autoplay-Policy: nur stummgeschaltet ohne Nutzergeste erlaubt
            return v.play();
        }""")
        page.wait_for_function(start_calls + " === 1", timeout=5000)
        check("natives Video-Play startet StartPlayback automatisch mit", True)

        cur_time = page.eval_on_selector("#pb-video", "e => e.currentTime")
        check("Video wird beim nativen Play NICHT auf 0 zurückgespult (anders als #pb-play)",
              cur_time > 1.0, f"currentTime={cur_time:.2f}")

        play_state = page.eval_on_selector("#pb-play", "e => e.dataset.state")
        check("App-eigener Knopf zeigt beim nativen Play Pause (Status synchron)",
              play_state == "pause", str(play_state))

        # Ein zweites 'play'-Event waehrend bereits laufender Wiedergabe
        # (z.B. video:resume nach Extended-O) darf StartPlayback nicht
        # erneut auslösen.
        page.evaluate("document.querySelector('#pb-video').dispatchEvent(new Event('play'))")
        page.wait_for_timeout(200)
        calls_after = page.evaluate(start_calls)
        check("ein zweites 'play'-Event während laufender Wiedergabe löst StartPlayback nicht erneut aus",
              calls_after == 1, f"calls={calls_after}")

        browser.close()

    shutdown()
    harness.unlink(missing_ok=True)
    clip_path.unlink(missing_ok=True)
    return check.report()


if __name__ == "__main__":
    sys.exit(main())
