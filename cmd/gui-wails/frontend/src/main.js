import './style.css';
import './theme_flow.css'; // V1 look (ChatGPT) — loaded after style.css
import './flow_fx.css'; // C1 Flow stage (ChatGPT #503)
import { ICONS } from './flow_icons.js';
import './help.js';
import { uiError, uiInfo, uiWarn, uiLog } from './notify.js';
import { CurrentVersion, CheckForUpdate, CheckForPatch, ApplyUpdate, ApplyPatch, GetSettings, ConnectDeviceVia, DisconnectDevice } from '../wailsjs/go/main/App';
import { releaseTag, releaseNotesPreview, formatUpdateError } from './update_ui.js';
import { initPlayback } from './playback.js';
import { applyPlayerLayoutV1 } from './player_layout.js';
import { initTraining } from './training.js';
import { initGenerator } from './generator.js';
import { initBenchmark } from './benchmark.js';
import { initRoiTraining } from './roi_training.js';
import { EventsOn } from '../wailsjs/runtime/runtime';
import { initDevice } from './device.js';
import { initLog } from './log.js';
import { initSettings } from './settings.js';
import { initSidebar } from './sidebar.js';
import { enhanceGeneratorPreview } from './roi_help.js';
import { enhancePlaybackOZone } from './ozone_ui.js';
import { initPostGenerateReview } from './postgen.js';
import { ensureEULAAccepted } from './legal.js';
import { initToolsHome } from './tools_home.js';
import { GetDonateLinks, IsSuiteBuild } from '../wailsjs/go/main/App';

// Owner 11 Oct (#475): main tabs Play · Generator · Training · Tools.
// Tools bundles Device, Log, Settings (+ suite Bench / AI Train) behind a
// sub-navigation; every panel and data-tab name stays the same, so
// switchTab('device') and `.tab-btn[data-tab="log"]` keep working.
const TOOLS_SECTIONS = new Set(['tools-home', 'device', 'log', 'settings', 'benchmark', 'roi-training']);
let lastToolsSection = 'tools-home';
let toolsHome = null; // Tools → Home (tools_home.js), re-checks on every visit

// Scene Map (Owner 11 Oct, V1): its own top tab, showing the Generator's
// scene-map section for the Generator video in a focused view. The code
// stays in generator.js (same IDs, same state) — only the view changes.
function setSceneMapView(on) {
  const gen = document.getElementById('tab-generator');
  if (!gen) return;
  gen.querySelectorAll('[data-sm-path]').forEach(n => n.removeAttribute('data-sm-path'));
  if (!on) { delete gen.dataset.view; return; }
  gen.dataset.view = 'scenemap';
  const target = gen.querySelector('#gen-advanced-scenemap');
  for (let n = target; n && n !== gen; n = n.parentElement) {
    n.setAttribute('data-sm-path', '');
    if (n.tagName === 'DETAILS') n.open = true;
  }
}

// Player workspace (Owner 11 Oct, V1 W1): one top tab "Player" with the view
// switch Play · Create · Edit. Create is the Generator panel (same IDs, same
// generator.js), Scene Map lives inside Create. The old data-tab values
// "generator" / "scenemap" stay as routes, so switchTab() calls and
// `.tab-btn[data-tab=…]` clicks elsewhere keep working.
const WORKSPACE_TABS = new Set(['playback', 'generator', 'scenemap']);
const WS_VIEWS = ['play', 'create', 'edit'];
let playView = 'play'; // last of play / edit, restored when Player opens
try {
  const v = localStorage.getItem('ws.view');
  if (v === 'play' || v === 'edit') playView = v;
} catch (_) { /* ignore */ }

function setWorkspaceView(view) {
  if (!WS_VIEWS.includes(view)) return;
  if (view === 'create') {
    switchTab('generator');
    return;
  }
  playView = view;
  try { localStorage.setItem('ws.view', view); } catch (_) { /* ignore */ }
  switchTab('playback');
}

function syncWorkspace(name) {
  const inWs = WORKSPACE_TABS.has(name);
  const view = !inWs ? '' : name === 'playback' ? playView : 'create';
  const nav = document.getElementById('ws-view');
  if (nav) nav.hidden = !inWs;
  document.body.dataset.wsView = view;
  const pb = document.getElementById('tab-playback');
  if (pb) pb.dataset.wsView = view || 'none';
  document.querySelectorAll('#ws-view [data-ws-view]').forEach(b => {
    const on = b.dataset.wsView === view;
    b.classList.toggle('is-active', on);
    b.setAttribute('aria-selected', on ? 'true' : 'false');
    b.tabIndex = on ? 0 : -1;
  });
  const sm = document.getElementById('ws-scenemap');
  if (sm) {
    sm.hidden = view !== 'create';
    sm.setAttribute('aria-pressed', name === 'scenemap' ? 'true' : 'false');
  }
  if (inWs) window.dispatchEvent(new CustomEvent('ws:view', { detail: { view } }));
}

function switchTab(name) {
  if (name === 'tools') name = lastToolsSection;
  const sceneMap = name === 'scenemap';
  setSceneMapView(sceneMap);
  const panelName = sceneMap ? 'generator' : name;
  const inTools = TOOLS_SECTIONS.has(name);
  if (inTools) lastToolsSection = name;
  const inWs = WORKSPACE_TABS.has(name);
  // Layout hooks: CSS can adapt per tab (Play hides the info column) / group.
  document.body.dataset.tab = name;
  document.body.dataset.tabGroup = inTools ? 'tools' : inWs ? 'player' : name;
  document.querySelectorAll('.tab-btn').forEach(b => {
    const on = b.dataset.tab === name || (inTools && b.dataset.tab === 'tools')
      || (inWs && b.dataset.tab === 'playback');
    b.classList.toggle('active', on);
    b.setAttribute('aria-selected', on ? 'true' : 'false');
    b.tabIndex = on ? 0 : -1;
  });
  const toolsNav = document.getElementById('tools-nav');
  if (toolsNav) toolsNav.hidden = !inTools;
  document.querySelectorAll('.tab-panel').forEach(p => {
    const on = p.id === 'tab-' + panelName;
    p.classList.toggle('active', on);
    if (on) p.removeAttribute('hidden');
    else p.setAttribute('hidden', '');
  });
  syncWorkspace(name);
  if (name === 'tools-home' && toolsHome) toolsHome.show();
}

function initTabA11y() {
  const rail = document.getElementById('rail');
  if (rail) rail.setAttribute('role', 'tablist');
  document.getElementById('tools-nav')?.setAttribute('role', 'tablist');
  document.querySelectorAll('.tab-btn').forEach(btn => {
    const name = btn.dataset.tab;
    btn.setAttribute('role', 'tab');
    btn.setAttribute('aria-controls', name === 'tools' ? 'tools-nav' : name === 'scenemap' ? 'tab-generator' : 'tab-' + name);
    btn.id = btn.id || 'tabbtn-' + name;
    const on = btn.classList.contains('active');
    btn.setAttribute('aria-selected', on ? 'true' : 'false');
    btn.tabIndex = on ? 0 : -1;
  });
  document.querySelectorAll('.tab-panel').forEach(p => {
    p.setAttribute('role', 'tabpanel');
    const name = p.id.replace(/^tab-/, '');
    p.setAttribute('aria-labelledby', 'tabbtn-' + name);
    if (!p.classList.contains('active')) p.setAttribute('hidden', '');
  });
  const arrowNav = (container) => container?.addEventListener('keydown', (e) => {
    const tabs = [...container.querySelectorAll('.tab-btn')].filter(b => b.offsetParent !== null || b === document.activeElement);
    const i = tabs.indexOf(document.activeElement);
    if (i < 0) return;
    if (e.key === 'ArrowDown' || e.key === 'ArrowRight') {
      e.preventDefault();
      const next = tabs[(i + 1) % tabs.length];
      next.focus();
      switchTab(next.dataset.tab);
    } else if (e.key === 'ArrowUp' || e.key === 'ArrowLeft') {
      e.preventDefault();
      const prev = tabs[(i - 1 + tabs.length) % tabs.length];
      prev.focus();
      switchTab(prev.dataset.tab);
    } else if (e.key === 'Home') {
      e.preventDefault();
      tabs[0].focus();
      switchTab(tabs[0].dataset.tab);
    } else if (e.key === 'End') {
      e.preventDefault();
      tabs[tabs.length - 1].focus();
      switchTab(tabs[tabs.length - 1].dataset.tab);
    }
  });
  arrowNav(rail);
  arrowNav(document.getElementById('tools-nav'));
}

initTabA11y();

document.querySelectorAll('.tab-btn').forEach(btn => {
  btn.addEventListener('click', () => switchTab(btn.dataset.tab));
});
// Workspace view switch (Play · Create · Edit) + Scene Map inside Create.
document.querySelectorAll('#ws-view [data-ws-view]').forEach(b => {
  b.addEventListener('click', () => setWorkspaceView(b.dataset.wsView));
});
document.getElementById('ws-view')?.addEventListener('keydown', e => {
  const btns = [...document.querySelectorAll('#ws-view [data-ws-view]')];
  const i = btns.indexOf(document.activeElement);
  if (i < 0) return;
  let j = -1;
  if (e.key === 'ArrowRight') j = (i + 1) % btns.length;
  else if (e.key === 'ArrowLeft') j = (i - 1 + btns.length) % btns.length;
  if (j < 0) return;
  e.preventDefault();
  btns[j].focus();
  setWorkspaceView(btns[j].dataset.wsView);
});
document.getElementById('ws-scenemap')?.addEventListener('click', () => {
  switchTab(document.body.dataset.tab === 'scenemap' ? 'generator' : 'scenemap');
});
// Other modules ask for a view (e.g. a Generator result opens in Edit).
window.addEventListener('ws:set', e => setWorkspaceView(e.detail && e.detail.view));
// Top bar (V1): folder = open an Emotion Script in the Player; gear = Settings.
document.getElementById('topbar-open')?.addEventListener('click', () => {
  switchTab('playback');
  document.getElementById('pb-choose')?.click();
});
document.getElementById('topbar-settings')?.addEventListener('click', () => switchTab('settings'));

// C3 (ChatGPT #507): one 24 px icon set for the top bar, the Player views
// and the Tools menu. Only the <svg> is swapped; every button keeps its own
// text label and title, so names and tests stay the same.
const NAV_ICONS = {
  playback: 'player', generator: 'create', training: 'training', scenemap: 'scenemap', tools: 'tools',
  'tools-home': 'status', device: 'device', log: 'log', settings: 'settings', benchmark: 'chart', 'roi-training': 'layers',
};
const VIEW_ICONS = { play: 'play', create: 'create', edit: 'edit' };
function applyFlowIcons() {
  const swap = (btn, name) => {
    const svg = btn?.querySelector(':scope > svg');
    if (svg && ICONS[name]) svg.outerHTML = ICONS[name];
  };
  document.querySelectorAll('.tab-btn[data-tab]').forEach(b => swap(b, NAV_ICONS[b.dataset.tab]));
  document.querySelectorAll('.ws-view-btn[data-ws-view]').forEach(b => swap(b, VIEW_ICONS[b.dataset.wsView]));
  swap(document.getElementById('ws-scenemap'), 'scenemap');
}
applyFlowIcons();

const playback = initPlayback(document.getElementById('tab-playback'));
initTraining(document.getElementById('tab-training'));
initGenerator(document.getElementById('tab-generator'), playback);
initBenchmark(document.getElementById('tab-benchmark'));
initRoiTraining(document.getElementById('tab-roi-training'));
toolsHome = initToolsHome(document.getElementById('tab-tools-home'), {
  go: name => switchTab(name),
  loadScriptPath: path => playback.loadScriptPath(path),
});
initDevice(document.getElementById('tab-device'));
initLog(document.getElementById('tab-log'));
initSettings(document.getElementById('tab-settings'));
initSidebar(document.getElementById('sidebar'));
enhanceGeneratorPreview(document.getElementById('tab-generator'));
enhancePlaybackOZone(document.getElementById('tab-playback'));
// V1.0 Player shell: Library · video/timeline · Inspector (docs/V1_PLAN.md S3).
applyPlayerLayoutV1(document.getElementById('tab-playback'), playback);
// Workspace view (Play / Edit remembered) once the Player layout exists.
syncWorkspace(document.body.dataset.tab || 'playback');
initPostGenerateReview();

// Topbar: Geräte-Status + Connect/Disconnect (Transport bleibt im Gerät-Tab).
(function initTopbarDevice() {
  const wrap = document.getElementById('topbar-device-wrap');
  const btn = document.getElementById('topbar-device');
  const nameEl = document.getElementById('topbar-device-name');
  const action = document.getElementById('topbar-device-action');
  if (!wrap || !btn || !nameEl || !action) return;

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
      btn.title = 'Connecting…';
      return;
    }
    if (connected) {
      wrap.classList.add('is-online');
      const label = st.mock
        ? 'Mock device'
        : (st.name && String(st.name).trim()) || 'Connected';
      const batt = (st.batteryOk && typeof st.batteryPct === 'number')
        ? ` · ${st.batteryPct}%`
        : '';
      nameEl.textContent = label + batt;
      action.textContent = 'Disconnect';
      action.disabled = busy;
      const battTitle = batt ? ` · Battery ${st.batteryPct}%` : '';
      btn.title = st.address
        ? `${label}${battTitle} · ${st.address} — Details in Tools → Device`
        : `${label}${battTitle} — Details in Tools → Device`;
    } else {
      wrap.classList.add('is-offline');
      nameEl.textContent = 'Not connected';
      action.textContent = 'Connect';
      action.disabled = busy;
      btn.title = 'Details / connection type in Tools → Device';
    }
  }

  window.addEventListener('device:status', e => {
    // Während eigener Topbar-Verbindung kein Fremd-Status überschreiben,
    // außer das Ergebnis ist bereits connected (Erfolg vom Gerät-Tab).
    if (busy && !(e.detail && e.detail.connected)) return;
    if (e.detail && e.detail.connected) busy = false;
    render(e.detail || {});
  });

  btn.addEventListener('click', () => switchTab('device'));

  action.addEventListener('click', async () => {
    if (busy) return;
    if (connected) {
      busy = true;
      action.disabled = true;
      try {
        const st = await DisconnectDevice();
        busy = false;
        render(st);
        window.dispatchEvent(new CustomEvent('device:status', { detail: st }));
      } catch (err) {
        busy = false;
        uiError('Disconnect failed: ' + err);
        render({ connected: true });
      }
      return;
    }
    busy = true;
    render({ connected: false });
    try {
      const s = await GetSettings();
      const transport = (s && s.deviceTransport) || 'ble';
      const url = (s && s.intifaceUrl) || '';
      const st = await ConnectDeviceVia(transport, url);
      busy = false;
      render(st);
      window.dispatchEvent(new CustomEvent('device:status', { detail: st }));
    } catch (err) {
      busy = false;
      render({ connected: false });
      uiError('Connection failed: ' + err + ' — choose connection type (BLE / Intiface / Mock) in Tools → Device.');
    }
  });

  render({ connected: false });
})();

// Edition switch (Owner 11 Oct): the Free build hides every [data-suite]
// control (AI Train, benchmark, learning export, model training). The CSS
// rule covers markup rendered later too. Default = Free until told.
document.documentElement.dataset.edition = 'free';
IsSuiteBuild().then(suite => {
  if (suite) document.documentElement.dataset.edition = 'suite';
}).catch(() => {});

document.body.dataset.tab = document.querySelector('.tab-btn.active')?.dataset.tab || 'playback';

// Freeware terms: ask once per EULA version (Decline quits the app).
ensureEULAAccepted();

// Header donate pill: shown only when a donation link is configured; opens
// Settings → Support SamNPlayer.
GetDonateLinks().then(links => {
  const btn = document.getElementById('topbar-donate');
  if (!btn || !Array.isArray(links) || !links.length) return;
  btn.hidden = false;
  btn.addEventListener('click', () => {
    switchTab('settings');
    document.querySelector('[data-donate-section]')?.scrollIntoView({ behavior: 'smooth' });
  });
}).catch(() => {});

let currentVersion = 'dev';
CurrentVersion().then(v => {
  currentVersion = v;
  document.getElementById('version-label').textContent = v === 'dev' ? 'dev' : v;
});

// Silent startup update check: never alert()/confirm(), never toast.
// Errors stay in the Log tab only; offer an in-app banner when a newer
// release is actually available. Manual check lives in Settings.
// Full release wins; only if none, offer an additive patch for this base.
function showUpdateBanner(label, { isPatch = false, notes = '' } = {}) {
  if (document.getElementById('update-banner')) return;
  const bar = document.createElement('div');
  bar.id = 'update-banner';
  bar.className = 'update-banner';
  bar.setAttribute('role', 'status');
  const kind = isPatch ? 'Patch' : 'Update';
  const noteHtml = notes
    ? `<span class="update-banner-notes">${escapeHtml(notes)}</span>`
    : '';
  bar.innerHTML = `<span>${kind} ${escapeHtml(label)} available (you have ${escapeHtml(currentVersion)}).</span>
    ${noteHtml}
    <button type="button" id="update-banner-apply" class="primary">Download &amp; restart</button>
    <button type="button" id="update-banner-dismiss">Later</button>`;
  const host = document.getElementById('topbar') || document.getElementById('app');
  host?.prepend(bar);
  bar.querySelector('#update-banner-apply')?.addEventListener('click', () => {
    const btn = bar.querySelector('#update-banner-apply');
    btn.disabled = true;
    btn.textContent = 'Downloading…';
    const apply = isPatch ? ApplyPatch() : ApplyUpdate();
    apply.catch(err => {
      // Successful apply calls os.Exit — window closes. A reject here is real.
      uiError((isPatch ? 'Patch' : 'Update') + ' failed: ' + formatUpdateError(err));
      btn.disabled = false;
      btn.textContent = 'Download & restart';
    });
  });
  bar.querySelector('#update-banner-dismiss')?.addEventListener('click', () => bar.remove());
}

function escapeHtml(s) {
  return String(s ?? '')
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;');
}

GetSettings().then(s => {
  if (!s.updateCheckOnStartup) return;
  CheckForUpdate().then(res => {
    if (res.error) {
      // Log only — no popup. Manual Settings check still surfaces the error.
      uiLog('WARN', 'Update check on startup: ' + formatUpdateError(res.error));
      return;
    }
    if (res.available) {
      showUpdateBanner(releaseTag(res.release), {
        isPatch: false,
        notes: releaseNotesPreview(res.release),
      });
      return;
    }
    // No full release → check additive patch channel.
    return CheckForPatch().then(pres => {
      if (pres.error) {
        uiLog('WARN', 'Patch check on startup: ' + formatUpdateError(pres.error));
        return;
      }
      if (!pres.available) return;
      const id = pres.patchId || (pres.patch && (pres.patch.id || pres.patch.ID)) || '?';
      const notes = releaseNotesPreview(pres.patch);
      showUpdateBanner(id, { isPatch: true, notes });
    });
  }).catch(err => uiLog('WARN', 'Update check on startup: ' + formatUpdateError(err)));
});


// --- Dateien per Drag & Drop -------------------------------------------
//
// Go liefert die echten Dateipfade (siehe registerFileDrop in app.go) - die
// Webview allein bekäme aus einem Drop nur einen Blob ohne Pfad, mit dem
// der Generator nichts anfangen kann.
//
// Die fallengelassene Datei bestimmt den Tab: ein Video gehört in den
// Generator, ein Skript in die Playback. Das erspart es, vorher den
// richtigen Tab zu suchen.
EventsOn('files:dropped', data => {
  const overlay = document.getElementById('drop-overlay');
  if (overlay) overlay.classList.remove('visible');

  if (data.videos && data.videos.length) {
    switchTab('generator');
    // Multi-drop → Create queue (DeepFunGen UX). First video loads; paths[]
    // carries the full batch so the generator can run Everyday Create in order.
    window.dispatchEvent(new CustomEvent('drop:video', {
      detail: {
        path: data.videos[0],
        paths: data.videos,
        extraCount: data.videos.length - 1,
      },
    }));
    return;
  }
  if (data.scripts && data.scripts.length) {
    switchTab('playback');
    window.dispatchEvent(new CustomEvent('drop:script', {
      detail: {
        path: data.scripts[0],
        paths: data.scripts,
        extraCount: data.scripts.length - 1,
      },
    }));
    return;
  }
  if (data.ignored) {
    uiWarn('Drop ignored — please drop a video or an Emotion Script.');
  }
});

// Sichtbare Feedback, solange etwas über dem Fenster schwebt.
const overlay = document.createElement('div');
overlay.id = 'drop-overlay';
overlay.innerHTML = '<div>Drop video or Emotion Script here</div>';
document.body.appendChild(overlay);

let dragDepth = 0;
window.addEventListener('dragenter', e => {
  e.preventDefault();
  dragDepth++;
  overlay.classList.add('visible');
});
window.addEventListener('dragover', e => e.preventDefault());
window.addEventListener('dragleave', () => {
  // dragleave feuert auch beim Wechsel zwischen Kindelementen - deshalb
  // zählen statt einfach auszublenden, sonst flackert die Anzeige.
  dragDepth = Math.max(0, dragDepth - 1);
  if (dragDepth === 0) overlay.classList.remove('visible');
});
window.addEventListener('drop', () => {
  dragDepth = 0;
  overlay.classList.remove('visible');
});
