// Training with any video (Owner, 12 Oct; docs/V1_TARGET_FEATURE_MAP.md).
//
// The Training stage shows either the pixel clip or a film the user picks.
// Training drives the device as before; the film only plays along: it
// starts with the session and pauses when the session ends. Loop and Muted
// are on by default. The film has its own URL on the local video server
// (SetTrainingVideo), so the Player's linked video is never replaced. Recent
// videos are shared with the Player library.

import { PickVideoFile, SetTrainingVideo, ClearTrainingVideo } from '../wailsjs/go/main/App';
import { readRecentVideos, pushRecentVideo } from './player_layout.js';
import { uiError } from './notify.js';

const KEY = 'tr.stage'; // {mode: 'pixel'|'video', path, loop, muted}
const IDLE_ART = new URL('./assets/flow/training-idle.svg', import.meta.url).href;

function loadState() {
  try {
    const s = JSON.parse(localStorage.getItem(KEY) || '{}') || {};
    return { mode: s.mode === 'video' ? 'video' : 'pixel', path: typeof s.path === 'string' ? s.path : '',
      loop: s.loop !== false, muted: s.muted !== false };
  } catch (_) {
    return { mode: 'pixel', path: '', loop: true, muted: true };
  }
}
function saveState(s) { try { localStorage.setItem(KEY, JSON.stringify(s)); } catch (_) { /* ignore */ } }
const baseName = p => String(p || '').split(/[\\/]/).pop();

/**
 * Mount the stage switch above `stageHost` (the pixel stage element).
 * @returns {{setRunning(running: boolean): void, mode(): string}}
 */
export function mountTrainingVideo(stageHost) {
  if (!stageHost) return { setRunning() {}, mode: () => 'pixel' };
  const state = loadState();
  let running = false;

  const bar = document.createElement('div');
  bar.className = 'tr-stage-bar';
  bar.dataset.trRegion = 'stage-switch';
  bar.innerHTML = `
    <div class="tr-stage-mode" role="radiogroup" aria-label="Training stage">
      <button type="button" role="radio" class="tr-stage-btn" data-mode="pixel" id="tr-stage-pixel">Pixel clip</button>
      <button type="button" role="radio" class="tr-stage-btn" data-mode="video" id="tr-stage-video">Video</button>
    </div>
    <div class="tr-stage-video-ctl" id="tr-stage-video-ctl">
      <button type="button" id="tr-video-choose">Choose video…</button>
      <select id="tr-video-recent" aria-label="Recent videos"><option value="">Recent videos</option></select>
      <label class="checkbox-row"><input type="checkbox" id="tr-video-loop" /> Loop</label>
      <label class="checkbox-row"><input type="checkbox" id="tr-video-muted" /> Muted</label>
      <span class="hint" id="tr-video-name"></span>
    </div>`;
  const view = document.createElement('div');
  view.className = 'tr-video-view';
  view.id = 'tr-video-view';
  view.innerHTML = `<video id="tr-video" playsinline preload="metadata" poster="${IDLE_ART}"></video>
    <p class="hint tr-video-empty" id="tr-video-empty">Choose a video — it plays along while the training runs. The device follows the training, not the film.</p>`;
  stageHost.before(bar, view);

  const q = sel => bar.querySelector(sel) || view.querySelector(sel);
  const video = q('#tr-video');
  const ctl = q('#tr-stage-video-ctl');
  const recentSel = q('#tr-video-recent');
  q('#tr-video-loop').checked = state.loop;
  q('#tr-video-muted').checked = state.muted;

  function renderRecent() {
    const list = readRecentVideos();
    recentSel.innerHTML = '<option value="">Recent videos</option>';
    for (const p of list) {
      const o = document.createElement('option');
      o.value = p;
      o.textContent = baseName(p);
      o.title = p;
      recentSel.appendChild(o);
    }
    recentSel.disabled = list.length === 0;
  }

  function apply() {
    const isVideo = state.mode === 'video';
    for (const b of bar.querySelectorAll('.tr-stage-btn')) {
      const on = b.dataset.mode === state.mode;
      b.classList.toggle('is-active', on);
      b.setAttribute('aria-checked', on ? 'true' : 'false');
    }
    ctl.hidden = !isVideo;
    view.hidden = !isVideo;
    stageHost.hidden = isVideo;
    video.loop = state.loop;
    video.muted = state.muted;
    q('#tr-video-name').textContent = state.path ? baseName(state.path) : '';
    q('#tr-video-empty').hidden = !!state.path;
    stageHost.closest('[data-tr-region="live"]')?.setAttribute('data-tr-stage', state.mode);
    saveState(state);
    if (!isVideo) video.pause();
    else if (running && video.src) video.play().catch(() => {});
  }

  async function useVideo(path) {
    if (!path) return;
    try {
      const url = await SetTrainingVideo(path);
      state.path = path;
      video.src = url;
      pushRecentVideo(path);
      renderRecent();
      apply();
    } catch (err) {
      uiError('Training video: ' + (err?.message || err));
    }
  }

  bar.querySelector('.tr-stage-mode').addEventListener('click', e => {
    const b = e.target.closest('.tr-stage-btn');
    if (!b || b.dataset.mode === state.mode) return;
    state.mode = b.dataset.mode;
    if (state.mode === 'pixel') ClearTrainingVideo().catch(() => {});
    else if (state.path && !video.src) useVideo(state.path);
    apply();
  });
  q('#tr-video-choose').addEventListener('click', async () => {
    try {
      const p = await PickVideoFile();
      if (p) await useVideo(p);
    } catch (err) {
      uiError('Choose video: ' + (err?.message || err));
    }
  });
  recentSel.addEventListener('change', () => {
    const p = recentSel.value;
    recentSel.value = '';
    if (p) useVideo(p);
  });
  q('#tr-video-loop').addEventListener('change', e => { state.loop = e.target.checked; apply(); });
  q('#tr-video-muted').addEventListener('change', e => { state.muted = e.target.checked; apply(); });

  renderRecent();
  apply();
  if (state.mode === 'video' && state.path) useVideo(state.path);

  return {
    // Called by training.js when a session starts / ends.
    setRunning(isRunning) {
      running = isRunning;
      if (state.mode !== 'video' || !video.src) return;
      if (isRunning) video.play().catch(() => {});
      else video.pause();
    },
    mode: () => state.mode,
  };
}
