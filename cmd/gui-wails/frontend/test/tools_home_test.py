"""Tools → Home (V1 target tools.png): real status only. Every row comes from
a binding (device status, runtime dependencies, app folders, diagnostics
history, cache, version / edition); quick tools route to real sections.

Run: python3 cmd/gui-wails/frontend/test/tools_home_test.py
"""

import pathlib
import sys

from playwright.sync_api import sync_playwright

from _harness import Checker, app_stub, serve

HERE = pathlib.Path(__file__).resolve().parent


def main():
    check = Checker()
    html = (HERE.parent / "index.html").read_text().replace('href="/src/style.css"', 'href="/src/style.css?real"')
    harness = HERE / "_tools_home_harness.html"
    harness.write_text(html)
    base, shutdown = serve(
        "window.__deps = [{id:'ffmpeg', label:'ffmpeg', required:true, found:true, path:'/usr/bin/ffmpeg'},"
        " {id:'python', label:'Python', required:false, found:false}];\n"
        "window.__failed = []; window.__hist = []; window.__health = 0; window.__logs = 0;\n"
        + app_stub({
            "GetEULA": "async () => ({version:'x', text:'', accepted:true})",
            "IsSuiteBuild": "async () => false",
            "CurrentVersion": "async () => '0.6.0'",
            "GetSettings": "async () => ({updateCheckOnStartup:false})",
            "GetDeviceStatus": "async () => ({connected:false, mock:false, name:'', address:'', rssi:0, sessionActive:false})",
            "GetRuntimeHealth": "async () => { window.__health++; return ({dirsCreated:[], dirsFailed: window.__failed.slice(), deps: window.__deps.slice(), ok: true}); }",
            "GetDiagnosticsHistory": "async () => window.__hist.slice()",
            "GetCacheInfo": "async () => ({path:'/c', files:3, bytes:2048, humanSize:'2.0 KB', clearOnExit:false})",
            "OpenLogFolder": "async () => { window.__logs++; }",
        }))
    try:
        with sync_playwright() as p:
            b = p.chromium.launch()
            page = b.new_page(viewport={"width": 1366, "height": 768})
            errors = []
            page.on("pageerror", lambda e: errors.append(str(e)))
            page.add_init_script("try{localStorage.clear()}catch(e){}")
            page.goto(f"{base}/test/_tools_home_harness.html")
            page.wait_for_timeout(800)
            check("no runtime check before Tools is opened", page.evaluate("window.__health") == 0)
            page.click('#rail .tab-btn[data-tab="tools"]')
            page.wait_for_function("document.querySelectorAll('#th-rows .th-row').length > 0", timeout=5000)
            check("Home shown first", page.is_visible("#tab-tools-home"))
            state = lambda rid: page.get_attribute(f"#th-row-{rid}", "data-state")
            text = lambda rid: page.text_content(f"#th-row-{rid} .th-value")
            check("device row: not connected, not green", state("device") == "off" and text("device") == "Not connected")
            check("found dependency green with path", state("dep-ffmpeg") == "ok" and "/usr/bin/ffmpeg" in text("dep-ffmpeg"))
            check("missing optional dependency is a warning", state("dep-python") == "warn")
            check("folders ready", state("folders") == "ok")
            check("diagnostics never run is not green", state("diagnostics") == "off" and text("diagnostics") == "Never run")
            check("cache from the binding", text("cache") == "2.0 KB · 3 file(s)")
            check("version and edition", text("version") == "0.6.0 · Free")
            check("summary: no problems", "No problems" in page.text_content("#th-summary"))

            # A connected device updates the row live (device:status event).
            page.evaluate("window.dispatchEvent(new CustomEvent('device:status', {detail:{connected:true, mock:true, name:'Neo 2'}}))")
            check("device row follows device:status", state("device") == "ok" and text("device") == "Connected · Neo 2 (simulated)")

            # Missing required dependency + failed folder + diagnostics run → re-check.
            page.evaluate("window.__deps[0] = {id:'ffmpeg', label:'ffmpeg', required:true, found:false};"
                          "window.__failed = ['/x'];"
                          "window.__hist = [{timestamp:'2026-10-12T10:00:00Z', mock:false, deviceName:'Neo 2', report:{interrupted:false}},"
                          " {timestamp:'2026-10-01T10:00:00Z', mock:false, deviceName:'Old', report:{interrupted:true}}]")
            page.click("#th-recheck")
            page.wait_for_function("document.querySelector('#th-row-dep-ffmpeg').dataset.state === 'bad'", timeout=3000)
            check("missing required dependency is red", text("dep-ffmpeg") == "Missing")
            check("failed folders are red", state("folders") == "bad")
            check("summary counts the problems", page.text_content("#th-summary").startswith("2 problem(s)"))
            check("newest diagnostics run shown", state("diagnostics") == "ok" and "Neo 2" in text("diagnostics"))
            check("re-check reads the device binding again", state("device") == "off")

            check("recent scripts empty hint", "Nothing yet" in page.text_content("#th-recent"))
            page.click("#th-open-logs")
            check("Open log folder calls the binding", page.evaluate("window.__logs") == 1)
            page.click("#th-go-device")
            check("Device & diagnostics opens Tools → Device", page.is_visible("#tab-device"))
            page.click('#tools-nav .tab-btn[data-tab="tools-home"]')
            page.click("#th-go-settings")
            check("Settings opens Tools → Settings", page.is_visible("#tab-settings"))
            page.click('#tools-nav .tab-btn[data-tab="tools-home"]')
            page.click("#th-go-log")
            check("Show log opens Tools → Log", page.is_visible("#tab-log"))
            check("no page errors", errors == [], str(errors[:3]))
            b.close()
    finally:
        shutdown()
        harness.unlink(missing_ok=True)
    return check.report()


if __name__ == "__main__":
    sys.exit(main())
