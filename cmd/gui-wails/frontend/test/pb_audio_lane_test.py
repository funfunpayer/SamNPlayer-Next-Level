"""V1 S4a: time ruler above the curve and the Audio Level lane (video loudness)
under the channel lanes. Audio lane is on by default (Owner), off via View.

Run: python3 cmd/gui-wails/frontend/test/pb_audio_lane_test.py
"""

import pathlib
import sys

from playwright.sync_api import sync_playwright

from _harness import Checker, app_stub, serve

PAGE = """<!doctype html><html><head><link rel="stylesheet" href="/src/style.css?real"></head>
<body><div id="tab-playback"></div>
<script type="module">
  import { initPlayback } from '/src/playback.js';
  window.__pb = initPlayback(document.querySelector('#tab-playback'));
  window.__ready = true;
</script></body></html>"""


def main():
    check = Checker()
    data_js = ("const STROKE = []; for (let i = 0; i < 200; i++) STROKE.push({ atMs: i * 500, pos: i % 2 ? 10 : 90 });\n"
               "window.__axes = { general: STROKE.map(p => ({ at: p.atMs, pos: p.pos })), vibration: STROKE.map(p => ({ at: p.atMs, pos: 50 })), suction: null };\n")
    base, shutdown = serve(data_js + app_stub({
        "PickFunscriptFile": "async () => '/tmp/t.samn'",
        "LoadFunscript": "async () => ({ path: '/tmp/t.samn', actionCount: STROKE.length, durationMs: 100000, videoPath: '', hasVideo: false })",
        "GetScriptCurve": "async () => STROKE.slice()",
        "GetScriptAxisActions": "async (axis) => (window.__axes[axis || 'general'] || null)",
        "GetHeatmap": "async n => Array.from({ length: n }, (_, i) => ({ atMs: i * 1000, intensity: 0.5 }))",
        "GetMarker": "async () => null",
        "VideoFileURL": "async () => ''",
        "GetAudioLevelEnvelope": "async () => { window.__audioCalls = (window.__audioCalls || 0) + 1; return Array.from({ length: 2000 }, (_, i) => Math.abs(Math.sin(i / 9))); }",
    }))
    harness = pathlib.Path(__file__).resolve().parent / "_pb_audio_lane_harness.html"
    harness.write_text(PAGE)
    try:
        with sync_playwright() as p:
            b = p.chromium.launch()
            page = b.new_page(viewport={"width": 1280, "height": 900})
            page.on("pageerror", lambda e: print("   [pageerror]", e))
            page.add_init_script("try{localStorage.clear()}catch(e){}")
            page.goto(f"{base}/test/_pb_audio_lane_harness.html")
            page.wait_for_function("window.__ready === true")
            check("ruler hidden before a script", page.is_hidden("#pb-ruler"))
            page.click("#pb-choose")
            page.wait_for_function("!document.querySelector('#pb-ruler').hidden", timeout=5000)
            check("ruler shown with the curve", page.is_visible("#pb-ruler"))
            labels = page.evaluate("""() => { const c = document.querySelector('#pb-ruler');
                const d = c.getContext('2d').getImageData(0, 0, c.width, c.height).data; let n = 0;
                for (let i = 3; i < d.length; i += 4) if (d[i] > 0) n++; return n; }""")
            check("ruler draws ticks and labels", labels > 200, str(labels))
            check("audio toggle on by default", page.is_checked("#pb-audio-lane-toggle"))
            check("no audio decode without a video", page.evaluate("window.__audioCalls || 0") == 0)
            h1 = page.evaluate("document.querySelector('#pb-axis-lanes').getBoundingClientRect().height")
            # A linked video: the stage gets .has-video (as after a real video load).
            page.evaluate("document.querySelector('#pb-video-stage').classList.add('has-video')")
            page.wait_for_function("(window.__audioCalls || 0) === 1", timeout=5000)
            page.wait_for_function(f"document.querySelector('#pb-axis-lanes').getBoundingClientRect().height > {h1}", timeout=3000)
            check("audio lane added under the channel lanes", True)
            check("aria-label names the audio lane",
                  "Audio level" in (page.get_attribute("#pb-axis-lanes", "aria-label") or ""))
            page.uncheck("#pb-audio-lane-toggle")
            page.wait_for_function(f"document.querySelector('#pb-axis-lanes').getBoundingClientRect().height <= {h1}", timeout=3000)
            check("toggle off removes the audio lane", True)
            check("toggle remembered", page.evaluate("localStorage.getItem('pb.audioLane')") == "0")
            page.check("#pb-audio-lane-toggle")
            page.wait_for_function(f"document.querySelector('#pb-axis-lanes').getBoundingClientRect().height > {h1}", timeout=3000)
            check("toggle on brings it back", True)
            b.close()
    finally:
        shutdown()
        harness.unlink(missing_ok=True)
    return check.report()


if __name__ == "__main__":
    sys.exit(main())
