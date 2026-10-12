"""Expert Smooth / Peakdist: preserve slider 0 (no || coerce).

Acceptance:
1. Smooth / Peakdist are range inputs with min=0.
2. Setting Smooth=0 / Peakdist=0 reaches PreviewPostprocess as 0
   (not coerced to 11 / 150).
3. Defaults remain 11 / 150 when untouched.

Run: python3 cmd/gui-wails/frontend/test/expert_smooth_peakdist_zero_test.py
"""

import pathlib
import sys

from playwright.sync_api import sync_playwright

from _harness import Checker, app_stub, serve

FRONTEND = pathlib.Path(__file__).resolve().parents[1]

PAGE = """<!doctype html><html><body><div id="root"></div>
<script type="module">
  import { initGenerator } from '/src/generator.js';
  initGenerator(document.querySelector('#root'), { loadScriptPath: async () => {} });
  window.__ready = true;
</script></body></html>"""

TINY_PNG_B64 = ("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNk"
                "+A8AAQUBAScY42YAAAAASUVORK5CYII=")


def main():
    check = Checker()
    base, shutdown = serve(app_stub({
        "PickVideoFile": "async () => '/tmp/clip.mp4'",
        "LoadFirstFrame": f"async () => ({{ width: 640, height: 360, "
                          f"pngBase64: '{TINY_PNG_B64}' }})",
        "LoadFrameAt": f"async () => ({{ width: 640, height: 360, "
                       f"pngBase64: '{TINY_PNG_B64}' }})",
        "SuggestPipeline": "async () => ({ Backend: 'csrt', Profile: 'standard', "
                           "Reason: 'test', GoPath: true })",
        "SuggestProfile": "async () => ({ found: false })",
        "CheckAIRoiAvailable": "async () => false",
        "CheckAudioCheckAvailable": "async () => true",
        "ScriptExistsForVideo": "async () => false",
        "PreviewPostprocess": (
            "async (req) => { window.__calls.push(['PreviewPostprocess', req]); "
            "return { keyframeCount: 4, peakCount: 2, valleyCount: 2, meanHz: 1.0, "
            "hint: 'ok', sample: [{atMs:0,pos:10},{atMs:1000,pos:90}], "
            "rawSample: [{atMs:0,pos:10},{atMs:1000,pos:90}], peaks: [] }; }"
        ),
    }))
    harness = FRONTEND / "test" / "_expert_smooth_peakdist_zero_harness.html"
    harness.write_text(PAGE)
    try:
        with sync_playwright() as p:
            browser = p.chromium.launch()
            page = browser.new_page()
            page.on("pageerror", lambda e: print("   [pageerror]", e))
            page.goto(f"{base}/test/_expert_smooth_peakdist_zero_harness.html")
            page.wait_for_function("window.__ready === true")

            check("smooth is range min 0",
                  page.locator("#gen-smooth").evaluate(
                      "e => e.type === 'range' && e.min === '0'"))
            check("peakdist is range min 0",
                  page.locator("#gen-peakdist").evaluate(
                      "e => e.type === 'range' && e.min === '0'"))
            check("smooth default 11",
                  page.locator("#gen-smooth").input_value() == "11")
            check("peakdist default 150",
                  page.locator("#gen-peakdist").input_value() == "150")

            # Open Advanced → Expert (toggle event drives first preview).
            page.evaluate("""() => {
              document.querySelector('#gen-advanced').open = true;
              const ex = document.querySelector('#gen-advanced-expert');
              ex.open = true;
              ex.dispatchEvent(new Event('toggle'));
            }""")
            page.wait_for_function(
                "() => (window.__calls || []).some(c => c[0] === 'PreviewPostprocess')",
                timeout=5000)
            page.evaluate(
                "window.__calls = (window.__calls || []).filter("
                "c => c[0] !== 'PreviewPostprocess')")

            page.evaluate("""() => {
              const s = document.querySelector('#gen-smooth');
              s.value = '0';
              s.dispatchEvent(new Event('input', { bubbles: true }));
              const p = document.querySelector('#gen-peakdist');
              p.value = '0';
              p.dispatchEvent(new Event('input', { bubbles: true }));
            }""")
            page.wait_for_function(
                "() => (window.__calls || []).some(c => c[0] === 'PreviewPostprocess')",
                timeout=5000)
            calls = page.evaluate(
                "() => (window.__calls || []).filter(c => c[0] === 'PreviewPostprocess')")
            req = calls[-1][1] if calls else {}
            sw = req.get("smoothWindow", req.get("SmoothWindow"))
            pd = req.get("minPeakDistanceMs", req.get("MinPeakDistanceMs"))
            check("PreviewPostprocess smoothWindow 0",
                  sw == 0, str(req))
            check("PreviewPostprocess minPeakDistanceMs 0",
                  pd == 0, str(req))
            check("smooth readout 0",
                  page.locator("#gen-smooth-val").inner_text() == "0")
            check("peakdist readout 0",
                  page.locator("#gen-peakdist-val").inner_text() == "0")

            browser.close()
    finally:
        shutdown()
        harness.unlink(missing_ok=True)
    return check.report()


if __name__ == "__main__":
    sys.exit(main())
