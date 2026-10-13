"""Regression: Video-Ende darf Film-Liste weiter schalten (nicht wie Stop).

Der Stop-Knopf setzt userStopRequested und blockiert Auto-Next bewusst.
Natürliches video-'ended' muss stop({ user: false }) nutzen, sonst bleibt
die Liste beim ersten Film stehen.

Ausführen: python3 cmd/gui-wails/frontend/test/pb_playlist_advance_test.py
"""

import pathlib
import sys

from playwright.sync_api import sync_playwright

from _harness import Checker, app_stub, serve

PLAYBACK_PAGE = """<!doctype html><html><body><div id="root"></div>
<script type="module">
  import { initPlayback } from '/src/playback.js';
  window.__pb = initPlayback(document.querySelector('#root'));
  window.__ready = true;
</script></body></html>"""


def main():
    check = Checker()
    src = (pathlib.Path(__file__).resolve().parent.parent / "src" / "playback.js").read_text()

    check(
        "stop() unterscheidet User-Stop von natürlichem Ende",
        "async function stop({ user = true } = {})" in src,
    )
    check(
        "Video-ended ruft stop({ user: false })",
        "stop({ user: false })" in src and "addEventListener('ended'" in src,
    )
    check(
        "onPlaybackFinished ehrt userStopRequested",
        "if (userStopRequested)" in src and "playNextInPlaylist" in src,
    )
    # Stop-Knopf bleibt Default (user=true) — addEventListener übergibt Event,
    # destructuring findet kein .user → Default true.
    check(
        "Stop-Knopf gebunden",
        "el('#pb-stop').addEventListener('click', stop)" in src,
    )
    check(
        "Connect-Fehler: failed-Payload blockiert Advance",
        "payload.failed" in src and "onPlaybackFinished(payload" in src,
    )
    check(
        "Aktiven Playlist-Eintrag entfernen lädt nach",
        "removingCurrent" in src and "loadScript(playlist[playlistIndex].path" in src,
    )
    check(
        "Shuffle + Repeat playlist controls",
        'id="pb-playlist-shuffle"' in src and 'id="pb-playlist-repeat"' in src
        and "Repeat playlist" in src,
    )
    check(
        "Repeat wraps playlist at end",
        "nextPlaylistIndexAfterAdvance" in src and "reshufflePlaylistForRepeatCycle" in src,
    )
    check(
        "onPlaybackFinished honors playlist repeat",
        "pb-playlist-repeat" in src and "playlistIndex < playlist.length - 1 || repeat" in src,
    )

    base, shutdown = serve(app_stub({"GetSettings": "async () => ({})"}))
    harness = pathlib.Path(__file__).resolve().parent / "_pb_playlist_shuffle_harness.html"
    harness.write_text(PLAYBACK_PAGE)
    with sync_playwright() as p:
        browser = p.chromium.launch()
        page = browser.new_page()
        page.goto(f"{base}/test/_pb_playlist_shuffle_harness.html")
        page.wait_for_function("window.__ready === true")
        check("shuffle control in DOM", page.locator("#pb-playlist-shuffle").count() == 1)
        check("repeat control in DOM", page.locator("#pb-playlist-repeat").count() == 1)
        page.evaluate(
            """() => {
              window.__pb.setPlaylistForTest(['/a.funscript', '/b.funscript', '/c.funscript'], 2);
              document.querySelector('#pb-playlist-repeat').checked = true;
            }"""
        )
        nxt = page.evaluate("() => window.__pb.nextPlaylistIndexAfterAdvance()")
        check("repeat wraps index to 0 at end", nxt == 0, str(nxt))
        browser.close()
    shutdown()
    harness.unlink(missing_ok=True)

    return check.report()


if __name__ == "__main__":
    sys.exit(main())
