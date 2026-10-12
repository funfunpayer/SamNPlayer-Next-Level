"""V1 W1 workspace: the Edit view turns curve editing and the scene strip on
(Play keeps them as the user set them); a Generator result opens in Edit.

Run: python3 cmd/gui-wails/frontend/test/ws_edit_view_test.py
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
    harness = HERE / "_ws_edit_view_harness.html"
    harness.write_text(html)
    base, shutdown = serve(app_stub({
        "GetEULA": "async () => ({version:'x', text:'', accepted:true})",
        "IsSuiteBuild": "async () => false",
        "CurrentVersion": "async () => 'test'",
        "GetSettings": "async () => ({updateCheckOnStartup:false, playbackMock:true})",
        "PickFunscriptFile": "async () => '/s/demo.samn'",
        "LoadFunscript": "async (p) => ({path:p, actionCount:%d, durationMs:60000, videoPath:'', hasVideo:false})" % len(acts),
        "GetScriptCurve": "async () => %s" % json.dumps([{"atMs": a["at"], "pos": a["pos"]} for a in acts]),
        "GetScriptActions": "async () => %s" % json.dumps(acts),
        "GetScriptAxisActions": "async (axis) => (!axis || axis === 'general') ? %s : null" % json.dumps(acts),
        "GetHeatmap": "async n => Array.from({ length: n }, (_, i) => ({ atMs: i * 200, intensity: 0.5 }))",
        "GetMarker": "async () => null",
        "VideoFileURL": "async () => ''",
        "GetScriptChapterMarks": "async () => [{ name: 'Intro', startTime: 5000, endTime: 0 }]",
    }))
    try:
        with sync_playwright() as p:
            b = p.chromium.launch()
            page = b.new_page(viewport={"width": 1600, "height": 900})
            errors = []
            page.on("pageerror", lambda e: errors.append(str(e)))
            page.add_init_script("try{localStorage.clear()}catch(e){}")
            page.goto(f"{base}/test/_ws_edit_view_harness.html")
            page.wait_for_timeout(800)
            page.click("#pb-choose")
            page.wait_for_function("document.querySelector('#pb-curve-edit-row').style.display === 'flex'", timeout=5000)
            check("Play: curve editing off", not page.is_checked("#pb-curve-edit"))
            check("Play: scene strip off by default", page.is_hidden("#pb-scene-strip"))

            page.click('#ws-view [data-ws-view="edit"]')
            page.wait_for_function("document.querySelector('#pb-curve-edit').checked", timeout=3000)
            check("Edit: curve editing on", True)
            check("Edit: Inspector on View", page.evaluate("document.querySelector('#tab-playback').dataset.pbInspector") == "view")
            page.wait_for_function("!document.querySelector('#pb-scene-strip').hidden", timeout=3000)
            check("Edit: scene strip on (markers)", page.evaluate("document.querySelectorAll('.pb-scene-mark').length") == 1)
            check("Play preference for scenes untouched", page.evaluate("localStorage.getItem('pb.scenes')") is None)

            page.click('#ws-view [data-ws-view="play"]')
            page.wait_for_function("!document.querySelector('#pb-curve-edit').checked", timeout=3000)
            check("back to Play: editing off again", True)
            check("back to Play: scene strip off again", page.is_hidden("#pb-scene-strip"))

            # Generator result (review) → Edit view, editing on after the curve is drawn.
            page.click('#ws-view [data-ws-view="create"]')
            page.evaluate("window.dispatchEvent(new CustomEvent('ws:set', { detail: { view: 'edit' } }))")
            page.wait_for_function("document.body.dataset.wsView === 'edit'", timeout=3000)
            page.evaluate("document.querySelector('#pb-curve-edit').checked = false; document.querySelector('#pb-curve-edit-row').style.display = 'none'")
            page.evaluate("document.querySelector('#pb-curve-edit-row').style.display = 'flex'")
            page.wait_for_function("document.querySelector('#pb-curve-edit').checked", timeout=3000)
            check("result in Edit: editing on once the curve shows", True)
            check("no page errors", errors == [], str(errors[:3]))
            b.close()
    finally:
        shutdown()
        harness.unlink(missing_ok=True)
    return check.report()


if __name__ == "__main__":
    sys.exit(main())
