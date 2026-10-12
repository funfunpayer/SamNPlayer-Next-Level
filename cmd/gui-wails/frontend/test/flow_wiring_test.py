"""ChatGPT C1/C3/C4 wiring: Flow icons in the top bar, Player views and Tools
menu (labels kept); Flow stage in the empty Player and in "Feel without
film", following Play / Pause / Stop; images via build-safe URLs.

Run: python3 cmd/gui-wails/frontend/test/flow_wiring_test.py
"""

import pathlib
import sys

from playwright.sync_api import sync_playwright

from _harness import Checker, app_stub, serve

PAGE = """<!doctype html><html><head><link rel="stylesheet" href="/src/style.css?real"></head>
<body><div id="root"></div>
<script type="module">
  import { initPlayback } from '/src/playback.js';
  window.__pb = initPlayback(document.querySelector('#root'));
  window.__ready = true;
</script></body></html>"""

STATE = "document.querySelector('#pb-flow-novideo .flow-fx').dataset.state"


def check_playback(check, base):
    page_file = pathlib.Path(__file__).resolve().parent / "_flow_wiring_harness.html"
    page_file.write_text(PAGE)
    try:
        with sync_playwright() as p:
            b = p.chromium.launch()
            page = b.new_page(viewport={"width": 1280, "height": 900})
            errors = []
            page.on("pageerror", lambda e: errors.append(str(e)))
            page.goto(f"{base}/test/_flow_wiring_harness.html")
            page.wait_for_function("window.__ready === true")
            check("empty Player shows the Flow stage",
                  page.evaluate("!!document.querySelector('#pb-empty #pb-flow-empty .flow-fx svg')"))
            check("no /src/ image path left in the Player markup",
                  page.evaluate("![...document.querySelectorAll('#root img')].some(i => i.getAttribute('src')?.startsWith('/src/'))"))
            page.click("#pb-choose")
            page.wait_for_function("document.querySelector('#root').dataset.pbState === 'loaded'", timeout=5000)
            check("feel-without-film stage starts idle", page.evaluate(STATE) == "idle")
            page.click("#pb-play")
            page.wait_for_function(f"{STATE} === 'playing'", timeout=3000)
            check("Play → stage playing", True)
            page.click("#pb-play")
            page.wait_for_function(f"{STATE} === 'paused'", timeout=3000)
            check("Pause → stage paused", True)
            page.click("#pb-stop")
            page.wait_for_function(f"{STATE} === 'idle'", timeout=3000)
            check("Stop → stage idle", True)
            check("no page errors", not errors, "; ".join(errors))
            b.close()
    finally:
        page_file.unlink(missing_ok=True)


def check_icons(check, base):
    with sync_playwright() as p:
        b = p.chromium.launch()
        page = b.new_page(viewport={"width": 1440, "height": 900})
        errors = []
        page.on("pageerror", lambda e: errors.append(str(e)))
        page.goto(f"{base}/index.html")
        page.wait_for_function("document.querySelectorAll('#ws-view .ws-view-btn svg[stroke-width=\"1.6\"]').length === 3", timeout=8000)
        flow = "(sel) => [...document.querySelectorAll(sel)].map(b => !!b.querySelector(':scope > svg[stroke-width=\"1.6\"]'))"
        rail = page.evaluate(f"({flow})('#rail .tab-btn[data-tab]')")
        check("top bar uses Flow icons", rail and all(rail), str(rail))
        tools = page.evaluate(f"({flow})('#tools-nav .tab-btn[data-tab]')")
        check("Tools menu uses Flow icons", tools and all(tools), str(tools))
        check("Scene Map button uses the Flow icon",
              page.evaluate("!!document.querySelector('#ws-scenemap > svg[stroke-width=\"1.6\"]')"))
        labels = page.evaluate("[...document.querySelectorAll('#rail .tab-btn:not([hidden]) > span')].map(s => s.textContent)")
        check("top bar labels kept", labels == ["Player", "Training", "Tools"], str(labels))
        check("icons stay decorative",
              page.evaluate("[...document.querySelectorAll('#rail svg, #ws-view svg')].every(s => s.getAttribute('aria-hidden') === 'true')"))
        check("no page errors", not errors, "; ".join(errors))
        b.close()


def check_asset_paths(check):
    """A "/src/assets/…" string in a JS module works in tests and dev but is
    not rewritten by Vite, so the packaged app shows a broken image (logo and
    partner graphic in 0.6.0 before this fix). Use new URL(…, import.meta.url)."""
    src = pathlib.Path(__file__).resolve().parent.parent / "src"
    bad = [f.name for f in sorted(src.rglob("*.js")) if "/src/assets/" in f.read_text(encoding="utf-8")]
    check("no /src/assets/ string in JS modules", not bad, ", ".join(bad))


def main():
    check = Checker()
    check_asset_paths(check)
    base, shutdown = serve(app_stub({
        "GetSettings": "async () => ({ updateCheckOnStartup: false })",
        "GetEULA": "async () => ({ version: 'x', text: '', accepted: true })",
        "PickFunscriptFile": "async () => '/tmp/t.samn'",
        "LoadFunscript": "async () => ({ path: '/tmp/t.samn', actionCount: 3, durationMs: 100000, videoPath: '', hasVideo: false })",
        "GetScriptCurve": "async () => [{ atMs: 0, pos: 0 }, { atMs: 50000, pos: 50 }, { atMs: 100000, pos: 100 }]",
        "GetHeatmap": "async n => Array.from({ length: n }, (_, i) => ({ atMs: i * 1000, intensity: 0.5 }))",
        "GetMarker": "async () => null",
        "VideoFileURL": "async () => ''",
        "SnapTimeMs": "async (t) => t",
    }))
    try:
        check_playback(check, base)
        check_icons(check, base)
    finally:
        shutdown()
    return check.report()


if __name__ == "__main__":
    sys.exit(main())
