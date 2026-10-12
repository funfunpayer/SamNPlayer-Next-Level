// Tools → Home (V1 target tools.png): one page with the app's real status
// and the tools behind it. Every row comes from a real check — device
// status, runtime dependencies (ffmpeg …), app folders, the last device
// diagnostics run, cache size, version and edition. Nothing is assumed
// healthy: a row that has not been checked says so.
import { GetDeviceStatus, GetRuntimeHealth, GetDiagnosticsHistory, GetCacheInfo, CurrentVersion, IsSuiteBuild, OpenLogFolder } from '../wailsjs/go/main/App';
import { readRecent } from './player_layout.js';

function make(tag, cls, text) {
  const e = document.createElement(tag);
  if (cls) e.className = cls;
  if (text != null) e.textContent = text;
  return e;
}

function baseName(p) { return String(p).split(/[\\/]/).pop(); }

function fmtWhen(ts) {
  const d = new Date(ts);
  if (Number.isNaN(d.getTime())) return '';
  return d.toLocaleString(undefined, { dateStyle: 'medium', timeStyle: 'short' });
}

// state: ok | warn | bad | off (not checked / not applicable)
function statusRow(id, label, value, state) {
  const li = make('li', 'th-row is-' + state);
  li.id = 'th-row-' + id;
  li.dataset.state = state;
  li.append(make('span', 'th-dot'), make('span', 'th-label', label), make('span', 'th-value', value));
  return li;
}

export function initToolsHome(panel, { go, loadScriptPath }) {
  if (!panel) return null;
  panel.innerHTML = `
    <div class="th">
      <header class="th-hero">
        <h2>Tools</h2>
        <p class="hint">Devices, diagnostics, logs and settings — with the app's real status.</p>
      </header>
      <section class="card th-status" aria-labelledby="th-status-h">
        <div class="th-card-head">
          <h3 id="th-status-h">Status</h3>
          <button type="button" id="th-recheck">Check again</button>
        </div>
        <p class="th-summary hint" id="th-summary">Checking…</p>
        <ul class="th-rows" id="th-rows"></ul>
      </section>
      <section class="card th-quick" aria-labelledby="th-quick-h">
        <h3 id="th-quick-h">Quick tools</h3>
        <div class="th-quick-grid">
          <button type="button" id="th-go-device">Device &amp; diagnostics</button>
          <button type="button" id="th-open-script">Open script…</button>
          <button type="button" id="th-go-log">Show log</button>
          <button type="button" id="th-open-logs">Open log folder</button>
          <button type="button" id="th-go-settings">Settings</button>
          <button type="button" id="th-check-update">Check for updates</button>
        </div>
      </section>
      <section class="card th-recent" aria-labelledby="th-recent-h">
        <h3 id="th-recent-h">Recent scripts</h3>
        <ul class="th-recent-list" id="th-recent"></ul>
      </section>
    </div>`;
  const q = sel => panel.querySelector(sel);
  const rows = q('#th-rows');
  let device = null;
  let checking = null;

  function deviceRow(st) {
    if (!st) return statusRow('device', 'Device', 'Not checked', 'off');
    if (st.connected) {
      const name = (st.name || 'Device') + (st.mock ? ' (simulated)' : '');
      return statusRow('device', 'Device', 'Connected · ' + name, 'ok');
    }
    return statusRow('device', 'Device', 'Not connected', 'off');
  }

  async function check() {
    if (checking) return checking;
    q('#th-recheck').disabled = true;
    q('#th-summary').textContent = 'Checking…';
    checking = (async () => {
      const [st, health, hist, cache, version, suite] = await Promise.all([
        GetDeviceStatus().catch(() => null),
        GetRuntimeHealth().catch(err => ({ error: String(err) })),
        GetDiagnosticsHistory().catch(() => null),
        GetCacheInfo().catch(() => null),
        CurrentVersion().catch(() => ''),
        IsSuiteBuild().catch(() => false),
      ]);
      if (st) device = st;
      const list = [deviceRow(device)];
      let problems = 0;
      if (health && !health.error) {
        for (const d of health.deps || []) {
          const state = d.found ? 'ok' : d.required ? 'bad' : 'warn';
          if (!d.found && d.required) problems++;
          list.push(statusRow('dep-' + d.id, d.label,
            d.found ? 'Found' + (d.path ? ' · ' + d.path : '') : (d.required ? 'Missing' : 'Not installed (optional)'), state));
        }
        const failed = (health.dirsFailed || []).length;
        if (failed) problems++;
        list.push(statusRow('folders', 'App folders',
          failed ? `${failed} folder(s) could not be created` : 'Ready', failed ? 'bad' : 'ok'));
      } else {
        problems++;
        list.push(statusRow('runtime', 'Runtime check', 'Failed: ' + (health ? health.error : 'no answer'), 'bad'));
      }
      const last = Array.isArray(hist) && hist.length ? hist[0] : null; // newest first
      if (last) {
        const r = last.report || {};
        const value = (r.interrupted ? 'Interrupted' : 'Completed') + ' · ' + fmtWhen(last.timestamp)
          + (last.deviceName ? ' · ' + last.deviceName : '') + (last.mock ? ' (simulated)' : '');
        list.push(statusRow('diagnostics', 'Last device diagnostics', value, r.interrupted ? 'warn' : 'ok'));
      } else {
        list.push(statusRow('diagnostics', 'Last device diagnostics', 'Never run', 'off'));
      }
      if (cache) list.push(statusRow('cache', 'Cache', `${cache.humanSize} · ${cache.files} file(s)`, 'ok'));
      list.push(statusRow('version', 'Version', (version || 'unknown') + ' · ' + (suite ? 'Suite' : 'Free'), version ? 'ok' : 'off'));
      rows.replaceChildren(...list);
      q('#th-summary').textContent = problems
        ? `${problems} problem(s) found — see the red rows.`
        : 'No problems found in the checks below.';
      panel.dataset.thProblems = String(problems);
    })();
    try { await checking; } finally {
      checking = null;
      q('#th-recheck').disabled = false;
    }
  }

  function renderRecent() {
    const list = q('#th-recent');
    const items = readRecent().slice(0, 6);
    list.replaceChildren();
    if (!items.length) {
      list.append(make('li', 'hint', 'Nothing yet — scripts you open appear here.'));
      return;
    }
    for (const p of items) {
      const li = make('li');
      const b = make('button', 'th-recent-item', baseName(p));
      b.type = 'button';
      b.title = p;
      b.dataset.path = p;
      li.append(b);
      list.append(li);
    }
  }

  q('#th-recheck').addEventListener('click', () => check());
  q('#th-go-device').addEventListener('click', () => go('device'));
  q('#th-go-log').addEventListener('click', () => go('log'));
  q('#th-go-settings').addEventListener('click', () => go('settings'));
  q('#th-open-logs').addEventListener('click', () => OpenLogFolder().catch(() => {}));
  q('#th-open-script').addEventListener('click', () => {
    go('playback');
    document.getElementById('pb-choose')?.click();
  });
  q('#th-check-update').addEventListener('click', () => {
    go('settings');
    document.getElementById('st-update-now')?.click();
  });
  q('#th-recent').addEventListener('click', async e => {
    const b = e.target.closest('[data-path]');
    if (!b) return;
    go('playback');
    await loadScriptPath(b.dataset.path).catch(() => {});
  });
  window.addEventListener('device:status', e => {
    device = e.detail || null;
    const old = rows.querySelector('#th-row-device');
    if (old) old.replaceWith(deviceRow(device));
  });

  return {
    show() { renderRecent(); check(); },
  };
}
