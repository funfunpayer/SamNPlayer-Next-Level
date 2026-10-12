"""Regressionstest: das Video folgt beim Ziehen eines Punkts im Kurven-
Editor mit.

Vorher zeigte das Video beim Bearbeiten weiterhin die Stelle, an der man
zuletzt pausiert/gesucht hatte - man musste den Zeitwert eines Punkts
gedanklich mit dem Bild abgleichen, statt es direkt zu sehen. Geprüft wird
hier mit einem echten, per ffmpeg synthetisch erzeugten Video (VP9/WebM,
da der headless Chromium dieser Umgebung kein H.264 dekodiert - dieselbe
Begründung wie in den anderen Video-Tests dieses Projekts): Ziehen eines
Punkts im Editor bewegt videoEl.currentTime auf die neue Zeit des Punkts,
sowohl beim Greifen (mousedown) als auch während des Ziehens (mousemove).

Die Videodatei wird bei jedem Lauf frisch erzeugt (wie die synthetischen
Test-Videos in generator/grid_lk_backend_test.py) statt als Binärdatei
mitgeführt zu werden.

Ausführen:  python3 cmd/gui-wails/frontend/test/curve_editor_video_sync_test.py
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
    clip_path = pathlib.Path(__file__).resolve().parent / "_sync_test_clip.webm"
    make_clip(clip_path)

    duration_ms = CLIP_DURATION_S * 1000
    base, shutdown = serve(app_stub({
        "PickFunscriptFile": "async () => '/tmp/test.funscript'",
        "LoadFunscript": f"async () => ({{ path: '/tmp/test.funscript', actionCount: 3, "
                         f"durationMs: {duration_ms}, videoPath: '/tmp/test.webm', hasVideo: true }})",
        "GetScriptCurve": f"async () => [{{ atMs: 0, pos: 0 }}, {{ atMs: {duration_ms // 2}, pos: 50 }}, "
                          f"{{ atMs: {duration_ms}, pos: 100 }}]",
        "GetScriptActions": f"async () => [{{ at: 0, pos: 0 }}, {{ at: {duration_ms // 2}, pos: 50 }}, "
                            f"{{ at: {duration_ms}, pos: 100 }}]",
        "SaveScriptActions": "async () => {}",
        "GetHeatmap": "async n => Array.from({ length: n }, (_, i) => ({ atMs: i * 100, intensity: 0.5 }))",
        "GetMarker": "async () => null",
        "VideoFileURL": "async () => '/test/_sync_test_clip.webm'",
        "GetSpeedHighlights": "async () => []",
        "ExportScriptHeatmapPNG": "async () => '/tmp/h.png'",
        "SavePlaybackProject": "async () => '/tmp/p.snp.json'",
        "EditCapSpeedRange": "async () => {}",
        "EditDeleteRange": "async () => {}",
        "SnapTimeMs": "async (t, fps) => t",
        "GetScriptOffset": "async () => 0",
        "GetOMarkers": "async () => []",
        "SaveOMarkers": "async () => {}",
    }))

    harness = FRONTEND / "test" / "_curve_editor_video_sync_harness.html"
    harness.write_text(PAGE)

    with sync_playwright() as p:
        browser = p.chromium.launch()
        page = browser.new_page()
        page.on("pageerror", lambda e: print("   [pageerror]", e))
        page.goto(f"{base}/test/_curve_editor_video_sync_harness.html")
        page.wait_for_function("window.__ready === true")

        page.click("#pb-choose")
        page.wait_for_function("document.querySelector('#pb-video').readyState >= 1", timeout=10000)
        page.check("#pb-curve-edit")
        page.wait_for_function(
            "document.querySelector('#pb-curve-edit-hint').style.display === 'block'", timeout=5000)

        # Video an eine ferne Stelle vorspulen, damit ein Sprung durchs
        # Ziehen unten eindeutig erkennbar ist (kein Zufallstreffer, weil
        # das Video zufällig schon dort steht).
        page.evaluate("document.querySelector('#pb-video').currentTime = 0.05")
        page.wait_for_timeout(100)

        box = page.locator("#pb-curve").bounding_box()
        pad = 6.0
        usable = box["height"] - 2 * pad

        def y_for_pos(pos):
            return box["y"] + pad + (1.0 - pos / 100.0) * usable

        # Middle point (half duration, pos=50) → 50% width.
        mid_x = box["x"] + box["width"] * 0.5
        mid_y = y_for_pos(50)

        page.mouse.move(mid_x, mid_y)
        page.mouse.down()
        page.wait_for_timeout(150)  # Video muss die Seek-Anfrage verarbeiten
        after_down = page.eval_on_selector("#pb-video", "e => e.currentTime")
        check("Greifen eines Punkts spult das Video an dessen Zeit (~3.0s)",
              abs(after_down - 3.0) < 0.3, f"currentTime={after_down:.2f}")

        # Drag toward the last point (near 100% width, pos=100).
        end_x = box["x"] + box["width"] * 0.98
        end_y = y_for_pos(100)
        page.mouse.move(end_x, end_y, steps=5)
        page.wait_for_timeout(150)
        after_move = page.eval_on_selector("#pb-video", "e => e.currentTime")
        check("Ziehen ans Ende spult das Video mit (~6.0s)",
              abs(after_move - CLIP_DURATION_S) < 0.5, f"currentTime={after_move:.2f}")
        check("Video folgt tatsächlich der Bewegung, nicht nur dem Greifen",
              after_move > after_down + 1.0, f"{after_down:.2f} -> {after_move:.2f}")

        page.mouse.up()

        browser.close()

    shutdown()
    harness.unlink(missing_ok=True)
    clip_path.unlink(missing_ok=True)
    return check.report()


if __name__ == "__main__":
    sys.exit(main())
