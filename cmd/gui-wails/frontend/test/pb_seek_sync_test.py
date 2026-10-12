"""Play seek: video moves on heatmap click; ReportVideoPosition on seeked while playing.

Regression: seekTo used to redraw only the heatmap and wait for timeupdate
before curve sync; device ReportVideoPosition also lagged until timeupdate.

Run: python3 cmd/gui-wails/frontend/test/pb_seek_sync_test.py
"""

import pathlib
import subprocess
import sys

from playwright.sync_api import sync_playwright

from _harness import Checker, app_stub, serve

FRONTEND = pathlib.Path(__file__).resolve().parent.parent
CLIP_DURATION_S = 4

PAGE = """<!doctype html><html><body><div id="root"></div>
<script type="module">
  import { initPlayback } from '/src/playback.js';
  window.__pb = initPlayback(document.querySelector('#root'));
  window.__ready = true;
</script></body></html>"""


def make_clip(path):
    subprocess.run([
        "ffmpeg", "-f", "lavfi",
        "-i", f"testsrc=duration={CLIP_DURATION_S}:size=320x240:rate=10",
        "-c:v", "libvpx-vp9", "-crf", "30", "-b:v", "0", "-y", str(path),
    ], check=True, capture_output=True)


def main():
    check = Checker()
    clip_path = pathlib.Path(__file__).resolve().parent / "_pb_seek_sync_clip.webm"
    make_clip(clip_path)
    duration_ms = CLIP_DURATION_S * 1000

    base, shutdown = serve(app_stub({
        "PickFunscriptFile": "async () => '/tmp/seek.funscript'",
        "LoadFunscript": (
            f"async () => ({{ path: '/tmp/seek.funscript', actionCount: 3, "
            f"durationMs: {duration_ms}, videoPath: '/tmp/seek.webm', hasVideo: true }})"
        ),
        "GetScriptCurve": (
            f"async () => [{{ atMs: 0, pos: 0 }}, {{ atMs: {duration_ms}, pos: 100 }}]"
        ),
        "GetHeatmap": (
            "async n => Array.from({ length: n }, (_, i) => "
            "({ atMs: i * 100, intensity: 0.5 }))"
        ),
        "GetMarker": "async () => null",
        "VideoFileURL": "async () => '/test/_pb_seek_sync_clip.webm'",
        "GetSpeedHighlights": "async () => []",
        "GetScriptOffset": "async () => 0",
        "GetOMarkers": "async () => []",
        "SaveOMarkers": "async () => {}",
        "SaveMarker": "async () => {}",
        "GetScriptBookmarks": "async () => []",
        "GetScriptChapterMarks": "async () => []",
        "ProbePlaybackVideo": "async () => ({ likelyPlayable: true })",
        "ReportVideoPosition": (
            "async (ms) => { window.__calls.push(['ReportVideoPosition', ms]); }"
        ),
        "StartPlayback": "async opts => { window.__calls.push(['StartPlayback', opts]); }",
        "StopPlayback": "async () => { window.__calls.push(['StopPlayback']); }",
        "SnapTimeMs": "async (t, fps) => t",
    }))
    harness = FRONTEND / "test" / "_pb_seek_sync_harness.html"
    harness.write_text(PAGE)

    with sync_playwright() as p:
        browser = p.chromium.launch()
        page = browser.new_page()
        page.on("pageerror", lambda e: print("   [pageerror]", e))
        page.goto(f"{base}/test/_pb_seek_sync_harness.html")
        page.wait_for_function("window.__ready === true")

        page.click("#pb-choose")
        page.wait_for_selector("#pb-heatmap", state="visible", timeout=5000)
        page.wait_for_function(
            "() => { const v = document.querySelector('video'); "
            "return v && v.readyState >= 1 && v.duration > 0; }",
            timeout=8000)

        page.evaluate("""() => {
          const v = document.querySelector('video');
          v.pause();
          const hm = document.querySelector('#pb-heatmap');
          const r = hm.getBoundingClientRect();
          const x = r.left + r.width * 0.5;
          const y = r.top + r.height * 0.5;
          hm.dispatchEvent(new MouseEvent('mousedown', {
            clientX: x, clientY: y, bubbles: true,
          }));
          window.dispatchEvent(new MouseEvent('mouseup', {
            clientX: x, clientY: y, bubbles: true,
          }));
        }""")
        page.wait_for_function(
            "() => { const v = document.querySelector('video'); "
            "return v && Math.abs(v.currentTime - 2) < 0.6; }",
            timeout=4000)
        pos = page.evaluate(
            "() => document.querySelector('video').currentTime")
        check("heatmap seek moved video near mid (~2s)", 1.4 <= pos <= 2.6)

        # Playing + video sync: setting currentTime must ReportVideoPosition via seeked.
        page.evaluate("() => { window.__calls = []; }")
        page.click("#pb-play")
        page.wait_for_function(
            "() => (window.__calls || []).some(c => c[0] === 'StartPlayback')",
            timeout=4000)
        page.evaluate("""() => {
          const v = document.querySelector('video');
          v.currentTime = 3.0;
        }""")
        page.wait_for_function(
            "() => (window.__calls || []).some("
            "c => c[0] === 'ReportVideoPosition' && c[1] >= 2800)",
            timeout=4000)
        reports = page.evaluate(
            "() => (window.__calls || []).filter(c => c[0] === 'ReportVideoPosition')"
            ".map(c => c[1])")
        check("seeked while playing reports ≥2.8s",
              any(ms >= 2800 for ms in reports))

        browser.close()

    shutdown()
    harness.unlink(missing_ok=True)
    clip_path.unlink(missing_ok=True)
    return check.report()


if __name__ == "__main__":
    sys.exit(main())
