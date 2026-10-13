// Player layout v1 (Owner's final mockup, docs/V1_PLAN.md S3).
//
// Arranges the existing Player controls into the 1.0 shell:
//   Library (left) · video + transport + bottom tabs (centre) · Inspector (right)
// Nothing is re-implemented here: existing elements are *moved* into place,
// so every id, listener and binding stays the same (playback.js queries are
// scoped to the Player root, which still contains everything). Panels are
// switched with classes; the real stylesheet hides inactive ones, so tests
// that run without CSS still reach every control.

import { SetChannelLevels, SetPeakReduce, SetStrokeOutput, SetMotorLevels, SaveVideoSnapshot } from '../wailsjs/go/main/App';
import { getSettingsCache, saveSetting } from './settings.js';
import { mountScenePanel } from './scene_panel.js';

const RECENT_KEY = 'pb.recent';
const CHAN_KEY = 'pb.chanLevels';
const RECENT_MAX = 8;
const FAV_KEY = 'pb.favorites';
const FAV_MAX = 50;
const VIDEO_KEY = 'pb.recentVideos';
const SCRIPT_KEYS = { imported: 'pb.scripts.imported', generated: 'pb.scripts.generated', edited: 'pb.scripts.edited' };
const SCRIPTS_MAX = 50;
const INSPECTOR_KEY = 'pb.inspectorTab';
const BOTTOM_KEY = 'pb.bottomTab';
const FX_TAB_KEY = 'pb.effectsTab';

function store(key, value) { try { localStorage.setItem(key, value); } catch (_) { /* ignore */ } }
function load(key) { try { return localStorage.getItem(key); } catch (_) { return null; } }

export function readRecent() {
  try {
    const list = JSON.parse(load(RECENT_KEY) || '[]');
    return Array.isArray(list) ? list.filter(p => typeof p === 'string' && p) : [];
  } catch (_) { return []; }
}

function readList(key) {
  try {
    const list = JSON.parse(load(key) || '[]');
    return Array.isArray(list) ? list.filter(p => typeof p === 'string' && p) : [];
  } catch (_) { return []; }
}

export function readFavorites() { return readList(FAV_KEY); }

/** Toggles path in Favorites; returns true when it is a favorite now. */
export function toggleFavorite(path) {
  if (!path) return false;
  const list = readFavorites();
  const on = !list.includes(path);
  const next = on ? [path, ...list].slice(0, FAV_MAX) : list.filter(p => p !== path);
  store(FAV_KEY, JSON.stringify(next));
  return on;
}

export function readRecentVideos() { return readList(VIDEO_KEY); }

/** Library "Scripts": imported (opened), generated (Generator result), edited (saved edit). */
export function readScripts(cat) { return SCRIPT_KEYS[cat] ? readList(SCRIPT_KEYS[cat]) : []; }

export function pushScript(path, cat) {
  const key = SCRIPT_KEYS[cat];
  if (!path || !key) return;
  store(key, JSON.stringify([path, ...readList(key).filter(p => p !== path)].slice(0, SCRIPTS_MAX)));
}

function escAttr(v) {
  return String(v).replace(/&/g, '&amp;').replace(/"/g, '&quot;').replace(/</g, '&lt;');
}
function escText(v) {
  return String(v).replace(/&/g, '&amp;').replace(/</g, '&lt;');
}

export function pushRecentVideo(path) {
  if (!path) return readRecentVideos();
  const list = [path, ...readRecentVideos().filter(p => p !== path)].slice(0, RECENT_MAX);
  store(VIDEO_KEY, JSON.stringify(list));
  return list;
}

export function pushRecent(path) {
  if (!path) return readRecent();
  const list = [path, ...readRecent().filter(p => p !== path)].slice(0, RECENT_MAX);
  store(RECENT_KEY, JSON.stringify(list));
  return list;
}

function baseName(p) { return String(p).split(/[\\/]/).pop(); }

function icon(path) {
  return `<svg viewBox="0 0 24 24" aria-hidden="true">${path}</svg>`;
}

const ICONS = {
  folder: icon('<path d="M3.5 7.2a1.7 1.7 0 0 1 1.7-1.7h4.1l2 2.1h7.5a1.7 1.7 0 0 1 1.7 1.7v8.2a1.7 1.7 0 0 1-1.7 1.7H5.2a1.7 1.7 0 0 1-1.7-1.7z"/>'),
  recent: icon('<circle cx="12" cy="12" r="8"/><path d="M12 7.5V12l3 2"/>'),
  list: icon('<path d="M8 7h11M8 12h11M8 17h11"/><circle cx="4.5" cy="7" r="1"/><circle cx="4.5" cy="12" r="1"/><circle cx="4.5" cy="17" r="1"/>'),
  map: icon('<path d="M12 21s-6.2-5.6-6.2-10.6a6.2 6.2 0 0 1 12.4 0C18.2 15.4 12 21 12 21z"/><circle cx="12" cy="10.4" r="2.3"/>'),
  wave: icon('<path d="M3 12h2l2-6 3 12 3-9 2 5 2-2h4"/>'),
  bpm: icon('<path d="M5 20V10M10 20V5M15 20v-7M20 20V8"/>'),
  edit: icon('<path d="M4.5 19.5h4l10-10a2.8 2.8 0 0 0-4-4l-10 10z"/><path d="M13.5 6.5l4 4"/>'),
  drop: icon('<path d="M12 4v10M7.5 9.5 12 14l4.5-4.5"/><path d="M5 17.5v1.5h14v-1.5"/>'),
  bookmark: icon('<path d="M7 4h10v16l-5-4-5 4z"/>'),
  image: icon('<rect x="4" y="5" width="16" height="14" rx="2"/><circle cx="9" cy="10" r="1.6"/><path d="M5 17l5-5 4 4 2-2 3 3"/>'),
  expand: icon('<path d="M4 9V4h5M15 4h5v5M20 15v5h-5M9 20H4v-5"/>'),
  chevron: icon('<path d="M7 10l5 5 5-5"/>'),
  spark: icon('<path d="M12 3.5l1.8 5.2 5.2 1.8-5.2 1.8L12 17.5l-1.8-5.2L5 10.5l5.2-1.8z"/><path d="M18.5 16l.7 1.8 1.8.7-1.8.7-.7 1.8-.7-1.8-1.8-.7 1.8-.7z"/>'),
  star: icon('<path d="M12 4.2l2.4 4.9 5.4.8-3.9 3.8.9 5.4-4.8-2.6-4.8 2.6.9-5.4-3.9-3.8 5.4-.8z"/>'),
};

/**
 * @param {HTMLElement} root  Player root (#tab-playback)
 * @param {{ loadScriptPath: (p: string) => Promise<void> }} api
 */
export function applyPlayerLayoutV1(root, api) {
  if (!root || root.dataset.pbLayout === 'v1') return;
  root.dataset.pbLayout = 'v1';
  const q = sel => root.querySelector(sel);

  // ---- Shell: library column + main column -------------------------------
  const main = document.createElement('div');
  main.className = 'pb-main-col';
  main.dataset.pbRegion = 'main';
  for (const sel of ['.pb-head', '#pb-empty', '#pb-loaded']) {
    const n = q(sel);
    if (n) main.appendChild(n);
  }

  const lib = document.createElement('aside');
  lib.className = 'pb-library';
  lib.dataset.pbRegion = 'library';
  lib.setAttribute('aria-label', 'Library');
  lib.innerHTML = `
    <p class="pb-lib-title">Library</p>
    <button type="button" class="pb-lib-item is-primary" id="pb-lib-local">${ICONS.folder}<span>Local Files</span></button>
    <button type="button" class="pb-lib-item" id="pb-lib-recent-btn" aria-expanded="true">${ICONS.recent}<span>Recent</span></button>
    <ol class="pb-lib-recent" id="pb-lib-recent"></ol>
    <button type="button" class="pb-lib-item" id="pb-lib-fav-btn" aria-expanded="true">${ICONS.star}<span>Favorites</span></button>
    <ol class="pb-lib-recent pb-lib-fav" id="pb-lib-fav"></ol>
    <button type="button" class="pb-lib-item" id="pb-lib-playlist">${ICONS.list}<span>Playlists</span><b class="pb-lib-count" id="pb-lib-playlist-count"></b></button>
    <p class="pb-lib-title">Scripts</p>
    ${[['imported', 'Imported', ICONS.drop], ['generated', 'Generated', ICONS.spark], ['edited', 'Edited', ICONS.edit]].map(([k, label, ic]) =>
    `<button type="button" class="pb-lib-item" id="pb-lib-${k}-btn" aria-expanded="false" aria-controls="pb-lib-${k}">${ic}<span>${label}</span><b class="pb-lib-count" id="pb-lib-${k}-count"></b></button>
    <ol class="pb-lib-recent" id="pb-lib-${k}" hidden></ol>`).join('')}
    <p class="pb-lib-title">Tools</p>
    <button type="button" class="pb-lib-item" id="pb-lib-editor" title="Edit the curve of the loaded script (drag / add / delete dots)">${ICONS.edit}<span>Editor</span></button>
    <button type="button" class="pb-lib-item" id="pb-lib-scenemap">${ICONS.map}<span>Scene Map</span></button>
    <button type="button" class="pb-lib-item" id="pb-lib-audio">${ICONS.wave}<span>Audio Analysis</span></button>
    <button type="button" class="pb-lib-item" id="pb-lib-bpm">${ICONS.bpm}<span>BPM Detection</span></button>
    <p class="pb-lib-title">Quick Access</p>
    <div class="pb-lib-drop" id="pb-lib-drop">${ICONS.drop}<span>Drag files here</span>
      <small>Video → Generator · Script → Player</small></div>
  `;
  root.prepend(lib);
  lib.after(main);

  // ---- Inspector (right): tabs Devices · Audio · Playback · View ----------
  const tools = q('#pb-tools');
  const head = tools.querySelector('.pb-tools-head');
  const title = head?.querySelector('.pb-tools-title');
  if (title) title.textContent = 'Inspector';
  const tabs = [['devices', 'Devices'], ['audio', 'Audio'], ['playback', 'Playback'], ['view', 'View']];
  const bar = document.createElement('div');
  bar.className = 'pb-insp-tabs';
  bar.dataset.pbRegion = 'inspector-tabs';
  bar.setAttribute('role', 'tablist');
  bar.innerHTML = tabs.map(([k, label]) =>
    `<button type="button" role="tab" class="pb-insp-tab" data-insp="${k}" id="pb-insp-tab-${k}">${label}</button>`).join('');
  const panels = {};
  for (const [k] of tabs) {
    const p = document.createElement('div');
    p.className = 'pb-insp-panel';
    p.dataset.inspPanel = k;
    p.id = `pb-insp-${k}`;
    p.setAttribute('role', 'tabpanel');
    p.setAttribute('aria-labelledby', `pb-insp-tab-${k}`);
    panels[k] = p;
  }
  // Devices: live channel rows (real values mirrored from the transport).
  const live = document.createElement('div');
  live.className = 'pb-chan-rows';
  // Stroke drives the feel recipe (not sent to the Neo 2 directly): live only.
  // Vibration / Suction: on/off + output level (device-side, files unchanged).
  live.innerHTML = [['stroke', 'Stroke', false], ['vibration', 'Vibration', true], ['suction', 'Suction', true]]
    .map(([k, label, ctl]) => `<div class="pb-chan-row" data-axis="${k}">
        ${ctl ? `<input type="checkbox" class="pb-chan-on" id="pb-chan-${k}-on" checked aria-label="${label} on" />` : '<span class="pb-chan-on-spacer"></span>'}
        <i class="pb-chan-dot"></i><span class="pb-chan-name">${label}</span>
        ${ctl ? `<input type="range" class="pb-chan-level" id="pb-chan-${k}-level" min="0" max="100" step="5" value="100" aria-label="${label} level" />`
              : '<span class="pb-chan-level-spacer"></span>'}
        <span class="pb-chan-val" id="pb-chan-${k}-lvl">${ctl ? '100%' : ''}</span>
        <span class="pb-chan-bar" title="Live output"><b id="pb-chan-${k}-fill"></b></span>
        <span class="pb-chan-live" id="pb-chan-${k}-val">–</span></div>`).join('');
  panels.devices.appendChild(live);
  // D3: a stroker (The Handy …) gets the script's positions. Its row gets an
  // on/off and a safety range; shown only while such a device is connected.
  const strokeCtl = document.createElement('div');
  strokeCtl.className = 'pb-stroke-ctl';
  strokeCtl.id = 'pb-stroke-ctl';
  strokeCtl.hidden = true;
  strokeCtl.innerHTML = `
    <label class="pb-stroke-on"><input type="checkbox" id="pb-stroke-on" checked /> Send stroke to the device</label>
    <div class="pb-stroke-range">
      <label>Bottom <input type="range" id="pb-stroke-lo" min="0" max="90" step="5" value="0" aria-label="Stroke range bottom" /></label>
      <label>Top <input type="range" id="pb-stroke-hi" min="10" max="100" step="5" value="100" aria-label="Stroke range top" /></label>
      <span class="pb-stroke-range-val" id="pb-stroke-range-val">0–100 %</span>
    </div>
    <p class="hint">Safety range: the device only moves inside it; the script's full stroke is scaled into it.</p>`;
  live.querySelector('.pb-chan-row[data-axis="stroke"]')?.after(strokeCtl);
  // Multi-motor devices (LELO F1S V3 …): one level per motor, shown only
  // while such a device is connected (rows built from its capabilities).
  const motorCtl = document.createElement('div');
  motorCtl.className = 'pb-motor-ctl';
  motorCtl.id = 'pb-motor-ctl';
  motorCtl.hidden = true;
  live.querySelector('.pb-chan-row[data-axis="vibration"]')?.after(motorCtl);

  // Effects knobs (mockup; Owner 11 Oct): Intensity = overall output of the
  // feel channels (master over the channel levels), Response = how direct the
  // device follows the script (inverse of Play smoothing). No Frequency knob:
  // the Neo 2 vibration has intensity only.
  const knob = (k, label, help) => `
    <label class="pb-knob" data-knob="${k}" title="${help}">
      <span class="pb-knob-label">${label}</span>
      <span class="pb-knob-dial" aria-hidden="true"><svg viewBox="0 0 48 48">
        <circle class="pb-knob-track" cx="24" cy="24" r="19" pathLength="100" />
        <circle class="pb-knob-arc" id="pb-knob-${k}-arc" cx="24" cy="24" r="19" pathLength="100" />
        <line class="pb-knob-tick" id="pb-knob-${k}-tick" x1="24" y1="24" x2="36" y2="24" /></svg></span>
      <input type="range" class="pb-knob-input" id="pb-knob-${k}" min="0" max="100" step="5" value="100" aria-label="${label}" />
      <span class="pb-knob-val" id="pb-knob-${k}-val">100%</span>
    </label>`;
  const fx = document.createElement('div');
  fx.className = 'pb-effects';
  fx.dataset.pbRegion = 'effects';
  fx.innerHTML = `<p class="pb-effects-title">Effects</p><div class="pb-knobs">
    ${knob('intensity', 'Intensity', 'Overall output of vibration and suction (on top of the channel levels). Live.')}
    ${knob('response', 'Response', 'How directly the device follows the script: high = crisp, low = smooth. Uses Play smoothing; applies from the next Play.')}
  </div>`;
  // "Sync with audio" (mockup) = the existing "Vibration from video audio"
  // switch; both stay in step.
  const avSrc = q('#pb-audio-vibration');
  if (avSrc) {
    const row = document.createElement('label');
    row.className = 'checkbox-row pb-effects-check';
    row.innerHTML = '<input type="checkbox" id="pb-fx-audio-sync" /> Sync with audio';
    row.title = 'Vibration follows the loudness of the video\'s sound (same switch as Audio → Vibration from video audio).';
    fx.appendChild(row);
    const mirror = row.querySelector('#pb-fx-audio-sync');
    const pull = () => { mirror.checked = avSrc.checked; mirror.disabled = avSrc.disabled; };
    mirror.addEventListener('change', () => {
      if (avSrc.checked === mirror.checked) return;
      avSrc.checked = mirror.checked;
      avSrc.dispatchEvent(new Event('change', { bubbles: true }));
    });
    avSrc.addEventListener('change', pull);
    new MutationObserver(pull).observe(avSrc, { attributes: true, attributeFilter: ['disabled', 'checked'] });
    pull();
  }
  // Reduce peaks: soft limiter in the player (above 60 % softened, full
  // strength → 76 %). Live; files unchanged.
  const peaks = document.createElement('label');
  peaks.className = 'checkbox-row pb-effects-check';
  peaks.title = 'Softens strong bursts: vibration and suction above 60 % are compressed (100 % comes out at 76 %). Live, files unchanged.';
  peaks.innerHTML = '<input type="checkbox" id="pb-fx-reduce-peaks" /> Reduce peaks';
  fx.appendChild(peaks);
  panels.devices.appendChild(fx);
  // Knob drawing: the SVG is turned 135°, so 0 % sits bottom-left and the
  // arc / pointer sweep 270° clockwise.
  function drawKnob(k, v) {
    const arc = q(`#pb-knob-${k}-arc`), tick = q(`#pb-knob-${k}-tick`);
    if (arc) arc.style.strokeDasharray = `${v * 0.75} 100`;
    if (tick) tick.setAttribute('transform', `rotate(${v * 2.7} 24 24)`);
    const val = q(`#pb-knob-${k}-val`);
    if (val) val.textContent = `${Math.round(v)}%`;
  }

  const move = (sel, panel) => {
    const n = typeof sel === 'string' ? q(sel) : sel;
    if (n) panels[panel].appendChild(n);
    return n;
  };
  // Devices: script → device mapping.
  ['#pb-axis-row', '#pb-optimize-neo2-status', '#pb-export-bake-details', '#pb-contact-block'].forEach(s => move(s, 'devices'));
  // Audio: audio vibration + BPM card.
  const bpmCard = document.createElement('div');
  bpmCard.className = 'pb-card';
  bpmCard.id = 'pb-bpm-card';
  bpmCard.innerHTML = '<p class="pb-card-title">BPM</p><div class="pb-card-body"></div>';
  const bpmBody = bpmCard.querySelector('.pb-card-body');
  for (const n of [q('#pb-bpm')?.closest('label'), q('#pb-bpm-auto'), q('#pb-bpm-auto-status'), q('#pb-bpm-grid')?.closest('label')]) {
    if (n) bpmBody.appendChild(n);
  }
  const avCard = document.createElement('div');
  avCard.className = 'pb-card';
  avCard.id = 'pb-audio-vib-card';
  avCard.innerHTML = '<p class="pb-card-title">Audio Vibration</p><div class="pb-card-body"></div>';
  for (const s of ['#pb-audio-vibration-row', '#pb-audio-vibration-hint']) {
    const n = q(s);
    if (n) avCard.querySelector('.pb-card-body').appendChild(n);
  }
  panels.audio.append(bpmCard, avCard);
  // Playback: script tools (offset), playback settings, checks, log.
  const st = document.createElement('div');
  st.className = 'pb-card';
  st.id = 'pb-script-tools-card';
  st.innerHTML = '<p class="pb-card-title">Script Tools</p><div class="pb-card-body"></div>';
  for (const s of ['#pb-offset-row', '#pb-offset-hint']) {
    const n = q(s);
    if (n) st.querySelector('.pb-card-body').appendChild(n);
  }
  panels.playback.appendChild(st);
  const adv = [...tools.querySelectorAll(':scope > details.pb-advanced')][0];
  if (adv) panels.playback.appendChild(adv);
  ['#pb-quality-details', '#pb-ofs-row', '#pb-marker-auto-row', '#pb-log'].forEach(s => move(s, 'playback'));
  // View: curve display / editing.
  ['#pb-curve-edit-row', '#pb-curve-edit-hint', '#pb-audio-lane-row', '#pb-scenes-row', '#pb-pos-overlay-row', '#pb-ofs-edit-details',
    '#pb-marker-hint', '#pb-omarker-details'].forEach(s => move(s, 'view'));
  // Anything left (future additions) lands in Playback so nothing is lost.
  [...tools.children].forEach(n => {
    if (n !== head && !n.classList.contains('pb-insp-panel')) panels.playback.appendChild(n);
  });
  tools.append(bar, ...Object.values(panels));

  function showInspector(k) {
    if (!panels[k]) k = 'devices';
    for (const [key, p] of Object.entries(panels)) p.classList.toggle('is-active', key === k);
    bar.querySelectorAll('[data-insp]').forEach(b => {
      const on = b.dataset.insp === k;
      b.classList.toggle('is-active', on);
      b.setAttribute('aria-selected', on ? 'true' : 'false');
      b.tabIndex = on ? 0 : -1;
    });
    root.dataset.pbInspector = k;
    store(INSPECTOR_KEY, k);
  }
  bar.addEventListener('click', e => {
    const b = e.target.closest('[data-insp]');
    if (b) showInspector(b.dataset.insp);
  });
  bar.addEventListener('keydown', e => {
    const keys = tabs.map(t => t[0]);
    const i = keys.indexOf(root.dataset.pbInspector);
    let j = i;
    if (e.key === 'ArrowRight') j = (i + 1) % keys.length;
    else if (e.key === 'ArrowLeft') j = (i - 1 + keys.length) % keys.length;
    else return;
    e.preventDefault();
    showInspector(keys[j]);
    bar.querySelector(`[data-insp="${keys[j]}"]`)?.focus();
  });
  showInspector(load(INSPECTOR_KEY) || 'devices');

  // ---- Video bar (mockup: "Select a video file…" + bookmark · snapshot · fullscreen)
  const stage = q('#pb-video-stage');
  const vbar = document.createElement('div');
  vbar.className = 'pb-videobar';
  vbar.dataset.pbRegion = 'videobar';
  vbar.innerHTML = `
    <button type="button" class="pb-vb-select" id="pb-vb-video" title="Link or change the video for this script">
      ${ICONS.folder}<span id="pb-vb-name">Select a video file…</span></button>
    <button type="button" class="pb-vb-icon pb-vb-more" id="pb-vb-recent" title="Recent videos"
      aria-label="Recent videos" aria-haspopup="menu" aria-expanded="false">${ICONS.chevron}</button>
    <ul class="pb-vb-menu" id="pb-vb-menu" role="menu" hidden></ul>
    <span class="pb-vb-spacer"></span>
    <button type="button" class="pb-vb-icon" id="pb-vb-bookmark" title="Add a bookmark at the current time" aria-label="Add bookmark">${ICONS.bookmark}</button>
    <button type="button" class="pb-vb-icon" id="pb-vb-snapshot" title="Save the current video frame as PNG" aria-label="Save frame">${ICONS.image}</button>
    <button type="button" class="pb-vb-icon" id="pb-vb-fs" title="Fullscreen" aria-label="Fullscreen">${ICONS.expand}</button>
    <span class="hint pb-vb-status" id="pb-vb-status"></span>`;
  stage?.before(vbar);
  const vbName = q('#pb-vb-name');
  const syncVideoName = () => {
    const p = root.dataset.pbVideoPath || '';
    vbName.textContent = p ? baseName(p) : 'Select a video file…';
    q('#pb-vb-snapshot').disabled = !p;
  };
  new MutationObserver(() => {
    syncVideoName();
    pushRecentVideo(root.dataset.pbVideoPath || '');
  }).observe(root, { attributes: true, attributeFilter: ['data-pb-video-path'] });
  // A clip chosen in Create is one click away in Play (recent videos).
  window.addEventListener('gen:video', e => pushRecentVideo(e.detail && e.detail.path));
  syncVideoName();
  const vbMenu = q('#pb-vb-menu');
  const vbMore = q('#pb-vb-recent');
  const closeMenu = () => { vbMenu.hidden = true; vbMore.setAttribute('aria-expanded', 'false'); };
  vbMore.addEventListener('click', e => {
    e.stopPropagation();
    if (!vbMenu.hidden) { closeMenu(); return; }
    const cur = root.dataset.pbVideoPath || '';
    const list = readRecentVideos().filter(p => p !== cur);
    const loaded = root.dataset.pbState === 'loaded';
    vbMenu.innerHTML = list.length
      ? list.map((p, i) => `<li role="none"><button type="button" role="menuitem" class="pb-vb-menu-item" data-i="${i}"
          title="${escAttr(p)}" ${loaded ? '' : 'disabled'}>${escText(baseName(p))}</button></li>`).join('')
      : '<li class="pb-lib-empty" role="none">No recent videos</li>';
    vbMenu._list = list;
    vbMenu.hidden = false;
    vbMore.setAttribute('aria-expanded', 'true');
    vbMenu.querySelector('button:not([disabled])')?.focus();
  });
  vbMenu.addEventListener('click', async e => {
    const b = e.target.closest('.pb-vb-menu-item');
    if (!b) return;
    const p = (vbMenu._list || [])[Number(b.dataset.i)];
    closeMenu();
    if (p && api.attachVideoPath) await api.attachVideoPath(p);
  });
  vbMenu.addEventListener('keydown', e => { if (e.key === 'Escape') { closeMenu(); vbMore.focus(); } });
  document.addEventListener('click', e => { if (!vbar.contains(e.target)) closeMenu(); });
  q('#pb-vb-video').addEventListener('click', () => {
    const change = q('#pb-video-change');
    const vid = root.dataset.pbVideo === 'true';
    (vid && change ? change : q('#pb-pick-video'))?.click();
  });
  q('#pb-vb-fs').addEventListener('click', () => q('#pb-video-fs')?.click());
  q('#pb-vb-bookmark').addEventListener('click', () => {
    showBottom('bookmarks');
    const name = q('#pb-bookmark-name');
    const add = q('#pb-bookmark-add');
    if (add && !add.disabled) add.click();
    else name?.focus();
  });
  q('#pb-vb-snapshot').addEventListener('click', async () => {
    const vid = q('#pb-video');
    const ms = Math.round((vid?.currentTime || 0) * 1000);
    const status = q('#pb-vb-status');
    try {
      const out = await SaveVideoSnapshot(ms);
      if (out) status.textContent = 'Saved ' + baseName(out);
    } catch (err) {
      status.textContent = 'Snapshot failed: ' + err;
    }
  });

  // ---- Bottom tabs: Timeline · Chapters · Bookmarks · Scene Map · Audio Analysis
  const timeline = q('.pb-timeline');
  const bottom = document.createElement('div');
  bottom.className = 'pb-bottom';
  bottom.dataset.pbRegion = 'bottom';
  const btabs = [['timeline', 'Timeline'], ['chapters', 'Chapters'], ['bookmarks', 'Bookmarks'],
    ['scenemap', 'Scene Map'], ['audio', 'Audio Analysis']];
  const bbar = document.createElement('div');
  bbar.className = 'pb-bottom-tabs';
  bbar.dataset.pbRegion = 'bottom-tabs';
  bbar.setAttribute('role', 'tablist');
  bbar.innerHTML = btabs.map(([k, label]) =>
    `<button type="button" role="tab" class="pb-bottom-tab" data-btab="${k}" id="pb-btab-${k}">${label}</button>`).join('');
  timeline.before(bottom);
  bottom.appendChild(bbar);
  const bpanels = {};
  const mk = k => {
    const p = document.createElement('div');
    p.className = 'pb-bottom-panel';
    p.dataset.btabPanel = k;
    p.setAttribute('role', 'tabpanel');
    bottom.appendChild(p);
    bpanels[k] = p;
    return p;
  };
  mk('timeline').appendChild(timeline);
  const openDetails = (sel, k) => {
    const d = q(sel);
    if (!d) return;
    d.open = true;
    d.classList.add('pb-in-bottom');
    // As a bottom tab the section has no summary to reopen it, and Play
    // closes these panels when the selection clears — keep it open.
    d.addEventListener('toggle', () => { if (!d.open) d.open = true; });
    mk(k).appendChild(d);
  };
  openDetails('#pb-chapter-details', 'chapters');
  openDetails('#pb-bookmark-details', 'bookmarks');
  // Scene Map: the chapters as scenes (scene_panel.js); the Create rhythm
  // heatmap stays one click away from its header.
  const scenePanel = mountScenePanel(mk('scenemap'), root, api);
  const audioPanel = mk('audio');
  const card = q('#pb-audio-card');
  if (card) audioPanel.appendChild(card);
  const segs = q('#pb-audio-segments');
  if (segs) audioPanel.appendChild(segs);
  function showBottom(k) {
    if (!bpanels[k]) k = 'timeline';
    for (const [key, p] of Object.entries(bpanels)) p.classList.toggle('is-active', key === k);
    bbar.querySelectorAll('[data-btab]').forEach(b => {
      const on = b.dataset.btab === k;
      b.classList.toggle('is-active', on);
      b.setAttribute('aria-selected', on ? 'true' : 'false');
    });
    root.dataset.pbBottom = k;
    store(BOTTOM_KEY, k);
    if (k === 'scenemap' && scenePanel) scenePanel.show();
    // Canvases inside a re-shown panel redraw via their ResizeObserver.
  }
  bbar.addEventListener('click', e => {
    const b = e.target.closest('[data-btab]');
    if (b) showBottom(b.dataset.btab);
  });
  showBottom(load(BOTTOM_KEY) || 'timeline');

  // ---- Lane names (mockup): coloured dot + name left of every lane ---------
  const LANE_NAMES = { general: ['Stroke', 'stroke'], vibration: ['Vibration', 'vibration'],
    suction: ['Suction', 'suction'], audio: ['Audio Level', 'audio'],
    surge: ['Surge', 'surge'], sway: ['Sway', 'sway'], twist: ['Twist', 'twist'], roll: ['Roll', 'roll'], pitch: ['Pitch', 'pitch'] };
  const names = document.createElement('div');
  names.className = 'pb-lane-names';
  names.dataset.pbRegion = 'lane-names';
  names.setAttribute('aria-hidden', 'true'); // canvases carry their own labels
  timeline.prepend(names);
  function placeLaneNames() {
    const base = timeline.getBoundingClientRect();
    const rows = [];
    const add = (el, label, key, top, h) => rows.push({ label, key, top: top - base.top, h });
    const curve = q('#pb-curve');
    if (curve && curve.style.display !== 'none' && curve.offsetHeight) {
      const r = curve.getBoundingClientRect();
      add(curve, 'Stroke', 'stroke', r.top, r.height);
    }
    const lanesEl = q('#pb-axis-lanes');
    const list = (root.dataset.pbLanes || '').split(',').filter(Boolean);
    if (lanesEl && !lanesEl.hidden && list.length) {
      const r = lanesEl.getBoundingClientRect();
      const h = r.height / list.length;
      list.forEach((k, i) => { const [label, key] = LANE_NAMES[k] || [k, k]; add(lanesEl, label, key, r.top + i * h, h); });
    }
    const bands = q('#pb-chapter-bands');
    if (bands && !bands.hidden) {
      const r = bands.getBoundingClientRect();
      add(bands, 'Chapters', 'chapters', r.top, r.height);
    }
    const strip = q('#pb-scene-strip');
    if (strip && !strip.hidden) {
      const r = strip.getBoundingClientRect();
      add(strip, 'Scene Markers', 'scenes', r.top, r.height);
    }
    names.innerHTML = rows.map(r => `<span class="pb-lane-name" data-lane="${r.key}" style="top:${Math.round(r.top)}px;height:${Math.round(r.h)}px"><i></i>${r.label}</span>`).join('');
  }
  let lanePending = false;
  const scheduleNames = () => {
    if (lanePending) return;
    lanePending = true;
    requestAnimationFrame(() => { lanePending = false; placeLaneNames(); });
  };
  if (typeof ResizeObserver !== 'undefined') new ResizeObserver(scheduleNames).observe(timeline);
  new MutationObserver(scheduleNames).observe(root, { attributes: true,
    attributeFilter: ['data-pb-lanes', 'data-pb-scenes', 'data-pb-chapters', 'data-pb-state', 'data-pb-bottom'] });
  window.addEventListener('resize', scheduleNames);
  scheduleNames();

  // ---- Library actions ----------------------------------------------------
  q('#pb-lib-local').addEventListener('click', () => q('#pb-choose')?.click());
  q('#pb-lib-scenemap').addEventListener('click', () => showBottom('scenemap'));
  // Editor: curve edit mode of the loaded script (existing #pb-curve-edit).
  q('#pb-lib-editor').addEventListener('click', () => {
    const box = q('#pb-curve-edit');
    if (root.dataset.pbState !== 'loaded' || !box || box.disabled) {
      q('#pb-choose')?.focus();
      return;
    }
    showInspector('view');
    showBottom('timeline');
    if (!box.checked) {
      box.checked = true;
      box.dispatchEvent(new Event('change'));
    }
    q('#pb-curve')?.scrollIntoView({ block: 'nearest' });
  });
  q('#pb-lib-audio').addEventListener('click', () => showBottom('audio'));

  // ---- Workspace Edit view (main.js sets data-ws-view): curve editing on,
  // Inspector on View, Timeline in front. Leaving Edit turns editing off
  // again only if Edit turned it on.
  let editTurnedOn = false;
  const setCurveEdit = on => {
    const box = q('#pb-curve-edit');
    if (!box || box.disabled || box.checked === on) return false;
    box.checked = on;
    box.dispatchEvent(new Event('change'));
    return true;
  };
  function applyWorkspaceView(view) {
    if (view === 'edit') {
      showInspector('view');
      showBottom('timeline');
      if (root.dataset.pbState === 'loaded' && setCurveEdit(true)) editTurnedOn = true;
    } else if (view === 'play' && editTurnedOn) {
      setCurveEdit(false);
      editTurnedOn = false;
    }
  }
  window.addEventListener('ws:view', e => applyWorkspaceView(e.detail && e.detail.view));
  // A load resets editing and shows the edit row only once the curve is
  // drawn — turn editing on at that moment when Edit is the view.
  const editRow = q('#pb-curve-edit-row');
  if (editRow) {
    new MutationObserver(() => {
      if (root.dataset.wsView === 'edit' && editRow.style.display !== 'none' && setCurveEdit(true)) editTurnedOn = true;
    }).observe(editRow, { attributes: true, attributeFilter: ['style'] });
  }
  q('#pb-lib-bpm').addEventListener('click', () => {
    showInspector('audio');
    q('#pb-bpm-auto')?.focus();
  });
  q('#pb-lib-playlist').addEventListener('click', () => {
    const pl = q('#pb-playlist');
    if (pl && !pl.hidden) pl.scrollIntoView({ block: 'nearest' });
    else q('#pb-queue-add')?.focus();
  });
  const recentList = q('#pb-lib-recent');
  function renderRecent() {
    const list = readRecent();
    recentList.innerHTML = list.length
      ? list.map((p, i) => `<li><button type="button" class="pb-lib-recent-item" data-i="${i}" title="${escAttr(p)}">${escText(baseName(p))}</button></li>`).join('')
      : '<li class="pb-lib-empty">Nothing yet</li>';
  }
  recentList.addEventListener('click', async e => {
    const b = e.target.closest('.pb-lib-recent-item');
    if (!b) return;
    const p = readRecent()[Number(b.dataset.i)];
    if (p) await api.loadScriptPath(p).catch(() => {});
  });
  q('#pb-lib-recent-btn').addEventListener('click', () => {
    const open = recentList.hidden;
    recentList.hidden = !open;
    q('#pb-lib-recent-btn').setAttribute('aria-expanded', open ? 'true' : 'false');
  });
  // Favorites: star next to the script name, list in the Library.
  const favList = q('#pb-lib-fav');
  const headActions = q('.pb-head-actions') || q('.pb-head');
  const star = document.createElement('button');
  star.type = 'button';
  star.id = 'pb-fav-toggle';
  star.className = 'pb-fav-toggle';
  star.innerHTML = ICONS.star;
  headActions?.prepend(star);
  let currentScript = '';
  function syncStar() {
    const on = !!currentScript && readFavorites().includes(currentScript);
    star.disabled = !currentScript;
    star.setAttribute('aria-pressed', on ? 'true' : 'false');
    star.title = on ? 'Remove from Favorites' : 'Add to Favorites';
    star.setAttribute('aria-label', star.title);
  }
  function renderFavorites() {
    const list = readFavorites();
    favList.innerHTML = list.length
      ? list.map((p, i) => `<li><button type="button" class="pb-lib-recent-item" data-i="${i}" title="${escAttr(p)}">${escText(baseName(p))}</button></li>`).join('')
      : '<li class="pb-lib-empty">Star a script to keep it here</li>';
    syncStar();
  }
  star.addEventListener('click', () => {
    toggleFavorite(currentScript);
    renderFavorites();
  });
  favList.addEventListener('click', async e => {
    const b = e.target.closest('.pb-lib-recent-item');
    if (!b) return;
    const p = readFavorites()[Number(b.dataset.i)];
    if (p) await api.loadScriptPath(p).catch(() => {});
  });
  q('#pb-lib-fav-btn').addEventListener('click', () => {
    const open = favList.hidden;
    favList.hidden = !open;
    q('#pb-lib-fav-btn').setAttribute('aria-expanded', open ? 'true' : 'false');
  });
  window.addEventListener('pb:loaded', e => {
    currentScript = (e.detail && e.detail.path) || '';
    pushRecent(currentScript);
    renderRecent();
    syncStar();
  });
  renderRecent();
  renderFavorites();

  // Scripts: Imported / Generated / Edited — folded lists with a count.
  const CATS = ['imported', 'generated', 'edited'];
  const EMPTY = { imported: 'Scripts you open show here', generated: 'Generator results show here', edited: 'Scripts you edit and save show here' };
  function renderScripts() {
    for (const k of CATS) {
      const list = readScripts(k);
      q(`#pb-lib-${k}`).innerHTML = list.length
        ? list.map((p, i) => `<li><button type="button" class="pb-lib-recent-item" data-cat="${k}" data-i="${i}" title="${escAttr(p)}">${escText(baseName(p))}</button></li>`).join('')
        : `<li class="pb-lib-empty">${EMPTY[k]}</li>`;
      q(`#pb-lib-${k}-count`).textContent = list.length ? String(list.length) : '';
    }
  }
  for (const k of CATS) {
    const listEl = q(`#pb-lib-${k}`);
    const btn = q(`#pb-lib-${k}-btn`);
    btn.addEventListener('click', () => {
      listEl.hidden = !listEl.hidden;
      btn.setAttribute('aria-expanded', listEl.hidden ? 'false' : 'true');
    });
    listEl.addEventListener('click', async e => {
      const b = e.target.closest('.pb-lib-recent-item');
      if (!b) return;
      const p = readScripts(b.dataset.cat)[Number(b.dataset.i)];
      if (p) await api.loadScriptPath(p).catch(() => {});
    });
  }
  window.addEventListener('pb:loaded', e => {
    const d = e.detail || {};
    // A Generator result re-opened from the Library stays "Generated".
    if (d.origin === 'generated') pushScript(d.path, 'generated');
    else if (!readScripts('generated').includes(d.path)) pushScript(d.path, 'imported');
    renderScripts();
  });
  window.addEventListener('pb:edited', e => {
    pushScript(e.detail && e.detail.path, 'edited');
    renderScripts();
  });
  renderScripts();
  const plCount = q('#pb-lib-playlist-count');
  const syncCount = () => {
    const n = q('#pb-playlist-list')?.children.length || 0;
    plCount.textContent = n > 1 ? String(n) : '';
  };
  const plList = q('#pb-playlist-list');
  if (plList) new MutationObserver(syncCount).observe(plList, { childList: true });
  syncCount();

  // ---- Live channel rows: mirror real values ------------------------------
  const mirror = (srcSel, k, scale) => {
    const src = q(srcSel);
    if (!src) return;
    const fill = q(`#pb-chan-${k}-fill`);
    const val = q(`#pb-chan-${k}-val`);
    const sync = () => {
      const n = parseFloat(String(src.textContent).replace(',', '.'));
      const pct = Number.isFinite(n) ? Math.max(0, Math.min(100, n * scale)) : null;
      fill.style.width = pct == null ? '0%' : pct + '%';
      val.textContent = pct == null ? '–' : Math.round(pct) + '%';
    };
    new MutationObserver(sync).observe(src, { childList: true, characterData: true, subtree: true });
    sync();
  };
  // All three show 0–100 already (#pb-vib / #pb-suc as "58%").
  mirror('#pb-pos-value', 'stroke', 1);
  mirror('#pb-vib', 'vibration', 1);
  mirror('#pb-suc', 'suction', 1);

  // ---- Channel on/off + level → device output (SetChannelLevels) ----------
  let saved = {};
  try { saved = JSON.parse(load(CHAN_KEY) || '{}') || {}; } catch (_) { saved = {}; }
  const chans = ['vibration', 'suction'];
  for (const k of chans) {
    const st = saved[k] || {};
    const on = q(`#pb-chan-${k}-on`), lvl = q(`#pb-chan-${k}-level`);
    if (on && typeof st.on === 'boolean') on.checked = st.on;
    if (lvl && Number.isFinite(st.level)) lvl.value = String(Math.max(0, Math.min(100, st.level)));
  }
  function applyChannels() {
    const out = {};
    const level = k => {
      const on = q(`#pb-chan-${k}-on`), lvl = q(`#pb-chan-${k}-level`);
      const v = Number(lvl?.value ?? 100);
      out[k] = { on: !!on?.checked, level: v };
      q(`#pb-chan-${k}-lvl`).textContent = on?.checked ? `${v}%` : 'Off';
      lvl.disabled = !on?.checked;
      q(`.pb-chan-row[data-axis="${k}"]`)?.classList.toggle('is-off', !on?.checked);
      return on?.checked ? v / 100 : 0;
    };
    const master = Math.max(0, Math.min(100, Number(q('#pb-knob-intensity')?.value ?? 100)));
    out.master = master;
    const reduce = !!q('#pb-fx-reduce-peaks')?.checked;
    out.reducePeaks = reduce;
    root.dataset.pbReducePeaks = reduce ? 'on' : 'off';
    SetPeakReduce(reduce).catch(() => {});
    drawKnob('intensity', master);
    const vib = level('vibration') * master / 100, suc = level('suction') * master / 100;
    try { // keep the stroke block (written by applyStroke)
      const prev = JSON.parse(load(CHAN_KEY) || '{}');
      if (prev && prev.stroke) out.stroke = prev.stroke;
      if (prev && prev.motors) out.motors = prev.motors;
    } catch (_) { /* ignore */ }
    store(CHAN_KEY, JSON.stringify(out));
    root.dataset.pbChannels = `${vib},${suc}`;
    SetChannelLevels(vib, suc).catch(() => {});
  }
  for (const k of chans) {
    q(`#pb-chan-${k}-on`)?.addEventListener('change', applyChannels);
    q(`#pb-chan-${k}-level`)?.addEventListener('input', applyChannels);
  }
  const intensity = q('#pb-knob-intensity');
  if (intensity && Number.isFinite(saved.master)) intensity.value = String(Math.max(0, Math.min(100, saved.master)));
  intensity?.addEventListener('input', applyChannels);
  const peakBox = q('#pb-fx-reduce-peaks');
  if (peakBox) {
    peakBox.checked = saved.reducePeaks === true;
    peakBox.addEventListener('change', applyChannels);
  }
  applyChannels();

  // ---- Stroke output (D3) + rows that follow the connected device ---------
  const strokeOn = q('#pb-stroke-on'), strokeLo = q('#pb-stroke-lo'), strokeHi = q('#pb-stroke-hi');
  const st0 = saved.stroke || {};
  if (typeof st0.on === 'boolean') strokeOn.checked = st0.on;
  if (Number.isFinite(st0.lo)) strokeLo.value = String(st0.lo);
  if (Number.isFinite(st0.hi)) strokeHi.value = String(st0.hi);
  async function applyStroke(e) {
    let lo = Number(strokeLo.value), hi = Number(strokeHi.value);
    // Keep at least 10 % apart: the slider being moved pushes the other one.
    if (hi - lo < 10) {
      if (e && e.target === strokeLo) hi = Math.min(100, lo + 10);
      else lo = Math.max(0, hi - 10);
      strokeLo.value = String(lo); strokeHi.value = String(hi);
    }
    q('#pb-stroke-range-val').textContent = `${lo}–${hi} %`;
    strokeLo.disabled = strokeHi.disabled = !strokeOn.checked;
    let cur = {};
    try { cur = JSON.parse(load(CHAN_KEY) || '{}') || {}; } catch (_) { cur = {}; }
    cur.stroke = { on: strokeOn.checked, lo, hi };
    store(CHAN_KEY, JSON.stringify(cur));
    root.dataset.pbStroke = strokeOn.checked ? `${lo},${hi}` : 'off';
    try { await SetStrokeOutput(strokeOn.checked, lo / 100, hi / 100); } catch (_) { /* binding missing in old builds */ }
  }
  strokeOn.addEventListener('change', applyStroke);
  strokeLo.addEventListener('input', applyStroke);
  strokeHi.addEventListener('input', applyStroke);
  applyStroke();

  // ---- Per-motor levels (multi-motor devices) ------------------------------
  const motorLabel = ch => {
    const m = /^([a-z]+?)(\d*)$/.exec(ch) || [ch, ch, ''];
    const base = m[1][0].toUpperCase() + m[1].slice(1);
    return `${base} motor ${m[2] || '1'}`;
  };
  function savedMotors() {
    try { return (JSON.parse(load(CHAN_KEY) || '{}') || {}).motors || {}; } catch (_) { return {}; }
  }
  function applyMotors() {
    const levels = {};
    motorCtl.querySelectorAll('input[data-motor]').forEach(inp => {
      const v = Number(inp.value);
      levels[inp.dataset.motor] = v / 100;
      const out = motorCtl.querySelector(`[data-motor-val="${inp.dataset.motor}"]`);
      if (out) out.textContent = `${v}%`;
    });
    let cur = {};
    try { cur = JSON.parse(load(CHAN_KEY) || '{}') || {}; } catch (_) { cur = {}; }
    cur.motors = Object.fromEntries(Object.entries(levels).map(([k, v]) => [k, Math.round(v * 100)]));
    store(CHAN_KEY, JSON.stringify(cur));
    SetMotorLevels(levels).catch(() => {});
  }
  function renderMotors(feats) {
    const motors = (feats || []).filter(f => f.driven && f.kind === 'scalar' && /^vibration\d*$/.test(String(f.channel)));
    motorCtl.replaceChildren();
    motorCtl.hidden = motors.length < 2;
    root.dataset.pbMotors = String(motors.length);
    if (motors.length < 2) return;
    const saved = savedMotors();
    const title = document.createElement('p');
    title.className = 'pb-motor-title';
    title.textContent = 'Motors';
    motorCtl.append(title);
    for (const f of motors) {
      const ch = String(f.channel);
      const lvl = Number.isFinite(saved[ch]) ? Math.max(0, Math.min(100, saved[ch])) : 100;
      const row = document.createElement('label');
      row.className = 'pb-motor-row';
      const name = document.createElement('span');
      name.textContent = motorLabel(ch);
      const inp = document.createElement('input');
      inp.type = 'range'; inp.min = '0'; inp.max = '100'; inp.step = '5'; inp.value = String(lvl);
      inp.dataset.motor = ch;
      inp.setAttribute('aria-label', `${motorLabel(ch)} level`);
      inp.addEventListener('input', applyMotors);
      const val = document.createElement('span');
      val.className = 'pb-motor-val';
      val.dataset.motorVal = ch;
      val.textContent = `${lvl}%`;
      row.append(name, inp, val);
      motorCtl.append(row);
    }
    applyMotors();
  }

  // Connected device → show what it can do: the stroke controls for a
  // stroker, no rows for channels it lacks. Without a device: all rows.
  window.addEventListener('device:status', e => {
    const st = e.detail || {};
    const feats = (st.connected && st.capabilities && Array.isArray(st.capabilities.features)) ? st.capabilities.features : null;
    const has = ch => !feats || feats.some(f => f.driven && String(f.channel).replace(/\d+$/, '') === ch);
    const stroker = !!feats && feats.some(f => f.driven && f.kind === 'linear');
    strokeCtl.hidden = !stroker;
    root.dataset.pbStroker = stroker ? 'on' : 'off';
    renderMotors(feats);
    for (const k of chans) {
      const row = q(`.pb-chan-row[data-axis="${k}"]`);
      if (row) row.hidden = !has(k);
    }
  });

  // Response ↔ Play smoothing (0–1, saved as playback.smoothing by playback.js).
  const smooth = q('#pb-smoothing');
  const response = q('#pb-knob-response');
  const syncResponse = () => {
    const sm = parseFloat(smooth?.value);
    const v = Number.isFinite(sm) ? Math.round((1 - sm) * 100) : 70;
    response.value = String(v);
    drawKnob('response', v);
  };
  if (smooth && response) {
    response.addEventListener('input', () => {
      const v = Number(response.value);
      smooth.value = String(Math.round((1 - v / 100) * 20) / 20);
      smooth.dispatchEvent(new Event('input'));
      drawKnob('response', v);
    });
    response.addEventListener('change', () => smooth.dispatchEvent(new Event('change')));
    smooth.addEventListener('input', syncResponse); // also fired after settings load
    syncResponse();
  }

  const fxApi = wireEffectsTabsAndPresets(fx, q);

  return { showInspector, showBottom, renderRecent, ...fxApi };
}

// ---- Effects sub-tabs and presets (V1 target, feature map) -----------------
// Effects · Curve · Limits · Advanced regroup controls that already exist:
// Curve = smoothing + sync mode, Limits = Max-Speed + Reduce peaks, Advanced =
// tick + soft-start. The rows are moved (ids, listeners and saved settings
// stay). A preset stores these values plus Intensity under a name in the
// settings file (playback.effect_presets); applying one fires the controls'
// own input/change events, so the normal save and device paths run.
const FX_TABS = [['effects', 'Effects'], ['curve', 'Curve'], ['limits', 'Limits'], ['advanced', 'Advanced']];
export const FX_DEFAULTS = Object.freeze({
  intensity: 100, smoothing: 0.3, maxSpeed: 0.6, tickMs: 50, softStartMs: 500, reducePeaks: false,
});
const FX_FIELDS = {
  intensity: ['#pb-knob-intensity', 'range'],
  smoothing: ['#pb-smoothing', 'range'],
  maxSpeed: ['#pb-maxspeed', 'range'],
  tickMs: ['#pb-tick', 'range'],
  softStartMs: ['#pb-softstart', 'range'],
  reducePeaks: ['#pb-fx-reduce-peaks', 'check'],
};

function wireEffectsTabsAndPresets(fx, q) {
  const title = fx.querySelector('.pb-effects-title');
  const bar = document.createElement('div');
  bar.className = 'pb-fx-tabs';
  bar.setAttribute('role', 'tablist');
  bar.setAttribute('aria-label', 'Effects');
  bar.innerHTML = FX_TABS.map(([k, label]) =>
    `<button type="button" role="tab" class="pb-fx-tab" data-fx="${k}" id="pb-fx-tab-${k}">${label}</button>`).join('');
  const panes = {};
  for (const [k] of FX_TABS) {
    const p = document.createElement('div');
    p.className = 'pb-fx-pane';
    p.dataset.fxPane = k;
    p.id = `pb-fx-${k}`;
    p.setAttribute('role', 'tabpanel');
    p.setAttribute('aria-labelledby', `pb-fx-tab-${k}`);
    panes[k] = p;
  }
  // Effects pane: everything that was in the block (knobs, Sync with audio).
  // Attach the panes first so q() (scoped to the Player root) finds the moved
  // controls below.
  const old = [...fx.children].filter(n => n !== title);
  title.after(bar, ...Object.values(panes));
  old.forEach(n => panes.effects.appendChild(n));
  const row = sel => q(sel)?.closest('.field-row');
  const put = (pane, ...nodes) => nodes.forEach(n => { if (n) panes[pane].appendChild(n); });
  put('curve', row('#pb-smoothing'), row('#pb-sync'));
  put('limits', row('#pb-maxspeed'), q('#pb-fx-reduce-peaks')?.closest('label'));
  put('advanced', row('#pb-tick'), row('#pb-softstart'));

  // Presets row (Effects pane).
  const pr = document.createElement('div');
  pr.className = 'pb-fx-presets';
  pr.innerHTML = `<label for="pb-fx-preset">Preset</label>
    <select id="pb-fx-preset" title="Saved effect settings: Intensity, Response / smoothing, Max-Speed, Reduce peaks, tick and soft-start"></select>
    <button type="button" id="pb-fx-preset-save" title="Save the current effect settings under a name">Save preset</button>
    <button type="button" id="pb-fx-preset-delete" title="Delete the selected preset">Delete</button>`;
  panes.effects.prepend(pr);

  function showFx(k) {
    if (!panes[k]) k = 'effects';
    for (const [key, p] of Object.entries(panes)) p.classList.toggle('is-active', key === k);
    bar.querySelectorAll('.pb-fx-tab').forEach(b => {
      const on = b.dataset.fx === k;
      b.classList.toggle('is-active', on);
      b.setAttribute('aria-selected', on ? 'true' : 'false');
      b.tabIndex = on ? 0 : -1;
    });
    fx.dataset.fxTab = k;
    store(FX_TAB_KEY, k);
  }
  bar.addEventListener('click', e => {
    const b = e.target.closest('.pb-fx-tab');
    if (b) showFx(b.dataset.fx);
  });
  bar.addEventListener('keydown', e => {
    if (e.key !== 'ArrowRight' && e.key !== 'ArrowLeft') return;
    const keys = FX_TABS.map(([k]) => k);
    const i = keys.indexOf(fx.dataset.fxTab);
    const next = keys[(i + (e.key === 'ArrowRight' ? 1 : keys.length - 1)) % keys.length];
    showFx(next);
    bar.querySelector(`[data-fx="${next}"]`)?.focus();
    e.preventDefault();
  });
  showFx(load(FX_TAB_KEY) || 'effects');

  // --- presets ---
  const select = pr.querySelector('#pb-fx-preset');
  let presets = {};
  const read = () => {
    const out = {};
    for (const [key, [sel, kind]] of Object.entries(FX_FIELDS)) {
      const n = q(sel);
      if (!n) continue;
      out[key] = kind === 'check' ? !!n.checked : Number(n.value);
    }
    return out;
  };
  const apply = values => {
    for (const [key, [sel, kind]] of Object.entries(FX_FIELDS)) {
      const n = q(sel);
      if (!n || !(key in values)) continue;
      if (kind === 'check') {
        if (n.checked === !!values[key]) continue;
        n.checked = !!values[key];
      } else {
        const v = Number(values[key]);
        if (!Number.isFinite(v)) continue;
        n.value = String(v);
        n.dispatchEvent(new Event('input', { bubbles: true }));
      }
      n.dispatchEvent(new Event('change', { bubbles: true }));
    }
  };
  const persist = () => saveSetting('playback.effect_presets', JSON.stringify(presets));
  const render = (selected = select.value) => {
    const names = Object.keys(presets).sort((a, b) => a.localeCompare(b));
    select.innerHTML = '<option value="">Custom</option><option value="__default">Default</option>'
      + names.map(() => '<option></option>').join('');
    // Names are user text: set via textContent / value, never as HTML.
    [...select.options].slice(2).forEach((o, i) => { o.value = names[i]; o.textContent = names[i]; });
    select.value = [...select.options].some(o => o.value === selected) ? selected : '';
    pr.querySelector('#pb-fx-preset-delete').disabled = !presets[select.value];
  };
  select.addEventListener('change', () => {
    const v = select.value;
    if (v === '__default') apply(FX_DEFAULTS);
    else if (presets[v]) apply(presets[v]);
    render(v);
  });
  pr.querySelector('#pb-fx-preset-save').addEventListener('click', () => {
    const suggested = presets[select.value] ? select.value : '';
    const name = (window.prompt('Preset name', suggested) || '').trim().slice(0, 40);
    if (!name || name === '__default') return;
    presets[name] = read();
    persist();
    render(name);
  });
  pr.querySelector('#pb-fx-preset-delete').addEventListener('click', () => {
    const v = select.value;
    if (!presets[v]) return;
    delete presets[v];
    persist();
    render('');
  });
  // Any manual change makes the selection "Custom" again.
  const fieldNodes = Object.values(FX_FIELDS).map(([sel]) => q(sel)).filter(Boolean);
  fieldNodes.forEach(n => n.addEventListener('input', e => { if (e.isTrusted) render(''); }));
  fieldNodes.forEach(n => n.addEventListener('change', e => { if (e.isTrusted) render(''); }));
  render('');
  getSettingsCache().then(s => {
    try {
      const parsed = JSON.parse(s?.playbackEffectPresets || '{}');
      if (parsed && typeof parsed === 'object' && !Array.isArray(parsed)) {
        presets = {};
        for (const [k, v] of Object.entries(parsed)) if (v && typeof v === 'object') presets[k] = v;
      }
    } catch (_) { presets = {}; }
    render(select.value);
  }).catch(() => {});

  return { showEffectsTab: showFx, readEffects: read, applyEffects: apply };
}
