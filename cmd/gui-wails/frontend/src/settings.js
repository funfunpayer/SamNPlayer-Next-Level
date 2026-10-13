import { showEULA, showThirdPartyNotices, renderDonateButtons, renderAbout } from './legal.js';
import { GetSettings, SetSetting, PickReportPath, ReportSummary, ReportExists, GetHardwareInfo, GetCacheInfo, ClearCache, TrainQualityModel, QualityModelInfo, OpenLogFolder, CheckAIRoiAvailable, CheckAIServerAvailable, CheckAISetup, InstallAISetup, CurrentVersion, CheckForUpdate, CheckForPatch, ApplyUpdate, ApplyPatch, GetRuntimeHealth, EnsureVideoTools, DeleteSceneMapLearningData, InstallPluginPack, OpenPluginsFolder, PluginsDir, ListInstalledPlugins } from '../wailsjs/go/main/App';
import { EventsOn } from '../wailsjs/runtime/runtime';
import { uiError, uiInfo } from './notify.js';
import { openHandbook } from './handbook.js';
import { wireDataHelp } from './help.js';
import { releaseTag, releaseNotesPreview, formatUpdateError } from './update_ui.js';

// new URL(…, import.meta.url): Vite copies the file into the build. A plain
// "/src/…" string works in dev but is missing in the packaged app.
const LOGO_URL = new URL('./assets/images/logo-universal.png', import.meta.url).href;
const PARTNER_URL = new URL('./assets/images/partner.svg', import.meta.url).href;

let cachedSettings = null;
let cachedPromise = null;

// getSettingsCache wird von anderen Modulen (playback.js) genutzt, um beim
// Start die gespeicherten Standardwerte zu übernehmen, ohne dass jedes Modul
// selbst GetSettings() aufrufen und auf die Reihenfolge achten muss.
export function getSettingsCache() {
  if (cachedSettings) return Promise.resolve(cachedSettings);
  if (!cachedPromise) cachedPromise = GetSettings().then(s => { cachedSettings = s; return s; });
  return cachedPromise;
}

export function saveSetting(key, value) {
  if (cachedSettings) {
    const map = {
      'update.check_on_startup': 'updateCheckOnStartup',
      'device.connect_test': 'deviceConnectTest',
      'log.level': 'logLevel',
      'playback.mock': 'playbackMock',
      'playback.sync_mode': 'playbackSync',
      'playback.tick_ms': 'playbackTickMs',
      'playback.max_speed': 'playbackMaxSpeed',
      'playback.smoothing': 'playbackSmoothing',
      'playback.soft_start_ms': 'playbackSoftStartMs',
      'playback.effect_presets': 'playbackEffectPresets',
      'playback.extended_o_enabled': 'playbackEOEnabled',
      'playback.extended_o_min': 'playbackEOMin',
      'playback.extended_o_hold_seconds': 'playbackEOHoldS',
      'playback.extended_o_restore_ms': 'playbackEORestoreMs',
      'playback.video_play_autostart': 'playbackVideoPlayAutostart',
      'playback.honor_inverted': 'playbackHonorInverted',
      'playback.audio_vibration': 'playbackAudioVibration',
      'playback.trajectory_overlay': 'playbackTrajectoryOverlay',
      'playback.cap_intensity': 'playbackCapIntensity',
      'playback.speed_highlights': 'playbackSpeedHL',
      'playback.speed_highlight_thresh': 'playbackSpeedHLThresh',
      'playback.scale_factor': 'playbackScaleFactor',
      'playback.scale_soft_edges': 'playbackScaleSoftEdges',
      'playback.fps_snap': 'playbackFpsSnap',
      'playback.bpm_grid': 'playbackBpmGrid',
      'playback.omarker_intensity': 'playbackOMarkerIntensity',
      'playback.feel_heatbands': 'playbackFeelHeatbands',
      'playback.feel_heatbands_opacity': 'playbackFeelHeatbandsOpacity',
      'playback.contact_intensity': 'playbackContactIntensity',
      'training.mock': 'trainingMock',
      'training.technique': 'trainingTechnique',
      'training.channel': 'trainingChannel',
      'training.cycles': 'trainingCycles',
      'training.ramp_up_ms': 'trainingRampUpMs',
      'training.hold_ms': 'trainingHoldMs',
      'training.rest_ms': 'trainingRestMs',
      'training.peak_intensity': 'trainingPeakIntensity',
      'training.plateau_fraction': 'trainingPlateauFraction',
      'training.progression_per_cycle': 'trainingProgressionPerCycle',
      'generator.aiRoiModelPath': 'aiRoiModelPath',
      'generator.aiPreferredClasses': 'aiPreferredClasses',
      'generator.aiBaseUrl': 'aiBaseUrl',
      'generator.roiTrainingDatasetDir': 'roiDatasetDir',
      'generator.collectLearningData': 'collectLearningData',
      'generator.applyAISetupAutomatically': 'applyAISetupAutomatically',
      'generator.contact_vibration_span': 'genContactVibrationSpan',
      'generator.contact_vibration_curve': 'genContactVibrationCurve',
      'generator.smooth_window': 'genSmoothWindow',
      'generator.min_peak_distance_ms': 'genMinPeakDistanceMs',
      'generator.peak_prominence': 'genPeakProminence',
      'generator.rdp_tolerance': 'genRDPTolerance',
      'generator.max_speed': 'genMaxSpeed',
      'generator.adaptive_keyframe': 'genAdaptiveKeyframe',
      'generator.roi_sample_every': 'roiSampleEvery',
      'generator.roi_box_scale': 'roiBoxScale',
    };
    const field = map[key];
    if (field) cachedSettings[field] = value;
  }
  return SetSetting(key, value);
}

export function initSettings(root) {
  root.innerHTML = `
    <h2>Settings</h2>
    <div class="row" style="align-items:center; margin-bottom:14px;">
      <button type="button" id="st-open-handbook" class="primary"
        data-help="Opens the in-app user handbook: Create, Play, gaps, AI path, troubleshooting.">Open user handbook</button>
      <span class="hint" style="margin:0">FAQ + how to use every Everyday control. Also on Create.</span>
    </div>
    <div class="checkbox-row">
      <input type="checkbox" id="st-update-check" />
      <label for="st-update-check">Check for updates on startup</label>
    </div>
    <div class="checkbox-row">
      <input type="checkbox" id="st-connect-test" />
      <label for="st-connect-test">On connect, run a short connection test (brief vib/suction)</label>
    </div>
    <p class="hint" style="margin-top:0">Off by default. When on: after a successful connect, a short pulse confirms that commands arrive.</p>

    <h3>Play</h3>
    <div class="checkbox-row">
      <input type="checkbox" id="st-honor-inverted" />
      <label for="st-honor-inverted"
        data-help="Some .funscript files (e.g. from OpenFunscripter) carry &quot;inverted&quot;: true. On: Play flips those files (100 − position) as the format intends. Off: they play exactly as the points are written — use this if such a file feels upside down. Review → Invert always flips what you currently see and removes the flag.">Honor "inverted" flag in .funscript files</label>
    </div>
    <p class="hint" style="margin-top:0">On by default (same as since v0.5.44). Changing this reloads the open script.</p>
    <div class="checkbox-row">
      <input type="checkbox" id="st-audio-vibration" />
      <label for="st-audio-vibration"
        data-help="On: during Play the vibration follows the loudness of the video's sound (music, voices, beats) instead of the script. Suction still follows the script. Needs a video with a sound track; without one, Play keeps the script vibration and says so in the log. Your files are not changed.">Vibration from video audio only</label>
    </div>
    <p class="hint" style="margin-top:0">Off by default. Takes effect at the next Play start; the first start may take a few seconds while the sound is read.</p>

    <details id="st-need-details" class="st-opt-panel">
      <summary>What you need — portable Play + Create, no extra install</summary>
      <ul class="hint" style="margin:8px 0 4px; padding-left:1.2em; line-height:1.55;">
        <li><b>Play + Create (default)</b> — use the <b>portable</b> download: app + ffmpeg in one folder. No extra install.</li>
        <li><b>Video tools missing?</b> Settings → Install video tools (one click), or re-download portable.</li>
        <li><b>Python</b> — only for the classic Python generator path and <b>AI Train</b>. The Go Create path does not need it.</li>
        <li data-suite><b>AI Train</b> (optional) — Python + “Install dependencies” in the AI Train tab (downloads ultralytics/torch; large). Skip if you only Play/Create.</li>
      </ul>
    </details>

    <div data-donate-section>
      <h3>Support SamNPlayer</h3>
      <p class="hint">SamNPlayer is free and stays free. Your donation keeps the project alive and makes
        new features possible — every contribution goes into further development. Donations are
        voluntary gifts: everyone gets the same app, with or without one.</p>
      <div class="row" id="st-donate-buttons" style="align-items:center; gap:8px;"></div>
    </div>

    <h3>Licence</h3>
    <p class="hint">Freeware for private, non-commercial use — not open source. No licence key needed.</p>
    <div class="row" style="align-items:center;">
      <button id="st-show-eula" type="button" class="secondary">Show licence agreement (EULA)</button>
      <button id="st-show-notices" type="button" class="secondary">Third-party notices</button>
    </div>


    <details id="st-plugins-details" class="st-opt-panel">
      <summary>Plugins — optional drop-folder packs</summary>
      <p class="hint" style="margin:8px 0 6px;">Drop-folder packs with <code>samn-plugin.json</code>. Open the folder or
        <b>Install pack…</b> to copy one in. Virtual Person product is <b>cancelled / out of scope</b>
        (not deferred) — Everyday Create/Play unchanged. See <code>docs/PLUGIN_SYSTEM.md</code>.</p>
      <p class="hint" id="st-plugins-path" style="margin-top:0">Plugins folder: …</p>
      <p class="hint" id="st-plugins-status" style="margin-top:0">…</p>
      <div class="row" style="align-items:center; margin-top:6px;">
        <button id="st-plugins-install" type="button" class="primary">Install pack…</button>
        <button id="st-plugins-open-folder" type="button">Open Plugins folder</button>
        <button id="st-plugins-refresh" type="button">Refresh</button>
      </div>
    </details>

    <h3>Runtime &amp; updates</h3>
    <div class="row" style="align-items:center;">
      <button id="st-runtime-check" type="button">Check folders &amp; dependencies</button>
      <button id="st-install-ffmpeg" type="button"
        data-help="Downloads a static ffmpeg into your SamNPlayer tools folder when missing. Prefer the portable release (ffmpeg already next to the app).">Install video tools</button>
      <span class="hint" id="st-runtime-status" style="margin:0"></span>
    </div>
    <div class="row" style="align-items:center;">
      <button id="st-update-now" type="button">Check for updates now</button>
      <button id="st-update-apply" type="button" class="primary" style="display:none;">Download &amp; restart</button>
      <span class="hint" id="st-update-status" style="margin:0"></span>
    </div>
    <details id="st-logs-details" class="st-opt-panel">
      <summary>Logs</summary>
      <div class="field-row" style="margin-top:8px;"><label>Log level</label>
        <select id="st-log-level">
          <option value="debug">debug</option>
          <option value="info">info</option>
          <option value="warn">warn</option>
          <option value="error">error</option>
        </select>
      </div>
      <div class="row">
        <span class="path-label" id="st-log-path"></span>
        <button id="st-open-log">Open log folder</button>
      </div>
    </details>

    <details id="st-optional-ai" class="st-opt-panel">
      <summary>Optional AI &amp; learning — Everyday Create/Play need none of this</summary>
      <p class="hint" style="margin:8px 0 10px;">
        Local models, teachers, and learning exports. Defaults stay off. Go CSRT Create is unchanged.
      </p>

    <h3>AI region detection</h3>
    <p class="hint">Local ONNX as an alternative to classical tip find. No model ships — leave empty for the default folder.</p>
    <div class="row">
      <input type="text" id="st-ai-roi-path" placeholder="(default folder)" style="flex:1;" />
      <button id="st-ai-roi-check">Check availability</button>
      <button id="st-open-ai-train" type="button" data-suite
        data-help="Switches to the AI Train tab (label tip ROIs / train ONNX; also Train Go profile model).">Open AI training</button>
    </div>
    <div class="field-row" style="margin-top:8px">
      <label for="st-ai-pref-classes"
        data-help="Comma-separated English body-part IDs (face,mouth,breasts,nipples,hand_1,hand_2,penis,glans,vagina). Empty = all classes from the model. Names need classes.json next to the .onnx.">Preferred classes</label>
      <input type="text" id="st-ai-pref-classes" placeholder="face,mouth,breasts,nipples,hand_1,hand_2,penis,glans,vagina" style="flex:1;" />
    </div>
    <p class="hint" id="st-ai-roi-status" style="margin-top:0"></p>

    <div data-suite>
    <h3>Scene map learning</h3>
    <p class="hint">Local JSON/JSONL under the ROI dataset. Default <b>off</b>. Never uploads; never writes YOLO train folders.</p>
    <div class="checkbox-row">
      <input type="checkbox" id="st-collect-learning" />
      <label for="st-collect-learning"
        data-help="Required before Export for learning. Off by default (Owner).">Collect learning data</label>
    </div>
    <div class="row" style="align-items:center; margin-top:6px;">
      <button id="st-delete-learning" type="button"
        data-help="Deletes only scene_map_learning under the ROI dataset folder. Hand YOLO samples stay.">Delete learning data</button>
      <span class="hint" id="st-learning-status" style="margin:0"></span>
    </div>
    </div>

    <h3>Scene proposals — Apply AI setup</h3>
    <p class="hint">Soft-fill empty Tip / contact from <code>.scene.json</code>. Default <b>off</b> — Everyday stays manual Apply.</p>
    <div class="checkbox-row">
      <input type="checkbox" id="st-apply-ai-setup" />
      <label for="st-apply-ai-setup"
        data-help="Opt-in (Owner 28 Sep). Soft-load .scene.json → fill empty Tip and contact partner. Never with Tf/Tj distance as curve source. Everyday CSRT tip path unchanged.">Apply AI setup automatically</label>
    </div>

    <h3>AI server (profile / quality)</h3>
    <p class="hint">Local Colibri / OpenAI-compatible server for Suggest profile fallback. Empty = <code>http://127.0.0.1:8080</code>.</p>
    <div class="row">
      <input type="text" id="st-ai-base-url" placeholder="http://127.0.0.1:8080" style="flex:1;" />
      <button id="st-ai-server-check" type="button"
        data-help="GET /v1/models on the URL above (or the default). Does not change Everyday Create.">Test AI server</button>
    </div>
    <p class="hint" id="st-ai-server-status" style="margin-top:0"></p>

    <h3>Local AI setup</h3>
    <p class="hint">GPU / packages / train venv / Ollama. Never touches Everyday CSRT OpenCV.</p>
    <div class="row" style="flex-wrap:wrap;gap:8px;">
      <button id="st-ai-setup-check" type="button"
        data-help="Runs ai_setup.py check — changes nothing.">Check AI setup</button>
      <button id="st-ai-setup-teachers" type="button" class="secondary"
        data-help="Install onnxruntime(+gpu) and NudeNet for teachers.">Install teachers</button>
      <button id="st-ai-setup-train" type="button" class="secondary"
        data-help="Create/update the separate train venv (PyTorch + rfdetr).">Install train</button>
      <button id="st-ai-setup-models" type="button" class="secondary"
        data-help="Pull vision models that fit this GPU (Ollama).">Install models</button>
    </div>
    <pre id="st-ai-setup-out" class="hint" style="white-space:pre-wrap; margin-top:6px; max-height:14em; overflow:auto;"></pre>
    </details>

    <details id="st-diagnostics-details" class="st-opt-panel">
      <summary>Hardware · Cache · Generator metrics</summary>
      <h3>Hardware</h3>
      <p class="hint">Which acceleration the generator can actually use. Having an
        NVIDIA GPU does not mean it is used — typical OpenCV pip packages are built
        without CUDA.</p>
      <div class="row"><button id="st-hardware">Check hardware</button></div>
      <pre id="st-hardware-out" class="hint" style="white-space:pre-wrap; margin-top:6px;"></pre>

      <h3>Cache</h3>
      <p class="hint">The generator stores tracking results so a re-run with different
        settings does not re-decode the whole video — about 36× faster. The cache grows
        with each video.</p>
      <div class="row">
        <span class="path-label" id="st-cache-info">…</span>
        <button id="st-cache-clear">Clear now</button>
      </div>
      <div class="checkbox-row">
        <input type="checkbox" id="st-cache-exit" />
        <label for="st-cache-exit">Clear automatically on quit</label>
      </div>

      <h3>Generator metrics</h3>
      <p class="hint">Appends a line of metrics for every Create run (motion amplitude,
        spectral concentration, tracker loss, runtime). Together with your judgments in
        the Generator tab, this is the basis for tuning quality scoring on real material —
        so far it relies on synthetic test videos. Leave empty to disable recording.</p>
      <div class="row">
        <input type="text" id="st-report-path" placeholder="(no recording)"
               style="flex:1;" />
        <button id="st-pick-report">Choose…</button>
        <button id="st-default-report">Default</button>
      </div>
      <p class="hint" id="st-report-status" style="margin-top:2px;"></p>
      <div class="row">
        <button id="st-report-summary">Show summary</button>
        <button id="st-model-info">Show model</button>
        <button id="st-model-train" class="primary" data-suite>Learn from judgments</button>
      </div>
      <p class="hint">Quality scoring uses thresholds set on synthetic test videos.
        With enough of your own judgments, a model can be learned instead. It is only
        adopted if it beats the current rules in cross-validation — otherwise nothing
        changes.</p>
      <pre id="st-report-out" class="hint" style="white-space:pre-wrap; margin-top:6px;"></pre>
    </details>
    <section id="st-about" class="st-about">
      <h3>About SamNPlayer</h3>
      <div class="st-about-head">
        <img src="${LOGO_URL}" alt="SamNPlayer logo" class="st-about-logo" />
        <div>
          <div class="st-about-name">Sam<span class="wm-n">N</span>Player</div>
          <div class="hint" id="st-about-version" style="margin:2px 0 0;">…</div>
        </div>
      </div>
      <p class="hint">Free player and script generator for SVAKOM Sam Neo 2 and Funscript devices.
        Made with care — supported by donations.</p>
      <figure class="st-about-partner">
        <img src="${PARTNER_URL}" alt="SamNPlayer and the Sex for him website — partners" />
        <figcaption class="hint" id="st-about-partner-text">…</figcaption>
      </figure>
      <div class="row" style="align-items:center;">
        <button id="st-about-partner" type="button" class="secondary" disabled>Visit website</button>
      </div>
    </section>
  `;

  const el = id => root.querySelector(id);

  wireDataHelp(root);
  el('#st-open-handbook')?.addEventListener('click', () => openHandbook());

  // Ohne das war unsichtbar, ob unter dem eingestellten Pfad schon Messwerte
  // stehen - "Show summary" beantwortete das zwar auch, aber erst
  // nach einem Klick und mit einer errorsmeldung statt eines einfachen
  // Hinweises, wenn (noch) nichts drin ist.
  async function updateReportStatus() {
    const status = el('#st-report-status');
    const path = el('#st-report-path').value.trim();
    if (!path) {
      status.textContent = '';
      return;
    }
    try {
      status.textContent = (await ReportExists())
        ? '✓ Already contains metrics.'
        : 'No metrics recorded yet (created on the next generator run).';
    } catch (err) {
      status.textContent = '';
    }
  }

  getSettingsCache().then(s => {
    el('#st-update-check').checked = s.updateCheckOnStartup;
    el('#st-connect-test').checked = !!s.deviceConnectTest;
    if (el('#st-honor-inverted')) {
      el('#st-honor-inverted').checked = s.playbackHonorInverted !== false;
    }
    if (el('#st-audio-vibration')) {
      el('#st-audio-vibration').checked = !!s.playbackAudioVibration;
    }
    el('#st-log-level').value = s.logLevel;
    el('#st-log-path').textContent = s.logPath || '(no log file written yet)';
    el('#st-report-path').value = s.reportPath || '';
    el('#st-cache-exit').checked = !!s.clearCacheOnExit;
    el('#st-report-path').dataset.default = s.defaultReportPath || '';
    el('#st-ai-roi-path').value = s.aiRoiModelPath || '';
    el('#st-ai-pref-classes').value = s.aiPreferredClasses || '';
    el('#st-ai-base-url').value = s.aiBaseUrl || '';
    el('#st-collect-learning').checked = !!s.collectLearningData;
    if (el('#st-apply-ai-setup')) {
      el('#st-apply-ai-setup').checked = !!s.applyAISetupAutomatically;
    }
    updateReportStatus();
    renderDonateButtons(el('#st-donate-buttons'));
    renderAbout(root);
    refreshPluginsStatus();
  });

  async function refreshPluginsStatus() {
    const box = el('#st-plugins-status');
    const pathBox = el('#st-plugins-path');
    if (!box) return;
    try {
      if (pathBox) {
        try {
          const dir = await PluginsDir();
          pathBox.textContent = 'Plugins folder: ' + (dir || '(unavailable)');
        } catch (_) {
          pathBox.textContent = 'Plugins folder: (unavailable)';
        }
      }
      const packs = await ListInstalledPlugins();
      const list = Array.isArray(packs) ? packs : [];
      if (list.length === 0) {
        box.textContent = 'No packs installed.';
      } else {
        box.textContent = list.map((p) => {
          const m = p.manifest || p.Manifest || {};
          return `${m.name || m.id || '?'} v${m.version || '?'}`;
        }).join(' · ');
      }
    } catch (err) {
      box.textContent = 'Plugins status failed: ' + err;
    }
  }

  el('#st-plugins-refresh')?.addEventListener('click', () => refreshPluginsStatus());
  el('#st-plugins-open-folder')?.addEventListener('click', async () => {
    const box = el('#st-plugins-status');
    try {
      await OpenPluginsFolder();
      await refreshPluginsStatus();
    } catch (err) {
      uiError('Open Plugins folder: ' + err, box);
    }
  });
  el('#st-plugins-install')?.addEventListener('click', async () => {
    const box = el('#st-plugins-status');
    try {
      const pack = await InstallPluginPack();
      const m = pack && (pack.manifest || pack.Manifest);
      if (m && (m.id || m.name)) {
        uiInfo(`Installed ${m.name || m.id} v${m.version || '?'}.`, box);
      } else {
        uiInfo('Install cancelled or no pack selected.', box);
      }
      await refreshPluginsStatus();
    } catch (err) {
      uiError('Install pack: ' + err, box);
      await refreshPluginsStatus();
    }
  });

  el('#st-show-eula')?.addEventListener('click', () => showEULA());
  el('#st-show-notices')?.addEventListener('click', () => showThirdPartyNotices());

  el('#st-update-check').addEventListener('change', e => saveSetting('update.check_on_startup', e.target.checked));
  el('#st-connect-test').addEventListener('change', e => saveSetting('device.connect_test', e.target.checked));
  el('#st-honor-inverted')?.addEventListener('change', e =>
    saveSetting('playback.honor_inverted', e.target.checked));
  el('#st-audio-vibration')?.addEventListener('change', e => {
    saveSetting('playback.audio_vibration', e.target.checked);
    const pb = document.querySelector('#pb-audio-vibration');
    if (pb && pb.checked !== e.target.checked) pb.checked = e.target.checked;
  });
  el('#st-runtime-check').addEventListener('click', async () => {
    const status = el('#st-runtime-status');
    status.textContent = 'Checking…';
    try {
      const h = await GetRuntimeHealth();
      const missing = (h.deps || []).filter(d => !d.found).map(d => d.label);
      const created = (h.dirsCreated || []).length;
      const parts = [];
      if (created) parts.push(`${created} folder(s) created`);
      if (missing.length) parts.push('missing: ' + missing.join(', '));
      else parts.push('Dependencies OK');
      if (h.resources) {
        parts.push(`${h.resources.goos}/${h.resources.goarch} · ${h.resources.numCPU} CPU · GOMAXPROCS ${h.resources.goMaxProcs}`);
      }
      status.textContent = (h.ok ? '✓ ' : '⚠ ') + parts.join(' · ');
    } catch (err) {
      status.textContent = 'Check failed: ' + err;
    }
  });
  EventsOn('runtime:tools', line => {
    const status = el('#st-runtime-status');
    if (status) status.textContent = String(line);
  });
  el('#st-install-ffmpeg').addEventListener('click', async () => {
    const status = el('#st-runtime-status');
    const btn = el('#st-install-ffmpeg');
    btn.disabled = true;
    status.textContent = 'Installing video tools (ffmpeg)…';
    try {
      await EnsureVideoTools();
      const h = await GetRuntimeHealth();
      const ff = (h.deps || []).find(d => d.id === 'ffmpeg');
      if (ff && ff.found) {
        uiInfo('Video tools ready: ' + (ff.path || 'ffmpeg'), status);
        status.textContent = '✓ ffmpeg: ' + (ff.path || 'ok');
      } else {
        status.textContent = 'Install finished but ffmpeg still missing — use the portable release.';
      }
    } catch (err) {
      uiError('Install video tools: ' + err, status);
    } finally {
      btn.disabled = false;
    }
  });
  el('#st-update-now').addEventListener('click', async () => {
    const status = el('#st-update-status');
    const btn = el('#st-update-now');
    const apply = el('#st-update-apply');
    btn.disabled = true;
    if (apply) apply.style.display = 'none';
    status.textContent = 'Checking…';
    try {
      const version = await CurrentVersion();
      const res = await CheckForUpdate();
      if (res.error) {
        uiError('Update check: ' + formatUpdateError(res.error), status);
      } else if (res.available) {
        const tag = releaseTag(res.release);
        const notes = releaseNotesPreview(res.release);
        status.textContent = notes
          ? `Version ${tag} available (current: ${version}). ${notes}`
          : `Version ${tag} available (current: ${version}).`;
        if (apply) {
          apply.style.display = '';
          apply.onclick = async () => {
            apply.disabled = true;
            apply.textContent = 'Downloading…';
            try {
              // Success path calls os.Exit — window closes. Reject = real failure.
              await ApplyUpdate();
            } catch (err) {
              uiError('Update failed: ' + formatUpdateError(err), status);
              apply.disabled = false;
              apply.textContent = 'Download & restart';
            }
          };
        }
      } else {
        // No full release → check additive patch channel (same UX).
        const pres = await CheckForPatch();
        if (pres.error) {
          uiError('Patch check: ' + formatUpdateError(pres.error), status);
        } else if (!pres.available) {
          status.textContent = `No update available (current: ${version}).`;
        } else {
          const id = pres.patchId || (pres.patch && (pres.patch.id || pres.patch.ID)) || '?';
          const notes = releaseNotesPreview(pres.patch);
          status.textContent = notes
            ? `Patch ${id} available (current: ${version}). ${notes}`
            : `Patch ${id} available (current: ${version}).`;
          if (apply) {
            apply.style.display = '';
            apply.onclick = async () => {
              apply.disabled = true;
              apply.textContent = 'Downloading…';
              try {
                await ApplyPatch();
              } catch (err) {
                uiError('Patch failed: ' + formatUpdateError(err), status);
                apply.disabled = false;
                apply.textContent = 'Download & restart';
              }
            };
          }
        }
      }
    } catch (err) {
      uiError('Update check: ' + formatUpdateError(err), status);
    } finally {
      btn.disabled = false;
    }
  });
  el('#st-log-level').addEventListener('change', e => saveSetting('log.level', e.target.value));
  el('#st-open-log').addEventListener('click', () => OpenLogFolder().catch(err => uiError('Log folder: ' + err)));

  el('#st-report-path').addEventListener('change', e =>
    saveSetting('generator.reportPath', e.target.value.trim()).then(updateReportStatus));

  el('#st-pick-report').addEventListener('click', async () => {
    try {
      const path = await PickReportPath();
      if (path) {
        el('#st-report-path').value = path;
        await saveSetting('generator.reportPath', path);
        updateReportStatus();
      }
    } catch (err) {
      uiError('Report path: ' + err);
    }
  });

  el('#st-default-report').addEventListener('click', async () => {
    const fallback = el('#st-report-path').dataset.default || '';
    el('#st-report-path').value = fallback;
    await saveSetting('generator.reportPath', fallback);
    updateReportStatus();
  });

  el('#st-ai-roi-path').addEventListener('change', e =>
    saveSetting('generator.aiRoiModelPath', e.target.value.trim()));

  el('#st-ai-pref-classes').addEventListener('change', e =>
    saveSetting('generator.aiPreferredClasses', e.target.value.trim()));

  el('#st-ai-base-url').addEventListener('change', e =>
    saveSetting('generator.aiBaseUrl', e.target.value.trim()));

  el('#st-ai-server-check')?.addEventListener('click', async () => {
    const status = el('#st-ai-server-status');
    if (status) status.textContent = 'Checking…';
    // Persist the field first so the Go probe reads the typed URL.
    try {
      await saveSetting('generator.aiBaseUrl', el('#st-ai-base-url').value.trim());
    } catch (_) { /* probe still uses last saved / empty = default */ }
    try {
      const available = await CheckAIServerAvailable();
      if (status) {
        status.textContent = available
          ? 'Reachable — Suggest profile can use the AI fallback; quality opinion available when enabled in the Generator.'
          : 'Not reachable — start coli serve (or your OpenAI-compatible server), then test again. Create still works without it.';
      }
    } catch (err) {
      if (status) status.textContent = 'Check failed: ' + err;
    }
  });

  function formatAISetupReport(r) {
    if (!r) return '(empty report)';
    const lines = [];
    const gpus = r.gpus || r.GPUs || [];
    if (gpus.length) {
      for (const g of gpus) {
        lines.push(`GPU: ${g.name || g.Name || '?'} (${g.vram_gb ?? g.VRAMGB ?? '?'} GB)`);
      }
    } else {
      lines.push('GPU: none reported');
    }
    const pkgs = r.packages || r.Packages || {};
    const pkgBits = Object.keys(pkgs).map((k) => `${k}=${pkgs[k] ? 'yes' : 'no'}`);
    if (pkgBits.length) lines.push('Packages: ' + pkgBits.join(', '));
    const servers = r.servers || r.Servers || {};
    for (const [name, s] of Object.entries(servers)) {
      const up = s.up ?? s.Up;
      const url = s.url || s.URL || '';
      const models = s.models || s.Models || [];
      lines.push(`Server ${name}: ${up ? 'up' : 'down'} ${url}${models.length ? ' [' + models.slice(0, 4).join(', ') + ']' : ''}`);
    }
    const steps = r.next_steps || r.NextSteps || [];
    if (steps.length) {
      lines.push('Next:');
      for (const s of steps) lines.push('  • ' + s);
    }
    return lines.join('\n');
  }

  el('#st-ai-setup-check')?.addEventListener('click', async () => {
    const out = el('#st-ai-setup-out');
    if (out) out.textContent = 'Checking…';
    try {
      const report = await CheckAISetup();
      if (out) out.textContent = formatAISetupReport(report);
    } catch (err) {
      if (out) out.textContent = 'Check failed: ' + err;
      uiError('Check AI setup: ' + err);
    }
  });

  function runAISetupInstall(profile) {
    const out = el('#st-ai-setup-out');
    if (out) out.textContent = `Installing ${profile}…\n`;
    InstallAISetup(profile).catch((err) => {
      if (out) out.textContent += 'Start failed: ' + err + '\n';
      uiError('Install AI setup: ' + err, out);
    });
  }
  el('#st-ai-setup-teachers')?.addEventListener('click', () => runAISetupInstall('teachers'));
  el('#st-ai-setup-train')?.addEventListener('click', () => runAISetupInstall('train'));
  el('#st-ai-setup-models')?.addEventListener('click', () => runAISetupInstall('models'));
  EventsOn('aisetup:progress', (line) => {
    const out = el('#st-ai-setup-out');
    if (out && line) out.textContent += String(line) + '\n';
  });
  EventsOn('aisetup:done', (payload) => {
    const out = el('#st-ai-setup-out');
    if (payload?.error) {
      if (out) out.textContent += 'Failed: ' + payload.error + '\n';
      uiError('AI setup install: ' + payload.error, out);
      return;
    }
    if (out) out.textContent += `Done (${payload?.profile || 'ok'}).\n`;
    uiInfo('AI setup install finished.', out);
  });

  el('#st-collect-learning')?.addEventListener('change', e =>
    saveSetting('generator.collectLearningData', e.target.checked));
  el('#st-apply-ai-setup')?.addEventListener('change', e =>
    saveSetting('generator.applyAISetupAutomatically', e.target.checked));

  el('#st-delete-learning')?.addEventListener('click', async () => {
    const status = el('#st-learning-status');
    if (status) status.textContent = 'Deleting…';
    try {
      await DeleteSceneMapLearningData();
      if (status) status.textContent = 'Deleted scene_map_learning (YOLO samples kept).';
      uiInfo('Scene map learning data deleted.', status);
    } catch (err) {
      if (status) status.textContent = '';
      uiError('Delete learning data: ' + err, status);
    }
  });

  el('#st-ai-roi-check').addEventListener('click', async () => {
    const status = el('#st-ai-roi-status');
    status.textContent = 'Checking…';
    try {
      const available = await CheckAIRoiAvailable();
      status.textContent = available
        ? 'Available — the Generator tab now offers smarter tip find.'
        : 'Not available — onnxruntime missing or no .onnx at '
          + '(specified or default) path.';
    } catch (err) {
      status.textContent = 'Check failed: ' + err;
    }
  });

  el('#st-open-ai-train')?.addEventListener('click', () => {
    const tab = document.querySelector('.tab-btn[data-tab="roi-training"]');
    if (tab) {
      tab.click();
      return;
    }
    const status = el('#st-ai-roi-status');
    if (status) {
      status.textContent = 'Open Tools → AI Train (label tip ROIs → train ONNX).';
    }
  });

  async function refreshCache() {
    try {
      const c = await GetCacheInfo();
      el('#st-cache-info').textContent =
        `${c.files} file(s), ${c.humanSize || '0 B'} — ${c.path}`;
    } catch (err) {
      el('#st-cache-info').textContent = 'unreadable: ' + err;
    }
  }
  refreshCache();

  el('#st-cache-clear').addEventListener('click', async () => {
    try {
      await ClearCache();
      await refreshCache();
    } catch (err) {
      uiError('Clear cache: ' + err, el('#st-cache-info'));
    }
  });

  el('#st-cache-exit').addEventListener('change', e =>
    saveSetting('cache.clearOnExit', e.target.checked));

  el('#st-hardware').addEventListener('click', async () => {
    const out = el('#st-hardware-out');
    out.textContent = 'Checking…';
    try {
      out.textContent = await GetHardwareInfo();
    } catch (err) {
      out.textContent = 'Could not determine: ' + err;
    }
  });

  el('#st-model-info').addEventListener('click', async () => {
    const out = el('#st-report-out');
    out.textContent = 'Querying model…';
    try {
      out.textContent = await QualityModelInfo();
    } catch (err) {
      out.textContent = 'Unavailable: ' + err;
    }
  });

  el('#st-model-train').addEventListener('click', async () => {
    const out = el('#st-report-out');
    out.textContent = 'Learning from ratings…';
    try {
      out.textContent = await TrainQualityModel();
    } catch (err) {
      out.textContent = 'Learning failed: ' + err;
    }
  });

  el('#st-report-summary').addEventListener('click', async () => {
    const out = el('#st-report-out');
    out.textContent = 'Reading values…';
    try {
      out.textContent = await ReportSummary();
    } catch (err) {
      out.textContent = 'No summary available: ' + err;
    }
  });
}
