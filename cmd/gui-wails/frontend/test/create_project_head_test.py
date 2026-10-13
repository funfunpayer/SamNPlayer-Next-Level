"""Create project header (V1 target create.png): after choosing a video the
header shows the real file name, resolution, fps, duration and codec from
ffprobe plus a poster frame. Nothing is shown before a video; a failed
probe says so instead of inventing values; a slow old probe never
overwrites the header of a newer video.

Run: python3 cmd/gui-wails/frontend/test/create_project_head_test.py
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
  initGenerator(document.querySelector('#root'), { loadScriptPath: () => {} });
  window.__ready = true;
</script></body></html>"""

TINY_PNG_B64 = ("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNk"
                "+A8AAQUBAScY42YAAAAASUVORK5CYII=")
POSTER = "data:image/png;base64," + TINY_PNG_B64


def main():
    check = Checker()
    base, shutdown = serve("window.__pick = '/films/Night Walk.mp4';\n" + app_stub({
        "PickVideoFile": "async () => window.__pick",
        "LoadFirstFrame": f"async () => ({{ width: 1920, height: 1080, pngBase64: '{TINY_PNG_B64}' }})",
        "LoadFrameAt": f"async () => ({{ width: 1920, height: 1080, pngBase64: '{TINY_PNG_B64}' }})",
        "SuggestPipeline": "async () => ({ Backend: 'csrt', Profile: 'standard', Reason: 'x', GoPath: true })",
        "SuggestProfile": "async () => ({ found: false })",
        "ScriptExistsForVideo": "async () => false",
        "ProbeCreateVideo": (
            "async (p) => {"
            " if (p.includes('broken')) throw new Error('not a video');"
            " if (p.includes('slow')) { await new Promise(r => setTimeout(r, 600)); return { name: 'slow.mp4', width: 640, height: 360, fps: 25, durationSec: 10, codec: 'h264', posterUrl: '' }; }"
            f" return {{ path: p, name: 'Night Walk.mp4', width: 1920, height: 1080, fps: 29.97, vfr: false, durationSec: 1936, codec: 'h264', posterUrl: '{POSTER}' }}; }}"
        ),
    }))
    harness = FRONTEND / "test" / "_create_project_head_harness.html"
    harness.write_text(PAGE)
    try:
        with sync_playwright() as p:
            b = p.chromium.launch()
            page = b.new_page(viewport={"width": 1400, "height": 900})
            errors = []
            page.on("dialog", lambda d: d.dismiss())
            page.on("pageerror", lambda e: errors.append(str(e)))
            page.goto(f"{base}/test/_create_project_head_harness.html")
            page.wait_for_function("window.__ready === true")
            check("no header before a video", not page.is_visible("#gen-project-head"))

            page.click("#gen-choose")
            page.wait_for_function("document.querySelector('#gen-project-meta').textContent.includes('fps')", timeout=5000)
            check("file name shown", page.text_content("#gen-project-name") == "Night Walk.mp4")
            meta = page.text_content("#gen-project-meta")
            check("real facts: resolution · fps · duration · codec", meta == "1920 × 1080  ·  29.97 fps  ·  32:16  ·  H264", meta)
            check("poster frame shown", page.is_visible("#gen-project-poster")
                  and page.get_attribute("#gen-project-poster", "src") == POSTER)
            check("header sits above the steps", page.evaluate(
                "document.querySelector('#gen-project-head').compareDocumentPosition(document.querySelector('#gen-steps')) & Node.DOCUMENT_POSITION_FOLLOWING") > 0)

            # A broken file: say so, no invented values, no stale poster.
            page.evaluate("window.__pick = '/films/broken.mp4'")
            page.click("#gen-choose")
            page.wait_for_function("document.querySelector('#gen-project-meta').textContent.startsWith('Video details not readable')", timeout=5000)
            check("probe failure is reported", "not a video" in page.text_content("#gen-project-meta"))
            check("no stale poster after a failed probe", not page.is_visible("#gen-project-poster"))
            check("name still shown for the broken file", page.text_content("#gen-project-name") == "broken.mp4")

            # Race: a slow probe for an older pick must not overwrite a newer one.
            page.evaluate("window.__pick = '/films/slow.mp4'")
            page.click("#gen-choose")
            page.evaluate("window.__pick = '/films/Night Walk.mp4'")
            page.click("#gen-choose")
            page.wait_for_timeout(1000)
            check("newer video wins over a slow older probe", page.text_content("#gen-project-name") == "Night Walk.mp4"
                  and "29.97 fps" in page.text_content("#gen-project-meta"))
            check("no page errors", not errors, "; ".join(errors))
            b.close()
    finally:
        shutdown()
        harness.unlink(missing_ok=True)
    return check.report()


if __name__ == "__main__":
    sys.exit(main())
