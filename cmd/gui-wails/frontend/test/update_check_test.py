"""Regressionstest für den manuellen "Jetzt nach Updates suchen"-Knopf im
Einstellungen-Tab.

Vorher gab es nur die stille Prüfung beim Programmstart, deren Ergebnis bei
einem Fehler (Netzwerk, GitHub-Ratenlimit, ...) komplett verschluckt wurde
(leeres .catch in main.js) - von außen war "kein Update gefunden, weil es
keins gibt" nicht von "die Prüfung ist stillschweigend gescheitert" zu
unterscheiden, und es gab keine Möglichkeit, auf Wunsch erneut zu prüfen.
Geprüft wird hier: kein Update -> Hinweis nennt die aktuelle Version;
Update verfügbar -> Hinweis nennt die neue Version (+ Changelog-Preview);
Fehler -> Hinweis zeigt die Fehlermeldung, nicht Stille / nicht roh '//'.
Zusätzlich: ohne Release, aber mit Patch → Patch-Hinweis + Apply-Knopf.

Ausführen:  python3 cmd/gui-wails/frontend/test/update_check_test.py
"""

import pathlib
import sys

from playwright.sync_api import sync_playwright

from _harness import Checker, app_stub, serve

PAGE = """<!doctype html><html><body><div id="root"></div>
<script type="module">
  import { initSettings } from '/src/settings.js';
  window.__settings = initSettings(document.querySelector('#root'));
  window.__ready = true;
</script></body></html>"""


def main():
    check = Checker()
    base, shutdown = serve(app_stub({
        "GetSettings": "async () => ({ reportPath: '', defaultReportPath: '' })",
        "CurrentVersion": "async () => '0.3.0'",
        "CheckForUpdate": "async () => window.__updateResult",
        "CheckForPatch": "async () => window.__patchResult",
        "ApplyPatch": "async () => { window.__patchApplied = true; }",
    }))

    harness = pathlib.Path(__file__).resolve().parent / "_update_check_harness.html"
    harness.write_text(PAGE)

    with sync_playwright() as p:
        browser = p.chromium.launch()
        page = browser.new_page()
        page.on("dialog", lambda d: d.dismiss())
        page.on("pageerror", lambda e: print("   [pageerror]", e))
        page.goto(f"{base}/test/_update_check_harness.html")
        page.wait_for_function("window.__ready === true")

        # Kein Update verfügbar.
        page.evaluate(
            "window.__updateResult = { available: false };"
            "window.__patchResult = { available: false };")
        page.click("#st-update-now")
        page.wait_for_function(
            "document.querySelector('#st-update-status').textContent.includes('No update')",
            timeout=5000)
        txt = page.locator("#st-update-status").inner_text()
        check("no update: status mentions 'No update'", "No update" in txt)
        check("kein Update: aktuelle Version wird genannt", "0.3.0" in txt, txt)

        # Update verfügbar (Dialog wird oben dismissed statt akzeptiert).
        page.evaluate(
            "window.__updateResult = { available: true, release: { tag_name: 'v0.4.0', body: '## Notes\\n* fixed boxes' } };"
            "window.__patchResult = { available: false };")
        page.click("#st-update-now")
        page.wait_for_function(
            "document.querySelector('#st-update-status').textContent.includes('v0.4.0')",
            timeout=5000)
        status = page.locator("#st-update-status").inner_text()
        check("Update verfügbar: neue Version im Hinweis", "v0.4.0" in status)
        check("Update verfügbar: Changelog-Preview im Hinweis", "fixed boxes" in status)
        check("Update verfügbar: Apply-Knopf sichtbar",
              page.locator("#st-update-apply").is_visible())

        # Fehler bei der Prüfung darf NICHT stillschweigend verschwinden.
        # Auch Slash-only-Müll darf nicht unverändert in der UI landen.
        page.evaluate(
            "window.__updateResult = { error: 'kein Netz' };"
            "window.__patchResult = { available: false };")
        page.click("#st-update-now")
        page.wait_for_function(
            "document.querySelector('#st-update-status').textContent.includes('kein Netz')",
            timeout=5000)
        check("Fehler bei der Prüfung wird angezeigt, nicht verschluckt",
              "kein Netz" in page.locator("#st-update-status").inner_text())

        page.evaluate(
            "window.__updateResult = { error: '//' };"
            "window.__patchResult = { available: false };")
        page.click("#st-update-now")
        page.wait_for_function(
            "document.querySelector('#st-update-status').textContent.length > 0",
            timeout=5000)
        slash_txt = page.locator("#st-update-status").inner_text()
        check("Slash-only Fehler wird nicht roh als '//' gezeigt",
              "//" not in slash_txt or "unknown" in slash_txt.lower(), slash_txt)

        # Kein Release, aber Patch → gleicher UX-Pfad (Hinweis + Apply).
        page.evaluate(
            "window.__updateResult = { available: false };"
            "window.__patchResult = { available: true, kind: 'patch', patchId: 'v0.3.0-p1' };"
            "window.__patchApplied = false;")
        page.click("#st-update-now")
        page.wait_for_function(
            "document.querySelector('#st-update-status').textContent.includes('v0.3.0-p1')",
            timeout=5000)
        check("Patch verfügbar: ID im Hinweis",
              "v0.3.0-p1" in page.locator("#st-update-status").inner_text())
        apply_visible = page.locator("#st-update-apply").is_visible()
        check("Patch verfügbar: Apply-Knopf sichtbar", apply_visible)
        if apply_visible:
            page.click("#st-update-apply")
            page.wait_for_function("window.__patchApplied === true", timeout=3000)
            check("Patch Apply ruft ApplyPatch", page.evaluate("window.__patchApplied"))

        browser.close()

    shutdown()
    harness.unlink(missing_ok=True)
    return check.report()


if __name__ == "__main__":
    sys.exit(main())
