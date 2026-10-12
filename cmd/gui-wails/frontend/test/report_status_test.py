"""Regressionstest für den Messwerte-Status im Einstellungen-Tab.

Vorher war unsichtbar, ob unter dem eingestellten Berichtspfad schon
Messwerte stehen - "Auswertung anzeigen" beantwortete das zwar auch, aber
erst nach einem Klick und mit einer Fehlermeldung statt eines einfachen
Hinweises. Geprüft wird: kein Pfad -> kein Hinweis; Pfad ohne Datei ->
"noch keine"; Pfad mit Datei -> "enthält bereits"; der Hinweis muss sich
aktualisieren, wenn der Pfad sich ändert (Eingabefeld, "Wählen…",
"Standard").

Ausführen:  python3 cmd/gui-wails/frontend/test/report_status_test.py
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
    # __reportExistsFor bildet Pfad -> vorhanden ab; ReportExists() selbst
    # kennt den Pfad nicht (wie die echte Go-Bindung), liest ihn also aus
    # dem zuletzt per SetSetting gemeldeten Wert - genau wie a.settings auf
    # der Go-Seite den zuletzt gespeicherten Pfad verwendet.
    base, shutdown = serve(app_stub({
        "GetSettings": "async () => ({ reportPath: '/tmp/with-data.jsonl', "
                       "defaultReportPath: '/tmp/default.jsonl' })",
        "SetSetting": "async (key, value) => { window.__calls.push(['set', key, value]); "
                      "if (key === 'generator.reportPath') window.__currentReportPath = value; }",
        "ReportExists": "async () => !!(window.__reportExistsFor || {})[window.__currentReportPath]",
        "PickReportPath": "async () => '/tmp/picked-empty.jsonl'",
    }))

    init_js = ("window.__currentReportPath = '/tmp/with-data.jsonl';\n"
               "window.__reportExistsFor = { '/tmp/with-data.jsonl': true, "
               "'/tmp/picked-empty.jsonl': false, '/tmp/default.jsonl': false };\n")

    harness = pathlib.Path(__file__).resolve().parent / "_report_status_harness.html"
    harness.write_text(f"<script>{init_js}</script>\n" + PAGE)

    with sync_playwright() as p:
        browser = p.chromium.launch()
        page = browser.new_page()
        page.on("dialog", lambda d: d.dismiss())
        page.on("pageerror", lambda e: print("   [pageerror]", e))
        page.goto(f"{base}/test/_report_status_harness.html")
        page.wait_for_function("window.__ready === true")
        page.evaluate(
            "document.querySelector('#st-diagnostics-details').open = true")

        page.wait_for_function(
            "document.querySelector('#st-report-status').textContent.includes('Already')",
            timeout=5000)
        check("path with data: 'Already' in status",
              "Already" in page.locator("#st-report-status").inner_text())

        page.click("#st-pick-report")
        page.wait_for_function(
            "document.querySelector('#st-report-status').textContent.includes('No metrics recorded')",
            timeout=5000)
        check("frisch gewählter, leerer Pfad: 'No metrics yet' im Hinweis",
              "No metrics recorded" in page.locator("#st-report-status").inner_text())

        page.click("#st-default-report")
        page.wait_for_function(
            "document.querySelector('#st-report-path').value === '/tmp/default.jsonl'",
            timeout=5000)
        check("Standard-Pfad ebenfalls ohne Daten -> 'No metrics yet'",
              "No metrics recorded" in page.locator("#st-report-status").inner_text())

        page.fill("#st-report-path", "")
        page.locator("#st-report-path").dispatch_event("change")
        page.wait_for_function(
            "document.querySelector('#st-report-status').textContent === ''", timeout=5000)
        check("leerer Pfad: kein Hinweis", page.locator("#st-report-status").inner_text() == "")

        browser.close()

    shutdown()
    harness.unlink(missing_ok=True)
    return check.report()


if __name__ == "__main__":
    sys.exit(main())
