"""Play: contact vib live SVG probe (Create Feel parity).

Acceptance:
1. Contact block shows probe after load with contactVibration.
2. PreviewContactVibration is called with span/curve.
3. SVG stroke + vib polylines get points; hint updates.
4. Contact off hides the probe. No stroke rewrite APIs.

Run: python3 cmd/gui-wails/frontend/test/pb_contact_vib_probe_test.py
"""

import pathlib
import sys

from playwright.sync_api import sync_playwright

from _harness import Checker, app_stub, serve

PAGE = """<!doctype html><html><body><div id="root"></div>
<script type="module">
  import { initPlayback } from '/src/playback.js';
  window.__calls = [];
  window.__pb = initPlayback(document.querySelector('#root'));
  window.__ready = true;
</script></body></html>"""

TEST_DIR = pathlib.Path(__file__).resolve().parent


def main():
    check = Checker()
    base, shutdown = serve(app_stub({
        "PickFunscriptFile": "async () => '/tmp/contact.funscript'",
        "LoadFunscript": (
            "async () => ({ path: '/tmp/contact.funscript', actionCount: 4, "
            "durationMs: 100000, videoPath: '', hasVideo: false, "
            "contactVibration: true, contactVibrationSpan: 0.75, "
            "contactVibrationCurve: 'soft' })"
        ),
        "GetScriptCurve": (
            "async () => ["
            "{ atMs: 0, pos: 20 }, { atMs: 25000, pos: 80 },"
            "{ atMs: 50000, pos: 30 }, { atMs: 100000, pos: 70 }]"
        ),
        "GetHeatmap": (
            "async n => Array.from({ length: n }, (_, i) => "
            "({ atMs: Math.round(i * 100000 / n), intensity: 0.45 }))"
        ),
        "GetMarker": "async () => null",
        "SaveMarker": "async () => {}",
        "VideoFileURL": "async () => ''",
        "GetSpeedHighlights": "async () => []",
        "ExportScriptHeatmapPNG": "async () => '/tmp/h.png'",
        "SavePlaybackProject": "async () => '/tmp/p.snp.json'",
        "EditCapSpeedRange": "async () => { window.__calls.push(['EditCapSpeedRange']); }",
        "EditDeleteRange": "async () => { window.__calls.push(['EditDeleteRange']); }",
        "EditScaleRange": "async () => { window.__calls.push(['EditScaleRange']); }",
        "SnapTimeMs": "async (t, fps) => t",
        "GetScriptOffset": "async () => 0",
        "GetOMarkers": "async () => []",
        "SaveOMarkers": "async () => {}",
        "GetScriptBookmarks": "async () => []",
        "SaveScriptBookmarks": "async () => {}",
        "ScriptChapters": "async () => []",
        "AnalyzeScript": "async () => ({})",
        "GetScriptChapterMarks": "async () => []",
        "SaveScriptChapterMarks": "async () => {}",
        "GetVibrationCurvePreview": "async () => []",
        "GetPlaybackSource": "async () => ({})",
        "GetStrengthPresets": "async () => []",
        "PreviewContactVibration": (
            "async (req) => { window.__calls.push(['PreviewContactVibration', req]); "
            "return { hint: 'Feel probe · span 0.75 · soft · vib active ~40% · peak 80', "
            "peakVib: 0.8, activePct: 40, "
            "sample: ["
            "  { atMs: 0, stroke: 20, vib: 0 },"
            "  { atMs: 500, stroke: 80, vib: 70 },"
            "  { atMs: 1000, stroke: 30, vib: 10 },"
            "  { atMs: 1500, stroke: 75, vib: 65 }"
            "] }; }"
        ),
        "SaveContactSettings": (
            "async () => { window.__calls.push(['SaveContactSettings']); }"
        ),
    }))
    harness = TEST_DIR / "_pb_contact_vib_probe_harness.html"
    harness.write_text(PAGE)
    try:
        with sync_playwright() as p:
            browser = p.chromium.launch()
            page = browser.new_page()
            page.on("pageerror", lambda e: print("   [pageerror]", e))
            page.goto(f"{base}/test/_pb_contact_vib_probe_harness.html")
            page.wait_for_function("window.__ready === true")

            check("probe markup present",
                  page.locator("#pb-contact-vib-probe").count() == 1)
            check("contact block hidden before load",
                  page.locator("#pb-contact-block").evaluate("e => e.hidden") is True)

            page.click("#pb-choose")
            page.wait_for_function(
                "document.querySelector('#pb-contact-block') && "
                "!document.querySelector('#pb-contact-block').hidden",
                timeout=5000)
            page.wait_for_function(
                "window.__calls.some(c => c[0] === 'PreviewContactVibration')",
                timeout=3000)

            check("contact block visible after load",
                  page.locator("#pb-contact-block").evaluate("e => e.hidden") is False)
            check("Contact probe details present",
                  page.locator("#pb-contact-probe-details").count() == 1)
            page.evaluate("""() => {
              const t = document.querySelector('#pb-contact-tune-details');
              if (t) t.open = true;
              const d = document.querySelector('#pb-contact-probe-details');
              if (d) d.open = true;
            }""")
            check("PreviewContactVibration called",
                  page.evaluate(
                      "window.__calls.filter(c => c[0]==='PreviewContactVibration').length") >= 1)
            last = page.evaluate(
                "window.__calls.filter(c => c[0]==='PreviewContactVibration').slice(-1)[0][1]")
            check("span reaches preview API",
                  abs(float(last.get("span", 0)) - 0.75) < 1e-6, str(last))
            check("curve soft reaches preview API",
                  (last.get("curve") or "") == "soft", str(last))

            page.wait_for_timeout(200)
            stroke = page.locator("#pb-contact-vib-stroke").get_attribute("points") or ""
            vib = page.locator("#pb-contact-vib-poly").get_attribute("points") or ""
            check("stroke polyline has points", stroke.count(",") >= 3, stroke)
            check("vib polyline has points", vib.count(",") >= 3, vib)
            hint = page.locator("#pb-contact-vib-preview").inner_text()
            check("hint mentions probe", "probe" in hint.lower() or "span" in hint.lower(), hint)
            check("active badge shows percent",
                  "40% active" in page.locator("#pb-cv-badge-active").inner_text())
            check("peak badge shows peak",
                  "peak 80" in page.locator("#pb-cv-badge-peak").inner_text())
            check("vib peak dots rendered",
                  page.locator("#pb-contact-vib-peaks circle").count() >= 1)

            # Collapse, then tweak Strength — probe details should auto-open.
            page.evaluate(
                "document.querySelector('#pb-contact-probe-details').open = false")
            page.evaluate("""() => {
              const i = document.querySelector('#pb-contact-intensity');
              i.value = '1.2';
              i.dispatchEvent(new Event('input', { bubbles: true }));
            }""")
            page.wait_for_timeout(40)
            check("Strength tweak auto-opens Contact probe details",
                  page.locator("#pb-contact-probe-details").evaluate("e => e.open") is True)
            # Restore default strength for later ×0 assertions.
            page.evaluate("""() => {
              const i = document.querySelector('#pb-contact-intensity');
              i.value = '1';
              i.dispatchEvent(new Event('input', { bubbles: true }));
            }""")
            page.wait_for_timeout(200)

            before = page.evaluate(
                "window.__calls.filter(c => c[0]==='PreviewContactVibration').length")
            page.locator("#pb-contact-span").fill("0.45")
            page.wait_for_function(
                f"window.__calls.filter(c => c[0]==='PreviewContactVibration').length > {before}",
                timeout=3000)
            mid = page.evaluate(
                "window.__calls.filter(c => c[0]==='PreviewContactVibration').slice(-1)[0][1]")
            check("span change refreshes preview",
                  abs(float(mid.get("span", 0)) - 0.45) < 1e-6, str(mid))

            # Strength min=0 must not coerce to 1 via || in the probe.
            page.evaluate("""() => {
              const i = document.querySelector('#pb-contact-intensity');
              i.value = '0';
              i.dispatchEvent(new Event('input', { bubbles: true }));
            }""")
            page.wait_for_timeout(250)
            hint0 = page.locator("#pb-contact-vib-preview").inner_text()
            check("strength 0 shows ×0.00 in hint",
                  "×0.00" in hint0 or "x0.00" in hint0.lower(), hint0)
            check("strength 0 peak badge is peak 0",
                  "peak 0" in page.locator("#pb-cv-badge-peak").inner_text())

            page.locator("#pb-contact-off").check()
            page.wait_for_timeout(250)
            check("contact off hides probe",
                  page.locator("#pb-contact-vib-probe").evaluate("e => e.hidden") is True)
            check("no Cap/Scale/Delete from probe",
                  page.evaluate(
                      "window.__calls.filter(c => "
                      "['EditCapSpeedRange','EditScaleRange','EditDeleteRange'].includes(c[0])).length") == 0)

            browser.close()
    finally:
        shutdown()
        harness.unlink(missing_ok=True)
    return check.report()


if __name__ == "__main__":
    sys.exit(main())
