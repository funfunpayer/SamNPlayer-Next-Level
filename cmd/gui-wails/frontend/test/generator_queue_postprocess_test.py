"""Generate queue + Expert postprocess preview (DeepFunGen UX port).

Acceptance:
1. Multi-drop shows a Create queue with all paths (not "ignored").
2. Expert has Peak prominence + live postprocess probe text.
3. PreviewPostprocess is called when Expert opens / knobs change.
4. Everyday defaults stay prominence=0 (profile default).

Run: python3 cmd/gui-wails/frontend/test/generator_queue_postprocess_test.py
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
        "LoadFirstFrame": f"async () => ({{ width: 1280, height: 720, "
                          f"pngBase64: '{TINY_PNG_B64}' }})",
        "LoadFrameAt": f"async () => ({{ width: 1280, height: 720, "
                       f"pngBase64: '{TINY_PNG_B64}' }})",
        "SuggestPipeline": "async () => ({ Backend: 'csrt', Profile: 'standard', "
                           "Reason: 'everyday', GoPath: true })",
        "SuggestProfile": "async () => ({ found: false })",
        "CheckAIRoiAvailable": "async () => true",
        "CheckAudioCheckAvailable": "async () => true",
        "ScriptExistsForVideo": "async () => false",
        "AutoDetectROI": (
            "async (path) => { setTimeout(() => window.__triggerEvent && "
            "window.__triggerEvent('generate:autoroi', { "
            "videoPath: path, x: 10, y: 20, w: 40, h: 50 }), 0); }"
        ),
        "PreviewPostprocess": (
            "async (req) => { window.__calls.push(['PreviewPostprocess', req]); "
            "return { keyframeCount: 12, peakCount: 5, valleyCount: 5, "
            "meanHz: 1.1, hint: 'Probe: 12 keyframes, 5 peaks', "
            "sample: ["
            "  { atMs: 0, pos: 20 }, { atMs: 500, pos: 80 },"
            "  { atMs: 1000, pos: 25 }, { atMs: 1500, pos: 75 },"
            "  { atMs: 2000, pos: 30 }"
            "], "
            "rawSample: ["
            "  { atMs: 0, pos: 30 }, { atMs: 500, pos: 70 },"
            "  { atMs: 1000, pos: 35 }, { atMs: 1500, pos: 65 },"
            "  { atMs: 2000, pos: 40 }"
            "], "
            "peaks: ["
            "  { atMs: 500, pos: 80 }, { atMs: 1500, pos: 75 }"
            "] }; }"
        ),
        "GenerateScript": (
            "async (opts) => { window.__calls.push(['GenerateScript', opts]); "
            "setTimeout(() => window.__triggerEvent && window.__triggerEvent("
            "'generate:done', { path: opts.videoPath.replace(/\\\\.[^.]+$/, '') "
            "+ '.funscript', seq: 1 }), 0); }"
        ),
        "ImproveGeneratedScript": "async (opts) => ({ path: opts.path, message: 'ok' })",
        "GetScriptCurve": "async () => [{ atMs: 0, pos: 20 }, { atMs: 1000, pos: 80 }]",
    }))
    harness = FRONTEND / "test" / "_generator_queue_postprocess_harness.html"
    harness.write_text(PAGE)

    with sync_playwright() as p:
        browser = p.chromium.launch()
        page = browser.new_page()
        page.on("dialog", lambda d: d.dismiss())
        page.on("pageerror", lambda e: print("   [pageerror]", e))
        page.goto(f"{base}/test/_generator_queue_postprocess_harness.html")
        page.wait_for_function("window.__ready === true")

        check("Peak prominence control present (Expert)",
              page.locator("#gen-prominence").count() == 1)
        check("Expert knobs are range sliders",
              page.locator("#gen-prominence").evaluate("e => e.type") == "range"
              and page.locator("#gen-smooth").evaluate("e => e.type") == "range"
              and page.locator("#gen-maxspeed").evaluate("e => e.type") == "range")
        check("Everyday prominence default is 0 (profile default)",
              page.locator("#gen-prominence").input_value() == "0")
        check("Max speed default is 0 (off)",
              page.locator("#gen-maxspeed").input_value() == "0")
        check("Smooth default is 11",
              page.locator("#gen-smooth").input_value() == "11")
        check("Postprocess preview line present",
              page.locator("#gen-postprocess-preview").count() == 1)
        check("Queue panel hidden before multi-drop",
              page.locator("#gen-queue").is_hidden())

        # Multi-drop → queue UI
        page.evaluate("""window.dispatchEvent(new CustomEvent('drop:video', {
          detail: {
            path: '/v/a.mp4',
            paths: ['/v/a.mp4', '/v/b.mp4', '/v/c.mp4'],
            extraCount: 2,
          },
        }))""")
        page.wait_for_function(
            "document.querySelector('#gen-queue') && "
            "!document.querySelector('#gen-queue').hidden",
            timeout=5000)
        check("Queue visible after multi-drop",
              page.locator("#gen-queue").is_visible())
        check("Queue lists all three videos",
              page.locator("#gen-queue-list li").count() == 3,
              str(page.locator("#gen-queue-list li").count()))
        check("Create queue button enabled",
              page.locator("#gen-queue-run").is_enabled())

        # Expert live probe
        page.evaluate("document.querySelector('#gen-advanced').open = true")
        page.evaluate("document.querySelector('#gen-advanced-expert').open = true")
        page.wait_for_function(
            "window.__calls.some(c => c[0] === 'PreviewPostprocess')",
            timeout=3000)
        check("Opening Expert calls PreviewPostprocess",
              page.evaluate("window.__calls.filter(c => c[0]==='PreviewPostprocess').length") >= 1)
        page.locator("#gen-prominence").fill("0.35")
        page.wait_for_timeout(250)
        check("Prominence readout shows 0.35",
              page.locator("#gen-prominence-val").inner_text() == "0.35")
        page.wait_for_function(
            "window.__calls.filter(c => c[0]==='PreviewPostprocess').length >= 2",
            timeout=3000)
        last = page.evaluate(
            "window.__calls.filter(c => c[0]==='PreviewPostprocess').slice(-1)[0][1]")
        check("Prominence 0.35 reaches PreviewPostprocess",
              abs(float(last.get("peakProminence", 0)) - 0.35) < 1e-6, str(last))
        preview = page.locator("#gen-postprocess-preview").inner_text()
        check("Live preview shows probe hint",
              "12 keyframes" in preview or "Probe" in preview, preview)
        pts = page.locator("#gen-postprocess-poly").get_attribute("points") or ""
        check("Probe SVG polyline has sample points",
              pts.count(",") >= 4, pts)
        raw_pts = page.locator("#gen-postprocess-raw").get_attribute("points") or ""
        check("Probe SVG raw underlay has points",
              raw_pts.count(",") >= 4, raw_pts)
        check("Probe SVG peak markers rendered",
              page.locator("#gen-postprocess-peaks circle").count() >= 2)
        badge_kf = page.locator("#gen-pp-badge-kf").inner_text()
        check("Probe badge shows keyframe count",
              "12 kf" in badge_kf, badge_kf)
        badge_peaks = page.locator("#gen-pp-badge-peaks").inner_text()
        check("Probe badge shows peak count",
              "5 peaks" in badge_peaks, badge_peaks)

        browser.close()

    shutdown()
    harness.unlink(missing_ok=True)
    return check.report()


if __name__ == "__main__":
    sys.exit(main())
