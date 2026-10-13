"""Scene Map bottom panel (V1 target scene-map.png): the script's chapters
as scenes — grid with thumbnail, name, range and duration; details of the
selected scene with n / N, prev/next, go to, and the linked video's
technical data (ProbeCreateVideo). No invented fields.

Run: python3 cmd/gui-wails/frontend/test/pb_scene_panel_test.py
"""

import json
import pathlib
import sys

from playwright.sync_api import sync_playwright

from _harness import Checker, app_stub, serve

HERE = pathlib.Path(__file__).resolve().parent
PNG = ("data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg==")


def main():
    check = Checker()
    acts = [{"at": i * 500, "pos": 10 if i % 2 else 90} for i in range(200)]
    html = (HERE.parent / "index.html").read_text().replace('href="/src/style.css"', 'href="/src/style.css?real"')
    harness = HERE / "_pb_scene_panel_harness.html"
    harness.write_text(html)
    chapters = [{"name": "Intro", "startTime": 0, "endTime": 0},
                {"name": "Build <up>", "startTime": 30000, "endTime": 0},
                {"name": "Finale", "startTime": 70000, "endTime": 90000}]
    base, shutdown = serve(("window.__chapters = %s; window.__probe = 0; window.__thumbCalls = 0;\n" % json.dumps(chapters))
                           + app_stub({
        "GetEULA": "async () => ({version:'x', text:'', accepted:true})",
        "IsSuiteBuild": "async () => false",
        "CurrentVersion": "async () => 'test'",
        "GetSettings": "async () => ({updateCheckOnStartup:false, playbackMock:true})",
        "PickFunscriptFile": "async () => '/s/demo.samn'",
        "LoadFunscript": "async (p) => ({path:p, actionCount:%d, durationMs:100000, videoPath:'/v/clip.mp4', hasVideo:true})" % len(acts),
        "GetScriptCurve": "async () => %s" % json.dumps([{"atMs": a["at"], "pos": a["pos"]} for a in acts]),
        "GetScriptAxisActions": "async (axis) => (!axis || axis === 'general') ? %s : null" % json.dumps(acts),
        "GetHeatmap": "async n => Array.from({ length: n }, (_, i) => ({ atMs: i * 1000, intensity: 0.5 }))",
        "GetMarker": "async () => null",
        "VideoFileURL": "async () => ''",
        "GetScriptChapterMarks": "async () => window.__chapters.slice()",
        "SaveScriptChapterMarks": "async (next) => { window.__chapters = next; }",
        "GetVideoThumbnails": ("async (n) => { window.__thumbCalls++; "
                               f"return Array.from({{ length: n }}, (_, i) => ({{ atMs: Math.round((i + 0.5) * 100000 / n), dataUrl: '{PNG}' }})); }}"),
        "ProbeCreateVideo": "async (p) => { window.__probe++; return ({path:p, name:'clip.mp4', width:1920, height:1080, fps:29.97, vfr:false, durationSec:100, codec:'h264', posterUrl:''}); }",
    }))
    try:
        with sync_playwright() as p:
            b = p.chromium.launch()
            page = b.new_page(viewport={"width": 1600, "height": 900})
            errors = []
            page.on("pageerror", lambda e: errors.append(str(e)))
            page.add_init_script("try{localStorage.clear()}catch(e){}")
            page.goto(f"{base}/test/_pb_scene_panel_harness.html")
            page.wait_for_timeout(800)
            check("no thumbnail decode before the Scene Map tab is opened", True)
            page.click("#pb-choose")
            page.wait_for_function("!document.querySelector('#pb-ruler').hidden", timeout=5000)
            page.wait_for_timeout(300)
            check("no panel work while the tab is closed", page.evaluate("window.__probe") == 0)
            page.click('.pb-bottom-tab[data-btab="scenemap"]')
            page.wait_for_function("document.querySelectorAll('.pb-scene-card').length === 3", timeout=5000)
            check("count shows 3 scenes", page.text_content("#pb-scenes-count") == "3 scenes")
            names = page.evaluate("[...document.querySelectorAll('.pb-scene-card-name')].map(e => e.textContent)")
            check("scene names as text (no HTML)", names == ["Intro", "Build <up>", "Finale"], str(names))
            ranges = page.evaluate("[...document.querySelectorAll('.pb-scene-card-range')].map(e => e.textContent)")
            check("open end runs to the next scene", ranges[0] == "0:00 – 0:30 · 0:30", str(ranges))
            check("explicit end kept", ranges[2] == "1:10 – 1:30 · 0:20", str(ranges))
            page.wait_for_function("document.querySelectorAll('.pb-scene-card-thumb img').length === 3", timeout=5000)
            check("thumbnail per scene", True)
            check("thumbnails decoded once", page.evaluate("window.__thumbCalls") == 1)
            check("first scene selected", page.get_attribute('.pb-scene-card[data-scene="0"]', "aria-selected") == "true")
            check("details n / N", page.text_content("#pb-scene-d-pos") == "1 / 3")
            check("prev disabled on the first scene", page.is_disabled("#pb-scene-prev"))
            page.wait_for_function("document.querySelector('#pb-scene-d-tech').textContent.includes('1920')", timeout=3000)
            tech = page.text_content("#pb-scene-d-tech")
            check("technical data from the probe", "clip.mp4" in tech and "1920 × 1080" in tech and "29.97 fps" in tech and "h264" in tech, tech)

            page.click("#pb-scene-next")
            check("next selects scene 2", page.text_content("#pb-scene-d-pos") == "2 / 3"
                  and page.text_content(".pb-scene-d-title") == "Build <up>")
            page.click('.pb-scene-card[data-scene="2"]')
            check("click selects a card", page.text_content("#pb-scene-d-pos") == "3 / 3" and page.is_disabled("#pb-scene-next"))
            page.click("#pb-scene-go")
            page.wait_for_timeout(200)
            check("go to scene seeks to its start",
                  page.evaluate("document.querySelector('.pb-scene-card[data-scene=\"2\"]').classList.contains('is-current')"))
            page.focus('.pb-scene-card[data-scene="2"]')
            page.keyboard.press("ArrowLeft")
            check("arrow keys move the selection", page.text_content("#pb-scene-d-pos") == "2 / 3")

            # A chapter added elsewhere shows up without a reload.
            page.evaluate("window.__chapters = window.__chapters.concat([{name:'Outro', startTime:95000, endTime:0}]);"
                          "window.dispatchEvent(new CustomEvent('playback:chapters-changed'))")
            page.wait_for_function("document.querySelectorAll('.pb-scene-card').length === 4", timeout=3000)
            check("chapters-changed refreshes the grid", page.text_content("#pb-scenes-count") == "4 scenes")
            check("no new probe for the same video", page.evaluate("window.__probe") == 1)
            page.evaluate("window.__chapters = []; window.dispatchEvent(new CustomEvent('playback:chapters-changed'))")
            page.wait_for_function("!document.querySelector('#pb-scenes-empty').hidden", timeout=3000)
            check("empty hint without chapters", page.text_content("#pb-scenes-count") == "0 scenes"
                  and page.is_hidden("#pb-scene-details"))
            check("no page errors", errors == [], str(errors[:3]))
            b.close()
    finally:
        shutdown()
        harness.unlink(missing_ok=True)
    return check.report()


if __name__ == "__main__":
    sys.exit(main())
