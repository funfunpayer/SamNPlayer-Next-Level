"""Playback shows contact marks from ScriptInfo when present.

Run: python3 cmd/gui-wails/frontend/test/playback_contact_marks_test.py
"""

import json
import pathlib

from playwright.sync_api import sync_playwright

from _harness import Checker, app_stub, serve

FRONTEND = pathlib.Path(__file__).resolve().parents[1]

PAGE = """<!doctype html><html><body><div id="root"></div>
<script type="module">
  import { initPlayback } from '/src/playback.js';
  window.__pb = initPlayback(document.querySelector('#root'));
  window.__ready = true;
</script></body></html>"""


def main():
    check = Checker()
    marks = {
        "tip_class": "glans",
        "primary": {"x": 100, "y": 200, "w": 80, "h": 60, "class": "nipples", "fixed": False},
        "extras": [{"x": 220, "y": 200, "w": 70, "h": 55, "class": "nipples", "fixed": True}],
        "drive_stroke": False,
    }
    info = {
        "path": "/tmp/clip.funscript",
        "actionCount": 3,
        "durationMs": 5000,
        "videoPath": "/tmp/clip.mp4",
        "hasVideo": True,
        "profile": "standard",
        "contactVibration": True,
        "contactVibrationSpan": 0.75,
        "contactVibrationCurve": "soft",
        "nativeFormat": False,
        "hasNeoAxes": False,
        "hasContactMarks": True,
        "contactMarks": marks,
        "playbackSource": "recipe",
    }
    base, shutdown = serve(app_stub({
        "PickFunscriptFile": "async () => '/tmp/clip.funscript'",
        "LoadFunscript": f"async () => ({json.dumps(info)})",
        "VideoFileURL": "async () => ''",
        "GetTrajectory": "async () => null",
        "GetMarker": "async () => null",
        "GetOMarkers": "async () => []",
        "GetHeatmap": "async () => []",
        "GetScriptCurve": "async () => [{atMs:0,pos:20},{atMs:2500,pos:80},{atMs:5000,pos:40}]",
        "GetScriptActions": "async () => [{at:0,pos:20},{at:2500,pos:80},{at:5000,pos:40}]",
        "GetScriptAxisActions": "async () => []",
        "GetScriptOffset": "async () => 0",
        "GetSettings": "async () => ({})",
        "ProbePlaybackVideo": "async () => ({ ok: true, playable: true })",
        "EnsurePlayablePlaybackVideo": "async () => ({ ok: true })",
        "ListDevices": "async () => []",
        "GetPlaylist": "async () => []",
        "ClearPlaybackVideo": "async () => {}",
        "GetSpeedHighlights": "async () => []",
        "GetVibrationCurve": "async () => []",
    }))
    harness = FRONTEND / "test" / "_playback_contact_marks_harness.html"
    harness.write_text(PAGE)

    with sync_playwright() as p:
        browser = p.chromium.launch()
        page = browser.new_page()
        page.on("dialog", lambda d: d.dismiss())
        page.on("pageerror", lambda e: print("   [pageerror]", e))
        page.goto(f"{base}/test/_playback_contact_marks_harness.html")
        page.wait_for_function("window.__ready === true")

        page.click("#pb-choose")
        page.wait_for_function(
            "() => { const r = document.querySelector('#pb-video-stage #pb-contact-marks-row');"
            " return r && r.style.display === 'flex'; }",
            timeout=8000)

        row = page.locator("#pb-video-stage #pb-contact-marks-row")
        check("Contact marks row visible", row.is_visible())
        check("Contact marks toggle on by default",
              page.locator("#pb-video-stage #pb-contact-marks-toggle").is_checked())
        hint = page.locator("#pb-video-stage #pb-contact-marks-hint").inner_text()
        check("Hint mentions tip class", "glans" in hint.lower() or "Tip:" in hint, hint)
        check("Hint says feel only", "feel only" in hint.lower(), hint)

        browser.close()

    shutdown()
    harness.unlink(missing_ok=True)
    return check.report()


if __name__ == "__main__":
    raise SystemExit(main())
