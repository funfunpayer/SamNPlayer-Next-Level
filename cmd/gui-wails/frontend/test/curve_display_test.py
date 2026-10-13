"""Regressionstest für die Funscript-Kurve unter dem Video.

Lädt das echte src/playback.js in Headless-Chromium; nur die wailsjs-Bindings
sind gestubbt. Geprüft wird, dass die Kurve nach dem Laden eines Skripts
erscheint, tatsächlich gezeichnet wird (nicht nur ein leeres Canvas) und der
Positionszeiger sich mit der Wiedergabe bewegt.

Ausführen:  python3 cmd/gui-wails/frontend/test/curve_display_test.py
"""

import pathlib
import sys

from playwright.sync_api import sync_playwright

from _harness import Checker, app_stub, serve

FRONTEND = pathlib.Path(__file__).resolve().parents[1]




PAGE = """<!doctype html><html><body><div id="root"></div>
<script type="module">
  import { initPlayback } from '/src/playback.js';
  window.__pb = initPlayback(document.querySelector('#root'));
  window.__ready = true;
</script></body></html>"""


# Zählt nicht-transparente Pixel - so lässt sich unterscheiden, ob wirklich
# etwas gezeichnet wurde oder nur ein leeres Canvas sichtbar ist.
COUNT_PIXELS = """
() => {
  const c = document.querySelector('#pb-curve');
  const ctx = c.getContext('2d');
  const d = ctx.getImageData(0, 0, c.width, c.height).data;
  let n = 0;
  for (let i = 3; i < d.length; i += 4) if (d[i] > 0) n++;
  return n;
}
"""


def main():
    curve_js = ("const CURVE = [];\n"
                "for (let i = 0; i < 400; i++) CURVE.push({ atMs: i * 250, pos: i % 2 ? 5 : 95 });\n")
    base, shutdown = serve(curve_js + app_stub({
        "PickFunscriptFile": "async () => '/tmp/test.funscript'",
        "LoadFunscript": "async () => ({ path: '/tmp/test.funscript', actionCount: CURVE.length, "
                         "durationMs: 100000, videoPath: '', hasVideo: false })",
        "GetScriptCurve": "async () => CURVE.slice()",
        "GetHeatmap": "async n => Array.from({ length: n }, (_, i) => ({ atMs: i * 1000, intensity: 0.5 }))",
        "GetMarker": "async () => null",
        "VideoFileURL": "async () => ''",
    }))
    (FRONTEND / "test" / "_curve_harness.html").write_text(PAGE)

    failures = []

    def check(name, cond, detail=""):
        print(("  OK   " if cond else "  FAIL ") + name + (f"  [{detail}]" if not cond else ""))
        if not cond:
            failures.append(name)

    with sync_playwright() as p:
        browser = p.chromium.launch()
        page = browser.new_page(viewport={"width": 1100, "height": 900})
        page.on("dialog", lambda d: d.dismiss())
        page.on("console", lambda m: print("   [console]", m.type, m.text) if m.type == "error" else None)
        page.on("pageerror", lambda e: print("   [pageerror]", e))
        page.goto(f"{base}/test/_curve_harness.html")
        page.wait_for_function("window.__ready === true")

        curve = page.locator("#pb-curve")
        check("vor dem Laden: Kurve unsichtbar",
              curve.evaluate("e => e.style.display") == "none",
              curve.evaluate("e => e.style.display"))

        # Skript laden
        page.click("#pb-choose")
        page.wait_for_function("document.querySelector('#pb-curve').style.display === 'block'",
                               timeout=5000)
        check("nach dem Laden: Kurve sichtbar", curve.evaluate("e => e.style.display") == "block")

        drawn = page.evaluate(COUNT_PIXELS)
        check("Kurve ist tatsächlich gezeichnet (nicht leer)", drawn > 500, f"{drawn} Pixel")

        # Die Kurve muss die volle Amplitude nutzen: bei einem Zickzack von
        # 5 bis 95 darf sie nicht als flache Linie in der Mitte enden.
        extent = page.evaluate("""
        () => {
          const c = document.querySelector('#pb-curve');
          const d = c.getContext('2d').getImageData(0, 0, c.width, c.height).data;
          let top = -1, bottom = -1;
          for (let y = 0; y < c.height; y++) {
            for (let x = 0; x < c.width; x++) {
              if (d[(y * c.width + x) * 4 + 3] > 0) {
                if (top < 0) top = y;
                bottom = y;
                break;
              }
            }
          }
          return { top, bottom, height: c.height };
        }
        """)
        span = extent["bottom"] - extent["top"]
        check("Kurve nutzt die volle Höhe (Amplitude erhalten)",
              span > extent["height"] * 0.6, f"{span} von {extent['height']} px")

        # Positionszeiger bewegt sich mit der Wiedergabe
        before = page.evaluate(COUNT_PIXELS)
        page.evaluate("window.__triggerEvent('playback:frame', "
                      "{ atMs: 50000, totalMs: 100000, vibration: 0.5, suction: 0.2 })")
        page.wait_for_timeout(200)
        after = page.evaluate(COUNT_PIXELS)
        check("Positionszeiger wird gezeichnet", after > before, f"{before} -> {after}")

        browser.close()

    shutdown()
    (FRONTEND / "test" / "_curve_harness.html").unlink(missing_ok=True)

    print(("FEHLGESCHLAGEN: " + ", ".join(failures)) if failures else "Alle Prüfungen bestanden.")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
