import {
  PickVideoFile, PickImageFile, LoadFirstFrame, LoadFrameAt,
  BootstrapRoiTrainingRegions, AddRoiStillTrainingSample, RunRoiModelTraining,
  ListRoiTrainingSamples, DiscardRoiTrainingSample, UpdateRoiTrainingSample, GetRoiDatasetSummary,
  GetRoiTrainingSampleImage, CheckRoiTrainingAvailable, CheckRoiTrainingStatus,
  InstallRoiTrainingDeps, ListRoiTrainingDevices, CancelRoiTraining,
  GetMotionProfileModelStatus, TrainMotionProfileModel,
} from '../wailsjs/go/main/App';
import { CLASS_PRESETS, MAX_REGIONS, normalizeClass, labelFor, labelWithRole, CANONICAL } from './bodyparts.js';
import { mountBodyFigure } from './body_figure.js';
import { EventsOn } from '../wailsjs/runtime/runtime';
import { getSettingsCache, saveSetting } from './settings.js';
import { wireDataHelp } from './help.js';
import { uiError, uiInfo, uiWarn } from './notify.js';

const MARK_COLORS = [
  { stroke: '#3dccc0', fill: 'rgba(61,204,192,0.16)' },
  { stroke: '#f2b03d', fill: 'rgba(242,176,61,0.18)' },
  { stroke: '#5ecf8a', fill: 'rgba(94,207,138,0.16)' },
  { stroke: '#e8eaed', fill: 'rgba(232,234,237,0.12)' },
  { stroke: '#7aa2ff', fill: 'rgba(122,162,255,0.16)' },
  { stroke: '#ff7a9a', fill: 'rgba(255,122,154,0.16)' },
  { stroke: '#c9a0ff', fill: 'rgba(201,160,255,0.14)' },
  { stroke: '#9ad6ff', fill: 'rgba(154,214,255,0.14)' },
  { stroke: '#ffd27a', fill: 'rgba(255,210,122,0.16)' },
];

export function initRoiTraining(root) {
  root.innerHTML = `
    <h2>AI training</h2>
    <p class="hint">
      Free tag boxes → assign a body-part label → sample → review → train locally.
      Draw boxes freely (up to ${MAX_REGIONS} per image), then assign each to a class
      (Face, Mouth, Breasts, Nipples, Hand 1/2, Penis, Glans, Vagina).
      The same label may be used more than once — <b>Nipples need two boxes</b> (left + right).
      <b>Use for training</b> tracks <b>every</b> labeled box through the clip (CSRT per tag) —
      not Glans-only. If you only paint Glans, only Glans is tracked — mark partners too.
      Add or delete tags anytime. Audio is stored with the dataset.
    </p>
    <div class="card" style="margin-bottom:16px; padding:12px 14px;">
      <h3 style="margin-top:0; margin-bottom:8px;">From marks to a good script</h3>
      <ol class="hint" style="margin:0; padding-left:1.2em; line-height:1.55;">
        <li><b>Train here</b> — marks → “Use for training” → discard bad samples → start training. Result: <code>roi_detector.onnx</code>.</li>
        <li><b>Then Create</b> — Load video → enable “Smarter tip find” → “Find tip area”. AI suggests the box only.</li>
        <li><b>Verify/correct boxes</b> — never apply blindly. Tip (glans) for CSRT stroke;
          optional partner marks (e.g. nipples) for Contact feel only.</li>
        <li><b>Create Emotion Script</b> — classic tracking (CSRT) writes the script. AI does not track by itself.</li>
        <li><b>Review in Play</b> — Feedback buttons (usable/…) improve Quality Doctor later, not region AI.</li>
      </ol>
    </div>

    <div class="card" id="rt-motion-profile-card" style="margin-bottom:16px; padding:12px 14px;">
      <h3 style="margin-top:0; margin-bottom:6px;">Learned generation profile (Go)</h3>
      <p class="hint" style="margin:0 0 8px;">
        Learns from scenes remembered in <b>Generator → Power-user: scene memory</b> and the
        Style selected there. Training/classification are pure Go and offline; measuring a
        new video's signature uses the existing local OpenCV adapter. It suggests
        Normal / Soft / Autotune only; it never changes settings or writes a script without Apply/Create.
      </p>
      <div class="row" style="align-items:center; flex-wrap:wrap;">
        <button id="rt-profile-refresh" type="button">Refresh learning data</button>
        <button id="rt-profile-train" class="primary" type="button" disabled>Train Go profile model</button>
        <button id="rt-open-create" type="button">Open Generator…</button>
        <button id="rt-open-settings" type="button"
          data-help="Settings → Check availability after an ONNX model is in the default or custom path.">Open Settings (Check availability)…</button>
      </div>
      <div class="path-label" id="rt-profile-status">Checking saved scenes…</div>
      <p class="hint" id="rt-profile-path" style="margin:4px 0 0;"></p>
    </div>

    <h3>1. Collect training data</h3>
    <div class="row">
      <button id="rt-pick-video" type="button">Choose video…</button>
      <button id="rt-pick-image" type="button"
        data-help="Single image without tracking — good to define classes; transfer to clips later.">Choose image…</button>
      <span class="path-label" id="rt-video-path">No source selected</span>
    </div>
    <div class="row" style="align-items:center; margin:6px 0;">
      <label style="width:auto;" data-help="Offset for black intro — pick frame to mark.">Time (s)</label>
      <input type="number" id="rt-seek" value="0" min="0" step="0.5" style="width:5em;" />
      <button id="rt-seek-btn" type="button" disabled>Load frame</button>
      <button id="rt-seek-plus" type="button" disabled title="+1 s">+1s</button>
      <button id="rt-seek-plus5" type="button" disabled title="+5 s">+5s</button>
    </div>
    <div id="rt-canvas-wrap">
      <canvas id="rt-canvas"></canvas>
    </div>
    <div class="row" style="align-items:center; flex-wrap:wrap;">
      <button id="rt-mark-next" type="button"
        data-help="Start next free tag box (same label may be reused — e.g. both nipples).">+ Mark</button>
      <button id="rt-delete-mark" type="button"
        data-help="Remove the active free tag box (Delete / Backspace).">Delete box</button>
      <button id="rt-clear-marks" type="button">Clear all</button>
      <span class="hint" style="margin:0">Active box: <span id="rt-active-mark">1</span>/${MAX_REGIONS} — Shift+drag = next empty · then assign label (type any new tag name)</span>
    </div>
    <p class="hint" id="rt-track-plan" style="margin:4px 0 8px 0;" aria-live="polite">
      Will track: (mark + label at least one box)
    </p>
    <div class="row" style="align-items:center; flex-wrap:wrap; margin:4px 0;">
      <input type="text" id="rt-new-tag" placeholder="new custom tag…" style="width:10em;"
        list="rt-class-list" aria-label="New custom tag name" />
      <button id="rt-add-tag" type="button"
        data-help="Assign this custom class name to the active (or first unlabeled) box. Registered into the dataset on Use for training.">+ Add tag</button>
      <span class="hint" style="margin:0">Or type in Box N / click body map. Custom names OK.</span>
    </div>
    <div id="rt-mark-fields"></div>
    <p class="hint" id="rt-nipples-hint" style="margin:4px 0 0; display:none;"></p>
    <datalist id="rt-class-list"></datalist>
    <div id="rt-body-figure" class="body-figure-host" aria-label="Human body map for class picking"></div>
    <div id="rt-class-chips" class="rt-chips" aria-label="Known classes"></div>
    <div class="field-row" style="align-items:center;">
      <label data-help="Write every N-th frame as a sample (1–120). Smaller = denser; larger = less redundancy. Default 12. AI Train only — Create CSRT unchanged.">Sampling (every N-th frame)</label>
      <input type="range" id="rt-sample-every" min="1" max="120" step="1" value="12" style="flex:1;" />
      <span class="hint" id="rt-sample-every-val" style="margin:0; min-width:2.5em;">12</span>
    </div>
    <div class="field-row" style="align-items:center;">
      <label data-help="Multiply marked box size for YOLO labels (0.5–2.0). 1.0 = exact mark; 1.1–1.2 adds a small pad. Prefer correcting boxes in review over a large scale. AI Train only.">Box scale</label>
      <input type="range" id="rt-box-scale" min="0.5" max="2.0" step="0.05" value="1.0" style="flex:1;" />
      <span class="hint" id="rt-box-scale-val" style="margin:0; min-width:2.5em;">1</span>
    </div>
    <div class="checkbox-row"><input type="checkbox" id="rt-extract-audio" checked />
      <label for="rt-extract-audio" style="width:auto"
        data-help="Saves audio as WAV under audio/ in the dataset — for future learning.">Save audio too</label></div>
    <div class="row">
      <button id="rt-bootstrap" class="primary" disabled
        data-help="Video: tracks marks and writes YOLO samples. Image: saves a still annotation.">Use for training</button>
      <button id="rt-cancel-run" type="button" hidden
        data-help="Stops the running bootstrap or YOLO training. Closing the app does the same.">Cancel</button>
    </div>
    <div class="path-label" id="rt-bootstrap-status"></div>
    <div id="rt-bootstrap-progress-wrap" class="run-progress" style="display:none;">
      <div class="run-progress-track"><div id="rt-bootstrap-progress-bar" class="run-progress-bar"></div></div>
      <div id="rt-bootstrap-progress-text" class="hint" style="margin-top:4px;"></div>
    </div>
    <pre id="rt-bootstrap-log" class="run-log" aria-label="Bootstrap progress log"></pre>

    <h3>2. Review</h3>
    <div class="field-row"><label data-help="Folder with images/ and labels/ from bootstrap.">Dataset folder</label>
      <input type="text" id="rt-dataset-dir" style="flex:1" />
    </div>
    <div class="row" style="align-items:center;">
      <button id="rt-refresh-review" type="button">Refresh</button>
      <label class="hint" style="margin:0"
        data-help="Shows only samples from the last run."><input type="checkbox" id="rt-only-new" checked /> only newly added</label>
    </div>
    <div id="rt-review-grid" class="hint">
      No samples loaded yet.
      <div class="row" style="margin-top:8px; gap:8px; flex-wrap:wrap;">
        <button type="button" id="rt-empty-load-frame" class="secondary">Load frame</button>
        <button type="button" id="rt-empty-refresh" class="secondary">Refresh review</button>
      </div>
    </div>

    <h3>3. Dataset summary</h3>
    <div class="row"><button id="rt-summary-refresh" type="button">Load summary</button></div>
    <div id="rt-summary" class="hint"></div>

    <h3>4. Train model</h3>
    <p class="hint">Optional. Only needed if you want your own ONNX region model.
      Play / Create work without this. Device: Auto picks CUDA → MPS → DirectML → CPU.</p>
    <div class="field-row"><label data-help="Training passes. 50–100 typical for small sets. Capped at 500 — more rarely helps small sets and can keep the GPU busy for days.">Epochs</label><input type="number" id="rt-epochs" value="100" min="1" max="500" /></div>
    <div class="field-row"><label data-help="auto = best available backend.">Device</label>
      <select id="rt-device">
        <option value="auto" selected>Automatic</option>
        <option value="cuda">NVIDIA CUDA</option>
        <option value="directml">DirectML (Windows)</option>
        <option value="mps">Apple MPS</option>
        <option value="cpu">CPU (very slow)</option>
      </select>
    </div>
    <p class="hint" id="rt-device-status" style="margin:0 0 8px;"></p>
    <div class="row"><button id="rt-train" class="primary" type="button" disabled>Start training</button>
      <button id="rt-cancel-train" type="button" hidden
        data-help="Stops the running YOLO training (minutes to hours). Closing the app also cancels.">Cancel</button>
      <button id="rt-install-deps" type="button"
        data-help="Installs ultralytics + onnx (+ PyTorch), then restores opencv-contrib-python so Use for training keeps CSRT. Prefer a real Python install (not the Windows Store stub). Large download.">Install AI train deps</button></div>
    <p class="hint" id="rt-train-unavailable" style="display:none; color:var(--danger);">
      AI train packages missing (optional). Use “Install AI train deps”
      only if you want to train a model (large: ultralytics/torch). That
      button also restores <code>opencv-contrib-python</code> so
      <b>Use for training</b> keeps CSRT after ultralytics. Prefer a real
      Python from python.org — not the Windows Store stub. Or:
      <code>pip uninstall opencv-python opencv-python-headless &amp;&amp; pip install opencv-contrib-python ultralytics onnx</code>.
      Details: see <code>docs/KI_TRAINING.md</code> in the repo.
    </p>
    <p class="hint" id="rt-dataset-hint" style="display:none; color:var(--danger);">
      No training samples yet. Mark region(s) above and click <b>Use for training</b>
      (or add a photo still) before starting training.
    </p>
    <p class="hint" id="rt-status-detail" style="margin:0 0 8px;"></p>
    <div class="path-label" id="rt-train-status"></div>
    <div id="rt-train-progress-wrap" class="run-progress" style="display:none;">
      <div class="run-progress-track"><div id="rt-train-progress-bar" class="run-progress-bar"></div></div>
      <div id="rt-train-progress-text" class="hint" style="margin-top:4px;"></div>
    </div>
    <pre id="rt-train-log" class="run-log" aria-label="Training progress log"></pre>
  `;

  wireDataHelp(root);

  const el = sel => root.querySelector(sel);
  const canvas = el('#rt-canvas');
  const ctx = canvas.getContext('2d');

  async function refreshMotionProfileModelStatus() {
    const statusEl = el('#rt-profile-status');
    const pathEl = el('#rt-profile-path');
    const trainBtn = el('#rt-profile-train');
    try {
      const status = await GetMotionProfileModelStatus();
      const profiles = (status.profiles || []).map(p => `${p.profile}: ${p.count}`).join(' · ');
      const modelText = status.modelAvailable
        ? `Model ready (${status.trainedSamples} samples)`
        : 'No trained model yet';
      statusEl.textContent = `${status.usableSamples || 0} usable saved scenes · ${modelText}`
        + (profiles ? ` · ${profiles}` : '');
      pathEl.textContent = status.readyToTrain
        ? 'Ready to train. New remembered scenes are included on the next training run.'
        : 'Remember at least two scenes with a selected Style in the Generator before training.';
      if (status.modelWarning) {
        pathEl.textContent += ' Existing model is unreadable/incompatible; training will replace it.';
      }
      pathEl.title = `Labels: ${status.labelsPath || ''}\nModel: ${status.modelPath || ''}`;
      trainBtn.disabled = !status.readyToTrain;
      return status;
    } catch (err) {
      statusEl.textContent = 'Profile learning status unavailable: ' + err;
      trainBtn.disabled = true;
      return null;
    }
  }

  el('#rt-profile-refresh').addEventListener('click', refreshMotionProfileModelStatus);
  el('#rt-profile-train').addEventListener('click', async () => {
    const btn = el('#rt-profile-train');
    const statusEl = el('#rt-profile-status');
    btn.disabled = true;
    statusEl.textContent = 'Training Go profile model…';
    try {
      const status = await TrainMotionProfileModel();
      statusEl.textContent = `Model trained from ${status.trainedSamples || status.usableSamples || 0} scenes. `
        + 'Create can now ask it for a profile suggestion.';
      uiInfo('Local motion-profile model trained. Suggestions stay opt-in in the Generator.');
      window.dispatchEvent(new CustomEvent('samn-motion-profile-refresh'));
      await refreshMotionProfileModelStatus();
    } catch (err) {
      uiError('Profile model training: ' + err, statusEl);
      await refreshMotionProfileModelStatus();
    }
  });
  el('#rt-open-settings')?.addEventListener('click', () => {
    const tab = document.querySelector('.tab-btn[data-tab="settings"]');
    if (tab) tab.click();
  });

  el('#rt-open-create').addEventListener('click', () => {
    document.querySelector('.tab-btn[data-tab="generator"]')?.click();
  });
  refreshMotionProfileModelStatus();

  let sourcePath = null;
  let sourceKind = null; // 'video' | 'image'
  let img = new Image();
  let nativeW = 0, nativeH = 0;
  let marks = Array(MAX_REGIONS).fill(null); // free tag boxes; assign labels after
  let cancelKind = null; // 'bootstrap' | 'train' | null — which Cancel is shown
  let trainPackagesAvailable = false;
  let activeMark = 0;
  let dragging = false, startX = 0, startY = 0, curX = 0, curY = 0;
  let lastPrefix = '';
  let datasetDir = '';
  let seekSec = 0;

  function markColor(i) {
    return MARK_COLORS[i % MARK_COLORS.length];
  }

  function countClassOnMarks(classId) {
    const n = normalizeClass(classId);
    if (!n) return 0;
    let c = 0;
    for (let i = 0; i < MAX_REGIONS; i++) {
      if (!marks[i]) continue;
      const v = normalizeClass(el(`#rt-class${i + 1}`)?.value || '');
      if (v === n) c += 1;
    }
    return c;
  }

  function updateNipplesHint() {
    const hint = el('#rt-nipples-hint');
    if (!hint) return;
    const n = countClassOnMarks('nipples');
    if (n === 1) {
      hint.style.display = '';
      hint.textContent = 'Nipples: mark the second nipple too (same label on another free box).';
    } else if (n >= 2) {
      hint.style.display = '';
      hint.textContent = `Nipples: ${n} boxes labeled — good.`;
    } else {
      hint.style.display = 'none';
      hint.textContent = '';
    }
  }

  function applyClassToActiveMark(classId) {
    const n = normalizeClass(classId);
    if (!n) return;
    // Prefer the first drawn box that still has no class — body-map clicks
    // usually happen right after marking, when activeMark already advanced.
    // Duplicate labels (e.g. second nipples) go on the next empty-class box.
    let idx = activeMark;
    for (let i = 0; i < MAX_REGIONS; i++) {
      if (marks[i] && !(el(`#rt-class${i + 1}`)?.value || '').trim()) {
        idx = i;
        break;
      }
    }
    const input = el(`#rt-class${idx + 1}`);
    if (input) {
      input.value = n;
      updateBootstrapEnabled();
    }
    bodyFigure?.setActive(n);
    updateNipplesHint();
  }

  const bodyFigure = mountBodyFigure(el('#rt-body-figure'), {
    onSelect: applyClassToActiveMark,
    getActive: () => el(`#rt-class${activeMark + 1}`)?.value || '',
  });

  const DISPLAY_W = 560;

  function renderMarkFields(classOverrides) {
    const prev = [];
    for (let i = 0; i < MAX_REGIONS; i++) {
      prev[i] = classOverrides
        ? (classOverrides[i] || '')
        : (el(`#rt-class${i + 1}`)?.value || '');
    }
    const wrap = el('#rt-mark-fields');
    wrap.innerHTML = '';
    for (let i = 0; i < MAX_REGIONS; i++) {
      const row = document.createElement('div');
      row.className = 'field-row';
      row.style.opacity = (i === 0 || marks[i] || i === activeMark) ? '1' : '0.55';
      const lab = document.createElement('label');
      lab.textContent = `Box ${i + 1}`;
      lab.style.borderLeft = `3px solid ${markColor(i).stroke}`;
      lab.style.paddingLeft = '6px';
      lab.title = 'Free tag box — assign any taxonomy label (duplicates OK)';
      const input = document.createElement('input');
      input.type = 'text';
      input.id = `rt-class${i + 1}`;
      input.setAttribute('list', 'rt-class-list');
      input.placeholder = 'assign label…';
      input.setAttribute('aria-label', `Label for free tag box ${i + 1}`);
      if (prev[i]) input.value = prev[i];
      input.addEventListener('input', () => {
        updateBootstrapEnabled();
        updateNipplesHint();
      });
      input.addEventListener('focus', () => {
        activeMark = i;
        el('#rt-active-mark').textContent = String(activeMark + 1);
        bodyFigure?.setActive(input.value || '');
      });
      const meta = document.createElement('span');
      meta.className = 'hint';
      meta.id = `rt-mark-label${i + 1}`;
      meta.style.margin = '0 0 0 8px';
      meta.textContent = marks[i]
        ? `${marks[i].w}×${marks[i].h}`
        : (i === activeMark ? 'draw…' : '—');
      row.appendChild(lab);
      row.appendChild(input);
      row.appendChild(meta);
      if (marks[i]) {
        const del = document.createElement('button');
        del.type = 'button';
        del.className = 'danger';
        del.textContent = 'Delete';
        del.title = `Delete free tag box ${i + 1}`;
        del.setAttribute('aria-label', `Delete free tag box ${i + 1}`);
        del.dataset.deleteMark = String(i);
        del.addEventListener('click', () => removeMarkAt(i));
        row.appendChild(del);
      }
      wrap.appendChild(row);
    }
    el('#rt-active-mark').textContent = String(activeMark + 1);
    bodyFigure?.setActive(el(`#rt-class${activeMark + 1}`)?.value || '');
    updateBootstrapEnabled();
    updateNipplesHint();
  }

  /** Remove one free tag and compact so later boxes stay contiguous for bootstrap. */
  function removeMarkAt(idx) {
    if (idx < 0 || idx >= MAX_REGIONS || !marks[idx]) return;
    const classVals = [];
    for (let i = 0; i < MAX_REGIONS; i++) {
      classVals.push(el(`#rt-class${i + 1}`)?.value || '');
    }
    const nextMarks = [];
    const nextClasses = [];
    for (let i = 0; i < MAX_REGIONS; i++) {
      if (i === idx) continue;
      if (marks[i]) {
        nextMarks.push(marks[i]);
        nextClasses.push(classVals[i]);
      }
    }
    marks = Array(MAX_REGIONS).fill(null);
    const classOverrides = Array(MAX_REGIONS).fill('');
    for (let i = 0; i < nextMarks.length; i++) {
      marks[i] = nextMarks[i];
      classOverrides[i] = nextClasses[i];
    }
    activeMark = Math.min(nextMarks.length, MAX_REGIONS - 1);
    renderMarkFields(classOverrides);
    redraw();
  }

  function collectLabeledRegions() {
    const regions = [];
    for (let i = 0; i < MAX_REGIONS; i++) {
      const cls = (el(`#rt-class${i + 1}`)?.value || '').trim();
      const m = marks[i];
      if (m && cls) {
        regions.push({
          ROI: { X: m.x, Y: m.y, W: m.w, H: m.h },
          ClassName: normalizeClass(cls) || cls,
        });
      }
    }
    return regions;
  }

  function updateBootstrapEnabled() {
    let labeled = 0;
    let missingClass = false;
    const names = [];
    for (let i = 0; i < MAX_REGIONS; i++) {
      if (!marks[i]) continue;
      const cls = (el(`#rt-class${i + 1}`)?.value || '').trim();
      if (!cls) missingClass = true;
      else {
        labeled += 1;
        names.push(labelFor(normalizeClass(cls) || cls) || cls);
      }
    }
    // Any labeled free-tag set unlocks bootstrap — not tip/glans / marks[0] only.
    // Stay disabled while train/bootstrap run is claimed.
    el('#rt-bootstrap').disabled = !!cancelKind
      || !(sourcePath && labeled > 0 && !missingClass);
    const plan = el('#rt-track-plan');
    if (plan) {
      if (!labeled) {
        plan.textContent = 'Will track: (mark + label at least one box — every labeled box, not Glans-only)';
      } else if (missingClass) {
        plan.textContent = `Will track ${labeled} labeled + unlabeled boxes waiting — assign a class to each painted box.`;
      } else {
        plan.textContent = `Will track ${labeled} tag${labeled === 1 ? '' : 's'}: ${names.join(', ')}`;
      }
    }
    updateNipplesHint();
  }

  function drawNativeRect(r, stroke, fill) {
    if (!r || !nativeW || !nativeH) return;
    const scaleX = canvas.width / nativeW, scaleY = canvas.height / nativeH;
    ctx.strokeStyle = stroke;
    ctx.lineWidth = 2;
    ctx.strokeRect(r.x * scaleX, r.y * scaleY, r.w * scaleX, r.h * scaleY);
    ctx.fillStyle = fill;
    ctx.fillRect(r.x * scaleX, r.y * scaleY, r.w * scaleX, r.h * scaleY);
  }

  function redraw() {
    ctx.clearRect(0, 0, canvas.width, canvas.height);
    if (img.src) ctx.drawImage(img, 0, 0, canvas.width, canvas.height);
    for (let i = 0; i < MAX_REGIONS; i++) {
      if (marks[i] && !(dragging && i === activeMark)) {
        const c = markColor(i);
        drawNativeRect(marks[i], c.stroke, c.fill);
      }
    }
    if (dragging) {
      const c = markColor(activeMark);
      const dx0 = Math.min(startX, curX), dy0 = Math.min(startY, curY);
      const dw = Math.abs(curX - startX), dh = Math.abs(curY - startY);
      ctx.strokeStyle = c.stroke;
      ctx.lineWidth = 2;
      ctx.strokeRect(dx0, dy0, dw, dh);
      ctx.fillStyle = c.fill;
      ctx.fillRect(dx0, dy0, dw, dh);
    }
  }

  canvas.addEventListener('mousedown', e => {
    const r = canvas.getBoundingClientRect();
    startX = curX = e.clientX - r.left;
    startY = curY = e.clientY - r.top;
    if (e.shiftKey) {
      for (let i = 0; i < MAX_REGIONS; i++) {
        if (!marks[i]) { activeMark = i; break; }
      }
    }
    dragging = true;
    renderMarkFields();
  });
  canvas.addEventListener('mousemove', e => {
    if (!dragging) return;
    const r = canvas.getBoundingClientRect();
    curX = e.clientX - r.left;
    curY = e.clientY - r.top;
    redraw();
  });
  window.addEventListener('mouseup', () => {
    if (!dragging) return;
    dragging = false;
    const w = Math.abs(curX - startX), h = Math.abs(curY - startY);
    if (w < 4 || h < 4) { redraw(); return; }
    const scaleX = nativeW / canvas.width, scaleY = nativeH / canvas.height;
    const x0 = Math.min(startX, curX), y0 = Math.min(startY, curY);
    marks[activeMark] = {
      x: Math.round(x0 * scaleX), y: Math.round(y0 * scaleY),
      w: Math.round(w * scaleX), h: Math.round(h * scaleY),
    };
    if (activeMark < MAX_REGIONS - 1) activeMark += 1;
    renderMarkFields();
    updateBootstrapEnabled();
    redraw();
  });

  el('#rt-mark-next').addEventListener('click', () => {
    for (let i = 0; i < MAX_REGIONS; i++) {
      if (!marks[i]) { activeMark = i; break; }
    }
    renderMarkFields();
  });

  function deleteActiveMark() {
    if (!marks[activeMark] && !(el(`#rt-class${activeMark + 1}`)?.value || '').trim()) {
      // Prefer deleting the last drawn box if active slot is empty.
      for (let i = MAX_REGIONS - 1; i >= 0; i--) {
        if (marks[i] || (el(`#rt-class${i + 1}`)?.value || '').trim()) {
          activeMark = i;
          break;
        }
      }
    }
    if (!marks[activeMark] && !(el(`#rt-class${activeMark + 1}`)?.value || '').trim()) return;
    marks[activeMark] = null;
    const input = el(`#rt-class${activeMark + 1}`);
    if (input) input.value = '';
    // Compact: shift later filled slots down so free tags stay contiguous.
    const kept = [];
    const classes = [];
    for (let i = 0; i < MAX_REGIONS; i++) {
      const cls = (el(`#rt-class${i + 1}`)?.value || '').trim();
      if (marks[i] || cls) {
        kept.push(marks[i]);
        classes.push(cls);
      }
    }
    marks = Array(MAX_REGIONS).fill(null);
    for (let i = 0; i < kept.length; i++) marks[i] = kept[i];
    activeMark = Math.min(kept.length, MAX_REGIONS - 1);
    renderMarkFields();
    for (let i = 0; i < MAX_REGIONS; i++) {
      const inp = el(`#rt-class${i + 1}`);
      if (inp) inp.value = classes[i] || '';
    }
    updateBootstrapEnabled();
    updateNipplesHint();
    redraw();
  }

  el('#rt-delete-mark').addEventListener('click', deleteActiveMark);
  el('#rt-add-tag').addEventListener('click', () => {
    const raw = (el('#rt-new-tag')?.value || '').trim();
    if (!raw) {
      uiWarn('Type a custom tag name first.');
      return;
    }
    applyClassToActiveMark(raw);
    el('#rt-new-tag').value = '';
    refreshClassList();
    uiInfo(`Tag “${normalizeClass(raw) || raw}” assigned to a free box.`);
  });
  el('#rt-new-tag')?.addEventListener('keydown', (e) => {
    if (e.key === 'Enter') {
      e.preventDefault();
      el('#rt-add-tag').click();
    }
  });
  window.addEventListener('keydown', (e) => {
    if (e.key !== 'Delete' && e.key !== 'Backspace') return;
    if (reviewEditorOpen) return;
    const t = e.target;
    if (t && (t.tagName === 'INPUT' || t.tagName === 'TEXTAREA' || t.tagName === 'SELECT' || t.isContentEditable)) {
      return;
    }
    if (!root.contains(document.activeElement) && document.activeElement !== canvas) return;
    e.preventDefault();
    deleteActiveMark();
  });

  el('#rt-clear-marks').addEventListener('click', () => {
    marks = Array(MAX_REGIONS).fill(null);
    activeMark = 0;
    renderMarkFields();
    updateBootstrapEnabled();
    redraw();
  });

  async function showPreview(path, sec) {
    const preview = sec > 0 ? await LoadFrameAt(path, sec) : await LoadFirstFrame(path);
    nativeW = preview.width; nativeH = preview.height;
    const displayH = Math.round(DISPLAY_W * nativeH / nativeW);
    canvas.width = DISPLAY_W; canvas.height = displayH;
    img.onload = redraw;
    img.src = 'data:image/png;base64,' + preview.pngBase64;
  }

  async function chooseVideo() {
    const path = await PickVideoFile();
    if (document.activeElement?.blur) document.activeElement.blur();
    window.focus();
    if (!path) return;
    sourcePath = path;
    sourceKind = 'video';
    seekSec = 0;
    el('#rt-seek').value = '0';
    el('#rt-video-path').textContent = path.split(/[\\/]/).pop();
    marks = Array(MAX_REGIONS).fill(null);
    activeMark = 0;
    renderMarkFields();
    updateBootstrapEnabled();
    el('#rt-seek-btn').disabled = false;
    el('#rt-seek-plus').disabled = false;
    el('#rt-seek-plus5').disabled = false;
    el('#rt-bootstrap-status').textContent = 'Loading frame…';
    try {
      await showPreview(path, 0);
      el('#rt-bootstrap-status').textContent = 'Mark region(s). If the start is black, seek forward first.';
    } catch (err) {
      uiError('Load video: ' + err, el('#rt-bootstrap-status'));
    }
  }

  async function chooseImage() {
    const path = await PickImageFile();
    if (document.activeElement?.blur) document.activeElement.blur();
    window.focus();
    if (!path) return;
    sourcePath = path;
    sourceKind = 'image';
    el('#rt-video-path').textContent = path.split(/[\\/]/).pop() + ' (image)';
    marks = Array(MAX_REGIONS).fill(null);
    activeMark = 0;
    renderMarkFields();
    updateBootstrapEnabled();
    el('#rt-seek-btn').disabled = true;
    el('#rt-seek-plus').disabled = true;
    el('#rt-seek-plus5').disabled = true;
    el('#rt-bootstrap-status').textContent = 'Loading image…';
    try {
      // Still: LoadFrameAt won't work; read via temporary — use same PNG path through a tiny hack:
      // Go AddStill uses DecodeConfig; for preview we open as data URL via fetch isn't available.
      // Use LoadFirstFrame on non-video fails — instead draw from filesystem via a Go helper.
      // Fallback: ask user to mark after we get dimensions from a video-less path.
      // We reuse LoadFrameAt only for video; for images, create an Image from file:// is blocked.
      // So call a zero-second dump only works for video. For images, use img from path via Go:
      const preview = await GetRoiTrainingSampleImage(path);
      img.onload = () => {
        nativeW = img.naturalWidth || img.width;
        nativeH = img.naturalHeight || img.height;
        const displayH = Math.round(DISPLAY_W * nativeH / Math.max(nativeW, 1));
        canvas.width = DISPLAY_W; canvas.height = displayH;
        redraw();
      };
      const ext = path.toLowerCase();
      const mime = ext.endsWith('.png') ? 'image/png'
        : ext.endsWith('.webp') ? 'image/webp'
        : ext.endsWith('.gif') ? 'image/gif'
        : 'image/jpeg';
      img.src = `data:${mime};base64,` + preview;
      el('#rt-bootstrap-status').textContent = 'Still image: mark regions, then save.';
    } catch (err) {
      uiError('Load image: ' + err, el('#rt-bootstrap-status'));
    }
  }

  async function seekTo(sec) {
    if (!sourcePath || sourceKind !== 'video') return;
    seekSec = Math.max(0, sec);
    el('#rt-seek').value = String(seekSec);
    el('#rt-bootstrap-status').textContent = `Loading frame at ${seekSec}s…`;
    try {
      await showPreview(sourcePath, seekSec);
      el('#rt-bootstrap-status').textContent = `Frame at ${seekSec}s — mark region(s).`;
    } catch (err) {
      uiError('Seek failed: ' + err, el('#rt-bootstrap-status'));
    }
  }

  el('#rt-pick-video').addEventListener('click', chooseVideo);
  el('#rt-pick-image').addEventListener('click', chooseImage);
  el('#rt-seek-btn').addEventListener('click', () => seekTo(parseFloat(el('#rt-seek').value) || 0));
  el('#rt-seek-plus').addEventListener('click', () => seekTo(seekSec + 1));
  el('#rt-seek-plus5').addEventListener('click', () => seekTo(seekSec + 5));

  function isCancelError(err) {
    return /cancel|abgebrochen|context canceled/i.test(String(err || ''));
  }

  function setCancelVisible(kind) {
    // kind: 'bootstrap' | 'train' | null — only that Cancel is shown.
    cancelKind = kind || null;
    const boot = el('#rt-cancel-run');
    const train = el('#rt-cancel-train');
    if (boot) boot.hidden = cancelKind !== 'bootstrap';
    if (train) train.hidden = cancelKind !== 'train';
    // Mutual disable: do not start the other run while one is claimed.
    if (cancelKind === 'bootstrap') {
      el('#rt-train').disabled = true;
    } else if (cancelKind === 'train') {
      el('#rt-bootstrap').disabled = true;
    } else {
      updateBootstrapEnabled();
      el('#rt-train').disabled = !trainPackagesAvailable;
    }
  }

  async function cancelRoiRun() {
    try {
      const ok = await CancelRoiTraining();
      if (!ok) uiWarn('No AI train run to cancel.');
    } catch (err) {
      uiError('Cancel training: ' + err);
    }
  }

  el('#rt-cancel-run')?.addEventListener('click', cancelRoiRun);
  el('#rt-cancel-train')?.addEventListener('click', cancelRoiRun);

  el('#rt-bootstrap').addEventListener('click', async () => {
    if (!sourcePath) return;
    const regions = collectLabeledRegions();
    if (!regions.length) return;
    el('#rt-bootstrap').disabled = true;
    setCancelVisible('bootstrap');
    el('#rt-bootstrap-status').textContent = 'Running…';
    el('#rt-bootstrap-log').textContent = '';
    showRunProgress('rt-bootstrap', true);
    try {
      if (sourceKind === 'image') {
        lastPrefix = await AddRoiStillTrainingSample(sourcePath, regions);
        showRunProgress('rt-bootstrap', false);
        if (cancelKind === 'bootstrap') setCancelVisible(null);
        el('#rt-bootstrap-status').textContent = 'Still sample saved.';
        updateBootstrapEnabled();
        refreshReview();
        refreshClassList();
        refreshDatasetReadyHint();
      } else {
        const sampleEvery = parseInt(el('#rt-sample-every').value, 10) || 12;
        const extractAudio = el('#rt-extract-audio').checked;
        const boxScale = parseFloat(el('#rt-box-scale')?.value) || 1.0;
        const tracked = regions.map(r => labelFor(r.ClassName) || r.ClassName).join(', ');
        el('#rt-bootstrap-status').textContent = tracked
          ? `Tracking ${regions.length} tags (${tracked})…`
          : 'Running…';
        lastPrefix = await BootstrapRoiTrainingRegions(
          sourcePath, regions, sampleEvery, extractAudio, seekSec > 0 ? seekSec : 0, boxScale,
        );
      }
    } catch (err) {
      showRunProgress('rt-bootstrap', false);
      // Failed claim must not hide Cancel for a still-running train.
      if (cancelKind === 'bootstrap') setCancelVisible(null);
      if (isCancelError(err)) {
        uiWarn('Bootstrap cancelled: ' + err, el('#rt-bootstrap-status'));
      } else {
        uiError('Save sample: ' + err, el('#rt-bootstrap-status'));
      }
      updateBootstrapEnabled();
    }
  });

  function showRunProgress(prefix, show) {
    const wrap = el(`#${prefix}-progress-wrap`);
    if (!wrap) return;
    wrap.style.display = show ? 'block' : 'none';
    if (show) {
      const bar = el(`#${prefix}-progress-bar`);
      const text = el(`#${prefix}-progress-text`);
      if (bar) { bar.style.width = '0%'; bar.style.opacity = '1'; }
      if (text) text.textContent = 'Starting…';
    }
  }

  function applyRunPercent(prefix, pct) {
    const wrap = el(`#${prefix}-progress-wrap`);
    const bar = el(`#${prefix}-progress-bar`);
    const text = el(`#${prefix}-progress-text`);
    if (!wrap || !bar || !text) return;
    if (wrap.style.display === 'none') wrap.style.display = 'block';
    if (pct < 0) {
      bar.style.width = '100%';
      bar.style.opacity = '0.35';
      text.textContent = 'Running… (length unknown)';
      return;
    }
    bar.style.opacity = '1';
    bar.style.width = pct + '%';
    text.textContent = pct + ' %';
  }

  EventsOn('roitraining:bootstrap:progress', line => {
    const log = el('#rt-bootstrap-log');
    log.textContent += line + '\n';
    log.scrollTop = log.scrollHeight;
  });
  EventsOn('roitraining:bootstrap:percent', pct => applyRunPercent('rt-bootstrap', pct));
  EventsOn('roitraining:bootstrap:done', payload => {
    showRunProgress('rt-bootstrap', false);
    if (cancelKind === 'bootstrap') setCancelVisible(null);
    updateBootstrapEnabled();
    if (payload.error) {
      if (isCancelError(payload.error)) {
        uiWarn('Bootstrap cancelled: ' + payload.error, el('#rt-bootstrap-status'));
      } else {
        uiError('Bootstrap failed: ' + payload.error, el('#rt-bootstrap-status'));
      }
      return;
    }
    lastPrefix = payload.prefix || lastPrefix;
    el('#rt-bootstrap-status').textContent = 'Done — samples in the review view.';
    refreshReview();
    refreshClassList();
    refreshDatasetReadyHint();
  });

  function normBox(box) {
    if (!box || typeof box !== 'object') return null;
    const xc = Number(box.xc ?? box.XC);
    const yc = Number(box.yc ?? box.YC);
    const w = Number(box.w ?? box.W);
    const h = Number(box.h ?? box.H);
    if (![xc, yc, w, h].every(Number.isFinite) || w <= 0 || h <= 0) return null;
    return {
      classId: box.classId ?? box.ClassID ?? 0,
      className: box.className || box.ClassName || '',
      xc, yc, w, h,
    };
  }

  function boxOverlay(box, opts = {}) {
    const b = normBox(box);
    if (!b) return null;
    const wrap = document.createElement('div');
    wrap.className = 'rt-box' + (opts.selected ? ' is-selected' : '');
    wrap.style.left = ((b.xc - b.w / 2) * 100) + '%';
    wrap.style.top = ((b.yc - b.h / 2) * 100) + '%';
    wrap.style.width = (b.w * 100) + '%';
    wrap.style.height = (b.h * 100) + '%';
    if (opts.index != null) wrap.dataset.boxIndex = String(opts.index);
    const label = document.createElement('span');
    label.className = 'rt-box-label';
    label.textContent = labelFor(b.className) || b.className || ('#' + b.classId);
    wrap.appendChild(label);
    if (opts.onSelect) {
      wrap.style.cursor = 'pointer';
      wrap.title = 'Select window to set label';
      wrap.addEventListener('click', (e) => {
        e.stopPropagation();
        opts.onSelect(opts.index);
      });
    }
    return wrap;
  }

  function paintBoxes(thumbWrap, boxes, opts = {}) {
    thumbWrap.querySelectorAll('.rt-box').forEach(n => n.remove());
    (boxes || []).forEach((box, i) => {
      const node = boxOverlay(box, {
        index: i,
        selected: opts.selectedIdx === i,
        onSelect: opts.onSelect,
      });
      if (node) thumbWrap.appendChild(node);
    });
  }

  function renderReviewEmpty(msg) {
    const grid = el('#rt-review-grid');
    if (!grid) return;
    grid.innerHTML = '';
    const text = document.createElement('div');
    text.className = 'hint';
    text.textContent = msg;
    grid.appendChild(text);
    const row = document.createElement('div');
    row.className = 'row';
    row.style.cssText = 'margin-top:8px; gap:8px; flex-wrap:wrap;';
    const loadBtn = document.createElement('button');
    loadBtn.type = 'button';
    loadBtn.className = 'secondary';
    loadBtn.id = 'rt-empty-load-frame';
    loadBtn.textContent = 'Load frame';
    loadBtn.addEventListener('click', () => el('#rt-seek-btn')?.click());
    const refreshBtn = document.createElement('button');
    refreshBtn.type = 'button';
    refreshBtn.className = 'secondary';
    refreshBtn.id = 'rt-empty-refresh';
    refreshBtn.textContent = 'Refresh review';
    refreshBtn.addEventListener('click', () => refreshReview());
    row.appendChild(loadBtn);
    row.appendChild(refreshBtn);
    grid.appendChild(row);
  }

  async function refreshReview() {
    datasetDir = el('#rt-dataset-dir').value.trim();
    const onlyNew = el('#rt-only-new').checked;
    const prefix = onlyNew ? lastPrefix : '';
    const grid = el('#rt-review-grid');
    grid.textContent = 'Loading…';
    try {
      const samples = await ListRoiTrainingSamples(datasetDir, prefix);
      if (!samples || samples.length === 0) {
        renderReviewEmpty(onlyNew && lastPrefix
          ? 'No new samples — uncheck to use the full dataset.'
          : 'No samples found.');
        return;
      }
      grid.innerHTML = '';
      const gridEl = document.createElement('div');
      gridEl.className = 'rt-grid';
      grid.appendChild(gridEl);
      for (const s of samples) {
        const card = document.createElement('div');
        card.className = 'rt-card';
        const thumbWrap = document.createElement('div');
        thumbWrap.className = 'rt-thumb-wrap';
        const imgEl = document.createElement('img');
        imgEl.className = 'rt-thumb';
        thumbWrap.appendChild(imgEl);
        // Paint labels as soon as we have geometry — do not wait on the image
        // RPC so overlays never disappear behind a slow/failed thumbnail load.
        paintBoxes(thumbWrap, s.boxes);
        const caption = document.createElement('div');
        caption.className = 'hint';
        caption.style.margin = '4px 0';
        caption.textContent = `${s.split} / ${s.name}`;
        const discardBtn = document.createElement('button');
        discardBtn.type = 'button';
        discardBtn.className = 'danger';
        discardBtn.textContent = 'Discard';
        const editBtn = document.createElement('button');
        editBtn.type = 'button';
        editBtn.textContent = 'Correct box';
        editBtn.title = 'Open large editor to view or redraw the box';
        const btnRow = document.createElement('div');
        btnRow.className = 'row';
        btnRow.style.gap = '6px';
        btnRow.appendChild(editBtn);
        btnRow.appendChild(discardBtn);
        thumbWrap.title = 'Click to open large review editor';
        thumbWrap.tabIndex = 0;
        thumbWrap.setAttribute('role', 'button');
        thumbWrap.setAttribute('aria-label', `Open review editor for ${s.split}/${s.name}`);
        card.appendChild(thumbWrap);
        card.appendChild(caption);
        card.appendChild(btnRow);
        gridEl.appendChild(card);

        GetRoiTrainingSampleImage(s.imagePath).then(b64 => {
          const p = String(s.imagePath || '').toLowerCase();
          const mime = p.endsWith('.png') ? 'image/png'
            : p.endsWith('.webp') ? 'image/webp'
            : p.endsWith('.gif') ? 'image/gif'
            : 'image/jpeg';
          imgEl.onload = () => paintBoxes(thumbWrap, s.boxes);
          imgEl.src = `data:${mime};base64,` + b64;
        }).catch(() => { caption.textContent += ' (could not load image)'; });

        const openEditor = (startAdjust) => {
          openReviewEditor({
            sample: s,
            card,
            thumbWrap,
            imgEl,
            startAdjust: !!startAdjust,
          }).catch(err => uiError('Review editor: ' + err));
        };
        thumbWrap.addEventListener('click', () => openEditor(false));
        thumbWrap.addEventListener('keydown', (e) => {
          if (e.key === 'Enter' || e.key === ' ') {
            e.preventDefault();
            openEditor(false);
          }
        });
        editBtn.addEventListener('click', () => openEditor(true));

        discardBtn.addEventListener('click', async () => {
          discardBtn.disabled = true;
          try {
            await DiscardRoiTrainingSample(datasetDir, s.split, s.name);
            card.remove();
            refreshDatasetReadyHint();
          } catch (err) {
            uiError('Discard sample: ' + err);
            discardBtn.disabled = false;
          }
        });
      }
    } catch (err) {
      grid.textContent = 'Error: ' + err;
    }
  }

  let reviewEditorOpen = null;

  function closeReviewEditor() {
    if (!reviewEditorOpen) return;
    const { overlay, cleanup } = reviewEditorOpen;
    cleanup();
    overlay.remove();
    reviewEditorOpen = null;
  }

  function escapeReviewText(s) {
    return String(s ?? '')
      .replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;');
  }

  /** Large review pane: full image + boxes, pan/zoom, confirm / discard / adjust.
   * Any detection window can get any taxonomy label (Face was previously the only
   * adjustable first-class box — Owner: set all detection/role labels). */
  async function openReviewEditor({ sample, card, thumbWrap, imgEl, startAdjust }) {
    closeReviewEditor();
    if (!imgEl.src) {
      throw new Error('image not loaded yet');
    }
    if (!imgEl.naturalWidth) {
      await new Promise((resolve, reject) => {
        const onOk = () => { cleanupWait(); resolve(); };
        const onErr = () => { cleanupWait(); reject(new Error('image failed to load')); };
        const cleanupWait = () => {
          imgEl.removeEventListener('load', onOk);
          imgEl.removeEventListener('error', onErr);
        };
        imgEl.addEventListener('load', onOk);
        imgEl.addEventListener('error', onErr);
        if (imgEl.complete && imgEl.naturalWidth) onOk();
      });
    }

    let boxes = (Array.isArray(sample.boxes) ? sample.boxes : [])
      .map(normBox).filter(Boolean);
    let selectedIdx = boxes.length ? 0 : -1;
    let adjustMode = !!startAdjust;
    let addMode = false;
    let scale = 1;
    let panX = 0;
    let panY = 0;
    let panning = false;
    let panOriginX = 0;
    let panOriginY = 0;
    let panStartX = 0;
    let panStartY = 0;
    let drawing = false;
    let x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    let drawOverlay = null;

    const overlay = document.createElement('div');
    overlay.className = 'rt-review-overlay';
    overlay.setAttribute('role', 'dialog');
    overlay.setAttribute('aria-modal', 'true');
    overlay.setAttribute('aria-label', 'Review training sample');

    const panel = document.createElement('div');
    panel.className = 'rt-review-panel';

    const head = document.createElement('header');
    head.className = 'rt-review-head';
    head.innerHTML = `
      <div>
        <p class="rt-review-kicker">AI Train · Review</p>
        <h2>${escapeReviewText(sample.split)} / ${escapeReviewText(sample.name)}</h2>
        <p class="rt-review-sub hint">Free tags: select a window · assign any label (duplicates OK, nipples×2) · Adjust redraw · + Add box · Delete box · Discard sample. Bootstrap tracked every labeled box — not Glans only.</p>
      </div>
      <button type="button" class="rt-review-close" aria-label="Close review editor">Close</button>
    `;

    const labelBar = document.createElement('div');
    labelBar.className = 'rt-review-labels';
    labelBar.innerHTML = `
      <label for="rt-review-box-sel">Window</label>
      <select id="rt-review-box-sel" aria-label="Detection window"></select>
      <label for="rt-review-class-sel">Label</label>
      <select id="rt-review-class-sel" aria-label="Detection / role label"></select>
      <input type="text" id="rt-review-class-custom" list="rt-class-list" placeholder="or type new tag…"
        aria-label="Custom tag name" style="width:9em;" />
      <button type="button" id="rt-review-apply-custom" title="Apply custom tag to selected window">Apply tag</button>
      <span class="hint rt-review-role-hint" id="rt-review-role-hint"></span>
    `;
    const boxSel = labelBar.querySelector('#rt-review-box-sel');
    const classSel = labelBar.querySelector('#rt-review-class-sel');
    const customInput = labelBar.querySelector('#rt-review-class-custom');
    const applyCustomBtn = labelBar.querySelector('#rt-review-apply-custom');
    const roleHint = labelBar.querySelector('#rt-review-role-hint');
    for (const p of CANONICAL) {
      const opt = document.createElement('option');
      opt.value = p.id;
      // Plain taxonomy label — generate roles (tracked/fixed/mask) do not gate
      // AI Train bootstrap; every free tag is CSRT-tracked during Use for training.
      opt.textContent = p.label;
      classSel.appendChild(opt);
    }

    const stage = document.createElement('div');
    stage.className = 'rt-review-stage';
    const viewport = document.createElement('div');
    viewport.className = 'rt-review-viewport';
    const scene = document.createElement('div');
    scene.className = 'rt-review-scene';
    const bigImg = document.createElement('img');
    bigImg.className = 'rt-review-img';
    bigImg.alt = `${sample.split}/${sample.name}`;
    bigImg.draggable = false;
    bigImg.src = imgEl.src;
    scene.appendChild(bigImg);
    viewport.appendChild(scene);
    stage.appendChild(viewport);

    const foot = document.createElement('div');
    foot.className = 'rt-review-foot';
    const status = document.createElement('p');
    status.className = 'hint rt-review-status';
    const actions = document.createElement('div');
    actions.className = 'rt-review-actions';
    const confirmBtn = document.createElement('button');
    confirmBtn.type = 'button';
    confirmBtn.className = 'primary';
    confirmBtn.textContent = 'Confirm correct';
    const adjustBtn = document.createElement('button');
    adjustBtn.type = 'button';
    adjustBtn.textContent = 'Adjust box';
    const addBoxBtn = document.createElement('button');
    addBoxBtn.type = 'button';
    addBoxBtn.id = 'rt-review-add-box';
    addBoxBtn.textContent = '+ Add box';
    addBoxBtn.title = 'Draw a new free tag box, then assign a label (e.g. second nipple)';
    const deleteBoxBtn = document.createElement('button');
    deleteBoxBtn.type = 'button';
    deleteBoxBtn.id = 'rt-review-delete-box';
    deleteBoxBtn.className = 'danger';
    deleteBoxBtn.textContent = 'Delete box';
    deleteBoxBtn.title = 'Remove the selected free tag from this sample';
    const discardModalBtn = document.createElement('button');
    discardModalBtn.type = 'button';
    discardModalBtn.className = 'danger';
    discardModalBtn.textContent = 'Discard sample';
    actions.appendChild(confirmBtn);
    actions.appendChild(adjustBtn);
    actions.appendChild(addBoxBtn);
    actions.appendChild(deleteBoxBtn);
    actions.appendChild(discardModalBtn);
    foot.appendChild(status);
    foot.appendChild(actions);

    panel.appendChild(head);
    panel.appendChild(labelBar);
    panel.appendChild(stage);
    panel.appendChild(foot);
    overlay.appendChild(panel);
    document.body.appendChild(overlay);

    function selectedLabel() {
      if (selectedIdx < 0 || !boxes[selectedIdx]) return '';
      return normalizeClass(boxes[selectedIdx].className) || boxes[selectedIdx].className || '';
    }

    function syncLabelControls() {
      boxSel.innerHTML = '';
      if (!boxes.length) {
        const opt = document.createElement('option');
        opt.value = '';
        opt.textContent = '(no boxes)';
        boxSel.appendChild(opt);
        boxSel.disabled = true;
        // Keep Label enabled in + Add box so the new free tag gets a class.
        classSel.disabled = !addMode;
        roleHint.textContent = addMode ? 'Pick label, then drag a new box' : '';
        return;
      }
      boxSel.disabled = false;
      classSel.disabled = false;
      boxes.forEach((b, i) => {
        const opt = document.createElement('option');
        opt.value = String(i);
        const name = labelFor(b.className) || b.className || ('#' + b.classId);
        opt.textContent = `${i + 1}/${boxes.length} · ${name}`;
        boxSel.appendChild(opt);
      });
      if (selectedIdx < 0 || selectedIdx >= boxes.length) selectedIdx = 0;
      boxSel.value = String(selectedIdx);
      const cur = selectedLabel();
      if (cur && ![...classSel.options].some((o) => o.value === cur)) {
        const opt = document.createElement('option');
        opt.value = cur;
        opt.textContent = labelFor(cur) || cur;
        classSel.appendChild(opt);
      }
      if (cur) classSel.value = cur;
      else classSel.value = CANONICAL[0].id;
      const role = (labelWithRole(cur || classSel.value).match(/\(([^)]+)\)/) || [])[1];
      roleHint.textContent = role
        ? `Generate role hint: ${role} — bootstrap still tracks this box`
        : '';
      deleteBoxBtn.disabled = selectedIdx < 0 || !boxes.length || adjustMode || addMode;
    }

    function repaintScene() {
      paintBoxes(scene, boxes, {
        selectedIdx,
        onSelect: (i) => {
          if (adjustMode || addMode) return;
          selectedIdx = i;
          syncLabelControls();
          repaintScene();
          status.textContent = `Window ${i + 1} selected — set Label, Adjust, + Add box, or Delete box.`;
        },
      });
    }

    function applyTransform() {
      scene.style.transform = `translate(${panX}px, ${panY}px) scale(${scale})`;
    }

    function fitToView() {
      const vw = viewport.clientWidth || 1;
      const vh = viewport.clientHeight || 1;
      const iw = bigImg.naturalWidth || imgEl.naturalWidth || 1;
      const ih = bigImg.naturalHeight || imgEl.naturalHeight || 1;
      scene.style.width = iw + 'px';
      scene.style.height = ih + 'px';
      bigImg.style.width = iw + 'px';
      bigImg.style.height = ih + 'px';
      scale = Math.min(vw / iw, vh / ih, 1) * 0.96;
      panX = (vw - iw * scale) / 2;
      panY = (vh - ih * scale) / 2;
      applyTransform();
      syncLabelControls();
      repaintScene();
    }

    function idleStatus() {
      const nip = boxes.filter((b) => normalizeClass(b.className) === 'nipples').length;
      let msg = 'Looks good? Confirm — or Label / Adjust / + Add box / Delete box / Discard sample.';
      if (nip === 1) {
        msg = 'Nipples: only one box — use + Add box for the second nipple (same label).';
      }
      return msg;
    }

    function setDrawModes({ adjust = false, add = false } = {}) {
      if (drawing && drawOverlay) {
        drawing = false;
        drawOverlay.remove();
        drawOverlay = null;
      }
      adjustMode = !!adjust;
      addMode = !!add;
      const drawingOn = adjustMode || addMode;
      viewport.classList.toggle('is-adjusting', drawingOn);
      adjustBtn.textContent = adjustMode ? 'Cancel adjust' : 'Adjust box';
      adjustBtn.classList.toggle('primary', adjustMode);
      addBoxBtn.textContent = addMode ? 'Cancel add' : '+ Add box';
      addBoxBtn.classList.toggle('primary', addMode);
      deleteBoxBtn.disabled = !boxes.length || adjustMode || addMode || selectedIdx < 0;
      confirmBtn.classList.toggle('primary', !drawingOn);
      classSel.disabled = false;
      if (addMode) {
        const nip = boxes.filter((b) => normalizeClass(b.className) === 'nipples').length;
        if (nip === 1) classSel.value = 'nipples';
        status.textContent = 'Drag a new free tag box. Label select chooses its class (Nipples for the second). Escape cancels.';
      } else if (adjustMode) {
        const who = selectedIdx >= 0
          ? (labelFor(boxes[selectedIdx]?.className) || `window ${selectedIdx + 1}`)
          : 'selected window';
        status.textContent = `Drag on the image to redraw ${who}. Escape cancels adjust.`;
      } else {
        status.textContent = idleStatus();
      }
    }

    function setAdjustMode(on) {
      setDrawModes(on ? { adjust: true } : {});
    }

    function setAddMode(on) {
      if (on && boxes.length >= MAX_REGIONS) {
        status.textContent = `At most ${MAX_REGIONS} free tag boxes per image.`;
        return;
      }
      setDrawModes(on ? { add: true } : {});
    }

    function clientToNorm(clientX, clientY) {
      const r = scene.getBoundingClientRect();
      if (r.width <= 0 || r.height <= 0) return { x: 0, y: 0 };
      return {
        x: Math.min(1, Math.max(0, (clientX - r.left) / r.width)),
        y: Math.min(1, Math.max(0, (clientY - r.top) / r.height)),
      };
    }

    function onWheel(e) {
      e.preventDefault();
      const factor = e.deltaY < 0 ? 1.12 : 1 / 1.12;
      const next = Math.min(8, Math.max(0.2, scale * factor));
      const rect = viewport.getBoundingClientRect();
      const mx = e.clientX - rect.left;
      const my = e.clientY - rect.top;
      panX = mx - (mx - panX) * (next / scale);
      panY = my - (my - panY) * (next / scale);
      scale = next;
      applyTransform();
    }

    function onPointerDown(e) {
      if (e.button !== 0) return;
      if (adjustMode || addMode) {
        e.preventDefault();
        drawing = true;
        const p = clientToNorm(e.clientX, e.clientY);
        x0 = x1 = p.x;
        y0 = y1 = p.y;
        drawOverlay = document.createElement('div');
        drawOverlay.className = 'rt-box rt-box-draft';
        scene.appendChild(drawOverlay);
        viewport.setPointerCapture?.(e.pointerId);
        return;
      }
      e.preventDefault();
      panning = true;
      panOriginX = panX;
      panOriginY = panY;
      panStartX = e.clientX;
      panStartY = e.clientY;
      viewport.classList.add('is-panning');
      viewport.setPointerCapture?.(e.pointerId);
    }

    function onPointerMove(e) {
      if (drawing && drawOverlay) {
        const p = clientToNorm(e.clientX, e.clientY);
        x1 = p.x;
        y1 = p.y;
        const L = Math.min(x0, x1), T = Math.min(y0, y1);
        const W = Math.abs(x1 - x0), H = Math.abs(y1 - y0);
        drawOverlay.style.left = (L * 100) + '%';
        drawOverlay.style.top = (T * 100) + '%';
        drawOverlay.style.width = (W * 100) + '%';
        drawOverlay.style.height = (H * 100) + '%';
        return;
      }
      if (!panning) return;
      panX = panOriginX + (e.clientX - panStartX);
      panY = panOriginY + (e.clientY - panStartY);
      applyTransform();
    }

    async function saveBoxes(nextBoxes, okMsg) {
      try {
        await UpdateRoiTrainingSample(datasetDir, sample.split, sample.name, nextBoxes);
        boxes = nextBoxes.map(normBox).filter(Boolean);
        sample.boxes = boxes;
        if (selectedIdx >= boxes.length) selectedIdx = boxes.length ? boxes.length - 1 : -1;
        syncLabelControls();
        repaintScene();
        paintBoxes(thumbWrap, boxes);
        status.textContent = okMsg || 'Saved.';
        uiInfo(okMsg || 'Saved.');
        return true;
      } catch (err) {
        uiError('Save box: ' + err);
        status.textContent = 'Save failed: ' + err;
        return false;
      }
    }

    async function finishDraw() {
      if (!drawing) return;
      drawing = false;
      const L = Math.min(x0, x1), T = Math.min(y0, y1);
      const W = Math.abs(x1 - x0), H = Math.abs(y1 - y0);
      if (drawOverlay) {
        drawOverlay.remove();
        drawOverlay = null;
      }
      if (W < 0.02 || H < 0.02) {
        status.textContent = 'Box too small — drag a larger region.';
        return;
      }
      const next = boxes.slice();
      const className = normalizeClass(classSel.value)
        || (addMode ? 'nipples' : '')
        || (boxes[selectedIdx]?.className)
        || 'face';
      const updatedBox = {
        classId: addMode ? 0 : (boxes[selectedIdx]?.classId ?? 0),
        className,
        xc: L + W / 2,
        yc: T + H / 2,
        w: W,
        h: H,
      };
      if (addMode) {
        if (next.length >= MAX_REGIONS) {
          status.textContent = `At most ${MAX_REGIONS} free tag boxes per image.`;
          setAddMode(false);
          return;
        }
        next.push(updatedBox);
        selectedIdx = next.length - 1;
      } else {
        const idx = selectedIdx >= 0 ? selectedIdx : 0;
        if (idx < next.length) next[idx] = updatedBox;
        else next.push(updatedBox);
        selectedIdx = Math.min(idx, next.length - 1);
      }
      const verb = addMode ? 'Box added' : 'Box saved';
      if (await saveBoxes(next, `${verb} (${labelFor(updatedBox.className) || updatedBox.className}).`)) {
        setDrawModes({});
      }
    }

    function onPointerUp() {
      if (drawing) {
        finishDraw();
        return;
      }
      if (!panning) return;
      panning = false;
      viewport.classList.remove('is-panning');
    }

    function onKey(e) {
      if (e.key !== 'Escape') return;
      if (adjustMode || addMode) {
        if (drawing && drawOverlay) {
          drawing = false;
          drawOverlay.remove();
          drawOverlay = null;
        }
        setDrawModes({});
        return;
      }
      closeReviewEditor();
    }

    function cleanup() {
      viewport.removeEventListener('wheel', onWheel);
      viewport.removeEventListener('pointerdown', onPointerDown);
      window.removeEventListener('pointermove', onPointerMove);
      window.removeEventListener('pointerup', onPointerUp);
      window.removeEventListener('keydown', onKey);
    }

    boxSel.addEventListener('change', () => {
      selectedIdx = parseInt(boxSel.value, 10);
      if (!Number.isFinite(selectedIdx)) selectedIdx = 0;
      syncLabelControls();
      repaintScene();
    });

    classSel.addEventListener('change', async () => {
      if (addMode) return; // label applies when the new box is drawn
      if (selectedIdx < 0 || !boxes[selectedIdx]) return;
      const n = normalizeClass(classSel.value);
      if (!n) return;
      const next = boxes.map((b, i) => (i === selectedIdx
        ? { ...b, className: n }
        : b));
      await saveBoxes(next, `Label → ${labelFor(n) || n}`);
    });

    async function applyCustomLabel() {
      if (addMode) {
        const n = normalizeClass(customInput.value);
        if (n) classSel.value = n;
        return;
      }
      if (selectedIdx < 0 || !boxes[selectedIdx]) return;
      const n = normalizeClass(customInput.value);
      if (!n) {
        uiWarn('Type a tag name first.');
        return;
      }
      if (![...classSel.options].some((o) => o.value === n)) {
        const opt = document.createElement('option');
        opt.value = n;
        opt.textContent = labelWithRole(n) || n;
        classSel.appendChild(opt);
      }
      classSel.value = n;
      const next = boxes.map((b, i) => (i === selectedIdx
        ? { ...b, className: n }
        : b));
      await saveBoxes(next, `Tag → ${labelWithRole(n) || n}`);
      customInput.value = '';
      refreshClassList();
    }
    applyCustomBtn.addEventListener('click', () => { applyCustomLabel(); });
    customInput.addEventListener('keydown', (e) => {
      if (e.key === 'Enter') {
        e.preventDefault();
        applyCustomLabel();
      }
    });

    head.querySelector('.rt-review-close').addEventListener('click', closeReviewEditor);
    overlay.addEventListener('click', (e) => {
      if (e.target === overlay) closeReviewEditor();
    });
    confirmBtn.addEventListener('click', () => {
      uiInfo('Sample confirmed.');
      closeReviewEditor();
    });
    adjustBtn.addEventListener('click', () => {
      if (adjustMode) setAdjustMode(false);
      else setAdjustMode(true);
    });
    addBoxBtn.addEventListener('click', () => {
      if (addMode) setAddMode(false);
      else setAddMode(true);
    });
    deleteBoxBtn.addEventListener('click', async () => {
      if (selectedIdx < 0 || !boxes[selectedIdx]) return;
      if (adjustMode || addMode) setDrawModes({});
      const removed = labelFor(boxes[selectedIdx].className)
        || boxes[selectedIdx].className
        || `window ${selectedIdx + 1}`;
      const next = boxes.filter((_, i) => i !== selectedIdx);
      selectedIdx = next.length ? Math.min(selectedIdx, next.length - 1) : -1;
      if (await saveBoxes(next, `Deleted ${removed}.`)) {
        syncLabelControls();
        repaintScene();
        status.textContent = next.length
          ? idleStatus()
          : 'No boxes left — + Add box or Discard sample.';
      }
    });
    discardModalBtn.addEventListener('click', async () => {
      discardModalBtn.disabled = true;
      confirmBtn.disabled = true;
      adjustBtn.disabled = true;
      addBoxBtn.disabled = true;
      deleteBoxBtn.disabled = true;
      try {
        await DiscardRoiTrainingSample(datasetDir, sample.split, sample.name);
        card.remove();
        refreshDatasetReadyHint();
        closeReviewEditor();
      } catch (err) {
        uiError('Discard sample: ' + err);
        discardModalBtn.disabled = false;
        confirmBtn.disabled = false;
        adjustBtn.disabled = false;
        addBoxBtn.disabled = false;
        deleteBoxBtn.disabled = false;
      }
    });

    viewport.addEventListener('wheel', onWheel, { passive: false });
    viewport.addEventListener('pointerdown', onPointerDown);
    window.addEventListener('pointermove', onPointerMove);
    window.addEventListener('pointerup', onPointerUp);
    window.addEventListener('keydown', onKey);

    reviewEditorOpen = { overlay, cleanup };

    const afterImg = () => {
      fitToView();
      setDrawModes(startAdjust ? { adjust: true } : {});
    };
    if (bigImg.complete && bigImg.naturalWidth) afterImg();
    else bigImg.addEventListener('load', afterImg, { once: true });
  }

  el('#rt-refresh-review').addEventListener('click', refreshReview);
  el('#rt-empty-refresh')?.addEventListener('click', () => {
    el('#rt-refresh-review')?.click();
  });
  el('#rt-empty-load-frame')?.addEventListener('click', () => {
    el('#rt-seek-btn')?.click();
  });
  el('#rt-dataset-dir').addEventListener('change', e => {
    saveSetting('generator.roiTrainingDatasetDir', e.target.value.trim());
  });

  async function refreshClassList() {
    let fromData = [];
    try {
      const summary = await GetRoiDatasetSummary(datasetDir || el('#rt-dataset-dir').value.trim());
      fromData = (summary.classes || []).map(c => c.className);
      const list = el('#rt-class-list');
      const names = [...new Set([...fromData, ...CLASS_PRESETS])];
      list.innerHTML = names.map(n => `<option value="${n}"></option>`).join('');
      renderClassChips(fromData, names);
      return summary;
    } catch {
      el('#rt-class-list').innerHTML = CLASS_PRESETS.map(n => `<option value="${n}"></option>`).join('');
      renderClassChips([], CLASS_PRESETS);
      return null;
    }
  }

  function renderClassChips(fromData, allNames) {
    const wrap = el('#rt-class-chips');
    if (!wrap) return;
    wrap.innerHTML = '';
    // Always offer the full taxonomy (+ extra dataset names). Showing only
    // classes.json (often just glans from early tip-only datasets) made other
    // BODY_REGIONS look unavailable for marking/tracking.
    const known = [...new Set([...(allNames || CLASS_PRESETS), ...(fromData || [])])];
    if (!known.length) {
      wrap.innerHTML = '<span class="hint">No classes yet — use the English body-part presets or mark a region.</span>';
      return;
    }
    const label = document.createElement('span');
    label.className = 'hint';
    label.style.marginRight = '6px';
    label.textContent = fromData.length ? 'Body parts (+ dataset):' : 'Body parts:';
    wrap.appendChild(label);
    known.forEach(name => {
      const btn = document.createElement('button');
      btn.type = 'button';
      btn.className = 'rt-chip';
      btn.textContent = labelFor(name) || name;
      btn.title = name;
      btn.addEventListener('click', () => applyClassToActiveMark(name));
      wrap.appendChild(btn);
    });
  }

  el('#rt-summary-refresh').addEventListener('click', async () => {
    datasetDir = el('#rt-dataset-dir').value.trim();
    const box = el('#rt-summary');
    box.textContent = 'Loading…';
    try {
      const summary = await GetRoiDatasetSummary(datasetDir);
      if (!summary.classes || summary.classes.length === 0) {
        box.textContent = 'No classes yet — collect data first.';
        return;
      }
      box.innerHTML = '<table style="border-collapse:collapse; width:100%;">'
        + '<thead><tr><th style="text-align:left; padding:4px 8px;">Klasse</th>'
        + '<th style="text-align:right; padding:4px 8px;">Train</th>'
        + '<th style="text-align:right; padding:4px 8px;">Val</th></tr></thead><tbody>'
        + summary.classes.map(c => `<tr><td style="padding:4px 8px;">${c.className}</td>`
          + `<td style="text-align:right; padding:4px 8px;">${c.trainCount}</td>`
          + `<td style="text-align:right; padding:4px 8px;">${c.valCount}</td></tr>`).join('')
        + '</tbody></table>';
      refreshClassList();
    } catch (err) {
      box.textContent = 'Error: ' + err;
    }
  });

  el('#rt-train').addEventListener('click', async () => {
    datasetDir = el('#rt-dataset-dir').value.trim();
    try {
      const summary = await GetRoiDatasetSummary(datasetDir);
      if (!summary.readyToTrain) {
        uiWarn('Collect samples first: mark region(s) → “Use for training”, then start training.',
          el('#rt-train-status'));
        const hint = el('#rt-dataset-hint');
        if (hint) hint.style.display = 'block';
        return;
      }
    } catch (_) { /* fall through; Go/Python will fail fast with a clear error */ }
    // Same cap as generator.MaxRoiTrainingEpochs (Go clamps too).
    const epochs = Math.min(500, Math.max(1, parseInt(el('#rt-epochs').value, 10) || 100));
    el('#rt-epochs').value = epochs;
    const device = el('#rt-device').value;
    el('#rt-train').disabled = true;
    setCancelVisible('train');
    el('#rt-train-status').textContent = 'Running…';
    el('#rt-train-log').textContent = '';
    showRunProgress('rt-train', true);
    try {
      await RunRoiModelTraining(epochs, device);
    } catch (err) {
      showRunProgress('rt-train', false);
      if (cancelKind === 'train') setCancelVisible(null);
      if (isCancelError(err)) {
        uiWarn('Training cancelled: ' + err, el('#rt-train-status'));
      } else {
        uiError('ROI training: ' + err, el('#rt-train-status'));
      }
      el('#rt-train').disabled = !trainPackagesAvailable;
    }
  });
  EventsOn('roitraining:train:progress', line => {
    const log = el('#rt-train-log');
    log.textContent += line + '\n';
    log.scrollTop = log.scrollHeight;
  });
  EventsOn('roitraining:train:percent', pct => applyRunPercent('rt-train', pct));
  EventsOn('roitraining:train:done', payload => {
    showRunProgress('rt-train', false);
    if (cancelKind === 'train') setCancelVisible(null);
    else el('#rt-train').disabled = !trainPackagesAvailable;
    if (payload.error) {
      if (isCancelError(payload.error)) {
        uiWarn('Training cancelled: ' + payload.error, el('#rt-train-status'));
      } else {
        uiError('Training failed: ' + payload.error, el('#rt-train-status'));
      }
      return;
    }
    el('#rt-train-status').textContent = 'Done: ' + payload.modelPath
      + ' — AI detection in the Generator tab updates automatically.';
    window.dispatchEvent(new CustomEvent('samn-ai-roi-refresh'));
  });

  getSettingsCache().then(s => {
    datasetDir = s.roiDatasetDir || s.defaultRoiDatasetDir || '';
    el('#rt-dataset-dir').value = datasetDir;
    if (s.roiSampleEvery != null && el('#rt-sample-every')) {
      const n = Math.round(Number(s.roiSampleEvery));
      if (Number.isFinite(n) && n >= 1 && n <= 120) {
        el('#rt-sample-every').value = String(n);
      }
    }
    if (s.roiBoxScale != null && el('#rt-box-scale')) {
      const n = Number(s.roiBoxScale);
      if (Number.isFinite(n) && n >= 0.5 && n <= 2.0) {
        el('#rt-box-scale').value = String(n);
      }
    }
    syncRoiTrainSampleKnobLabels();
    refreshClassList();
    refreshDatasetReadyHint();
  });

  function applyTrainAvailability(available, detail) {
    trainPackagesAvailable = !!available;
    el('#rt-train').disabled = !trainPackagesAvailable || !!cancelKind;
    el('#rt-train-unavailable').style.display = available ? 'none' : 'block';
    if (el('#rt-status-detail') && detail) {
      el('#rt-status-detail').textContent = detail;
    }
    if (available) refreshDatasetReadyHint();
  }

  async function refreshDatasetReadyHint() {
    const hint = el('#rt-dataset-hint');
    if (!hint) return;
    datasetDir = el('#rt-dataset-dir').value.trim();
    if (!datasetDir) {
      hint.style.display = 'block';
      return;
    }
    try {
      const summary = await GetRoiDatasetSummary(datasetDir);
      hint.style.display = summary.readyToTrain ? 'none' : 'block';
    } catch (_) {
      hint.style.display = 'block';
    }
  }

  CheckRoiTrainingStatus().then(st => {
    applyTrainAvailability(!!st.ultralytics, st.detail || '');
  }).catch(() => {
    CheckRoiTrainingAvailable().then(available => {
      applyTrainAvailability(available, '');
    }).catch(() => applyTrainAvailability(false, ''));
  });

  el('#rt-install-deps')?.addEventListener('click', async () => {
    el('#rt-install-deps').disabled = true;
    el('#rt-train').disabled = true;
    el('#rt-bootstrap').disabled = true;
    el('#rt-train-status').textContent = 'Installing dependencies… (wait — no Cancel for install)';
    el('#rt-train-log').textContent = '';
    try {
      await InstallRoiTrainingDeps();
    } catch (err) {
      uiError('Installation: ' + err, el('#rt-train-status'));
      el('#rt-install-deps').disabled = false;
      updateBootstrapEnabled();
      CheckRoiTrainingStatus().then(st => {
        applyTrainAvailability(!!st.ultralytics, st.detail || '');
      }).catch(() => applyTrainAvailability(false, ''));
    }
  });
  EventsOn('roitraining:deps:progress', line => {
    const log = el('#rt-train-log');
    log.textContent += line + '\n';
    log.scrollTop = log.scrollHeight;
  });
  EventsOn('roitraining:deps:done', payload => {
    el('#rt-install-deps').disabled = false;
    updateBootstrapEnabled();
    if (payload.error) {
      uiError('Install failed: ' + payload.error, el('#rt-train-status'));
      CheckRoiTrainingStatus().then(st => {
        applyTrainAvailability(!!st.ultralytics, st.detail || '');
      }).catch(() => applyTrainAvailability(false, ''));
      return;
    }
    const st = payload.status || {};
    applyTrainAvailability(!!st.ultralytics, st.detail || 'Dependencies installed');
    const bootstrapOk = !!st.opencv;
    el('#rt-train-status').textContent = bootstrapOk
      ? 'Dependencies OK — bootstrap (CSRT) and training ready.'
      : 'Ultralytics OK, but OpenCV trackers still missing — re-run Install or pip install opencv-contrib-python.';
    uiInfo(bootstrapOk
      ? 'AI training dependencies installed (CSRT restored).'
      : 'AI train deps installed; fix OpenCV (opencv-contrib-python) for Use for training.');
  });

  ListRoiTrainingDevices().then(devices => {
    if (!Array.isArray(devices) || !devices.length) return;
    const sel = el('#rt-device');
    const current = sel.value || 'auto';
    sel.innerHTML = devices.map(d => {
      const mark = d.available ? '' : ' — not detected';
      return `<option value="${d.id}">${d.label}${mark}</option>`;
    }).join('');
    if ([...sel.options].some(o => o.value === current)) sel.value = current;
    else sel.value = 'auto';
    const avail = devices.filter(d => d.available && d.id !== 'auto').map(d => d.id);
    el('#rt-device-status').textContent = avail.length
      ? ('Detected: ' + avail.join(', '))
      : 'No GPU backend detected — Auto falls back to CPU.';
  }).catch(() => {
    el('#rt-device-status').textContent = '';
  });

  function syncRoiTrainSampleKnobLabels() {
    const every = el('#rt-sample-every');
    const everyLab = el('#rt-sample-every-val');
    if (every && everyLab) everyLab.textContent = String(Math.round(Number(every.value) || 12));
    const scale = el('#rt-box-scale');
    const scaleLab = el('#rt-box-scale-val');
    if (scale && scaleLab) {
      const n = Number(scale.value);
      scaleLab.textContent = Number.isFinite(n) ? (Math.round(n * 100) / 100).toString() : '1';
    }
  }
  syncRoiTrainSampleKnobLabels();
  el('#rt-sample-every')?.addEventListener('input', syncRoiTrainSampleKnobLabels);
  el('#rt-sample-every')?.addEventListener('change', e => {
    syncRoiTrainSampleKnobLabels();
    const n = Math.round(Number(e.target.value) || 12);
    saveSetting('generator.roi_sample_every', n);
  });
  el('#rt-box-scale')?.addEventListener('input', syncRoiTrainSampleKnobLabels);
  el('#rt-box-scale')?.addEventListener('change', e => {
    syncRoiTrainSampleKnobLabels();
    const n = Number(e.target.value);
    saveSetting('generator.roi_box_scale', Number.isFinite(n) ? n : 1.0);
  });

  renderMarkFields();
  el('#rt-class-list').innerHTML = CLASS_PRESETS.map(n => `<option value="${n}"></option>`).join('');
}
