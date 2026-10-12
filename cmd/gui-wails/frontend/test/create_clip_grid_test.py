"""Create source clips grid (V1 target create.png): a multi-video drop shows
one card per clip with poster, name, duration and status; the loaded clip is
highlighted; clicking another card loads it and keeps the queue; file names
are always text (a "<" in a name used to break the list).

Run: python3 cmd/gui-wails/frontend/test/create_clip_grid_test.py
"""

import pathlib
import sys

from playwright.sync_api import sync_playwright

from _harness import Checker, app_stub, serve

FRONTEND = pathlib.Path(__file__).resolve().parents[1]

PAGE = """<!doctype html><html><head><link rel="stylesheet" href="/src/style.css?real"></head>
<body><div id="root"></div>
<script type="module">
  import { initGenerator } from '/src/generator.js';
  initGenerator(document.querySelector('#root'), { loadScriptPath: async () => {} });
  window.__ready = true;
</script></body></html>"""

TINY_PNG_B64 = ("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNk"
                "+A8AAQUBAScY42YAAAAASUVORK5CYII=")
POSTER = "data:image/png;base64," + TINY_PNG_B64
PATHS = ["/v/a.mp4", "/v/b <b>x.mp4", "/v/c.mp4"]


def main():
    check = Checker()
    base, shutdown = serve("window.__probes = [];\n" + app_stub({
        "LoadFirstFrame": f"async () => ({{ width: 1280, height: 720, pngBase64: '{TINY_PNG_B64}' }})",
        "LoadFrameAt": f"async () => ({{ width: 1280, height: 720, pngBase64: '{TINY_PNG_B64}' }})",
        "SuggestPipeline": "async () => ({ Backend: 'csrt', Profile: 'standard', Reason: 'x', GoPath: true })",
        "SuggestProfile": "async () => ({ found: false })",
        "ScriptExistsForVideo": "async () => false",
        "AutoDetectROI": "async () => {}",
        "ProbeCreateVideo": (
            "async (p) => { window.__probes.push(p);"
            " if (p.includes('c.mp4')) throw new Error('unreadable');"
            f" return {{ path: p, name: p.split('/').pop(), width: 1280, height: 720, fps: 30, durationSec: p.includes('a.mp4') ? 75 : 3725, codec: 'h264', posterUrl: '{POSTER}' }}; }}"
        ),
    }))
    harness = FRONTEND / "test" / "_create_clip_grid_harness.html"
    harness.write_text(PAGE)
    try:
        with sync_playwright() as p:
            b = p.chromium.launch()
            page = b.new_page(viewport={"width": 1400, "height": 900})
            errors = []
            page.on("dialog", lambda d: d.dismiss())
            page.on("pageerror", lambda e: errors.append(str(e)))
            page.goto(f"{base}/test/_create_clip_grid_harness.html")
            page.wait_for_function("window.__ready === true")
            page.evaluate("""(paths) => window.dispatchEvent(new CustomEvent('drop:video', {
              detail: { path: paths[0], paths, extraCount: paths.length - 1 } }))""", PATHS)
            page.wait_for_function("document.querySelectorAll('#gen-queue-list li.gen-clip').length === 3", timeout=5000)
            names = page.evaluate("[...document.querySelectorAll('.gen-clip-name')].map(n => n.textContent)")
            check("one card per clip, names as text", names == ["a.mp4", "b <b>x.mp4", "c.mp4"], str(names))
            check("no HTML injected from a file name", page.evaluate("!document.querySelector('#gen-queue-list b')"))
            page.wait_for_function("document.querySelectorAll('.gen-clip-thumb img:not([hidden])').length === 2", timeout=5000)
            metas = page.evaluate("[...document.querySelectorAll('.gen-clip-meta')].map(n => n.textContent)")
            check("duration + status per card", metas == ["01:15 · queued", "1:02:05 · queued", "queued"], str(metas))
            check("unreadable clip keeps its card without a poster",
                  page.evaluate("document.querySelectorAll('.gen-clip')[2].querySelector('img').hidden"))
            probes = page.evaluate("window.__probes")
            # Grid: each clip once (cached); the header reads the loaded clip once more.
            check("each clip probed once by the grid (+1 for the header)",
                  sorted(set(probes)) == sorted(PATHS) and probes.count(PATHS[0]) == 2
                  and probes.count(PATHS[1]) == 1 and probes.count(PATHS[2]) == 1, str(probes))
            check("loaded clip highlighted", page.evaluate(
                "[...document.querySelectorAll('.gen-clip')].map(c => c.classList.contains('is-current'))") == [True, False, False])

            page.click(".gen-clip >> nth=1")
            page.wait_for_function("document.querySelectorAll('.gen-clip')[1].classList.contains('is-current')", timeout=5000)
            check("click loads that clip into Create", page.text_content("#gen-project-name") == "b <b>x.mp4")
            check("queue kept after the click", page.locator("#gen-queue-list li").count() == 3 and page.is_visible("#gen-queue"))
            check("no page errors", not errors, "; ".join(errors))
            b.close()
    finally:
        shutdown()
        harness.unlink(missing_ok=True)
    return check.report()


if __name__ == "__main__":
    sys.exit(main())
