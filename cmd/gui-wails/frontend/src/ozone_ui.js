import { ApplySuggestedOZone, SuggestPolarity, InvertLoadedScript, SaveOMarkers, GetOMarkers, ApplyRingDown } from '../wailsjs/go/main/App';
import { uiError } from './notify.js';

export function enhancePlaybackOZone(root) {
  const oRow = root.querySelector('#pb-omarker-add-row');
  if (!oRow || root.querySelector('#pb-ozone-suggest')) return;

  const suggestBtn = document.createElement('button');
  suggestBtn.id = 'pb-ozone-suggest';
  suggestBtn.type = 'button';
  suggestBtn.textContent = 'Suggest O-zone';
  oRow.appendChild(suggestBtn);

  const invertBtn = document.createElement('button');
  invertBtn.id = 'pb-polarity-invert';
  invertBtn.type = 'button';
  invertBtn.textContent = 'Check / invert direction';
  oRow.appendChild(invertBtn);

  const ringBtn = document.createElement('button');
  ringBtn.id = 'pb-ringdown';
  ringBtn.type = 'button';
  ringBtn.textContent = 'Ring-down';
  ringBtn.title = 'Append damped half-cycles after current position and save';
  oRow.appendChild(ringBtn);

  const status = document.createElement('span');
  status.id = 'pb-ozone-status';
  status.className = 'hint';
  status.style.margin = '0';
  oRow.appendChild(status);

  suggestBtn.addEventListener('click', async () => {
    try {
      const zone = await ApplySuggestedOZone();
      if (!zone || !zone.ok) {
        status.textContent = zone && zone.reason ? zone.reason : 'no suggestion';
        return;
      }
      status.textContent = zone.reason;
      window.dispatchEvent(new CustomEvent('ozone:suggested', { detail: zone }));
    } catch (err) {
      uiError('O-zone: ' + err, status);
    }
  });

  // Knopf-Klick = Bestätigung — kein zusätzliches Popup.
  invertBtn.addEventListener('click', async () => {
    try {
      const hint = await SuggestPolarity();
      const msg = (hint && hint.reason) || '';
      if (hint && hint.suggestInvert) {
        await InvertLoadedScript();
        status.textContent = 'Direction inverted and saved. ' + msg;
        window.dispatchEvent(new CustomEvent('polarity:inverted'));
      } else {
        status.textContent = msg || 'Direction looks consistent.';
      }
    } catch (err) {
      uiError('Polarity: ' + err, status);
    }
  });

  ringBtn.addEventListener('click', async () => {
    try {
      const video = root.querySelector('#pb-video');
      const nowMs = video ? Math.round((video.currentTime || 0) * 1000) : 0;
      // Knopf-Klick = Bestätigung — kein zusätzliches Popup.
      await ApplyRingDown(nowMs, 2);
      status.textContent = 'Ring-down appended after ' + nowMs + ' ms and saved.';
      window.dispatchEvent(new CustomEvent('ringdown:applied', { detail: { atMs: nowMs } }));
    } catch (err) {
      uiError('Ring-down: ' + err, status);
    }
  });

  document.addEventListener('keydown', (e) => {
    const tag = (e.target.tagName || '').toLowerCase();
    if (tag === 'input' || tag === 'select' || tag === 'textarea') return;
    if (e.key !== 'o' && e.key !== 'O') return;
    e.preventDefault();
    const video = root.querySelector('#pb-video');
    const nowMs = video && !video.paused ? Math.round(video.currentTime * 1000) : 0;
    window.dispatchEvent(new CustomEvent('ozone:hotkey', { detail: { nowMs } }));
  });
}

// Vom Playback-Tab auf ozone:hotkey aufgerufen; liefert die neue Marker-Liste.
export async function applyHotkeyOMarker(scriptPath, nowMs, existing) {
  if (!scriptPath || nowMs < 0) return existing || [];
  try {
    const markers = Array.isArray(existing) ? existing.slice() : await GetOMarkers(scriptPath);
    const start = Math.max(0, nowMs);
    const end = start + 4000;
    markers.push({ startMs: start, endMs: end, kind: 'primary', intensity: 1 });
    await SaveOMarkers(scriptPath, markers);
    return markers;
  } catch (err) {
    uiError('O-Marker Hotkey: ' + err);
    throw err;
  }
}
