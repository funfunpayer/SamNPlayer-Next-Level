"""V1 S5 transport: speed 0.5–2× (video playbackRate + SetPlaybackSpeed +
StartPlayback speed), previous item / back to start, loop toggle mirrors
Loop selection, volume / mute on the video, fullscreen button.

Run: python3 cmd/gui-wails/frontend/test/pb_transport_test.py
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
        "GetSettings": "async () => ({playbackMock:true, playbackSync:'independent', playbackTickMs:50})",
        "PickFunscriptFile": "async () => '/s/a.funscript'",
        "LoadFunscript": "async (p) => { window.__loaded = (window.__loaded || []).concat([p]); return ({ path: p, actionCount: 4, durationMs: 4000, videoPath: '', hasVideo: false }); }",
        "GetScriptCurve": "async () => [{atMs:0,pos:0},{atMs:2000,pos:100},{atMs:4000,pos:0}]",
        "GetScriptAxisActions": "async () => [{at:0,pos:0},{at:2000,pos:100},{at:4000,pos:0}]",
        "GetHeatmap": "async n => Array.from({ length: n }, (_, i) => ({ atMs: i * 10, intensity: 0.5 }))",
        "GetMarker": "async () => null",
        "VideoFileURL": "async () => ''",
        "SetPlaybackSpeed": "async (v) => { window.__speed = v; return v; }",
        "StartPlayback": "async (o) => { window.__startOpts = o; }",
        "StopPlayback": "async () => {}",
    }))
    harness = pathlib.Path(__file__).resolve().parent / "_pb_transport_harness.html"
    harness.write_text(PAGE)
    try:
        with sync_playwright() as p:
            b = p.chromium.launch()
            page = b.new_page()
            page.on("pageerror", lambda e: print("   [pageerror]", e))
            page.add_init_script("try{localStorage.clear()}catch(e){}")
            page.goto(f"{base}/test/_pb_transport_harness.html")
            page.wait_for_function("window.__ready === true")
            page.click("#pb-choose")
            page.wait_for_function("(window.__loaded || []).length === 1", timeout=5000)
            page.evaluate("document.querySelector('#pb-loaded').hidden = false")

            opts = page.evaluate("[...document.querySelectorAll('#pb-speed option')].map(o => o.value)")
            check("speed choices 0.5× – 2×", opts == ["0.5", "0.75", "1", "1.25", "1.5", "2"], str(opts))
            page.select_option("#pb-speed", "1.5")
            page.wait_for_function("window.__speed === 1.5", timeout=3000)
            check("speed change calls SetPlaybackSpeed", True)
            check("video playbackRate follows speed", page.evaluate("document.querySelector('#pb-video').playbackRate") == 1.5)
            page.evaluate("document.querySelector('#pb-play').disabled = false")
            page.click("#pb-play")
            page.wait_for_function("!!window.__startOpts", timeout=3000)
            check("StartPlayback carries the speed", page.evaluate("window.__startOpts.speed") == 1.5)

            check("loop button starts off", page.get_attribute("#pb-t-loop", "aria-pressed") == "false")
            page.click("#pb-t-loop")
            check("loop button toggles Loop selection", page.is_checked("#pb-loop")
                  and page.get_attribute("#pb-t-loop", "aria-pressed") == "true")

            page.fill("#pb-volume", "0.3")
            page.dispatch_event("#pb-volume", "input")
            check("volume slider sets video volume", abs(page.evaluate("document.querySelector('#pb-video').volume") - 0.3) < 1e-6)
            check("volume remembered", page.evaluate("localStorage.getItem('pb.volume')") == "0.3")
            page.click("#pb-mute")
            check("mute toggles the video", page.evaluate("document.querySelector('#pb-video').muted")
                  and page.get_attribute("#pb-mute", "aria-pressed") == "true")

            page.evaluate("window.__fs = 0; document.querySelector('#pb-video-fs').addEventListener('click', () => window.__fs++)")
            page.click("#pb-t-fs")
            check("fullscreen button uses the video fullscreen", page.evaluate("window.__fs") == 1)

            # Previous: with a playlist, go to the previous item.
            page.evaluate("window.__pb.setPlaylistForTest(['/s/a.funscript', '/s/b.funscript'], 1)")
            page.click("#pb-prev")
            page.wait_for_function("window.__loaded[window.__loaded.length - 1] === '/s/a.funscript' && window.__pb.getPlaylistIndex() === 0", timeout=5000)
            check("previous loads the previous playlist item", True)
            b.close()
    finally:
        shutdown()
        harness.unlink(missing_ok=True)
    return check.report()


if __name__ == "__main__":
    sys.exit(main())
