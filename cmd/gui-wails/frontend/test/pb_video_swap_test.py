"""Play: swap/replace video after the first film is already linked.

Root cause: VideoFileURL was a fixed http://127.0.0.1:port/video string, so
assigning videoEl.src again after SetPlaybackVideo did not reload. The UI
must call SetPlaybackVideo for the second pick and set a distinct src so
the <video> element actually switches.

Covers: load script (no companion) → Link video… → Video… → different file.

Ausführen: python3 cmd/gui-wails/frontend/test/pb_video_swap_test.py
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
        "PickFunscriptFile": "async () => '/tmp/swap.funscript'",
        "LoadFunscript": (
            "async () => ({ path: '/tmp/swap.funscript', actionCount: 4, "
            "durationMs: 8000, videoPath: '', hasVideo: false })"
        ),
        "GetScriptCurve": (
            "async () => [{ atMs: 0, pos: 10 }, { atMs: 8000, pos: 90 }]"
        ),
        "GetHeatmap": (
            "async n => Array.from({ length: n }, (_, i) => "
            "({ atMs: i * 100, intensity: 0.4 }))"
        ),
        "GetMarker": "async () => null",
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
        "ProbePlaybackVideo": (
            "async () => ({ path: '', codec: 'h264', likelyPlayable: true })"
        ),
        "PickVideoFile": """async () => {
          window.__videoPicks = (window.__videoPicks || 0) + 1;
          return window.__videoPicks === 1
            ? '/tmp/first.mp4'
            : '/tmp/second.mp4';
        }""",
        "SetPlaybackVideo": """async (path) => {
          window.__setVideoCalls = window.__setVideoCalls || [];
          window.__setVideoCalls.push(path);
          const n = window.__setVideoCalls.length;
          return 'http://127.0.0.1:9/video?v=' + n;
        }""",
        "VideoFileURL": """async () => {
          const n = (window.__setVideoCalls || []).length;
          return n ? ('http://127.0.0.1:9/video?v=' + n) : '';
        }""",
    }))

    harness = pathlib.Path(__file__).resolve().parent / "_pb_video_swap_harness.html"
    harness.write_text(PAGE)

    with sync_playwright() as p:
        browser = p.chromium.launch()
        page = browser.new_page()
        page.on("pageerror", lambda e: print("   [pageerror]", e))
        page.goto(f"{base}/test/_pb_video_swap_harness.html")
        page.wait_for_function("window.__ready === true")

        page.click("#pb-choose")
        page.wait_for_function(
            "document.querySelector('#pb-video-stage').classList.contains('no-video')",
            timeout=5000,
        )
        check(
            "load without companion → no-video stage",
            page.locator("#pb-video-stage").evaluate(
                "e => e.classList.contains('no-video')"
            ),
        )

        page.click("#pb-pick-video")
        page.wait_for_function(
            "document.querySelector('#pb-video-stage').classList.contains('has-video')",
            timeout=5000,
        )
        first_src = page.eval_on_selector("#pb-video", "e => e.getAttribute('src') || ''")
        first_calls = page.evaluate("window.__setVideoCalls.slice()")
        check("first link calls SetPlaybackVideo", first_calls == ["/tmp/first.mp4"], str(first_calls))
        check("first src has cache-bust v=1", first_src.endswith("?v=1"), first_src)
        check(
            "stage has-video after first link",
            page.locator("#pb-video-stage").evaluate(
                "e => e.classList.contains('has-video')"
            ),
        )

        # Swap via chrome "Video…" (same path users use after first load).
        page.locator("#pb-video-stage").hover()
        page.click("#pb-video-change")
        page.wait_for_function(
            "() => (window.__setVideoCalls || []).length === 2",
            timeout=5000,
        )
        second_src = page.eval_on_selector("#pb-video", "e => e.getAttribute('src') || ''")
        second_calls = page.evaluate("window.__setVideoCalls.slice()")
        check(
            "swap calls SetPlaybackVideo with second file",
            second_calls == ["/tmp/first.mp4", "/tmp/second.mp4"],
            str(second_calls),
        )
        check("second src has cache-bust v=2", second_src.endswith("?v=2"), second_src)
        check("src actually changed after swap", second_src != first_src, f"{first_src} → {second_src}")
        check(
            "still has-video after swap",
            page.locator("#pb-video-stage").evaluate(
                "e => e.classList.contains('has-video')"
            ),
        )

        browser.close()

    shutdown()
    harness.unlink(missing_ok=True)
    return check.report()


if __name__ == "__main__":
    sys.exit(main())
