"""V1 Player shell (S3, docs/V1_PLAN.md): Library · main · Inspector with
tabs Devices / Audio / Playback / View, bottom tabs Timeline / Chapters /
Bookmarks / Scene Map / Audio Analysis. Existing controls are moved, not
re-created — ids stay unique and keep working.

Run: python3 cmd/gui-wails/frontend/test/player_layout_test.py
"""

import json
import pathlib
import sys

from playwright.sync_api import sync_playwright

from _harness import Checker, app_stub, serve

HERE = pathlib.Path(__file__).resolve().parent


def main():
    check = Checker()
    acts = [{"at": i * 500, "pos": 10 if i % 2 else 90} for i in range(120)]
    html = (HERE.parent / "index.html").read_text().replace('href="/src/style.css"', 'href="/src/style.css?real"')
    harness = HERE / "_player_layout_harness.html"
    harness.write_text(html)
    base, shutdown = serve(app_stub({
        "GetEULA": "async () => ({version:'x', text:'', accepted:true})",
        "IsSuiteBuild": "async () => false",
        "CurrentVersion": "async () => 'test'",
        "GetSettings": "async () => ({updateCheckOnStartup:false, playbackMock:true})",
        "PickFunscriptFile": "async () => '/s/demo.samn'",
        "LoadFunscript": "async (p) => { window.__loaded = (window.__loaded || []).concat([p]); return ({path:p, actionCount:%d, durationMs:60000, videoPath:'', hasVideo:false}); }" % len(acts),
        "GetScriptCurve": "async () => %s" % json.dumps([{"atMs": a["at"], "pos": a["pos"]} for a in acts]),
        "GetScriptAxisActions": "async (axis) => (!axis || axis === 'general') ? %s : null" % json.dumps(acts),
        "GetHeatmap": "async n => Array.from({ length: n }, (_, i) => ({ atMs: i * 200, intensity: 0.5 }))",
        "GetMarker": "async () => null",
        "VideoFileURL": "async () => ''",
        "SetChannelLevels": "async (v, s) => { window.__levels = [v, s]; }",
        "SaveVideoSnapshot": "async (ms) => { window.__snap = ms; return '/out/frame.png'; }",
        "SetPlaybackVideo": "async (p) => { window.__linked = p; return ''; }",
    }))
    try:
        with sync_playwright() as p:
            b = p.chromium.launch()
            page = b.new_page(viewport={"width": 1600, "height": 900})
            errors = []
            page.on("pageerror", lambda e: errors.append(str(e)))
            page.add_init_script("try{localStorage.clear()}catch(e){}")
            page.goto(f"{base}/test/_player_layout_harness.html")
            page.wait_for_timeout(800)
            root = "document.querySelector('#tab-playback')"
            check("layout v1 applied", page.evaluate(f"{root}.dataset.pbLayout") == "v1")
            for r in ["library", "main", "inspector-tabs", "bottom", "bottom-tabs"]:
                check(f"region '{r}' present", page.evaluate(f"!!document.querySelector('[data-pb-region=\"{r}\"]')"))
            dup = page.evaluate("""() => { const seen = {}; let d = [];
                document.querySelectorAll('#tab-playback [id]').forEach(e => { if (seen[e.id]) d.push(e.id); seen[e.id] = 1; }); return d; }""")
            check("no duplicate ids after moving controls", dup == [], str(dup[:5]))

            check("Library visible", page.is_visible(".pb-library"))
            check("Recent starts empty", "Nothing yet" in page.text_content("#pb-lib-recent"))
            page.click("#pb-lib-local")
            page.wait_for_function(f"{root}.dataset.pbState === 'loaded'", timeout=5000)
            check("Local Files opens the script picker and loads", page.evaluate("window.__loaded") == ["/s/demo.samn"])
            page.wait_for_function("document.querySelector('#pb-lib-recent').textContent.includes('demo.samn')", timeout=3000)
            check("Recent lists the loaded script", True)
            page.click(".pb-lib-recent-item")
            page.wait_for_function("window.__loaded.length === 2", timeout=3000)
            check("Recent entry reloads it", page.evaluate("window.__loaded[1]") == "/s/demo.samn")
            # Favorites (S6): star next to the script name, list in the Library.
            check("Favorites starts with a hint", "Star a script" in page.text_content("#pb-lib-fav"))
            check("star enabled once loaded", page.is_enabled("#pb-fav-toggle")
                  and page.get_attribute("#pb-fav-toggle", "aria-pressed") == "false")
            page.click("#pb-fav-toggle")
            check("star → favorite", page.get_attribute("#pb-fav-toggle", "aria-pressed") == "true"
                  and "demo.samn" in page.text_content("#pb-lib-fav"))
            check("favorites remembered", page.evaluate("JSON.parse(localStorage.getItem('pb.favorites'))") == ["/s/demo.samn"])
            page.click("#pb-lib-fav .pb-lib-recent-item")
            page.wait_for_function("window.__loaded.length === 3", timeout=3000)
            check("favorite entry loads it", page.evaluate("window.__loaded[2]") == "/s/demo.samn")
            page.click("#pb-fav-toggle")
            check("star again removes it", "Star a script" in page.text_content("#pb-lib-fav"))

            tabs = page.evaluate("[...document.querySelectorAll('.pb-insp-tab')].map(b => b.textContent.trim())")
            check("Inspector tabs Devices · Audio · Playback · View", tabs == ["Devices", "Audio", "Playback", "View"], str(tabs))
            check("Devices: channel rows Stroke / Vibration / Suction",
                  page.evaluate("[...document.querySelectorAll('.pb-chan-name')].map(e => e.textContent)") == ["Stroke", "Vibration", "Suction"])
            check("Devices shows the axis row", page.is_visible("#pb-axis-row"))
            check("channels start at full output", page.evaluate("window.__levels") == [1, 1])
            page.fill("#pb-chan-vibration-level", "50")
            page.dispatch_event("#pb-chan-vibration-level", "input")
            check("vibration level 50% → SetChannelLevels(0.5, 1)", page.evaluate("window.__levels") == [0.5, 1])
            page.uncheck("#pb-chan-suction-on")
            check("suction off → level 0", page.evaluate("window.__levels") == [0.5, 0]
                  and page.text_content("#pb-chan-suction-lvl") == "Off")
            check("channel settings remembered",
                  page.evaluate("JSON.parse(localStorage.getItem('pb.chanLevels')).suction.on") is False)
            page.check("#pb-chan-suction-on")
            check("Stroke row has no level control (drives the recipe)", page.evaluate("!document.querySelector('#pb-chan-stroke-level')"))
            page.click('.pb-insp-tab[data-insp="audio"]')
            check("Audio tab: BPM and audio vibration", page.is_visible("#pb-bpm-auto") and page.is_visible("#pb-audio-vibration"))
            check("Devices content hidden on Audio tab", page.is_hidden("#pb-axis-row"))
            page.click('.pb-insp-tab[data-insp="playback"]')
            check("Playback tab: Script Tools (offset)", page.is_visible("#pb-script-tools-card"))
            page.click('.pb-insp-tab[data-insp="view"]')
            check("View tab: 0–100 overlay control", page.is_visible("#pb-pos-overlay-toggle") or page.evaluate("!!document.querySelector('#pb-insp-view #pb-pos-overlay-row')"))
            page.keyboard.press("ArrowLeft")
            check("← on inspector tabs", page.evaluate(f"{root}.dataset.pbInspector") in ("view", "playback"))

            btabs = page.evaluate("[...document.querySelectorAll('.pb-bottom-tab')].map(b => b.textContent.trim())")
            check("bottom tabs as mockup", btabs == ["Timeline", "Chapters", "Bookmarks", "Scene Map", "Audio Analysis"], str(btabs))
            check("Timeline tab shows the curve", page.is_visible("#pb-curve"))
            page.click('.pb-bottom-tab[data-btab="chapters"]')
            check("Chapters tab shows the chapter section", page.is_visible("#pb-chapter-details"))
            check("curve hidden while Chapters shown", page.is_hidden("#pb-curve"))
            page.click('.pb-bottom-tab[data-btab="timeline"]')
            page.click('.pb-bottom-tab[data-btab="scenemap"]')
            check("Scene Map bottom tab shows the scenes panel", page.is_visible("#pb-scenes-count"))
            page.click("#pb-scenes-heatmap")
            check("Scene Map header link opens the Create rhythm heatmap",
                  page.get_attribute("#tab-generator", "data-view") == "scenemap")
            page.click('#rail .tab-btn[data-tab="playback"]')
            page.click("#pb-lib-editor")
            check("Library → Editor turns on curve editing", page.is_checked("#pb-curve-edit")
                  and page.evaluate(f"{root}.dataset.pbInspector") == "view")
            page.click("#pb-lib-bpm")
            check("Library → BPM Detection opens Inspector Audio", page.evaluate(f"{root}.dataset.pbInspector") == "audio")
            # Video bar above the video.
            page.click('#rail .tab-btn[data-tab="playback"]')
            check("video bar present", page.is_visible("[data-pb-region='videobar']"))
            check("no video → placeholder text", page.text_content("#pb-vb-name") == "Select a video file…")
            check("snapshot disabled without a video", page.is_disabled("#pb-vb-snapshot"))
            page.evaluate(f"{root}.dataset.pbVideo = 'true'; {root}.dataset.pbVideoPath = '/v/My Clip.mp4'")
            page.wait_for_function("document.querySelector('#pb-vb-name').textContent === 'My Clip.mp4'", timeout=3000)
            check("video bar shows the video name", True)
            page.click("#pb-vb-snapshot")
            page.wait_for_function("window.__snap !== undefined", timeout=3000)
            check("snapshot saves via SaveVideoSnapshot", "frame.png" in page.text_content("#pb-vb-status"))
            page.evaluate("window.__fsv = 0; document.querySelector('#pb-video-fs').addEventListener('click', () => window.__fsv++)")
            page.click("#pb-vb-fs")
            check("video bar fullscreen", page.evaluate("window.__fsv") == 1)
            page.click("#pb-vb-bookmark")
            check("bookmark icon opens Bookmarks", page.evaluate(f"{root}.dataset.pbBottom") == "bookmarks")
            # Scripts (S6): Imported / Generated / Edited from real origins.
            check("Imported lists the opened script", page.text_content("#pb-lib-imported-count") == "1")
            check("Generated / Edited start empty", page.text_content("#pb-lib-generated-count") == ""
                  and page.text_content("#pb-lib-edited-count") == "")
            page.evaluate("""() => {
                window.dispatchEvent(new CustomEvent('pb:loaded', { detail: { path: '/out/My "best" <cut>.samn', origin: 'generated' } }));
                window.dispatchEvent(new CustomEvent('pb:edited', { detail: { path: '/s/demo.samn' } })); }""")
            check("Generator result → Generated", page.text_content("#pb-lib-generated-count") == "1"
                  and page.text_content("#pb-lib-imported-count") == "1")
            check("saved edit → Edited", page.text_content("#pb-lib-edited-count") == "1")
            page.click("#pb-lib-generated-btn")
            check("category list unfolds", page.is_visible("#pb-lib-generated")
                  and page.get_attribute("#pb-lib-generated-btn", "aria-expanded") == "true")
            item = page.query_selector("#pb-lib-generated .pb-lib-recent-item")
            check("paths with quotes / < are escaped", item.get_attribute("title") == '/out/My "best" <cut>.samn'
                  and item.text_content() == 'My "best" <cut>.samn')
            n = page.evaluate("window.__loaded.length")
            item.click()
            page.wait_for_function(f"window.__loaded.length === {n + 1}", timeout=3000)
            check("generated entry loads it, stays out of Imported",
                  page.text_content("#pb-lib-imported-count") == "1")
            # Recent videos (S6): chevron menu next to the video name.
            page.evaluate(f"{root}.dataset.pbVideoPath = '/v/Other.mp4'")
            page.wait_for_function("document.querySelector('#pb-vb-name').textContent === 'Other.mp4'", timeout=3000)
            page.click("#pb-vb-recent")
            check("recent-video menu open", page.is_visible("#pb-vb-menu")
                  and page.get_attribute("#pb-vb-recent", "aria-expanded") == "true")
            items = page.evaluate("[...document.querySelectorAll('.pb-vb-menu-item')].map(b => b.textContent.trim())")
            check("menu lists earlier videos, not the current one", items == ["My Clip.mp4"], str(items))
            page.click(".pb-vb-menu-item")
            page.wait_for_function("window.__linked === '/v/My Clip.mp4'", timeout=3000)
            check("menu entry links that video", page.is_hidden("#pb-vb-menu"))
            check("no page errors", errors == [], str(errors[:3]))
            b.close()
    finally:
        shutdown()
        harness.unlink(missing_ok=True)
    return check.report()


if __name__ == "__main__":
    sys.exit(main())
