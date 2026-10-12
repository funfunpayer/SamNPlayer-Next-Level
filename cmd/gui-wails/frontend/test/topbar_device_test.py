"""Topbar-Geräteindikator: Name + Verbinden/Trennen oben rechts."""

import pathlib
import sys

from playwright.sync_api import sync_playwright

from _harness import app_stub, serve

FRONTEND = pathlib.Path(__file__).resolve().parents[1]

PAGE = """<!doctype html><html><head><link rel="stylesheet" href="/src/style.css"/></head>
<body>
<div id="app">
  <div class="app-main">
    <header id="topbar">
      <h1 class="topbar-brand"><span class="mark">SamN</span><span class="rest">Player</span></h1>
      <div class="pills">
        <div id="topbar-device-wrap" class="topbar-device-wrap">
          <button type="button" id="topbar-device" class="topbar-device is-offline">
            <svg class="topbar-device-icon" viewBox="0 0 24 24"><rect x="7" y="4" width="10" height="16" rx="3"/><path d="M12 17h.01"/></svg>
            <span id="topbar-device-name">Not connected</span>
            <i id="topbar-device-led" class="topbar-device-led"></i>
          </button>
          <button type="button" id="topbar-device-action" class="topbar-device-action">Connect</button>
        </div>
        <span id="version-label" class="pill">dev</span>
      </div>
    </header>
    <div class="stage">
      <div class="stage-main">
        <section id="tab-playback" class="tab-panel active"></section>
        <section id="tab-device" class="tab-panel"></section>
      </div>
    </div>
  </div>
</div>
<nav id="rail" style="display:none">
  <button class="tab-btn active" data-tab="playback"></button>
  <button class="tab-btn" data-tab="device"></button>
</nav>
<script type="module">
  import { ConnectDeviceVia, DisconnectDevice, GetSettings } from '../wailsjs/go/main/App';

  function switchTab(name) {
    document.querySelectorAll('.tab-btn').forEach(b => b.classList.toggle('active', b.dataset.tab === name));
    document.querySelectorAll('.tab-panel').forEach(p => p.classList.toggle('active', p.id === 'tab-' + name));
    window.__lastTab = name;
  }
  const wrap = document.getElementById('topbar-device-wrap');
  const btn = document.getElementById('topbar-device');
  const nameEl = document.getElementById('topbar-device-name');
  const action = document.getElementById('topbar-device-action');
  let connected = false;
  let busy = false;

  function render(st) {
    connected = !!(st && st.connected);
    wrap.classList.remove('is-online', 'is-offline', 'is-searching');
    if (busy && !connected) {
      wrap.classList.add('is-searching');
      nameEl.textContent = 'Searching…';
      action.textContent = '…';
      action.disabled = true;
      return;
    }
    if (connected) {
      wrap.classList.add('is-online');
      nameEl.textContent = st.mock ? 'Mock device' : (st.name && String(st.name).trim()) || 'Connected';
      action.textContent = 'Disconnect';
      action.disabled = busy;
    } else {
      wrap.classList.add('is-offline');
      nameEl.textContent = 'Not connected';
      action.textContent = 'Connect';
      action.disabled = busy;
    }
  }
  window.addEventListener('device:status', e => {
    if (busy && !(e.detail && e.detail.connected)) return;
    if (e.detail && e.detail.connected) busy = false;
    render(e.detail || {});
  });
  btn.addEventListener('click', () => switchTab('device'));
  action.addEventListener('click', async () => {
    if (busy) return;
    if (connected) {
      busy = true; action.disabled = true;
      const st = await DisconnectDevice();
      busy = false; render(st);
      window.dispatchEvent(new CustomEvent('device:status', { detail: st }));
      return;
    }
    busy = true; render({ connected: false });
    const s = await GetSettings();
    const st = await ConnectDeviceVia((s && s.deviceTransport) || 'ble', (s && s.intifaceUrl) || '');
    busy = false; render(st);
    window.dispatchEvent(new CustomEvent('device:status', { detail: st }));
  });
  render({ connected: false });
  window.__ready = true;
  window.__setDevice = (st) => window.dispatchEvent(new CustomEvent('device:status', { detail: st }));
</script>
</body></html>"""


def main():
    state_js = (
        "let state = { connected: false, mock: false, name: '', address: '' };\n"
    )
    base, shutdown = serve(state_js + app_stub({
        "GetDeviceStatus": "async () => ({ ...state })",
        "GetSettings": "async () => ({ deviceTransport: 'ble', intifaceUrl: '' })",
        "ConnectDeviceVia": "async (t, u) => { window.__calls.push(['connect', t, u]); "
                            "state.connected = true; state.name = 'Sam Neo 2 Pro'; "
                            "state.address = 'AA:BB'; return { ...state }; }",
        "DisconnectDevice": "async () => { window.__calls.push(['disconnect']); "
                            "state.connected = false; state.name = ''; return { ...state }; }",
    }))
    (FRONTEND / "test" / "_topbar_device_harness.html").write_text(PAGE)
    failures = []

    def check(name, cond, detail=""):
        print(("  OK   " if cond else "  FAIL ") + name + (f"  [{detail}]" if not cond else ""))
        if not cond:
            failures.append(name)

    with sync_playwright() as p:
        browser = p.chromium.launch()
        page = browser.new_page()
        page.on("pageerror", lambda e: print("   [pageerror]", e))
        page.goto(f"{base}/test/_topbar_device_harness.html")
        page.wait_for_function("window.__ready === true")

        label = lambda: page.locator("#topbar-device-name").inner_text()
        action = lambda: page.locator("#topbar-device-action").inner_text()

        check("Start offline", label() == "Not connected", label())
        check("Start Connect button", action() == "Connect", action())
        check("Start Klasse offline",
              "is-offline" in page.locator("#topbar-device-wrap").get_attribute("class"))

        page.click("#topbar-device-action")
        page.wait_for_function("document.querySelector('#topbar-device-name').textContent === 'Sam Neo 2 Pro'")
        check("Verbinden setzt Name", label() == "Sam Neo 2 Pro", label())
        check("After connect: Disconnect", action() == "Disconnect", action())
        check("Online-Klasse", "is-online" in page.locator("#topbar-device-wrap").get_attribute("class"))
        check("Connect mit gespeichertem Transport",
              page.evaluate("window.__calls.some(c => c[0]==='connect' && c[1]==='ble')"))

        page.click("#topbar-device-action")
        page.wait_for_function("document.querySelector('#topbar-device-name').textContent === 'Not connected'")
        check("Trennen wieder offline", label() == "Not connected", label())
        check("After disconnect: Connect", action() == "Connect", action())

        page.click("#topbar-device")
        check("Name-Klick öffnet Gerätetab", page.evaluate("window.__lastTab") == "device")

        browser.close()
    shutdown()
    (FRONTEND / "test" / "_topbar_device_harness.html").unlink(missing_ok=True)

    if failures:
        print("FEHLGESCHLAGEN:", ", ".join(failures))
        return 1
    print("Alle Prüfungen bestanden.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
