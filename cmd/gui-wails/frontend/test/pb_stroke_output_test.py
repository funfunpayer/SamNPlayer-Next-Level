"""D3: the Player Inspector follows the connected device. A stroker (driven
linear channel) shows stroke on/off and a safety range, sent live via
SetStrokeOutput; rows for channels the device lacks are hidden. Without a
device every row shows.

Run: python3 cmd/gui-wails/frontend/test/pb_stroke_output_test.py
"""

import pathlib
import sys

from playwright.sync_api import sync_playwright

from _harness import Checker, app_stub, serve

HERE = pathlib.Path(__file__).resolve().parent

HANDY = ("{connected:true, name:'The Handy', capabilities:{device:'The Handy', features:"
         "[{channel:'stroke', kind:'linear', index:0, driven:true}]}}")
LELO = ("{connected:true, name:'LELO F1S V3', capabilities:{device:'LELO', features:"
        "[{channel:'vibration', kind:'scalar', index:0, driven:true}, {channel:'vibration2', kind:'scalar', index:1, driven:true}]}}")


def main():
    check = Checker()
    html = (HERE.parent / "index.html").read_text().replace('href="/src/style.css"', 'href="/src/style.css?real"')
    harness = HERE / "_pb_stroke_output_harness.html"
    harness.write_text(html)
    base, shutdown = serve("window.__stroke = [];\n" + app_stub({
        "GetEULA": "async () => ({version:'x', text:'', accepted:true})",
        "IsSuiteBuild": "async () => false",
        "CurrentVersion": "async () => 'test'",
        "GetSettings": "async () => ({updateCheckOnStartup:false})",
        "SetStrokeOutput": "async (on, lo, hi) => { window.__stroke.push([on, lo, hi]); return [lo, hi]; }",
        "SetMotorLevels": "async (m) => { window.__motors = (window.__motors || []).concat([m]); }",
    }))
    try:
        with sync_playwright() as p:
            b = p.chromium.launch()
            page = b.new_page(viewport={"width": 1600, "height": 900})
            errors = []
            page.on("pageerror", lambda e: errors.append(str(e)))
            page.add_init_script("try{localStorage.clear()}catch(e){}")
            page.goto(f"{base}/test/_pb_stroke_output_harness.html")
            page.wait_for_timeout(800)
            # The rows live in the Inspector → Devices panel (hidden before a script loads);
            # check DOM state (hidden attributes), not layout visibility.
            hidden = lambda sel: page.evaluate(f"document.querySelector('{sel}').hidden")
            check("stroke controls hidden without a stroker", hidden("#pb-stroke-ctl"))
            check("initial output sent: on, full range", page.evaluate("window.__stroke[0]") == [True, 0, 1])

            page.evaluate(f"window.dispatchEvent(new CustomEvent('device:status', {{detail: {HANDY}}}))")
            check("stroker shows the stroke controls", not hidden("#pb-stroke-ctl"))
            check("stroker without motors hides vibration / suction rows",
                  hidden('.pb-chan-row[data-axis="vibration"]') and hidden('.pb-chan-row[data-axis="suction"]'))
            check("stroke row stays", not hidden('.pb-chan-row[data-axis="stroke"]'))

            page.evaluate("const e = document.querySelector('#pb-stroke-lo'); e.value = '20'; e.dispatchEvent(new Event('input'))")
            page.evaluate("const e = document.querySelector('#pb-stroke-hi'); e.value = '25'; e.dispatchEvent(new Event('input'))")
            last = page.evaluate("window.__stroke[window.__stroke.length - 1]")
            check("range kept at least 10 % apart (moving top pushes bottom)", last == [True, 0.15, 0.25], str(last))
            check("range label", page.text_content("#pb-stroke-range-val") == "15–25 %")
            page.evaluate("const e = document.querySelector('#pb-stroke-on'); e.checked = false; e.dispatchEvent(new Event('change'))")
            check("stroke off sent", page.evaluate("window.__stroke[window.__stroke.length - 1][0]") is False)
            check("range sliders disabled while off", page.is_disabled("#pb-stroke-lo"))
            saved = page.evaluate("JSON.parse(localStorage.getItem('pb.chanLevels') || '{}')")
            check("stroke settings saved", saved.get("stroke") == {"on": False, "lo": 15, "hi": 25}, str(saved))
            # Changing a vibration level must not drop the stroke block.
            page.evaluate("const e = document.querySelector('#pb-chan-vibration-level'); e.value = '50'; e.dispatchEvent(new Event('input'))")
            saved = page.evaluate("JSON.parse(localStorage.getItem('pb.chanLevels') || '{}')")
            check("channel change keeps stroke settings", saved.get("stroke", {}).get("lo") == 15, str(saved))

            page.evaluate(f"window.dispatchEvent(new CustomEvent('device:status', {{detail: {LELO}}}))")
            check("vibrator: no stroke controls", hidden("#pb-stroke-ctl"))
            check("two motors → per-motor sliders", not hidden("#pb-motor-ctl")
                  and page.evaluate("document.querySelectorAll('#pb-motor-ctl input[data-motor]').length") == 2)
            labels = page.evaluate("[...document.querySelectorAll('#pb-motor-ctl .pb-motor-row span:first-child')].map(e => e.textContent)")
            check("motor names", labels == ["Vibration motor 1", "Vibration motor 2"], str(labels))
            check("levels sent on connect (full)", page.evaluate("window.__motors[window.__motors.length - 1]") == {"vibration": 1, "vibration2": 1})
            page.evaluate("const e = document.querySelector('#pb-motor-ctl input[data-motor=\"vibration2\"]'); e.value = '40'; e.dispatchEvent(new Event('input'))")
            check("motor 2 at 40 % sent", page.evaluate("window.__motors[window.__motors.length - 1]") == {"vibration": 1, "vibration2": 0.4})
            saved = page.evaluate("JSON.parse(localStorage.getItem('pb.chanLevels') || '{}')")
            check("motor levels saved, stroke kept", saved.get("motors") == {"vibration": 100, "vibration2": 40}
                  and saved.get("stroke", {}).get("lo") == 15, str(saved))
            page.evaluate("const e = document.querySelector('#pb-chan-vibration-level'); e.value = '60'; e.dispatchEvent(new Event('input'))")
            saved = page.evaluate("JSON.parse(localStorage.getItem('pb.chanLevels') || '{}')")
            check("channel change keeps motor levels", saved.get("motors", {}).get("vibration2") == 40, str(saved))
            page.evaluate(f"window.dispatchEvent(new CustomEvent('device:status', {{detail: {LELO}}}))")
            check("reconnect restores the saved motor level",
                  page.input_value('#pb-motor-ctl input[data-motor="vibration2"]') == "40")
            check("vibrator: vibration row (motor 2 counts as vibration), no suction row",
                  not hidden('.pb-chan-row[data-axis="vibration"]') and hidden('.pb-chan-row[data-axis="suction"]'))
            page.evaluate("window.dispatchEvent(new CustomEvent('device:status', {detail: {connected:false}}))")
            check("disconnected: no motor sliders", hidden("#pb-motor-ctl"))
            check("disconnected: every row back",
                  not hidden('.pb-chan-row[data-axis="vibration"]') and not hidden('.pb-chan-row[data-axis="suction"]'))
            check("no page errors", errors == [], str(errors[:3]))
            b.close()
    finally:
        shutdown()
        harness.unlink(missing_ok=True)
    return check.report()


if __name__ == "__main__":
    sys.exit(main())
