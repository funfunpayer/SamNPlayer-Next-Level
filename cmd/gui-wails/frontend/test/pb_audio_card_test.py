"""V1 S8a: Audio Analysis card — whole-video waveform with BPM readout and
beat grid. Loads the sound when the Audio Analysis tab is shown, even with
the Audio Level lane off.

Run: python3 cmd/gui-wails/frontend/test/pb_audio_card_test.py
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

INK = """() => { const c = document.querySelector('#pb-aa-wave');
  const d = c.getContext('2d').getImageData(0, 0, c.width, c.height).data; let n = 0;
  for (let i = 3; i < d.length; i += 4) if (d[i] > 0) n++; return n; }"""


def main():
    check = Checker()
    data_js = ("const STROKE = []; for (let i = 0; i < 200; i++) STROKE.push({ atMs: i * 500, pos: i % 2 ? 10 : 90 });\n"
               "window.__audioCalls = 0;\n")
    base, shutdown = serve(data_js + app_stub({
        "PickFunscriptFile": "async () => '/tmp/t.samn'",
        "LoadFunscript": "async () => ({ path: '/tmp/t.samn', actionCount: STROKE.length, durationMs: 100000, videoPath: '', hasVideo: false })",
        "GetScriptCurve": "async () => STROKE.slice()",
        "GetHeatmap": "async n => Array.from({ length: n }, (_, i) => ({ atMs: i * 1000, intensity: 0.5 }))",
        "GetMarker": "async () => null",
        "VideoFileURL": "async () => ''",
        "GetAudioLevelEnvelope": "async () => { window.__audioCalls++; return Array.from({ length: 2000 }, (_, i) => Math.abs(Math.sin(i / 9))); }",
        "EstimatePlaybackAudioBPM": "async () => 96",
    }))
    harness = pathlib.Path(__file__).resolve().parent / "_pb_audio_card_harness.html"
    harness.write_text(PAGE)
    try:
        with sync_playwright() as p:
            b = p.chromium.launch()
            page = b.new_page(viewport={"width": 1280, "height": 900})
            page.on("pageerror", lambda e: print("   [pageerror]", e))
            page.add_init_script("try{localStorage.clear();localStorage.setItem('pb.audioLane','0')}catch(e){}")
            page.goto(f"{base}/test/_pb_audio_card_harness.html")
            page.wait_for_function("window.__ready === true")
            page.click("#pb-choose")
            page.wait_for_function("!document.querySelector('#pb-ruler').hidden", timeout=5000)
            check("card present", page.is_visible("#pb-audio-card"))
            check("hint without a video", "Link a video" in page.inner_text("#pb-aa-empty"))
            check("BPM readout empty", page.inner_text("#pb-aa-bpm").strip() == "— BPM")

            page.evaluate("document.querySelector('#pb-video-stage').classList.add('has-video')")
            page.wait_for_timeout(300)
            check("lane off: no decode before the tab is shown", page.evaluate("window.__audioCalls") == 0)
            page.evaluate("document.querySelector('#tab-playback').dataset.pbBottom = 'audio'")
            page.wait_for_function("window.__audioCalls === 1", timeout=3000)
            page.wait_for_function("!document.querySelector('#pb-aa-wave').hidden", timeout=3000)
            check("Audio Analysis tab loads the sound", True)
            check("waveform drawn", page.evaluate(INK) > 500, str(page.evaluate(INK)))
            check("hint hidden with a waveform", page.is_hidden("#pb-aa-empty"))
            check("audio lane stays off", "Audio level" not in (page.get_attribute("#pb-axis-lanes", "aria-label") or ""))

            ink0 = page.evaluate(INK)
            page.evaluate("""() => { const i = document.querySelector('#pb-bpm'); i.value = '120';
                i.dispatchEvent(new Event('input')); }""")
            page.wait_for_function("document.querySelector('#pb-aa-bpm').textContent === '120 BPM'", timeout=2000)
            check("BPM readout follows the field", True)
            check("beat grid adds lines", page.evaluate(INK) > ink0)

            page.click("#pb-aa-detect")
            page.wait_for_function("document.querySelector('#pb-aa-bpm').textContent === '96 BPM'", timeout=3000)
            check("From audio fills the readout", page.input_value("#pb-bpm") == "96")

            box = page.query_selector("#pb-aa-wave").bounding_box()
            page.mouse.click(box["x"] + box["width"] * 0.5, box["y"] + box["height"] / 2)
            page.wait_for_timeout(200)
            check("click seeks (playhead drawn mid-wave)", page.evaluate("""() => {
                const c = document.querySelector('#pb-aa-wave'); const x = Math.round(c.width / 2);
                const d = c.getContext('2d').getImageData(x - 2, 0, 5, 3).data;
                for (let i = 3; i < d.length; i += 4) if (d[i] > 0) return true; return false; }"""))
            b.close()
    finally:
        shutdown()
        harness.unlink(missing_ok=True)
    return check.report()


if __name__ == "__main__":
    sys.exit(main())
