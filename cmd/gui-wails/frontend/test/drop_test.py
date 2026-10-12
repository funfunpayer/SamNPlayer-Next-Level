"""Regressionstest für Drag & Drop.

Geprüft wird die Weiche, nicht die Webview: Go liefert die echten Dateipfade
(die Webview bekäme aus einem Drop nur einen Blob ohne Pfad, mit dem der
Generator nichts anfangen kann), und das Frontend entscheidet daraus, welcher
Tab zuständig ist.

Der eigentliche Nutzen ist genau diese Weiche - ein Video soll im Generator
landen, ein Skript in der Wiedergabe, ohne dass man vorher den richtigen Tab
sucht. Geht sie kaputt, landet die Datei still im falschen Tab.

Ausführen:  python3 cmd/gui-wails/frontend/test/drop_test.py
"""

import sys

from playwright.sync_api import sync_playwright

from _harness import Checker, app_stub, serve

PAGE = """<!doctype html><html><body>
<div id="app">
  <nav id="tabbar">
    <button class="tab-btn active" data-tab="playback">Playback</button>
    <button class="tab-btn" data-tab="training">Training</button>
    <button class="tab-btn" data-tab="generator">Generate</button>
    <button class="tab-btn" data-tab="device">Device</button>
    <button class="tab-btn" data-tab="log">Log</button>
    <button class="tab-btn" data-tab="settings">Settings</button>
    <button class="tab-btn" data-tab="roi-training">AI Train</button>
    <span id="version-label"></span>
  </nav>
  <section id="tab-playback" class="tab-panel active"></section>
  <section id="tab-training" class="tab-panel"></section>
  <section id="tab-generator" class="tab-panel"></section>
  <section id="tab-benchmark" class="tab-panel"></section>
  <section id="tab-device" class="tab-panel"></section>
  <section id="tab-log" class="tab-panel"></section>
  <section id="tab-settings" class="tab-panel"></section>
  <section id="tab-roi-training" class="tab-panel"></section>
  <aside id="sidebar"></aside>
</div>
<script type="module">
  window.__dropped = [];
  window.addEventListener('drop:video', e => window.__dropped.push(['video', e.detail]));
  window.addEventListener('drop:script', e => window.__dropped.push(['script', e.detail]));
  await import('/src/main.js');
  window.__ready = true;
</script></body></html>"""


def main():
    check = Checker()
    base, shutdown = serve(app_stub({
        "CurrentVersion": "async () => 'test'",
        "CheckForUpdate": "async () => ({ available: false })",
        "GetSettings": "async () => ({})",
        "GetDeviceStatus": "async () => ({ connected: false })",
        "GetCacheInfo": "async () => ({ files: 0, humanSize: '0 B', path: '/tmp' })",
        "LoadFunscript": "async () => ({ path: '', actionCount: 0, durationMs: 0 })",
        "GetHeatmap": "async () => []",
        "GetScriptCurve": "async () => []",
        "GetMarker": "async () => null",
    }))
    harness = None

    with sync_playwright() as p:
        browser = p.chromium.launch()
        page = browser.new_page()
        page.on("dialog", lambda d: d.dismiss())
        page.on("pageerror", lambda e: print("   [pageerror]", e))

        import pathlib
        harness = pathlib.Path(__file__).resolve().parent / "_drop_harness.html"
        harness.write_text(PAGE)
        page.goto(f"{base}/test/_drop_harness.html")
        page.wait_for_function("window.__ready === true")

        # The open tab (body[data-tab]); in the Player workspace the Player
        # button stays highlighted while Create (= generator) is shown.
        active = lambda: page.evaluate("document.body.dataset.tab || document.querySelector('.tab-btn.active').dataset.tab")
        check("Start im Wiedergabe-Tab", active() == "playback", str(active()))

        # --- Video: muss in den Generator führen --------------------------
        page.evaluate("""window.__triggerEvent('files:dropped',
            { videos: ['/videos/clip.mp4'], scripts: [], ignored: 0 })""")
        page.wait_for_function("window.__dropped.length > 0")
        check("Video wechselt in den Generator-Tab", active() == "generator", str(active()))
        dropped = page.evaluate("window.__dropped[0]")
        check("Videopfad kommt unverändert an",
              dropped[0] == "video"
              and dropped[1]["path"] == "/videos/clip.mp4"
              and dropped[1].get("extraCount") == 0
              and dropped[1].get("paths") == ["/videos/clip.mp4"],
              str(dropped))

        # --- Skript: muss in die Wiedergabe führen ------------------------
        page.evaluate("""window.__triggerEvent('files:dropped',
            { videos: [], scripts: ['/skripte/a.funscript'], ignored: 0 })""")
        page.wait_for_function("window.__dropped.length > 1")
        check("Skript wechselt in den Wiedergabe-Tab", active() == "playback", str(active()))
        dropped = page.evaluate("window.__dropped[1]")
        check("Skriptpfad kommt unverändert an",
              dropped[0] == "script"
              and dropped[1]["path"] == "/skripte/a.funscript"
              and dropped[1].get("paths") == ["/skripte/a.funscript"],
              str(dropped))

        # --- Beides zugleich: Video hat Vorrang ---------------------------
        page.evaluate("""window.__triggerEvent('files:dropped',
            { videos: ['/v/b.mp4'], scripts: ['/s/b.funscript'], ignored: 0 })""")
        page.wait_for_function("window.__dropped.length > 2")
        check("bei Video und Skript gewinnt das Video",
              page.evaluate("window.__dropped[2][0]") == "video"
              and page.evaluate("window.__dropped.length") == 3,
              str(page.evaluate("window.__dropped")))

        # --- Mehrere Videos: Queue (DeepFunGen UX), nicht still ignorieren ----
        page.evaluate("""window.__triggerEvent('files:dropped',
            { videos: ['/v/first.mp4', '/v/second.mp4', '/v/third.mp4'], scripts: [], ignored: 0 })""")
        page.wait_for_function("window.__dropped.length > 3")
        dropped = page.evaluate("window.__dropped[3]")
        check("mehrere Videos: nur der erste Pfad wird geladen",
              dropped[1]["path"] == "/v/first.mp4", str(dropped))
        check("mehrere Videos: komplette Liste wird mitgeschickt",
              dropped[1].get("paths") == ["/v/first.mp4", "/v/second.mp4", "/v/third.mp4"],
              str(dropped))
        check("mehrere Videos: Anzahl der übrigen wird mitgeschickt",
              dropped[1]["extraCount"] == 2, str(dropped))
        page.wait_for_function(
            "document.querySelector('#gen-queue') && !document.querySelector('#gen-queue').hidden",
            timeout=5000)
        check("mehrere Videos: Queue-UI sichtbar",
              page.locator("#gen-queue-list li").count() == 3,
              str(page.locator("#gen-queue-list li").count()))
        check("mehrere Videos: Queue-Hinweis in der Oberfläche",
              "queue" in page.locator("#gen-status").inner_text().lower()
              or "2" in page.locator("#gen-queue-summary").inner_text(),
              page.locator("#gen-status").inner_text() + " | " + page.locator("#gen-queue-summary").inner_text())

        # --- Mehrere Skripte: Film-Liste statt „ignoriert“ -----------------
        page.evaluate("window.__dropped.length = 0")
        page.evaluate("""window.__triggerEvent('files:dropped',
            { videos: [], scripts: ['/s/first.funscript', '/s/second.funscript'], ignored: 0 })""")
        page.wait_for_function("window.__dropped.length > 0")
        dropped = page.evaluate("window.__dropped[0]")
        check("mehrere Skripte: erster Pfad kommt an",
              dropped[1]["path"] == "/s/first.funscript", str(dropped))
        check("mehrere Skripte: komplette Liste wird mitgeschickt",
              dropped[1].get("paths") == ["/s/first.funscript", "/s/second.funscript"], str(dropped))
        page.wait_for_function(
            "document.querySelector('#pb-playlist') && !document.querySelector('#pb-playlist').hidden",
            timeout=5000)
        check("mehrere Skripte: Film-Liste sichtbar",
              page.locator("#pb-playlist-list li").count() == 2)

        # --- Ablagefläche erscheint und verschwindet ----------------------
        overlay_visible = "document.getElementById('drop-overlay').classList.contains('visible')"
        check("Ablagefläche zunächst unsichtbar", not page.evaluate(overlay_visible))
        page.evaluate("window.dispatchEvent(new Event('dragenter'))")
        check("Ablagefläche erscheint beim Ziehen", page.evaluate(overlay_visible))
        page.evaluate("window.dispatchEvent(new Event('dragleave'))")
        check("Ablagefläche verschwindet wieder", not page.evaluate(overlay_visible))

        browser.close()

    shutdown()
    if harness:
        harness.unlink(missing_ok=True)
    return check.report()


if __name__ == "__main__":
    sys.exit(main())
