import { EventsOn } from '../wailsjs/runtime/runtime';

// initSidebar baut das rechte Panel (Gerät / Skript / Qualität) aus dem
// UI-Mock. Es hält keinen eigenen fachlichen Zustand, sondern liest ihn aus
// dem bereits vorhandenen Zustand der anderen Tabs (deren DOM bleibt immer
// gemountet, nur mit display:none) bzw. denselben Backend-Events, die
// playback.js/device.js schon empfangen - Wails' EventsOn erlaubt mehrere
// Abonnenten pro Ereignis. Das vermeidet eine zweite Zustandsverwaltung, die
// mit der ersten auseinanderlaufen könnte.
export function initSidebar(root) {
  root.innerHTML = `
    <div class="card">
      <h2>Device</h2>
      <div class="dev-name"><i class="led" id="sb-led"></i><span id="sb-dev-name">Loading status…</span></div>
      <div class="meter">
        <div class="row" style="margin:0"><span>Vibration</span><span id="sb-vib-pct">-</span></div>
        <div class="bar"><span id="sb-vib-bar" style="width:0%;background:var(--accent)"></span></div>
      </div>
      <div class="meter">
        <div class="row" style="margin:0"><span>Suction</span><span id="sb-suc-pct">-</span></div>
        <div class="bar"><span id="sb-suc-bar" style="width:0%;background:var(--teal)"></span></div>
      </div>
    </div>
    <div class="card">
      <h2>Script</h2>
      <div class="chip" id="sb-recipe-chip">No script loaded</div>
      <p class="hint" id="sb-recipe-hint" style="margin-top:8px"></p>
    </div>
    <div class="card">
      <h2>Quality</h2>
      <p class="hint" id="sb-quality-hint" style="margin:0">No analysis yet.</p>
    </div>
  `;

  const el = id => root.querySelector(id);

  // Liest denselben Status, den device.js ohnehin alle 2s beim Backend
  // abfragt und als 'device:status'-Event verteilt, statt ihn hier ein
  // zweites Mal per GetDeviceStatus() abzufragen.
  function renderDevice(st) {
    if (st.connected) {
      el('#sb-led').style.background = 'var(--ok)';
      el('#sb-led').style.boxShadow = '0 0 8px var(--ok)';
      el('#sb-dev-name').textContent = st.mock ? 'Mock device connected' : (st.name || 'Connected');
    } else {
      el('#sb-led').style.background = '#555';
      el('#sb-led').style.boxShadow = 'none';
      el('#sb-dev-name').textContent = 'Not connected';
    }
  }

  function resetMeters() {
    el('#sb-vib-pct').textContent = '-';
    el('#sb-vib-bar').style.width = '0%';
    el('#sb-suc-pct').textContent = '-';
    el('#sb-suc-bar').style.width = '0%';
  }

  // Skript-/Rezept-Info: liest, was der Playback-Tab bereits anzeigt,
  // statt den Ladezustand ein zweites Mal zu verwalten.
  function refreshRecipe() {
    const pathLabel = document.getElementById('pb-script-path');
    const syncSelect = document.getElementById('pb-sync');
    if (!pathLabel || pathLabel.textContent === 'No script selected'
        || pathLabel.textContent === 'No Emotion Script selected') {
      el('#sb-recipe-chip').textContent = 'No script loaded';
      el('#sb-recipe-hint').textContent = '';
      return;
    }
    const name = pathLabel.textContent.split(/[\\/]/).pop();
    el('#sb-recipe-chip').textContent = name;
    el('#sb-recipe-hint').textContent = syncSelect
      ? `Sync mode: ${syncSelect.value} (Play tab)`
      : '';
  }

  // Qualität: übernimmt die Quality-Doctor-Ausgabe des letzten Generator-
  // laufs, sonst die Skript-Analyse aus dem Playback-Tab.
  function refreshQuality() {
    const genQuality = document.getElementById('gen-quality');
    const pbAnalysis = document.getElementById('pb-analysis');
    const box = el('#sb-quality-hint');
    if (genQuality && genQuality.style.display !== 'none' && genQuality.innerHTML.trim()) {
      box.innerHTML = genQuality.innerHTML;
    } else if (pbAnalysis && pbAnalysis.style.display !== 'none' && (pbAnalysis.textContent || '').trim()) {
      // textContent works even when parent #pb-quality-details is collapsed.
      box.textContent = pbAnalysis.textContent;
    } else {
      box.textContent = 'No analysis yet.';
    }
  }

  EventsOn('playback:frame', f => {
    const vibRaw = Number(f && f.vibration);
    const sucRaw = Number(f && f.suction);
    const vib = Math.round((Number.isFinite(vibRaw) ? vibRaw : 0) * 100);
    const suc = Math.round((Number.isFinite(sucRaw) ? sucRaw : 0) * 100);
    el('#sb-vib-pct').textContent = vib + '%';
    el('#sb-vib-bar').style.width = vib + '%';
    el('#sb-suc-pct').textContent = suc + '%';
    el('#sb-suc-bar').style.width = suc + '%';
  });
  EventsOn('playback:done', resetMeters);
  window.addEventListener('device:status', e => renderDevice(e.detail));

  // Skript-/Qualitätsanzeige ändert sich auch außerhalb dieses Panels -
  // regelmäßiges Nachsehen statt eines fehleranfälligen Netzes aus
  // Cross-Modul-Events. Der Gerätestatus kommt jetzt per Event von
  // device.js, das ihn ohnehin schon im selben Takt abfragt.
  setInterval(refreshRecipe, 1000);
  setInterval(refreshQuality, 1000);
  refreshRecipe();
  refreshQuality();
}
