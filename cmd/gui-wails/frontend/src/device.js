import { GetDeviceStatus, ConnectDevice, DisconnectDevice, TestVibration, TestSuction, TestStop, TestRawValue, ConnectDeviceVia, GetSettings, RunDeviceDiagnostics, GetDiagnosticsHistory } from '../wailsjs/go/main/App';
import { EventsOn } from '../wailsjs/runtime/runtime';
import { wireDataHelp } from './help.js';
import { uiError } from './notify.js';

// Zweck dieses Tabs: sichtbar machen, ob überhaupt ein Gerät gefunden und
// richtig erkannt wurde, und die Ansteuerung isoliert prüfen zu können -
// ohne dafür eine Playback starten zu müssen.

export function initDevice(root) {
  root.innerHTML = `
    <h2>Device</h2>

    <div class="dev-layout" data-dev-region="layout">
    <div class="dev-col dev-col-connect" data-dev-region="connect">
    <div id="dev-status" class="row">
      <div class="dev-visual" aria-hidden="true">
        <div class="dev-shell">
          <div class="dev-fill-suc" id="dev-fill-suc" style="height:0%"></div>
          <div class="dev-fill-vib" id="dev-fill-vib" style="height:0%"></div>
        </div>
      </div>
      <div class="dev-status-meta">
        <div class="dev-status-line">
          <span id="dev-dot"></span>
          <span id="dev-status-text">Loading status…</span>
        </div>
        <p class="hint" id="dev-status-sub" style="margin:0">Not connected yet.</p>
        <div class="dev-caps" id="dev-caps" hidden></div>
      </div>
    </div>

    <div class="row">
      <button id="dev-connect" class="primary">Connect</button>
      <button id="dev-disconnect" disabled>Disconnect</button>
      <select id="dev-transport" style="margin-left:12px;"
        data-help="BLE = direct Bluetooth. Intiface = via Buttplug server (other devices too). Mock = UI without hardware.">
        <option value="ble">Direct via Bluetooth</option>
        <option value="intiface">Via Intiface Central</option>
        <option value="tcode">TCode over Wi-Fi (OSR2 / SR6)</option>
        <option value="mock">Mock device (no hardware)</option>
      </select>
      <input type="text" id="dev-intiface-url" placeholder="e.g. 192.168.1.50 or empty = this machine"
             style="display:none; width:220px;" />
      <input type="text" id="dev-tcode-addr" placeholder="e.g. 192.168.1.50 or tcp://osr.local:8000"
             style="display:none; width:240px;" aria-label="TCode device address" />
    </div>
    <p class="hint" id="dev-transport-hint">Searches up to 20 s for “Sam Neo 2”.</p>

    </div>
    <div class="dev-col dev-col-test" data-dev-region="test">
    <fieldset id="dev-test" disabled style="margin-top:16px; border:1px solid var(--border);
              border-radius:4px; padding:12px;">
      <legend style="padding:0 6px;">Function test</legend>

      <div class="row" style="align-items:center;">
        <label style="width:110px;" data-help="11 internal levels (0–10). Values between levels have no effect.">Vibration</label>
        <input type="range" id="dev-vib" min="0" max="100" value="0" style="flex:1;">
        <span id="dev-vib-val" class="dev-level-val">0 % (level 0)</span>
      </div>

      <div class="row" style="align-items:center;">
        <label style="width:110px;" data-help="6 internal levels (0–5). Values between levels have no effect.">Suction</label>
        <input type="range" id="dev-suc" min="0" max="100" value="0" style="flex:1;">
        <span id="dev-suc-val" class="dev-level-val">0 % (level 0)</span>
      </div>

      <div class="row" style="margin-top:10px;">
        <button id="dev-pulse" data-help="Two seconds of medium vibration, then off.">Short test pulse</button>
        <button id="dev-stop" class="danger">All off</button>
      </div>
    </fieldset>

    <details id="dev-raw-details" class="st-opt-panel">
      <summary>Raw value test</summary>
    <fieldset id="dev-raw" disabled style="margin-top:10px; border:1px solid var(--border);
              border-radius:4px; padding:12px;">
      <legend style="padding:0 6px;" data-help="Sends the level value without conversion. 0–10 / 0–5 come from Buttplug — whether firmware accepts more is unclear. Try 3 vs 4, then 20/50/100.">Raw value test</legend>
      <div class="row" style="align-items:center;">
        <select id="dev-raw-channel">
          <option value="vibration">Vibration</option>
          <option value="suction">Suction</option>
        </select>
        <input type="number" id="dev-raw-value" min="0" max="255" value="0" style="width:90px;" />
        <button id="dev-raw-send">Send</button>
        <span id="dev-raw-hint" class="hint"></span>
      </div>
    </fieldset>
    </details>

    <details id="dev-diag-details" class="st-opt-panel">
      <summary>Diagnostics</summary>
    <fieldset id="dev-diag" disabled style="margin-top:10px; border:1px solid var(--border);
              border-radius:4px; padding:12px;">
      <legend style="padding:0 6px;" data-help="Automated test sequence (raw acceptance, update rate, channel interaction). Measures write latency and accepted values — not felt intensity. The device moves briefly several times.">Device diagnostics</legend>
      <div class="row"><button id="diag-run">Run diagnostics</button></div>
      <div id="diag-status" class="path-label"></div>
      <div id="diag-result" style="margin-top:8px;"></div>
      <h4 style="margin:14px 0 4px;">History</h4>
      <div id="diag-history" class="hint">Loading…</div>
    </fieldset>
    </details>

    <div id="dev-log" class="hint" style="margin-top:12px; white-space:pre-wrap;"></div>
    </div>
    </div>
  `;

  wireDataHelp(root);

  const el = id => root.querySelector(id);
  let busy = false;
  let searching = false;

  function log(msg) {
    el('#dev-log').textContent = new Date().toLocaleTimeString() + '  ' + msg;
  }

  function logError(msg) {
    log(msg);
    uiError(msg);
  }

  function setFills(vibPct, sucPct) {
    el('#dev-fill-vib').style.height = Math.max(0, Math.min(100, vibPct)) + '%';
    el('#dev-fill-suc').style.height = Math.max(0, Math.min(100, sucPct * 0.85)) + '%';
  }

  // Das Gerät kennt intern feste Stufen (Vibration 0-10, Sog 0-5). Die
  // Prozentanzeige allein wäre irreführend, weil sich zwischen zwei Stufen
  // nichts ändert - deshalb wird die tatsächliche Stufe mit angezeigt.
  const vibStep = pct => Math.round(pct / 100 * 10);
  const sucStep = pct => Math.round(pct / 100 * 5);

  function renderCaps(st) {
    const box = el('#dev-caps');
    if (!st || !st.connected) {
      box.hidden = true;
      box.innerHTML = '';
      return;
    }
    const chips = [];
    const transportLabel = {
      ble: 'Direct BLE',
      intiface: 'Intiface',
      mock: 'Mock',
    }[st.transport] || (st.mock ? 'Mock' : '');
    if (transportLabel) chips.push(transportLabel);
    // D1: every channel the device has; not-driven ones are marked.
    const feats = (st.capabilities && Array.isArray(st.capabilities.features)) ? st.capabilities.features : null;
    if (feats && feats.length) {
      for (const f of feats) {
        const name = String(f.channel || '').replace(/^./, c => c.toUpperCase());
        chips.push(f.driven
          ? { text: name, channel: f.channel }
          : { text: `${name} · coming`, channel: f.channel, off: true,
            title: 'The device has this channel; SamNPlayer does not drive it yet (docs/DEVICE_SUPPORT.md).' });
      }
    } else {
      if (st.capVibration) chips.push('Vibration');
      if (st.capSuction) chips.push('Suction');
    }
    if (st.capBattery) {
      chips.push(st.batteryOk ? `Battery ${st.batteryPct}%` : 'Battery');
    }
    if (st.capRaw) chips.push('Raw values');
    if (chips.length === 0) {
      box.hidden = true;
      box.innerHTML = '';
      return;
    }
    box.hidden = false;
    const esc = t => String(t).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/"/g, '&quot;');
    box.innerHTML = chips.map(c => (typeof c === 'string'
      ? `<span class="dev-cap">${esc(c)}</span>`
      : `<span class="dev-cap${c.off ? ' dev-cap-off' : ''}" data-channel="${esc(c.channel)}"${c.title ? ` title="${esc(c.title)}"` : ''}>${esc(c.text)}</span>`)).join('');
  }

  function render(st) {
    const dot = el('#dev-dot');
    const text = el('#dev-status-text');
    const sub = el('#dev-status-sub');
    const box = el('#dev-status');
    box.classList.remove('is-connected', 'is-searching', 'is-session');

    if (st.sessionActive) {
      box.classList.add('is-session');
      dot.style.background = 'var(--warn, #d9a441)';
      text.textContent = 'Playback or training is running — device test unavailable.';
      sub.textContent = 'Test again after the session ends.';
      box.style.borderColor = 'var(--warn, #d9a441)';
      renderCaps(null);
    } else if (searching) {
      box.classList.add('is-searching');
      dot.style.background = 'var(--warn, #d9a441)';
      text.textContent = 'Searching for device…';
      sub.textContent = 'Up to 20 seconds.';
      box.style.borderColor = 'var(--warn, #d9a441)';
      renderCaps(null);
    } else if (st.connected) {
      box.classList.add('is-connected');
      dot.style.background = 'var(--ok)';
      box.style.borderColor = 'var(--ok)';
      // Name/Adresse/RSSI stay in #dev-status-text — Playwright + sidebar
      // contract (device_display_test.py). Sub line is a short caption only.
      const parts = [st.mock ? 'Mock device connected' : 'Connected'];
      if (st.name) parts.push(st.name);
      if (st.address) parts.push(st.address);
      if (st.rssi) parts.push(`Signal ${st.rssi} dBm`);
      if (st.batteryOk && typeof st.batteryPct === 'number') {
        parts.push(`Battery ${st.batteryPct}%`);
      }
      text.textContent = parts.join('  ·  ');
      sub.textContent = st.mock
        ? 'Simulated device'
        : (st.batteryOk
          ? `Ready for function test · Battery ${st.batteryPct}%.`
          : 'Ready for function test.');
      renderCaps(st);
    } else {
      dot.style.background = '#777';
      box.style.borderColor = 'var(--border)';
      text.textContent = 'Not connected';
      sub.textContent = 'Choose a connection and tap Connect.';
      setFills(0, 0);
      renderCaps(null);
    }

    const canTest = st.connected && !st.sessionActive;
    el('#dev-test').disabled = !canTest;
    el('#dev-raw').disabled = !canTest;
    el('#dev-diag').disabled = !canTest;
    el('#dev-connect').disabled = st.connected || st.sessionActive || busy;
    el('#dev-disconnect').disabled = !st.connected || busy;
    el('#dev-transport').disabled = st.connected || busy;
    el('#dev-intiface-url').disabled = st.connected || busy;
    el('#dev-tcode-addr').disabled = st.connected || busy;

    // Sidebar + Topbar hören auf dasselbe Event (ein Poll, mehrere Anzeigen).
    window.dispatchEvent(new CustomEvent('device:status', { detail: st }));
  }

  async function refresh() {
    try {
      const st = await GetDeviceStatus();
      render(st);
    } catch (e) {
      log('Could not fetch status: ' + e);
    }
  }

  // Erklärung und Adressfeld to die gewählte Verbindungsart anpassen.
  el('#dev-transport').addEventListener('change', e => {
    const intiface = e.target.value === 'intiface';
    const tcode = e.target.value === 'tcode';
    el('#dev-intiface-url').style.display = intiface ? 'inline-block' : 'none';
    el('#dev-tcode-addr').style.display = tcode ? 'inline-block' : 'none';
    el('#dev-transport-hint').innerHTML = tcode
      ? 'Sends TCode to an OSR2 / SR6 (or other TCode device) running Wi-Fi firmware. '
      + 'Enter its IP address; plain addresses use UDP port 8000, or write tcp://host:port. '
      + 'UDP has no handshake: if the device does not move, check the address and firmware settings. '
      + 'The stroke (L0) follows the script; extra axes come later.'
      : intiface
      ? 'Connects via a running Buttplug server. Start Intiface Central, '
      + 'connect the device there and start the server. Benefit: no dedicated '
      + 'Bluetooth adapter, and it works with any Buttplug-supported '
      + 'device. Leave address empty for default ws://127.0.0.1:12345. '
      + 'If the socket drops once, SamNPlayer tries <b>one</b> automatic '
      + 'reconnect — then tap Connect again if it stays down.'
      : e.target.value === 'mock'
        ? 'Simulates a device to test the UI without hardware.'
        : 'Searches up to 20 seconds for a device named "Sam Neo 2". The device '
        + 'must be on and not connected to another app.';
  });

  el('#dev-connect').addEventListener('click', async () => {
    busy = true;
    searching = true;
    el('#dev-connect').disabled = true;
    render({ connected: false, sessionActive: false });
    try {
      const transport = el('#dev-transport').value;
      const address = transport === 'tcode' ? el('#dev-tcode-addr').value.trim() : el('#dev-intiface-url').value.trim();
      const st = await ConnectDeviceVia(transport, address);
      searching = false;
      busy = false;
      render(st);
      log('Connected.');
    } catch (e) {
      searching = false;
      busy = false;
      await refresh();
      log('Connection failed: ' + e);
    }
  });

  el('#dev-disconnect').addEventListener('click', async () => {
    busy = true;
    try {
      const st = await DisconnectDevice();
      busy = false;
      el('#dev-vib').value = 0;
      el('#dev-suc').value = 0;
      el('#dev-vib-val').textContent = '0 % (level 0)';
      el('#dev-suc-val').textContent = '0 % (level 0)';
      setFills(0, 0);
      render(st);
      log('Disconnected.');
    } catch (e) {
      busy = false;
      await refresh();
      log('Disconnect error: ' + e);
    }
  });

  el('#dev-vib').addEventListener('input', async e => {
    const pct = Number(e.target.value);
    el('#dev-vib-val').textContent = `${pct} % (level ${vibStep(pct)})`;
    setFills(pct, Number(el('#dev-suc').value));
    try {
      await TestVibration(pct / 100);
    } catch (err) {
      logError('Vibration: ' + err);
    }
  });

  el('#dev-suc').addEventListener('input', async e => {
    const pct = Number(e.target.value);
    el('#dev-suc-val').textContent = `${pct} % (level ${sucStep(pct)})`;
    setFills(Number(el('#dev-vib').value), pct);
    try {
      await TestSuction(pct / 100);
    } catch (err) {
      logError('Suction: ' + err);
    }
  });

  el('#dev-pulse').addEventListener('click', async () => {
    try {
      log('Test pulse running…');
      await TestVibration(0.5);
      setTimeout(async () => {
        try {
          await TestStop();
          el('#dev-vib').value = 0;
          el('#dev-vib-val').textContent = '0 % (level 0)';
          log('Test pulse finished.');
        } catch (e) {
          log('Turn off after pulse: ' + e);
        }
      }, 2000);
    } catch (e) {
      log('Test pulse: ' + e);
    }
  });

  el('#dev-raw-send').addEventListener('click', async () => {
    const channel = el('#dev-raw-channel').value;
    const value = Number(el('#dev-raw-value').value);
    const expected = channel === 'vibration' ? 10 : 5;
    el('#dev-raw-hint').textContent = value > expected
      ? `above documented maximum (${expected})`
      : '';
    try {
      await TestRawValue(channel, value);
      log(`Raw value ${value} to ${channel} sent.`);
    } catch (err) {
      logError('Raw value: ' + err);
    }
  });

  el('#dev-stop').addEventListener('click', async () => {
    try {
      await TestStop();
      el('#dev-vib').value = 0;
      el('#dev-suc').value = 0;
      el('#dev-vib-val').textContent = '0 % (level 0)';
      el('#dev-suc-val').textContent = '0 % (level 0)';
      log('All off.');
    } catch (e) {
      log('Stop: ' + e);
    }
  });

  // Device diagnostics: siehe app_diagnostics.go/device/diagnostics.go.
  function phaseLabel(phase) {
    return phase.replace(/_/g, ' ');
  }

  function renderDiagReport(report) {
    if (!report) return '';
    const rows = (report.phases || []).map(p => `
      <tr><td>${phaseLabel(p.phase)}</td><td>${p.commands}</td>
        <td>${p.errors > 0 ? `<span style="color:var(--danger)">${p.errors}</span>` : '0'}</td>
        <td>${p.meanLatencyMs.toFixed(1)} ms</td><td>${p.maxLatencyMs.toFixed(1)} ms</td></tr>
    `).join('');
    const interrupted = report.interrupted
      ? '<p style="color:var(--warn, #d9a441)">Run was interrupted, incomplete.</p>' : '';
    const notes = (report.notes || []).map(n => `<p class="hint">${n}</p>`).join('');
    return `
      ${interrupted}
      <table class="bench-table">
        <thead><tr><th>Phase</th><th>Commands</th><th>Errors</th><th>Avg latency</th><th>Max latency</th></tr></thead>
        <tbody>${rows}</tbody>
      </table>
      ${notes}
    `;
  }

  function renderDiagHistoryRow(entry) {
    const r = entry.report || {};
    const errors = (r.phases || []).reduce((sum, p) => sum + p.errors, 0);
    const commands = (r.phases || []).reduce((sum, p) => sum + p.commands, 0);
    const when = new Date(entry.timestamp);
    const whenText = isNaN(when.getTime()) ? entry.timestamp : when.toLocaleString();
    return `<div>${whenText}${entry.mock ? ' (Mock)' : entry.deviceName ? ` (${entry.deviceName})` : ''} — `
      + `${commands} commands, ${errors} errors${r.interrupted ? ', interrupted' : ''}</div>`;
  }

  async function refreshDiagHistory() {
    const box = el('#diag-history');
    try {
      const history = await GetDiagnosticsHistory();
      if (!Array.isArray(history) || history.length === 0) {
        box.textContent = 'No diagnostic run recorded yet.';
        return;
      }
      box.innerHTML = history.map(renderDiagHistoryRow).join('');
    } catch (err) {
      uiError('Diagnostics history: ' + err, box);
    }
  }

  el('#diag-run').addEventListener('click', async () => {
    el('#diag-run').disabled = true;
    el('#diag-status').textContent = 'Running… (vibration/suction move briefly several times)';
    el('#diag-result').innerHTML = '';
    try {
      await RunDeviceDiagnostics();
    } catch (err) {
      el('#diag-run').disabled = false;
      uiError('Diagnostics: ' + err, el('#diag-status'));
    }
  });

  EventsOn('diagnostics:entry', e => {
    const value = typeof e.sentRaw === 'number' ? e.sentRaw : e.wantedValue;
    el('#diag-status').textContent =
      `Running… ${phaseLabel(e.phase)}${e.channel ? ' · ' + e.channel : ''}${value !== undefined ? ' · ' + value : ''}`
      + (e.error ? ` · error: ${e.error}` : '');
  });
  EventsOn('diagnostics:done', entry => {
    el('#diag-run').disabled = false;
    el('#diag-status').textContent = 'Done.';
    el('#diag-result').innerHTML = renderDiagReport(entry.report);
    refreshDiagHistory();
  });

  refreshDiagHistory();

  // Zuletzt benutzte Verbindungsart und Adresse wiederherstellen.
  GetSettings().then(s => {
    if (s.deviceTransport) {
      el('#dev-transport').value = s.deviceTransport;
      el('#dev-transport').dispatchEvent(new Event('change'));
    }
    if (s.intifaceUrl) el('#dev-intiface-url').value = s.intifaceUrl;
    if (s.tcodeAddress) el('#dev-tcode-addr').value = s.tcodeAddress;
  }).catch(() => {});

  refresh();
  // Der Zustand kann sich außerhalb dieses Tabs ändern (eine Playback
  // startet oder endet), deshalb regelmäßig nachfragen statt nur beim Laden.
  setInterval(refresh, 2000);

  return { refresh };
}
