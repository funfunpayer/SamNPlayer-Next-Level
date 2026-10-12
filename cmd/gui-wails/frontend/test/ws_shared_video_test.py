"""V1 W3 workspace: one clip for Create and Play — Create offers the Play
video in step 1, and a clip chosen in Create shows in Play's recent videos.

Run: python3 cmd/gui-wails/frontend/test/ws_shared_video_test.py
"""

import pathlib
import sys

from playwright.sync_api import sync_playwright

from _harness import Checker, app_stub, serve

HERE = pathlib.Path(__file__).resolve().parent


def main():
    check = Checker()
    html = (HERE.parent / "index.html").read_text().replace('href="/src/style.css"', 'href="/src/style.css?real"')
    harness = HERE / "_ws_shared_video_harness.html"
    harness.write_text(html)
    base, shutdown = serve(app_stub({
        "GetEULA": "async () => ({version:'x', text:'', accepted:true})",
        "IsSuiteBuild": "async () => false",
        "CurrentVersion": "async () => 'test'",
        "GetSettings": "async () => ({updateCheckOnStartup:false})",
    }))
    try:
        with sync_playwright() as p:
            b = p.chromium.launch()
            page = b.new_page(viewport={"width": 1600, "height": 900})
            errors = []
            page.on("pageerror", lambda e: errors.append(str(e)))
            page.add_init_script("try{localStorage.clear()}catch(e){}")
            page.goto(f"{base}/test/_ws_shared_video_harness.html")
            page.wait_for_timeout(800)
            page.click('#ws-view [data-ws-view="create"]')
            check("no Play video → no offer", page.is_hidden("#gen-use-player-row"))
            # Play links a video (as after a real link: root carries the path).
            page.evaluate("document.querySelector('#tab-playback').dataset.pbVideoPath = '/v/Beach clip.mp4'")
            page.wait_for_function("!document.querySelector('#gen-use-player-row').hidden", timeout=3000)
            check("Create offers the Play video", page.text_content("#gen-use-player-name") == "Beach clip.mp4")
            page.click("#gen-use-player-video")
            page.wait_for_function("document.querySelector('#gen-video-path').textContent === 'Beach clip.mp4'", timeout=5000)
            check("one click loads it in Create", True)
            check("offer hides once both use the same clip", page.is_hidden("#gen-use-player-row"))
            # Create → Play: the clip is in Play's recent videos.
            page.evaluate("window.dispatchEvent(new CustomEvent('gen:video', { detail: { path: '/v/Other.mp4' } }))")
            recent = page.evaluate("JSON.parse(localStorage.getItem('pb.recentVideos') || '[]')")
            check("Create clip listed in Play recent videos", recent[:1] == ["/v/Other.mp4"], str(recent))
            check("no page errors", errors == [], str(errors[:3]))
            b.close()
    finally:
        shutdown()
        harness.unlink(missing_ok=True)
    return check.report()


if __name__ == "__main__":
    sys.exit(main())
