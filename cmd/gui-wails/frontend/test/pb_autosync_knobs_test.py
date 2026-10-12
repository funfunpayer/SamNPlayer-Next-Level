"""V1 S7b knobs (Intensity = master over the channel levels, Response =
inverse Play smoothing) and S8b Auto sync (audio): suggest only, Apply sets
the normal script offset.

Run: python3 cmd/gui-wails/frontend/test/pb_autosync_knobs_test.py
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
    harness = HERE / "_pb_autosync_knobs_harness.html"
    harness.write_text(html)
    base, shutdown = serve("window.__sync = { offsetMs: 420, currentMs: 0, r: 0.41, margin: 0.12, confidence: 'high', note: 'Clear match: +420 ms.' };\n" + app_stub({
        "GetEULA": "async () => ({version:'x', text:'', accepted:true})",
        "IsSuiteBuild": "async () => false",
        "CurrentVersion": "async () => 'test'",
        "GetSettings": "async () => ({updateCheckOnStartup:false, playbackMock:true, playbackSmoothing:0.3, playbackSoftStartMs:500, playbackTickMs:50, playbackMaxSpeed:400})",
        "SetSetting": "async (k, v) => { window.__saved = Object.assign(window.__saved || {}, { [k]: v }); }",
        "PickFunscriptFile": "async () => '/s/demo.samn'",
        "LoadFunscript": "async (p) => ({path:p, actionCount:%d, durationMs:60000, videoPath:'/v/clip.mp4', hasVideo:true})" % len(acts),
        "GetScriptCurve": "async () => %s" % json.dumps([{"atMs": a["at"], "pos": a["pos"]} for a in acts]),
        "GetHeatmap": "async n => Array.from({ length: n }, (_, i) => ({ atMs: i * 200, intensity: 0.5 }))",
        "GetMarker": "async () => null",
        "VideoFileURL": "async () => ''",
        "SetChannelLevels": "async (v, s) => { window.__levels = [v, s]; }",
        "SetPeakReduce": "async (on) => { window.__peaks = on; }",
        "SetScriptOffset": "async (ms) => { window.__offset = ms; }",
        "GetScriptAxisActions": "async (axis) => axis === 'vibration' ? %s : (!axis || axis === 'general') ? %s : null" % (json.dumps([{"at": a["at"], "pos": 50} for a in acts]), json.dumps(acts)),
        "GetAudioLevelEnvelope": "async () => Array.from({ length: 1200 }, (_, i) => Math.abs(Math.sin(i / 9)))",
        "EstimateAudioSyncOffset": "async () => { window.__syncCalls = (window.__syncCalls || 0) + 1; return window.__sync; }",
    }))
    try:
        with sync_playwright() as p:
            b = p.chromium.launch()
            page = b.new_page(viewport={"width": 1600, "height": 900})
            errors = []
            page.on("pageerror", lambda e: errors.append(str(e)))
            page.add_init_script("try{localStorage.clear()}catch(e){}")
            page.goto(f"{base}/test/_pb_autosync_knobs_harness.html")
            page.wait_for_timeout(800)
            check("Auto sync disabled before a script", page.is_disabled("#pb-auto-sync"))
            page.click("#pb-choose")
            page.wait_for_function("document.querySelector('#tab-playback').dataset.pbState === 'loaded'", timeout=5000)
            # Knobs (Inspector → Devices).
            check("Effects region with two knobs", page.evaluate("document.querySelectorAll('[data-pb-region=\"effects\"] .pb-knob').length") == 2)
            labels = page.evaluate("[...document.querySelectorAll('.pb-knob-label')].map(e => e.textContent)")
            check("Intensity · Response (no Frequency)", labels == ["Intensity", "Response"], str(labels))
            page.wait_for_function("document.querySelector('#pb-knob-response-val').textContent === '70%'", timeout=3000)
            check("Response mirrors smoothing 0.3 → 70%", True)
            page.evaluate("""() => { const k = document.querySelector('#pb-knob-intensity'); k.value = '50';
                k.dispatchEvent(new Event('input')); }""")
            check("Intensity 50% scales both channels", page.evaluate("window.__levels") == [0.5, 0.5])
            page.fill("#pb-chan-vibration-level", "40")
            page.dispatch_event("#pb-chan-vibration-level", "input")
            check("master × channel level", page.evaluate("window.__levels") == [0.2, 0.5])
            check("Intensity remembered", page.evaluate("JSON.parse(localStorage.getItem('pb.chanLevels')).master") == 50)
            page.evaluate("""() => { const k = document.querySelector('#pb-knob-response'); k.value = '100';
                k.dispatchEvent(new Event('input')); k.dispatchEvent(new Event('change')); }""")
            check("Response 100% → smoothing 0", page.evaluate("document.querySelector('#pb-smoothing').value") == "0")
            check("smoothing saved as setting", (page.evaluate("window.__saved || {}") or {}).get("playback.smoothing") == 0)
            # Sync with audio mirrors the Audio tab switch.
            page.check("#pb-fx-audio-sync")
            check("Sync with audio sets the real switch", page.evaluate("document.querySelector('#pb-audio-vibration').checked"))
            page.evaluate("""() => { const s = document.querySelector('#pb-audio-vibration'); s.checked = false;
                s.dispatchEvent(new Event('change', { bubbles: true })); }""")
            check("and follows it back", not page.is_checked("#pb-fx-audio-sync"))
            # Reduce peaks.
            check("Reduce peaks off by default", page.evaluate("window.__peaks") is False and not page.is_checked("#pb-fx-reduce-peaks"))
            page.click("#pb-fx-tab-limits")  # Reduce peaks lives under Effects → Limits
            page.check("#pb-fx-reduce-peaks")
            check("Reduce peaks → SetPeakReduce(true)", page.evaluate("window.__peaks") is True)
            check("Reduce peaks remembered", page.evaluate("JSON.parse(localStorage.getItem('pb.chanLevels')).reducePeaks") is True)
            # Lane names column (Timeline).
            page.wait_for_function("document.querySelectorAll('.pb-lane-name').length >= 3", timeout=5000)
            lanes = page.evaluate("[...document.querySelectorAll('.pb-lane-name')].map(e => e.textContent.trim())")
            check("lane names Stroke · Vibration · Audio Level", lanes == ["Stroke", "Vibration", "Audio Level"], str(lanes))
            off = page.evaluate("""() => { const n = document.querySelector('.pb-lane-name[data-lane="stroke"]').getBoundingClientRect();
                const c = document.querySelector('#pb-curve').getBoundingClientRect();
                return [Math.round(n.top - c.top), Math.round(c.left - n.right)]; }""")
            check("Stroke name sits left of the curve, same top", abs(off[0]) <= 1 and off[1] >= 0, str(off))
            check("no in-canvas lane labels in the V1 layout", page.evaluate("document.querySelector('#tab-playback').dataset.pbLayout") == "v1")
            page.click("#pb-fx-tab-effects")
            page.focus("#pb-knob-intensity")
            page.keyboard.press("ArrowLeft")
            check("knob works with the keyboard", page.text_content("#pb-knob-intensity-val") == "45%")

            # Auto sync (Inspector → Playback → Script Tools).
            page.click('.pb-insp-tab[data-insp="playback"]')
            page.wait_for_function("!document.querySelector('#pb-auto-sync').disabled", timeout=3000)
            check("enabled with script + video", True)
            page.click("#pb-auto-sync")
            page.wait_for_function("!document.querySelector('#pb-autosync-apply').hidden", timeout=3000)
            check("suggestion shown, nothing applied yet", "+420" in page.text_content("#pb-autosync-apply")
                  and page.evaluate("window.__offset") is None)
            page.click("#pb-autosync-apply")
            page.wait_for_function("window.__offset === 420", timeout=3000)
            check("Apply sets the script offset", page.input_value("#pb-offset") == "420")
            page.evaluate("window.__sync = { offsetMs: 0, currentMs: 420, r: 0.03, margin: 0.01, confidence: 'none', note: 'No clear match.' }")
            page.click("#pb-auto-sync")
            page.wait_for_function("document.querySelector('#pb-autosync-status').textContent.includes('No clear match')", timeout=3000)
            check("no match → no Apply", page.is_hidden("#pb-autosync-apply"))
            check("no page errors", errors == [], str(errors[:3]))
            b.close()
    finally:
        shutdown()
        harness.unlink(missing_ok=True)
    return check.report()


if __name__ == "__main__":
    sys.exit(main())
