"""D5: Tools → Device offers "TCode over Wi-Fi (OSR2 / SR6)". Choosing it
shows its own address field (remembered from settings), the hint explains
UDP / TCP, and Connect passes that address — not the Intiface one.

Run: python3 cmd/gui-wails/frontend/test/device_tcode_test.py
"""

import pathlib
import sys

from playwright.sync_api import sync_playwright

from _harness import Checker, app_stub, serve

HERE = pathlib.Path(__file__).resolve().parent

PAGE = """<!doctype html><html><body><div id="root"></div>
<script type="module">
  import { initDevice } from '/src/device.js';
  initDevice(document.querySelector('#root'));
  window.__ready = true;
</script></body></html>"""


def main():
    check = Checker()
    base, shutdown = serve("window.__calls = []; let state = { connected: false, sessionActive: false };\n" + app_stub({
        "GetDeviceStatus": "async () => ({ ...state })",
        "GetSettings": "async () => ({ deviceTransport: 'tcode', tcodeAddress: '192.168.1.50', intifaceUrl: 'ws://x:12345' })",
        "ConnectDeviceVia": ("async (t, a) => { window.__calls.push([t, a]); state = { connected: true, transport: t, "
                             "name: 'TCode device (Stroke L0)', address: 'udp://' + a + ':8000', "
                             "capabilities: { device: 'TCode device', features: [{ channel: 'stroke', kind: 'linear', index: 0, driven: true }] } }; "
                             "return { ...state }; }"),
        "GetDiagnosticsHistory": "async () => []",
    }))
    harness = HERE / "_device_tcode_harness.html"
    harness.write_text(PAGE)
    try:
        with sync_playwright() as p:
            b = p.chromium.launch()
            page = b.new_page()
            errors = []
            page.on("pageerror", lambda e: errors.append(str(e)))
            page.goto(f"{base}/test/_device_tcode_harness.html")
            page.wait_for_function("window.__ready === true")
            page.wait_for_timeout(300)
            check("TCode option offered", page.evaluate(
                "[...document.querySelectorAll('#dev-transport option')].some(o => o.value === 'tcode')"))
            check("saved transport restored", page.input_value("#dev-transport") == "tcode")
            check("TCode address field shown, Intiface field hidden",
                  page.is_visible("#dev-tcode-addr") and page.is_hidden("#dev-intiface-url"))
            check("saved TCode address restored", page.input_value("#dev-tcode-addr") == "192.168.1.50")
            check("hint explains UDP / TCP", "UDP" in page.text_content("#dev-transport-hint"))
            page.select_option("#dev-transport", "intiface")
            check("switching to Intiface swaps the fields",
                  page.is_hidden("#dev-tcode-addr") and page.is_visible("#dev-intiface-url"))
            page.select_option("#dev-transport", "tcode")
            page.click("#dev-connect")
            page.wait_for_function("window.__calls.length === 1", timeout=3000)
            check("Connect passes the TCode address", page.evaluate("window.__calls[0]") == ["tcode", "192.168.1.50"])
            page.wait_for_timeout(200)
            check("address field locked while connected", page.is_disabled("#dev-tcode-addr"))
            check("no page errors", errors == [], str(errors[:3]))
            b.close()
    finally:
        shutdown()
        harness.unlink(missing_ok=True)
    return check.report()


if __name__ == "__main__":
    sys.exit(main())
