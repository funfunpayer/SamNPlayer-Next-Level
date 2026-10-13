"""Play video sync: pause/waiting reports idle so the device zeros immediately.

Backend player.Sync treats SyncIdleSentinel as quiet (no syncStaleAfter wait).
Frontend must call ReportVideoSyncIdle on video pause/waiting while playing
with “Device follows video position” on.

Run: python3 cmd/gui-wails/frontend/test/pb_video_sync_idle_test.py
"""

import pathlib
import sys

from playwright.sync_api import sync_playwright

from _harness import Checker, app_stub, serve

FRONTEND = pathlib.Path(__file__).resolve().parent.parent

PAGE = """<!doctype html><html><body><div id="root"></div>
<script type="module">
  import { initPlayback } from '/src/playback.js';
  window.__pb = initPlayback(document.querySelector('#root'));
  window.__ready = true;
</script></body></html>"""


def main():
    check = Checker()
    base, shutdown = serve(app_stub({
        "PickFunscriptFile": "async () => '/tmp/idle.funscript'",
        "LoadFunscript": (
            "async () => ({ path: '/tmp/idle.funscript', actionCount: 2, "
            "durationMs: 10000, videoPath: '/tmp/idle.mp4', hasVideo: true })"
        ),
        "GetScriptCurve": (
            "async () => [{ atMs: 0, pos: 0 }, { atMs: 10000, pos: 100 }]"
        ),
        "GetHeatmap": "async () => ({ points: [] })",
        "GetVibrationCurvePreview": "async () => null",
        "AnalyzeScript": "async () => ({})",
        "GetScriptOffset": "async () => 0",
        "GetMarker": "async () => null",
        "GetOMarkers": "async () => []",
        "GetScriptBookmarks": "async () => []",
        "GetScriptChapterMarks": "async () => []",
        "GetSpeedHighlights": "async () => []",
        "VideoFileURL": "async () => ''",
        "ProbePlaybackVideo": "async () => ({ ok: true })",
        "StartPlayback": "async () => { window.__calls.push(['StartPlayback']); }",
        "StopPlayback": "async () => { window.__calls.push(['StopPlayback']); }",
        "ReportVideoPosition": (
            "async (ms) => { window.__calls.push(['ReportVideoPosition', ms]); }"
        ),
        "ReportVideoSyncIdle": (
            "async () => { window.__calls.push(['ReportVideoSyncIdle']); }"
        ),
        "GetSettings": "async () => ({})",
    }))
    harness = FRONTEND / "test" / "_pb_video_sync_idle_harness.html"
    harness.write_text(PAGE)

    with sync_playwright() as p:
        browser = p.chromium.launch()
        page = browser.new_page()
        page.on("pageerror", lambda e: print("   [pageerror]", e))
        page.goto(f"{base}/test/_pb_video_sync_idle_harness.html")
        page.wait_for_function("window.__ready === true")

        page.click("#pb-choose")
        page.wait_for_function(
            "() => document.querySelector('#pb-use-video-sync')",
            timeout=5000)
        check("video sync toggle present",
              page.locator("#pb-use-video-sync").count() == 1)
        # Ensure sync on (default checked when row shown).
        page.evaluate("""() => {
          const c = document.querySelector('#pb-use-video-sync');
          if (c && !c.checked) { c.checked = true; c.dispatchEvent(new Event('change')); }
        }""")

        page.click("#pb-play")
        page.wait_for_function(
            "() => (window.__calls || []).some(c => c[0]==='StartPlayback')",
            timeout=5000)
        page.evaluate("window.__calls = []")

        page.evaluate("document.querySelector('#pb-video').dispatchEvent(new Event('pause'))")
        page.wait_for_timeout(40)
        idle = page.evaluate(
            "() => (window.__calls || []).filter(c => c[0]==='ReportVideoSyncIdle')")
        check("pause while playing reports sync idle",
              len(idle) >= 1, str(idle))

        page.evaluate("window.__calls = []")
        page.evaluate("document.querySelector('#pb-video').dispatchEvent(new Event('waiting'))")
        page.wait_for_timeout(40)
        idle2 = page.evaluate(
            "() => (window.__calls || []).filter(c => c[0]==='ReportVideoSyncIdle')")
        check("waiting while playing reports sync idle",
              len(idle2) >= 1, str(idle2))

        page.evaluate("window.__calls = []")
        page.evaluate("document.querySelector('#pb-video').dispatchEvent(new Event('playing'))")
        page.wait_for_timeout(40)
        pos = page.evaluate(
            "() => (window.__calls || []).filter(c => c[0]==='ReportVideoPosition')")
        check("playing after idle reports video position",
              len(pos) >= 1, str(pos))

        # After Stop, pause must not report idle (playing=false).
        page.click("#pb-stop")
        page.wait_for_timeout(40)
        page.evaluate("window.__calls = []")
        page.evaluate("document.querySelector('#pb-video').dispatchEvent(new Event('pause'))")
        page.wait_for_timeout(40)
        idle3 = page.evaluate(
            "() => (window.__calls || []).filter(c => c[0]==='ReportVideoSyncIdle')")
        check("pause after stop does not report idle",
              len(idle3) == 0, str(idle3))

        browser.close()
    shutdown()
    harness.unlink(missing_ok=True)
    return check.report()


if __name__ == "__main__":
    sys.exit(main())
