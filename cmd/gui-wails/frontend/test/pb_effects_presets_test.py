"""Effects sub-tabs (Effects · Curve · Limits · Advanced) and effect presets
(V1 target, feature map): existing controls regrouped, presets saved to the
settings file, applying one runs the controls' own save / device paths.

Run: python3 cmd/gui-wails/frontend/test/pb_effects_presets_test.py
"""

import json
import pathlib
import sys

from playwright.sync_api import sync_playwright

from _harness import Checker, app_stub, serve

HERE = pathlib.Path(__file__).resolve().parent

SAVED = {"Night": {"intensity": 40, "smoothing": 0.8, "maxSpeed": 0.4, "tickMs": 80, "softStartMs": 1500, "reducePeaks": True}}


def main():
    check = Checker()
    html = (HERE.parent / "index.html").read_text().replace('href="/src/style.css"', 'href="/src/style.css?real"')
    harness = HERE / "_pb_effects_presets_harness.html"
    harness.write_text(html)
    base, shutdown = serve(app_stub({
        "GetEULA": "async () => ({version:'x', text:'', accepted:true})",
        "IsSuiteBuild": "async () => false",
        "CurrentVersion": "async () => 'test'",
        "GetSettings": "async () => ({updateCheckOnStartup:false, playbackMock:true, playbackSmoothing:0.3, playbackSoftStartMs:500, playbackTickMs:50, playbackMaxSpeed:0.6, playbackEffectPresets: %s})"
                       % json.dumps(json.dumps(SAVED)),
        "SetSetting": "async (k, v) => { window.__saved = Object.assign(window.__saved || {}, { [k]: v }); }",
        "SetChannelLevels": "async (v, s) => { window.__levels = [v, s]; }",
        "SetPeakReduce": "async (on) => { window.__peaks = on; }",
        "PickFunscriptFile": "async () => '/s/demo.samn'",
        "LoadFunscript": "async (p) => ({path:p, actionCount:3, durationMs:60000, videoPath:'', hasVideo:false})",
        "GetScriptCurve": "async () => [{atMs:0,pos:0},{atMs:30000,pos:100},{atMs:60000,pos:0}]",
        "GetHeatmap": "async n => Array.from({ length: n }, (_, i) => ({ atMs: i * 200, intensity: 0.5 }))",
        "GetMarker": "async () => null",
        "VideoFileURL": "async () => ''",
    }))
    pane = "(k) => document.querySelector('#pb-fx-' + k)"
    try:
        with sync_playwright() as p:
            b = p.chromium.launch()
            page = b.new_page(viewport={"width": 1600, "height": 900})
            errors = []
            page.on("pageerror", lambda e: errors.append(str(e)))
            page.add_init_script("try{localStorage.clear()}catch(e){}")
            page.goto(f"{base}/test/_pb_effects_presets_harness.html")
            page.wait_for_function("!!document.querySelector('#pb-fx-preset')", timeout=8000)
            tabs = page.evaluate("[...document.querySelectorAll('.pb-fx-tab')].map(b => b.textContent)")
            check("four Effects sub-tabs", tabs == ["Effects", "Curve", "Limits", "Advanced"], str(tabs))
            where = page.evaluate("""() => Object.fromEntries(['#pb-knob-intensity', '#pb-fx-audio-sync', '#pb-smoothing', '#pb-sync',
                '#pb-maxspeed', '#pb-fx-reduce-peaks', '#pb-tick', '#pb-softstart'].map(s => [s, document.querySelector(s)?.closest('.pb-fx-pane')?.dataset.fxPane || null]))""")
            check("controls regrouped into the right panes", where == {
                "#pb-knob-intensity": "effects", "#pb-fx-audio-sync": "effects", "#pb-smoothing": "curve", "#pb-sync": "curve",
                "#pb-maxspeed": "limits", "#pb-fx-reduce-peaks": "limits", "#pb-tick": "advanced", "#pb-softstart": "advanced"}, str(where))
            page.click("#pb-choose")
            page.wait_for_function("document.querySelector('#tab-playback').dataset.pbState === 'loaded'", timeout=5000)
            page.click("#pb-insp-tab-devices")
            check("Effects pane shown first", page.is_visible("#pb-fx-effects") and not page.is_visible("#pb-fx-limits"))
            page.click("#pb-fx-tab-limits")
            check("Limits tab shows Max-Speed and Reduce peaks",
                  page.is_visible("#pb-maxspeed") and page.is_visible("#pb-fx-reduce-peaks") and not page.is_visible("#pb-fx-effects"))
            page.keyboard.press("ArrowRight")
            check("arrow key moves to Advanced", page.evaluate("document.activeElement.dataset.fx") == "advanced" and page.is_visible("#pb-tick"))
            check("tab remembered", page.evaluate("localStorage.getItem('pb.effectsTab')") == "advanced")

            page.click("#pb-fx-tab-effects")
            page.wait_for_function("[...document.querySelectorAll('#pb-fx-preset option')].some(o => o.value === 'Night')", timeout=3000)
            opts = page.evaluate("[...document.querySelectorAll('#pb-fx-preset option')].map(o => o.textContent)")
            check("saved preset listed with Custom / Default", opts == ["Custom", "Default", "Night"], str(opts))
            check("Delete disabled for Custom", page.is_disabled("#pb-fx-preset-delete"))
            page.select_option("#pb-fx-preset", "Night")
            page.wait_for_function("window.__peaks === true", timeout=3000)
            vals = page.evaluate("""() => ({i: +document.querySelector('#pb-knob-intensity').value, s: +document.querySelector('#pb-smoothing').value,
                m: +document.querySelector('#pb-maxspeed').value, t: +document.querySelector('#pb-tick').value,
                f: +document.querySelector('#pb-softstart').value, r: document.querySelector('#pb-knob-response-val').textContent})""")
            check("preset sets every control", vals == {"i": 40, "s": 0.8, "m": 0.4, "t": 80, "f": 1500, "r": "20%"}, str(vals))
            saved = page.evaluate("window.__saved")
            check("applied values saved like a manual change",
                  saved.get("playback.smoothing") == 0.8 and saved.get("playback.max_speed") == 0.4
                  and saved.get("playback.tick_ms") == 80 and saved.get("playback.soft_start_ms") == 1500, str(saved))
            check("intensity reaches the device", page.evaluate("window.__levels") == [0.4, 0.4], str(page.evaluate("window.__levels")))
            check("selection stays on the preset", page.evaluate("document.querySelector('#pb-fx-preset').value") == "Night")

            # A manual change turns the selection back to Custom.
            page.click("#pb-fx-tab-limits")
            page.fill("#pb-maxspeed", "1.2")
            page.dispatch_event("#pb-maxspeed", "change")
            page.locator("#pb-maxspeed").press("ArrowRight")
            check("manual change → Custom", page.evaluate("document.querySelector('#pb-fx-preset').value") == "")

            # Save a new preset (name with HTML stays text).
            page.click("#pb-fx-tab-effects")
            page.once("dialog", lambda d: d.accept("<b>Late</b>"))
            page.click("#pb-fx-preset-save")
            stored = json.loads(page.evaluate("window.__saved['playback.effect_presets']"))
            check("new preset stored in settings", "<b>Late</b>" in stored and "Night" in stored, str(stored.keys()))
            check("stored values match the controls", stored["<b>Late</b>"]["maxSpeed"] == page.evaluate("+document.querySelector('#pb-maxspeed').value"))
            check("preset name shown as text", page.evaluate("[...document.querySelectorAll('#pb-fx-preset option')].some(o => o.textContent === '<b>Late</b>')")
                  and page.evaluate("!document.querySelector('#pb-fx-preset b')"))
            check("new preset selected", page.evaluate("document.querySelector('#pb-fx-preset').value") == "<b>Late</b>")

            # Default restores the built-in values; Delete removes a preset.
            page.select_option("#pb-fx-preset", "__default")
            page.wait_for_function("window.__peaks === false", timeout=3000)
            check("Default restores defaults", page.evaluate("[+document.querySelector('#pb-knob-intensity').value, +document.querySelector('#pb-smoothing').value, +document.querySelector('#pb-tick').value]") == [100, 0.3, 50])
            page.select_option("#pb-fx-preset", "Night")
            page.click("#pb-fx-preset-delete")
            stored = json.loads(page.evaluate("window.__saved['playback.effect_presets']"))
            check("Delete removes the preset", "Night" not in stored and "<b>Late</b>" in stored, str(stored.keys()))
            check("no page errors", not errors, "; ".join(errors))
            b.close()
    finally:
        shutdown()
        harness.unlink(missing_ok=True)
    return check.report()


if __name__ == "__main__":
    sys.exit(main())
