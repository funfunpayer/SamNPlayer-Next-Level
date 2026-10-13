"""Regressionstest für den Geräte-Tab.

Lädt das echte src/device.js in Headless-Chromium; nur die wailsjs-Bindings
sind gestubbt. Der Stub bildet die Zustandslogik aus app_device.go nach
(verbunden / nicht verbunden / Session läuft) - getestet wird, dass die
Oberfläche daraus die richtigen Schlüsse zieht.

Der wichtigste Punkt: solange nicht verbunden ist, MÜSSEN die Testregler
gesperrt sein. Ein Regler, der sich bewegen lässt, ohne dass ein Gerät
dranhängt, macht genau die Unsicherheit wieder auf, wegen der dieser Tab
überhaupt gebaut wurde.

Ausführen:  python3 cmd/gui-wails/frontend/test/device_display_test.py
"""

import pathlib
import sys

from playwright.sync_api import sync_playwright

from _harness import app_stub, serve

FRONTEND = pathlib.Path(__file__).resolve().parents[1]



PAGE = """<!doctype html><html><body><div id="root"></div>
<script type="module">
  import { initDevice } from '/src/device.js';
  initDevice(document.querySelector('#root'));
  window.__ready = true;
</script></body></html>"""


def main():
    state_js = ("let state = { connected: false, mock: false, name: '', "
                "address: '', rssi: 0, sessionActive: false, batteryOk: false, batteryPct: 0 };\n"
                "window.__setState = s => { state = Object.assign(state, s); };\n")
    base, shutdown = serve(state_js + app_stub({
        "GetDeviceStatus": "async () => ({ ...state })",
        "ConnectDevice": "async (mock) => { window.__calls.push(['connect', mock]); "
                         "if (state.sessionActive) throw new Error('Session laeuft'); "
                         "state.connected = true; state.mock = mock; "
                         "state.name = mock ? 'Mock-Geraet' : 'Sam Neo 2 Pro'; "
                         "state.address = mock ? '' : 'AA:BB:CC:DD:EE:FF'; "
                         "state.rssi = mock ? 0 : -58; "
                         "state.batteryOk = !mock; state.batteryPct = mock ? 0 : 73; "
                         "state.transport = mock ? 'mock' : 'ble'; "
                         "state.capVibration = true; state.capSuction = true; "
                         "state.capBattery = !mock; state.capRaw = !mock; "
                         "return { ...state }; }",
        "DisconnectDevice": "async () => { window.__calls.push(['disconnect']); "
                            "state.connected = false; state.name = ''; state.address = ''; "
                            "state.rssi = 0; state.batteryOk = false; state.batteryPct = 0; "
                            "state.capVibration = false; state.capSuction = false; "
                            "state.capBattery = false; state.capRaw = false; "
                            "return { ...state }; }",
        "TestVibration": "async v => { window.__calls.push(['vib', v]); }",
        "TestSuction": "async v => { window.__calls.push(['suc', v]); }",
        "TestStop": "async () => { window.__calls.push(['stop']); }",
        "TestRawValue": "async (c, v) => { window.__calls.push(['raw', c, v]); }",
        "ConnectDeviceVia": "async (transport, url) => { "
                            "window.__calls.push(['connect', transport, url]); "
                            "if (state.sessionActive) throw new Error('Session laeuft'); "
                            "state.connected = true; state.mock = transport === 'mock'; "
                            "state.name = transport === 'intiface' ? 'Testgeraet (ueber Intiface)' "
                            ": 'Sam Neo 2 Pro'; state.address = 'AA:BB:CC:DD:EE:FF'; "
                            "state.rssi = -58; "
                            "state.batteryOk = transport !== 'mock'; "
                            "state.batteryPct = transport === 'mock' ? 0 : 73; "
                            "state.transport = transport; "
                            "state.capVibration = true; state.capSuction = true; "
                            "state.capBattery = transport !== 'mock'; "
                            "state.capRaw = transport === 'ble'; "
                            "return { ...state }; }",
        "RunDeviceDiagnostics": "async () => { window.__calls.push(['diagnose']); }",
        "GetDiagnosticsHistory": "async () => ([])",
    }))
    (FRONTEND / "test" / "_device_harness.html").write_text(PAGE)

    failures = []

    def check(name, cond, detail=""):
        print(("  OK   " if cond else "  FAIL ") + name + (f"  [{detail}]" if not cond else ""))
        if not cond:
            failures.append(name)

    with sync_playwright() as p:
        browser = p.chromium.launch()
        page = browser.new_page()
        page.on("dialog", lambda d: d.dismiss())
        page.on("pageerror", lambda e: print("   [pageerror]", e))
        page.goto(f"{base}/test/_device_harness.html")
        page.wait_for_function("window.__ready === true")
        page.wait_for_function("!document.querySelector('#dev-status-text').textContent.includes('Loading')")

        status = lambda: page.locator("#dev-status-text").inner_text()
        disabled = lambda sel: page.locator(sel).evaluate("e => e.disabled")

        # --- Ausgangszustand ---
        check("Start: 'Not connected'", "Not connected" in status(), status())
        check("Start: Testbereich gesperrt", disabled("#dev-test"))
        check("Start: Trennen gesperrt", disabled("#dev-disconnect"))
        check("Start: Verbinden möglich", not disabled("#dev-connect"))

        # --- Verbinden mit echter Hardware ---
        # Verbindungsart wählen - die Weiche muss beim Backend ankommen.
        page.select_option("#dev-transport", "intiface")
        check("Adressfeld erscheint nur bei Intiface",
              page.locator("#dev-intiface-url").evaluate("e => e.style.display") != "none")
        check("Erklärung wechselt mit",
              "Intiface Central" in page.locator("#dev-transport-hint").inner_text(),
              page.locator("#dev-transport-hint").inner_text())
        page.select_option("#dev-transport", "ble")
        check("Adressfeld verschwindet wieder",
              page.locator("#dev-intiface-url").evaluate("e => e.style.display") == "none")

        page.click("#dev-connect")
        page.wait_for_function("document.querySelector('#dev-status-text').textContent.includes('Sam Neo 2')")
        connect_call = page.evaluate("window.__calls.filter(c => c[0] === 'connect').pop()")
        check("gewählte Verbindungsart kommt beim Backend an",
              connect_call[1] == "ble", str(connect_call))
        check("Verbunden: Gerätename sichtbar", "Sam Neo 2 Pro" in status(), status())
        check("Verbunden: Adresse sichtbar", "AA:BB:CC:DD:EE:FF" in status(), status())
        check("Verbunden: Signalstärke sichtbar", "-58" in status(), status())
        check("Verbunden: Akku sichtbar wenn gemeldet", "Battery 73%" in status(), status())
        caps = page.locator("#dev-caps").inner_text()
        check("Verbunden: Fähigkeitschips sichtbar", "Vibration" in caps and "Suction" in caps, caps)
        check("Capability chip uses English Battery (not Akku)",
              "Battery" in caps and "Akku" not in caps, caps)
        # D1: a stroker over Intiface reports its own channels.
        page.evaluate("""window.__setState({ name: 'The Handy (via Intiface, Stroke (not driven yet))', transport: 'intiface',
            capabilities: { device: 'The Handy', battery: false, features: [
              { channel: 'stroke', kind: 'linear', index: 0, driven: false } ] } })""")
        page.wait_for_function("document.querySelector('#dev-caps').textContent.includes('Stroke')", timeout=5000)
        chip = page.locator('#dev-caps .dev-cap[data-channel="stroke"]')
        check("D1: stroker shows its Stroke channel", chip.count() == 1)
        check("D1: not-driven channel is marked 'coming'", "coming" in chip.inner_text()
              and "dev-cap-off" in (chip.get_attribute("class") or ""))
        check("D1: no Vibration chip for a stroke-only device",
              page.locator('#dev-caps .dev-cap[data-channel="vibration"]').count() == 0)
        page.evaluate("""window.__setState({ name: 'Sam Neo 2 Pro', transport: 'ble', capabilities: { device: 'Sam Neo 2', battery: true,
            features: [ { channel: 'vibration', kind: 'scalar', index: 0, driven: true },
                        { channel: 'suction', kind: 'scalar', index: 1, driven: true } ] } })""")
        page.wait_for_function("!document.querySelector('#dev-caps').textContent.includes('Stroke')", timeout=5000)
        check("D1: Neo 2 channels driven (no 'coming')", "coming" not in page.locator("#dev-caps").inner_text())
        check("Verbunden: Testbereich frei", not disabled("#dev-test"))
        check("Verbunden: Verbinden gesperrt", disabled("#dev-connect"))

        # --- Regler senden echte Werte, Stufenanzeige stimmt ---
        page.eval_on_selector("#dev-vib", "e => { e.value = 50; e.dispatchEvent(new Event('input')); }")
        page.wait_for_function("window.__calls.some(c => c[0] === 'vib')")
        vib = page.evaluate("window.__calls.filter(c => c[0] === 'vib').pop()")
        check("Vibrationsregler sendet 0.5", abs(vib[1] - 0.5) < 1e-9, str(vib))
        check("Stufenanzeige Vibration 50% = Stufe 5",
              "level 5" in page.locator("#dev-vib-val").inner_text(),
              page.locator("#dev-vib-val").inner_text())

        page.eval_on_selector("#dev-suc", "e => { e.value = 100; e.dispatchEvent(new Event('input')); }")
        page.wait_for_function("window.__calls.some(c => c[0] === 'suc')")
        check("Stufenanzeige Sog 100% = Stufe 5",
              "level 5" in page.locator("#dev-suc-val").inner_text(),
              page.locator("#dev-suc-val").inner_text())

        # --- "Alles aus" setzt auch die Regler zurück ---
        page.click("#dev-stop")
        page.wait_for_function("window.__calls.some(c => c[0] === 'stop')")
        check("Alles aus: Regler zurückgesetzt",
              page.locator("#dev-vib").input_value() == "0"
              and page.locator("#dev-suc").input_value() == "0",
              page.locator("#dev-vib").input_value())

        # --- Rohwert-Test: nur bei bestehender Verbindung bedienbar ------
        page.locator("#dev-raw-details").evaluate("e => { e.open = true }")
        check("Rohwert-Bereich bei Verbindung frei", not disabled("#dev-raw"))
        page.eval_on_selector("#dev-raw-value", "e => e.value = 42")
        page.click("#dev-raw-send")
        page.wait_for_function("window.__calls.some(c => c[0] === 'raw')")
        raw = page.evaluate("window.__calls.filter(c => c[0] === 'raw').pop()")
        check("Rohwert wird unverändert gesendet", raw[2] == 42, str(raw))
        check("Hinweis bei Wert über dem documented maximum",
              "documented maximum" in page.locator("#dev-raw-hint").inner_text(),
              page.locator("#dev-raw-hint").inner_text())

        # --- Geräte-Diagnose: nur bei bestehender Verbindung bedienbar ----
        page.locator("#dev-diag-details").evaluate("e => { e.open = true }")
        check("Diagnose-Bereich bei Verbindung frei", not disabled("#dev-diag"))
        page.click("#diag-run")
        page.wait_for_function("window.__calls.some(c => c[0] === 'diagnose')")
        check("Diagnose-Knopf während des Laufs gesperrt", disabled("#diag-run"))
        check("Status zeigt 'Läuft'", "Running" in page.locator("#diag-status").inner_text(),
              page.locator("#diag-status").inner_text())

        page.evaluate("window.__triggerEvent('diagnostics:entry', "
                       "{ phase: 'raw_sweep_vibration', channel: 'vibration', sentRaw: 128, latencyMs: 12.5 })")
        page.wait_for_function(
            "document.querySelector('#diag-status').textContent.includes('raw sweep vibration')")
        check("Live-Log zeigt Kanal und Wert der laufenden Phase",
              "vibration" in page.locator("#diag-status").inner_text()
              and "128" in page.locator("#diag-status").inner_text(),
              page.locator("#diag-status").inner_text())

        page.evaluate("""window.__triggerEvent('diagnostics:done', {
            timestamp: new Date().toISOString(), mock: true, deviceName: 'Mock-Geraet',
            report: {
                startedAt: new Date().toISOString(), durationMs: 500, rawCapable: true,
                interrupted: false,
                phases: [{ phase: 'raw_sweep_vibration', commands: 32, errors: 0,
                           meanLatencyMs: 3.2, maxLatencyMs: 9.1 }],
                log: [], notes: ['Not measured: felt intensity.'],
            },
        })""")
        page.wait_for_function("document.querySelector('#diag-run').disabled === false")
        check("Nach Abschluss: Knopf wieder frei", not disabled("#diag-run"))
        check("Ergebnis zeigt die Phase aus dem Bericht",
              "raw sweep vibration" in page.locator("#diag-result").inner_text(),
              page.locator("#diag-result").inner_text())
        check("Ergebnis zeigt die Nicht-gemessen-Notiz",
              "Not measured" in page.locator("#diag-result").inner_text(),
              page.locator("#diag-result").inner_text())

        # --- Laufende Session sperrt den Test ---
        page.evaluate("window.__setState({ sessionActive: true })")
        page.wait_for_function("document.querySelector('#dev-test').disabled === true", timeout=5000)
        check("Session läuft: Testbereich gesperrt", disabled("#dev-test"))
        check("Session läuft: Rohwert-Bereich gesperrt", disabled("#dev-raw"))
        check("Session läuft: Diagnose-Bereich gesperrt", disabled("#dev-diag"))
        check("Session läuft: Hinweis sichtbar", "unavailable" in status(), status())
        page.evaluate("window.__setState({ sessionActive: false })")

        # --- Trennen ---
        page.wait_for_function("document.querySelector('#dev-disconnect').disabled === false", timeout=5000)
        page.click("#dev-disconnect")
        page.wait_for_function("document.querySelector('#dev-status-text').textContent.includes('Not connected')")
        check("Getrennt: Testbereich wieder gesperrt", disabled("#dev-test"))

        browser.close()

    shutdown()
    (FRONTEND / "test" / "_device_harness.html").unlink(missing_ok=True)

    print(("FEHLGESCHLAGEN: " + ", ".join(failures)) if failures else "Alle Prüfungen bestanden.")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
