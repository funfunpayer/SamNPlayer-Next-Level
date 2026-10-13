import { SubmitFeedback, PickVideoFile, PickContactPointsFile, PickSceneProposalsFile, LoadFirstFrame, LoadFrameAt, GenerateScript, PreviewPostprocess, PreviewContactVibration, CancelGenerate, CancelROIDetection, CancelContactPoints, CheckGeneratorDependencies, ScriptExistsForVideo, AutoDetectROI, DetectExpectedTipROI, SuggestROICandidates, ListAIDetections, CheckAIRoiAvailable, CheckAudioCheckAvailable, SuggestProfile, SuggestPipeline, LabelSceneWithProfile, ImproveGeneratedScript, GetScriptCurve, ScanSceneMap, SceneMapAvailable, LoadSceneMapForVideo, LoadSceneProposalAt, LoadSceneProposalsBesideVideo, ExportSceneMapLearning, SuggestExcludePriors, ReviewAutoContactCandidate, ImportContactCandidatesForVideo, GenerateContactPointsForVideo, AIScriptWriterStatus, DraftAIScript, ExportAIScriptImitation, KeepAIScriptDraft, GetScriptChapterMarks, SaveScriptChapterMarks, ProbeCreateVideo } from '../wailsjs/go/main/App';
import { EventsOn } from '../wailsjs/runtime/runtime';
import {
  CONTACT_CLASS_ORDER, TIP_CLASS_ORDER,
  CONTACT_VIBE_IDS, AI_TIP_CLASS_ORDER, SCENE_REGION_CLASS_ORDER,
  labelFor, normalizeClass, orderedCanonical, onlyCanonical,
} from './bodyparts.js';
import { mountBodyFigure } from './body_figure.js';
import { getSettingsCache, saveSetting } from './settings.js';
import { uiError, uiInfo, uiWarn } from './notify.js';
import { wireDataHelp } from './help.js';
import { openHandbook } from './handbook.js';
import { rememberNativeSize } from './roi_help.js';

export function initGenerator(root, playback) {
  root.classList.add('tab-create');
  root.innerHTML = `
    <header class="create-head">
      <h2>Create Emotion Script</h2>
      <p class="create-lede">From a quiet video — motion becomes feel you can play.</p>
      <div class="create-head-actions">
        <button type="button" id="gen-open-handbook" class="handbook-open-btn"
          data-help="Opens the in-app handbook: Create steps, gaps, AI export, Play map.">User handbook</button>
      </div>
    </header>
    <section class="gen-project-head" id="gen-project-head" aria-label="Project" hidden>
      <img class="gen-project-poster" id="gen-project-poster" alt="" hidden />
      <div class="gen-project-info">
        <h3 class="gen-project-name" id="gen-project-name"></h3>
        <p class="gen-project-meta" id="gen-project-meta"></p>
      </div>
    </section>
    <nav class="gen-steps" id="gen-steps" aria-label="Create workflow">
      <ol class="gen-steps-list">
        <li class="gen-step-item is-current" data-step="1"><span class="gen-step-num">1</span> Video</li>
        <li class="gen-step-item" data-step="2"><span class="gen-step-num">2</span> Tip &amp; contact</li>
        <li class="gen-step-item" data-step="3"><span class="gen-step-num">3</span> Feel</li>
        <li class="gen-step-item" data-step="4"><span class="gen-step-num">4</span> Create</li>
        <li class="gen-step-item" data-step="5"><span class="gen-step-num">5</span> Review</li>
      </ol>
    </nav>
    <p class="hint gen-step-prompt" id="gen-step-prompt" style="margin-top:0">
      Start gently — choose a video; we find the motion and shape your Emotion Script.
    </p>
    <div class="path-label" id="gen-status"></div>
    <div class="hint" id="gen-pipeline" style="margin-top:4px;"></div>

    <section class="gen-step-panel" id="gen-step-video" data-step="1">
      <h3 class="gen-step-title">1 · Video</h3>
      <div class="row">
        <button id="gen-choose" class="primary">Choose video…</button>
        <span class="path-label" id="gen-video-path">No video selected</span>
        <button id="gen-check-deps">Check dependencies</button>
      </div>
      <div class="row gen-use-player" id="gen-use-player-row" hidden>
        <button type="button" id="gen-use-player-video" class="secondary"
          data-help="Use the video that is linked in Play, so Create and Play work on the same clip.">Use Player video</button>
        <span class="hint" id="gen-use-player-name" style="margin:0;"></span>
      </div>
      <p class="hint" id="gen-empty-cta" style="margin:8px 0 0 0;">
        Start here: pick a short clip → Find tip → Create Emotion Script. No Python needed for Everyday Go CSRT.
      </p>
      <div id="gen-queue" class="gen-queue" hidden>
        <div class="row" style="align-items:center; flex-wrap:wrap; gap:8px; margin-top:8px;">
          <span class="hint" id="gen-queue-summary" style="margin:0;"></span>
          <button type="button" id="gen-queue-run" class="primary"
            data-help="Runs Everyday Create on each queued video in order (auto tip-find per clip). Progress/ETA covers the whole batch. Cancel stops the remaining queue.">Create queue</button>
          <button type="button" id="gen-queue-clear" class="secondary"
            data-help="Drop remaining queued videos; keep the current clip.">Clear queue</button>
        </div>
        <ol id="gen-queue-list" class="gen-queue-list" aria-label="Generate queue"></ol>
      </div>
    </section>

    <section class="gen-step-panel" id="gen-step-region" data-step="2" hidden>
      <h3 class="gen-step-title">2 · Tip &amp; contact</h3>
      <div class="gen-mark-snack" id="gen-mark-snack" aria-live="polite">
        <ol class="gen-mark-snack-steps">
          <li data-snack="tip" class="is-current">1 · Tip (stroke)</li>
          <li data-snack="partner">2 · Partner touch</li>
          <li data-snack="extra">3 · More (optional)</li>
        </ol>
        <p class="hint gen-mark-snack-hint" id="gen-mark-snack-hint">
          Like FunGen2: you do <b>not</b> paint a motion path. Tip = the small box CSRT follows for the stroke.
          Partner marks = where Contact vibration should feel — not the stroke.
        </p>
      </div>
      <div class="row gen-mark-primary-row" style="align-items:center;">
        <button id="gen-autoroi" class="primary" disabled
          data-help="Auto tip box for Everyday CSRT (not a motion path). Paint/drag only if Find missed. Optional AI when checked.">Find tip area</button>
        <button id="gen-candidates" type="button" disabled
          data-help="Shows ranked tip candidates when auto Find is unsure. Click = Tip. Shift-click = optional contact (mouth / nipple-breast) when Contact vibration is on.">Show other spots</button>
        <button id="gen-seed-suggest" type="button" disabled hidden
          data-help="Proposes Tip + optional contact area. Apply required.">Suggest Tip+2nd</button>
        <span class="hint" id="gen-seed-status" style="margin:0"></span>
      </div>
      <details class="gen-adv-nested gen-mark-disclose" id="gen-mark-ai-details">
        <summary>AI assist &amp; scene proposals</summary>
        <div class="row" style="align-items:center; flex-wrap:wrap; gap:8px; margin-top:6px;">
          <button id="gen-ai-detections" type="button" disabled
            data-help="Lists all AI Train class boxes on this frame (nipples, breasts, glans, …) — not tip-only. Click a box to set Tip; Shift-click = optional contact. Needs Settings → AI model.">Show all AI tags</button>
          <span class="checkbox-row" style="margin:0"><input type="checkbox" id="gen-ai-roi" disabled />
            <label for="gen-ai-roi" style="width:auto"
              data-help="Optional AI tip box suggestion — never writes the curve. Needs Settings → AI model.">Smarter tip find (optional)</label></span>
          <select id="gen-ai-target-class" disabled style="min-width:10em;">
            <option value="">Expected body point…</option>
          </select>
        </div>
        <div class="hint" id="gen-ai-target-status" style="margin:4px 0 6px 0;"></div>
        <div id="gen-scene-proposals" style="margin:8px 0 6px 0;padding:8px;border:1px solid rgba(255,255,255,0.08);">
          <p class="hint" style="margin:0 0 6px 0;">
            Scene proposals (<code>.scene.json</code>) — roles + scene type from teachers × motion.
            <b>Apply</b> sets Tip (primary); partner is proposal-only until you apply as contact
            (unless Settings → <b>Apply AI setup automatically</b> is on — still undoable).
          </p>
          <div id="gen-ai-applied" class="gen-ai-applied" hidden>
            <span id="gen-ai-applied-label" class="hint" style="margin:0;"></span>
            <button type="button" id="gen-ai-applied-undo" class="secondary">Undo AI apply</button>
          </div>
          <div class="row" style="align-items:center;gap:8px;flex-wrap:wrap;">
            <button type="button" class="secondary" id="gen-scene-proposals-load" disabled
              data-help="Choose a scene_roles.py JSON. Shows the proposal for the current Time seek.">Load scene proposals…</button>
            <button type="button" class="secondary" id="gen-scene-proposals-beside" disabled
              data-help="Load &lt;clip&gt;.scene.json beside this video if present.">Use beside video</button>
            <span class="hint" id="gen-scene-type-chip" style="margin:0;display:none;" aria-live="polite"></span>
            <span class="hint" id="gen-scene-proposals-status" style="margin:0;"></span>
          </div>
          <div id="gen-scene-proposals-actions" style="display:none;margin-top:6px;">
            <div class="row" style="align-items:center;gap:8px;flex-wrap:wrap;">
              <span class="hint" id="gen-scene-primary-label" style="margin:0;"></span>
              <button type="button" class="primary" id="gen-scene-apply-primary"
                data-help="Apply the proposed primary stroke target as Tip ROI (+ region class when canonical).">Apply as Tip</button>
              <span class="hint" id="gen-scene-partner-label" style="margin:0;"></span>
              <button type="button" class="secondary" id="gen-scene-apply-partner" hidden
                data-help="Optional: apply the contact partner as ROI2. Never automatic (TFTJ no silent ROI2).">Apply as contact</button>
              <button type="button" class="secondary" id="gen-scene-proposals-dismiss"
                data-help="Clear the loaded scene proposal overlay without changing Tip/ROI2.">Dismiss</button>
            </div>
          </div>
        </div>
      </details>
      <p class="hint" id="gen-autoroi-hint" style="margin:0 0 6px 0">Auto tip after load — drag the box anytime to refine.</p>

      <div class="row" style="align-items:center; margin:4px 0;">
        <label style="width:auto;" data-help="Seek past a black intro before marking the region.">Time (s)</label>
        <input type="number" id="gen-seek" value="0" min="0" step="0.5" style="width:5em;" disabled />
        <button id="gen-seek-btn" type="button" disabled>Frame</button>
        <button id="gen-seek-plus" type="button" disabled>+1s</button>
        <button id="gen-seek-plus5" type="button" disabled>+5s</button>
        <label class="checkbox-row" style="margin:0 0 0 8px;"
          data-help="0–100 stroke gauge over the preview. Moves with Time/Frame after Create (and after AI draft, before Keep). Turn off anytime.">
          <input type="checkbox" id="gen-pos-overlay-toggle" checked />
          0–100 on video
        </label>
      </div>
      <div id="roi-canvas-wrap">
        <canvas id="roi-canvas"></canvas>
        <div id="gen-pos-overlay" class="pos-gauge" hidden aria-hidden="true">
          <div class="pos-gauge-scale" aria-hidden="true">
            <span>100</span><span>50</span><span>0</span>
          </div>
          <div class="pos-gauge-track">
            <div class="pos-gauge-fill" id="gen-pos-fill"></div>
            <div class="pos-gauge-knob" id="gen-pos-knob"></div>
          </div>
          <div class="pos-gauge-value" id="gen-pos-value">—</div>
        </div>
      </div>
      <div class="path-label" id="gen-roi-label">No tip box yet (Find tip runs on Create)</div>
      <p class="hint" id="gen-pipeline-auto" style="margin:4px 0 8px 0;"></p>

      <div id="gen-contact-marks-wrap" style="margin-top:8px; padding-top:8px; border-top:1px solid rgba(255,255,255,0.08);">
        <p class="hint" style="margin:0 0 6px 0;">
          Optional touch points for Contact vibration — not the stroke. Skip anytime; Create still works.
        </p>
        <div class="row gen-mark-snack-actions" style="align-items:center; flex-wrap:wrap; gap:8px; margin:6px 0;">
          <label style="width:auto;" data-help="Optional tip label: Glans preferred, or whole Penis. Empty = any. Contact-vibe set only — not required for CSRT Create.">Tip (glans/penis)</label>
          <select id="gen-region-class" style="min-width:8em;">
            <option value="">(any)</option>
          </select>
          <label style="width:auto;" data-help="Main contact partner: Mouth or Nipple/breast. Defaults to Nipples. Optional — not a motion path.">Contact (mouth / nipple)</label>
          <select id="gen-region-class2" style="min-width:8em;">
            <option value="">(pick class)</option>
          </select>
        </div>
        <div class="row gen-mark-snack-actions" style="align-items:center; margin-top:6px; flex-wrap:wrap; gap:8px;">
          <button id="gen-roi2-toggle" type="button"
            data-help="Mark one touch point (gold): mouth or nipple/breast. Not a stroke path. Stroke vib still follows tip CSRT depth — the mark is for contact feel / later spatial vibe.">Mark contact area</button>
          <button id="gen-target-add" type="button"
            data-help="Add another touch point (magenta) — e.g. second nipple or mouth. Same contact-vibe set. With tip trajectory on, extras can follow unless Stay fixed is checked.">+ Another contact area</button>
          <label style="width:auto; margin:0;" data-help="Type for the next extra contact mark (mouth / nipple / breast).">Extra type</label>
          <select id="gen-target-class" style="min-width:7em;">
            <option value="">(any)</option>
          </select>
          <span class="hint" id="gen-roi2-hint" style="margin:0">Optional. Vib = stroke depth; gold mark = Contact feel.</span>
        </div>
        <div class="path-label" id="gen-roi2-label">No contact area marked</div>
        <div class="path-label" id="gen-extras-label" style="display:none;"></div>
        <details class="gen-adv-nested gen-mark-disclose" id="gen-mark-options-details">
          <summary>Mark options (Ignore, Partner fixed, body map)</summary>
          <div class="row" style="align-items:center; flex-wrap:wrap; gap:8px; margin-top:6px;">
            <label class="checkbox-row" style="margin:0;"
              data-help="Partner fixed (pixels): keep the gold box where you drew it — do not track it. Use when nipple/mouth barely move and only the tip/camera moves. Off (default with Contact vib) = track that area so camera pans stay in sync.">
              <input type="checkbox" id="gen-roi2-fixed" /> Partner fixed (pixels)
            </label>
            <label class="checkbox-row" style="margin:0;"
              data-help="Stay fixed (extra): keep the next magenta box where you drew it. Off (default when tip trajectory is recorded) = follow partner on Play. Use when the second contact barely moves.">
              <input type="checkbox" id="gen-extra-contact-sticky" /> Stay fixed
            </label>
            <button id="gen-extras-clear" type="button"
              data-help="Clear extra contact areas and soft masks (keeps tip + first contact mark).">Clear extras</button>
            <button id="gen-mask-add" type="button"
              data-help="Ignore region (black): exclude a wrong latch (knees, background). Same as Scene map → Ignore. Does not drive the stroke; use when auto tip/heatmap fails onto the wrong part.">+ Ignore region (black)</button>
          </div>
          <div id="gen-selected-mark" class="gen-selected-mark" hidden>
            <span id="gen-selected-mark-label" class="hint" style="margin:0;"></span>
            <button type="button" id="gen-selected-mark-delete" class="secondary"
              data-help="Removes the mark selected on the preview (Tip / contact / Ignore / Scene map).">Delete selected</button>
            <span class="hint" style="margin:0;">Click body map to set class · click empty preview to deselect</span>
          </div>
          <div id="gen-body-figure" class="body-figure-host gen-body-figure-compact"
            aria-label="Body map — contact vibe: mouth, nipple/breast, tip"></div>
          <p class="hint" style="margin:4px 0 0 0;">
            <b>Click a painted mark</b>, then the body map (mouth · nipple/breast · tip only).
          </p>
        </details>
      </div>
    </section>

    <section class="gen-step-panel" id="gen-step-motion" data-step="3" hidden>
      <h3 class="gen-step-title">3 · How it feels</h3>
      <div class="row" style="align-items:center;">
        <label style="width:auto;" data-help="Normal = everyday feel. Soft = gentler. Autotune = extra cleanup for noisy clips.">Style</label>
        <select id="gen-profile">
          <option value="standard">Normal</option>
          <option value="weich">Soft</option>
          <option value="autotune">Autotune</option>
        </select>
      </div>
      <p class="hint" id="gen-profile-hint" style="margin:0 0 10px 0;">
        Everyday: auto tip → Create (no motion-path marking). Contact vibration (below) adds feel — on by default.
        Optional contact marks in Step 2 are touch points only. Use Advanced → Scene map → <b>Ignore (black)</b>
        only when detections latch onto the wrong part (knees etc.).
      </p>
      <div id="gen-contact-vibration-wrap">
        <div class="checkbox-row" id="gen-contact-vibration-row">
          <input type="checkbox" id="gen-contact-vibration" checked />
          <label for="gen-contact-vibration"
            data-help="Extra vibration on deep strokes (high position / stroke depth). On by default. When on, Step 2 shows optional touch-point marks (mouth · nipple/breast · tip). Not a stroke path — Create works without them. Marks store where touch should feel.">Contact vibration (on by default)</label>
        </div>
        <div id="gen-contact-vibration-opts" style="display:none; margin:4px 0 10px 22px;">
          <div class="field-row" style="align-items:center;">
            <label style="width:auto;" data-help="Lower = engages earlier (wider contact window). Higher = deep only (near peak position). Default 0.75 = top quarter of the video signal.">Sensitivity</label>
            <input type="range" id="gen-contact-span" min="40" max="95" step="5" value="75" style="flex:1;" />
            <span class="hint" id="gen-contact-span-label" style="margin:0; min-width:7em;">deep only</span>
          </div>
          <div class="field-row" style="align-items:center;">
            <label style="width:auto;" data-help="linear = 1:1. soft = gentle onset (t²) — closer to contact feel. peak = stronger peak (√t). impulse = quiet mid-window, sharp near deep peaks (Advanced experiment).">Curve</label>
            <select id="gen-contact-curve">
              <option value="linear">Linear</option>
              <option value="soft" selected>Soft onset (contact-like)</option>
              <option value="peak">Stronger peak</option>
              <option value="impulse">Impulse (peaks only, experiment)</option>
            </select>
          </div>
          <details class="gen-mark-disclose" id="gen-contact-probe-details">
            <summary>Feel probe</summary>
            <div class="gen-contact-vib-probe" id="gen-contact-vib-probe" aria-live="polite">
              <div class="gen-contact-vib-badges" id="gen-contact-vib-badges">
                <span class="gen-cv-badge" id="gen-cv-badge-active">— active</span>
                <span class="gen-cv-badge" id="gen-cv-badge-peak">— peak</span>
              </div>
              <svg id="gen-contact-vib-svg" class="gen-contact-vib-svg" viewBox="0 0 320 72" preserveAspectRatio="none" aria-hidden="true">
                <polyline id="gen-contact-vib-stroke" class="gen-contact-vib-stroke" fill="none" stroke-width="1.4" points="" />
                <polyline id="gen-contact-vib-poly" class="gen-contact-vib-poly" fill="none" stroke-width="1.8" points="" />
                <g id="gen-contact-vib-peaks" class="gen-contact-vib-peaks"></g>
              </svg>
              <p class="hint" id="gen-contact-vib-preview" style="margin:4px 0 0 0;">
                Feel probe: change Sensitivity / Curve for live vib feedback (synthetic bounce — not your clip).
              </p>
            </div>
          </details>
        </div>
      </div>

      <details id="gen-power-user" style="margin:6px 0 8px 0;">
        <summary style="cursor:pointer;">Power-user: scene memory</summary>
        <div class="row" style="align-items:center; margin-top:8px;">
          <button id="gen-suggest-profile" disabled
            data-help="Compares the motion signature to saved scenes first, optionally to a local AI server. Suggestion only — nothing is applied automatically.">Suggest profile</button>
          <span class="hint" id="gen-suggest-status" style="margin:0"></span>
        </div>
        <div class="row" style="align-items:center;" data-suite>
          <input type="text" id="gen-scene-label" placeholder="Name for this scene (optional)" style="flex:1;" />
          <button id="gen-label-scene" disabled
            data-help="Saves the motion signature, this name and the Style currently selected above. The AI training tab can learn a local profile model from confirmed examples.">Remember scene + style</button>
        </div>
      </details>
    </section>

    <section class="gen-step-panel" id="gen-step-run" data-step="4" hidden>
      <h3 class="gen-step-title">4 · Create</h3>
      <details id="gen-advanced" class="gen-adv" style="margin:6px 0 10px 0;">
        <summary style="cursor:pointer;">Advanced settings — optional (Everyday Create works with this closed)</summary>
        <div style="margin-top:8px;">
          <p class="hint" id="gen-advanced-intro" style="margin:0 0 8px 0;">
            Everyday stays <b>tip → Go CSRT → Create</b>. Open only for polarity, long-clip assist, Ignore marks, or rare tuning — hybrid options never replace CSRT.
          </p>

          <details id="gen-advanced-tracking" class="gen-adv-nested">
            <summary>Tracking &amp; polarity</summary>
            <div class="checkbox-row"><input type="checkbox" id="gen-invert" /><label for="gen-invert"
              data-help="Flips the stroke curve up↔down (100−pos). Use when the stroke feels inverted — not a tracker failure. Example: tip moves down but the script rises.">Invert motion direction</label></div>
            <div class="checkbox-row"><input type="checkbox" id="gen-camcomp" checked /><label for="gen-camcomp"
              data-help="Compensates camera pans using background features. Recommended for moving camera. Default on.">Camera motion compensation</label></div>
            <div class="checkbox-row"><input type="checkbox" id="gen-scenecut" checked /><label for="gen-scenecut"
              data-help="Detects hard cuts and re-anchors the tracker afterward. Default on.">Scene-cut detection</label></div>
            <div class="checkbox-row"><input type="checkbox" id="gen-capture-trajectory" /><label for="gen-capture-trajectory"
              data-help="Records tip (x,y) per frame for Feel Stage A (buzz when tip grazes a contact mark) and the Play overlay. Not a user-drawn stroke path — Create never requires this. Soft-on with Contact vib; CSRT only.">Record tip trajectory (optional feel — not required to Create)</label></div>
          </details>

          <details id="gen-advanced-antidrift" class="gen-adv-nested">
            <summary>Long-clip anti-drift</summary>
            <p class="hint" style="margin:8px 0 6px 0;">
              Still Go CSRT. Rhythm / teachers steer only when opted in — off = bit-identical Everyday.
            </p>
            <div class="checkbox-row"><input type="checkbox" id="gen-rhythm-grid" /><label for="gen-rhythm-grid"
              data-help="Starts inside the confirmed target box and follows only nearby cells with matching rhythm. A stronger unrelated body part cannot take over merely because CSRT drifts toward it. Opt-in; Go CSRT path only; ~+18% analysis time.">Rhythm-robust signal (target-locked, long clips)</label></div>
            <div class="checkbox-row"><input type="checkbox" id="gen-contact-points" disabled /><label for="gen-contact-points"
              data-help="VLM1: load a contact_points.py JSON so the rhythm grid can search near teacher contact points when the tip box is far away (>3 cells). Needs Rhythm-robust signal on. Empty/off = bit-identical. Build via Generate below or CLI. Never a default.">Use contact points (teachers JSON)</label></div>
            <div class="row" id="gen-contact-points-row" style="align-items:center; gap:8px; flex-wrap:wrap; display:none;">
              <input type="text" id="gen-contact-points-path" placeholder="(contact_points JSON)" style="flex:1; min-width:12em;" disabled
                data-help="Path from generator/contact_points.py (e.g. clip.contact.json). Only sent when the checkbox above is on and Rhythm-robust signal is on." />
              <button type="button" class="secondary" id="gen-contact-points-pick" disabled
                data-help="Choose an existing contact_points.py JSON.">Choose…</button>
            </div>
            <div class="checkbox-row" id="gen-contact-verify-row" style="display:none;"><input type="checkbox" id="gen-contact-verify" disabled /><label for="gen-contact-verify"
              data-help="Hybrid assist: keep a teacher contact point only where the Go CSRT rhythm grid measures ≥1.5× stronger signal than at its own cell. Needs Use contact points + path. Off = every loaded point steers (same as CLI --contact-verify 0). Default off — measured K=1.5; never Everyday.">Verify with the engine (hybrid, K=1.5)</label></div>
            <p class="hint" id="gen-contact-verify-hint" style="display:none; margin:0 0 6px 0;">Optional hybrid: engine drops weak teacher points (K=1.5). Default off — Everyday Create unchanged when off.</p>
            <details id="gen-contact-points-gen" class="gen-adv-nested" style="display:none; margin:6px 0 8px 0;">
              <summary>Generate teachers JSON (opt-in)</summary>
              <p class="hint" style="margin:8px 0 6px 0;">Writes <code>.contact.json</code> beside the video. Does not change Everyday Create.</p>
              <div class="checkbox-row"><input type="checkbox" id="gen-cp-nudenet" checked /><label for="gen-cp-nudenet"
                data-help="NudeNet teacher (optional pip install). Fast local boxes.">NudeNet</label></div>
              <div class="checkbox-row"><input type="checkbox" id="gen-cp-ollama" /><label for="gen-cp-ollama"
                data-help="Ask Ollama Qwen2.5-VL if the server is up. Skipped when unreachable.">Ollama Qwen2.5-VL</label></div>
              <div class="checkbox-row"><input type="checkbox" id="gen-cp-lmstudio" /><label for="gen-cp-lmstudio"
                data-help="Ask LM Studio vision model if the local server is up.">LM Studio vision</label></div>
              <div class="row" style="align-items:center;gap:8px;flex-wrap:wrap;margin-top:4px;">
                <button type="button" class="secondary" id="gen-contact-points-run"
                  data-help="Runs contact_points.py with the checked teachers, fills the path above, and enables Use contact points.">Generate contact points</button>
                <span class="hint" id="gen-contact-points-gen-status" style="margin:0;"></span>
              </div>
            </details>
          </details>

          <details id="gen-advanced-scenemap" class="gen-adv-nested">
            <summary>Scene map</summary>
            <p class="hint" style="margin:8px 0 6px 0;">
              Heatmap + Ignore marks without Create. Same Ignore job as Step 2 → <b>+ Ignore region</b>.
            </p>
            <div class="row" style="align-items:center;gap:8px;flex-wrap:wrap;">
              <button type="button" class="secondary" id="gen-scene-map" disabled
                data-help="Quick rhythm heatmap (~6×8s windows) without running Generate. Explicit only — never auto before Create (Owner).">Show scene map</button>
              <span class="hint" id="gen-scene-map-status" style="margin:0;"></span>
            </div>
            <div id="gen-scene-map-tools" style="display:none;margin:8px 0 4px 0;">
              <div class="row" style="align-items:center;gap:8px;flex-wrap:wrap;">
                <label style="width:auto;" data-help="Which 8s window’s rhythm scores to draw on the preview.">Map window</label>
                <input type="range" id="gen-scene-map-win" min="0" max="0" value="0" style="flex:1;min-width:120px;" />
                <span class="hint" id="gen-scene-map-win-label" style="margin:0;"></span>
              </div>
              <div class="checkbox-row"><input type="checkbox" id="gen-scene-map-overlay" checked /><label for="gen-scene-map-overlay"
                data-help="Draw the rhythm heatmap over the preview (Advanced). Off = hide overlay only; marks stay.">Show heatmap overlay</label></div>
              <div class="row" style="align-items:center;gap:8px;flex-wrap:wrap;">
                <label style="width:auto;" data-help="Advanced only. Ignore = exclude wrong latch (knees…). Source = optional hint when auto stroke search fails — not Everyday motion-path paint. Region = full taxonomy for train/review.">Mark</label>
                <select id="gen-scene-map-mark-kind">
                  <option value="exclude" selected>Ignore / black (not for recognition)</option>
                  <option value="source">Source (only if auto stroke not found)</option>
                  <option value="region">Region (full taxonomy / train)</option>
                </select>
                <select id="gen-scene-map-mark-class" style="display:none;" aria-label="Region class"></select>
                <label class="checkbox-row" style="margin:0;"
                  data-help="Stay fixed: keep the painted box where you drew it. Off (default for Ignore) = Create tracks the box so it moves with the subject (knees, thigh, etc.).">
                  <input type="checkbox" id="gen-scene-map-mark-sticky" /> Stay fixed
                </label>
                <button type="button" class="secondary" id="gen-scene-map-mark">Paint mark</button>
                <button type="button" class="secondary" id="gen-scene-map-marks-clear">Clear marks</button>
              </div>
              <p class="hint" id="gen-scene-map-marks-label" style="margin:4px 0 0 0;"></p>
              <details id="gen-scene-map-learning" class="gen-adv-nested" data-suite style="margin-top:8px;">
                <summary>Learning export &amp; teacher candidates</summary>
                <div class="row" style="align-items:center;gap:8px;flex-wrap:wrap;margin-top:8px;">
                  <button type="button" class="secondary" id="gen-scene-map-export"
                    data-help="Writes local scene_map_learning JSON for this clip’s companion .samn. Requires Settings → Collect learning data. Never trains YOLO.">Export for learning</button>
                  <button type="button" class="secondary" id="gen-scene-map-suggest"
                    data-help="L1 priors: pre-fill Ignore boxes from your Collect exports (regions you often paint out, e.g. lower-left knees). Suggest only — review on the map; Clear removes them. Needs ≥3 clips with Ignore exports. Never auto-Create.">Suggest ignores from learning</button>
                  <span class="hint" id="gen-scene-map-export-status" style="margin:0;"></span>
                </div>
                <div id="gen-auto-candidates" style="margin-top:8px;padding-top:8px;border-top:1px solid var(--border);">
                  <p class="hint" style="margin:0 0 6px 0;">Teacher contact candidates (<code>author:auto</code>) — Accept → <code>reviewed:true</code> for P5c; Reject deletes.</p>
                  <div class="row" style="align-items:center;gap:8px;flex-wrap:wrap;">
                    <button type="button" class="secondary" id="gen-import-contact-candidates"
                      data-help="Writes teacher-consensus boxes into the companion .samn as unreviewed auto region marks. Needs an existing scene map.">Import candidates…</button>
                  <span class="hint" id="gen-auto-candidates-status" style="margin:0;"></span>
                </div>
                <div id="gen-auto-candidates-list" style="margin-top:6px;"></div>
              </div>
            </details>
          </div>
          </details>

          <details id="gen-advanced-ai-draft" class="gen-adv-nested" data-suite>
            <summary>AI draft (experimental) — review-only after CSRT</summary>
            <p class="hint" id="gen-ai-script-hint" style="margin:8px 0 6px 0;">
              Does <b>not</b> replace Everyday Create. Export classical → draft stretch → Keep required.
            </p>
            <div class="row" style="align-items:center;gap:8px;flex-wrap:wrap;">
              <button type="button" class="secondary" id="gen-ai-script-export" disabled
                data-help="Saves this Create result as a local training sample (actions + quality) under ai_script_imitation. Does not train a model and does not change Everyday CSRT. Enabled after Create finishes.">Export classical run</button>
              <button type="button" class="secondary" id="gen-ai-script-draft" disabled
                data-help="Experimental: drafts a stroke from your exported classical samples (duration + tip box aspect match, then stretch). Off until ≥1 Export classical run. Shows the draft on the 0–100 gauge for review before Keep. Does not replace CSRT Create.">AI draft script</button>
              <button type="button" class="primary" id="gen-ai-script-keep" disabled hidden
                data-help="Writes the reviewed AI draft beside the video as .samn (+ .funscript). Explicit only — never auto.">Keep draft</button>
              <button type="button" class="secondary" id="gen-ai-script-discard" disabled hidden
                data-help="Drops the current AI draft without writing. Everyday Create result stays.">Discard</button>
              <span class="hint" id="gen-ai-script-status" style="margin:0;"></span>
            </div>
          </details>

          <details id="gen-advanced-signal" class="gen-adv-nested">
            <summary>Signal &amp; quality</summary>
            <div class="checkbox-row"><input type="checkbox" id="gen-dynrange" checked /><label for="gen-dynrange"
              data-help="Smoothly lifts weak sections to usable strength. Default on.">Sliding dynamics</label></div>
            <div class="checkbox-row"><input type="checkbox" id="gen-retry" checked /><label for="gen-retry"
              data-help="Automatically retries with other signal parameters when quality is poor. Default on.">Auto-Retry</label></div>
            <div class="checkbox-row"><input type="checkbox" id="gen-auto-ozone" /><label for="gen-auto-ozone"
              data-help="Suggests O-markers in the last eighth (highest mean position) only when the ending is clearly high. Classic from signal, no AI model.">Suggest O-markers automatically</label></div>
            <!-- Audio check lives in Review → Improve (post-generate). Still default-on at generate time via hidden input. -->
            <input type="checkbox" id="gen-audio-check" checked style="display:none" aria-hidden="true" />
            <!-- Ballast removed: AI second opinion + Flow downscale (no Everyday effect). -->
          </details>

          <details id="gen-advanced-expert" class="gen-adv-nested">
            <summary>Expert tuning — defaults are fine for Everyday</summary>
            <p class="hint" id="gen-backend-hint" style="margin:8px 0 6px 0;">
              Tracking is <b>CSRT tip (Go path)</b>. Flow / 4-zone stay CLI-only.
            </p>
            <!-- Keep #gen-backend in DOM for payload + Playwright; product GUI is CSRT-only. -->
            <div class="gen-adv-sr-only" aria-hidden="true">
              <label for="gen-backend">Tracking method</label>
              <select id="gen-backend" tabindex="-1">
                <option value="csrt" selected>CSRT (mark tip, Go path)</option>
              </select>
            </div>
            <div class="checkbox-row"><input type="checkbox" id="gen-contact-impulse" /><label for="gen-contact-impulse"
              data-help="Same as Feel → Curve → Impulse (peaks only). Prefer the Curve control in step 3; this Advanced mirror stays in sync. Opt-in experiment — Everyday Soft onset stays default. Does not change stroke CSRT, Follow/Ignore marks, or Enforcement.">Peak-emphasis contact vib (same as Feel → Curve → Impulse)</label></div>
            <div class="field-row"><label data-help="Both axes are tracked; Auto picks the larger span. Force only when clearly wrong.">Motion axis</label>
              <select id="gen-axis">
                <option value="" selected>Automatic (recommended)</option>
                <option value="x">Force horizontal</option>
                <option value="y">Force vertical</option>
              </select>
            </div>
            <div class="checkbox-row"><input type="checkbox" id="gen-adaptive" checked /><label for="gen-adaptive"
              data-help="Adds extra keyframes for asymmetric motion. Default on.">Adaptive Keyframes</label></div>
            <div class="checkbox-row"><input type="checkbox" id="gen-perscene" /><label for="gen-perscene"
              data-help="Re-searches the tip region after each hard cut (Python PerSceneROI). On Go-CSRT portable builds this is soft-ignored — cuts still re-anchor, Everyday stays on Go CSRT (#338/#339). Prefer leaving off unless you intentionally use the Python path.">Re-find region after each cut (Python-only; soft-ignored on Go CSRT)</label></div>
            <div class="field-row" style="align-items:center;">
              <label data-help="Signal smoothing window width in frames. Larger = calmer but slower. Default 11.">Smoothing window</label>
              <input type="range" id="gen-smooth" min="0" max="51" step="1" value="11" style="flex:1;" />
              <span class="hint" id="gen-smooth-val" style="margin:0; min-width:2.5em;">11</span>
            </div>
            <div class="field-row" style="align-items:center;">
              <label data-help="Minimum spacing between keyframes in milliseconds. Default 150.">Min keyframe spacing (ms)</label>
              <input type="range" id="gen-peakdist" min="0" max="500" step="10" value="150" style="flex:1;" />
              <span class="hint" id="gen-peakdist-val" style="margin:0; min-width:3em;">150</span>
            </div>
            <div class="field-row" style="align-items:center;">
              <label data-help="Peak prominence as a fraction of the position span (0–1). Filters soft wiggles. 0 = profile default on Create (Autotune≈0.35, Soft≈0.2). Everyday leaves this at 0.">Peak prominence (0 = profile default)</label>
              <input type="range" id="gen-prominence" min="0" max="1" step="0.05" value="0" style="flex:1;" />
              <span class="hint" id="gen-prominence-val" style="margin:0; min-width:2.5em;">0</span>
            </div>
            <div class="field-row" style="align-items:center;">
              <label data-help="Ramer–Douglas–Peucker tolerance for thinning. 0 = off.">RDP tolerance (0 = off)</label>
              <input type="range" id="gen-rdp" min="0" max="20" step="0.5" value="0" style="flex:1;" />
              <span class="hint" id="gen-rdp-val" style="margin:0; min-width:2.5em;">0</span>
            </div>
            <div class="field-row" style="align-items:center;">
              <label data-help="Max position change per second (0–100 scale). 0 = off. Protects the device. Autotune sets 400.">Max speed (0 = off)</label>
              <input type="range" id="gen-maxspeed" min="0" max="800" step="50" value="0" style="flex:1;" />
              <span class="hint" id="gen-maxspeed-val" style="margin:0; min-width:2.5em;">0</span>
            </div>
            <div class="gen-postprocess-probe" id="gen-postprocess-probe">
              <div class="gen-postprocess-badges" id="gen-postprocess-badges" aria-live="polite">
                <span class="gen-pp-badge" id="gen-pp-badge-kf">— kf</span>
                <span class="gen-pp-badge" id="gen-pp-badge-peaks">— peaks</span>
                <span class="gen-pp-badge" id="gen-pp-badge-valleys">— valleys</span>
                <span class="gen-pp-badge" id="gen-pp-badge-hz">— Hz</span>
              </div>
              <svg id="gen-postprocess-svg" class="gen-postprocess-svg" viewBox="0 0 320 72" preserveAspectRatio="none" aria-hidden="true">
                <polyline id="gen-postprocess-raw" class="gen-postprocess-raw" fill="none" stroke-width="1.2" points="" />
                <polyline id="gen-postprocess-poly" class="gen-postprocess-poly" fill="none" stroke="currentColor" stroke-width="1.6" points="" />
                <g id="gen-postprocess-peaks" class="gen-postprocess-peaks"></g>
              </svg>
              <p class="hint" id="gen-postprocess-preview" style="margin:6px 0 0 0;" aria-live="polite">
                Postprocess probe: muted = raw · teal = after knobs · dots = peaks (synthetic — not your clip).
              </p>
            </div>
          </details>
        </div>
      </details>

      <div class="row">
        <button id="gen-generate" class="primary" disabled>Create Emotion Script</button>
        <button id="gen-cancel" type="button" disabled>Cancel</button>
      </div>
      <div id="gen-progress-wrap" style="display:none; margin-top:8px;">
        <div class="progress-bar" style="margin:0;">
          <div id="gen-progress-bar" class="progress-bar-fill" style="width:0%;"></div>
        </div>
        <div id="gen-progress-text" class="hint" style="margin-top:4px;"></div>
        <div id="gen-preview-steer-tip" class="hint" style="margin-top:4px;"></div>
      </div>
      <pre id="gen-log" class="run-log" aria-label="Creation progress"></pre>
      <p class="hint">
        Creates one <b>Emotion Script</b> for Play. Share to other apps is optional later.
      </p>
    </section>

    <section class="gen-step-panel" id="gen-step-result" data-step="5" hidden>
      <h3 class="gen-step-title">5 · Review &amp; improve</h3>
      <div id="gen-improve" style="display:none; margin-top:4px; padding:10px;
           border:1px solid var(--border); border-radius:4px;">
        <div style="margin-bottom:6px;">
          Soft polish on the CSRT result — trim ends, fill gaps, heal tracker-loss windows, optional audio check.
          Gaps already get an auto pass right after Create; re-run here after trim or with audio spacing.
        </div>
        <div class="row" style="align-items:center; flex-wrap:wrap; gap:8px;">
          <label style="width:auto;" data-help="Cut black intro / late credits. 0 = keep from start.">Start (s)</label>
          <input type="number" id="gen-improve-start" value="0" min="0" step="0.5" style="width:5em;" />
          <label style="width:auto;" data-help="Cut after this time. 0 = keep to end.">End (s)</label>
          <input type="number" id="gen-improve-end" value="0" min="0" step="0.5" style="width:5em;" />
        </div>
        <div class="row" style="align-items:center; flex-wrap:wrap; gap:8px; margin-top:6px;">
          <label class="checkbox-row" style="margin:0;"
            data-help="Inserts linear points across long holes (tracker loss / sparse keyframes). Does not invent motion from audio.">
            <input type="checkbox" id="gen-improve-fill" checked /> Fill gaps
          </label>
          <label class="checkbox-row" style="margin:0;"
            data-help="Rewrites known tracker-loss windows (metadata tracking_gaps): drops junk points inside and bridges the range. Clears those windows afterward so Contact vib is not muted forever. Does not re-run CSRT. Default bridge is a straight line — enable Rhythm under More for stroke-rhythm bridge.">
            <input type="checkbox" id="gen-improve-heal" checked /> Heal tracking gaps
          </label>
          <label class="checkbox-row" style="margin:0;"
            data-help="When filling gaps, space new points using audio tempo (half-period) if ffmpeg finds a clear beat. Still linear positions — not audio→curve.">
            <input type="checkbox" id="gen-improve-audio-fill" checked /> Align fill to audio tempo
          </label>
          <label class="checkbox-row" style="margin:0;"
            data-help="Compares script Hz to audio Hz (warn only). Also stamps Speech-Hold + holding/gentle/intense/climax segment hints for Review (never rewrites the stroke curve). Needs ffmpeg.">
            <input type="checkbox" id="gen-improve-audio" checked /> Audio check
          </label>
        </div>
        <details class="gen-mark-disclose" id="gen-improve-more-details" style="margin-top:6px;">
          <summary>More: Rhythm bridge &amp; repair span</summary>
          <div class="row" style="align-items:center; flex-wrap:wrap; gap:8px; margin-top:6px;">
            <label class="checkbox-row" style="margin:0;"
              data-help="Opt-in: when Healing tracking gaps, bridge with the stroke rhythm around each window instead of a straight line (line fallback if no rhythm). Off by default — does not change Everyday Create / auto-Improve.">
              <input type="checkbox" id="gen-improve-heal-rhythm" /> Rhythm bridge
            </label>
            <label class="checkbox-row" style="margin:0;"
              data-help="Opt-in: rewrite only this time range with a rhythm bridge (line fallback). Does not clear tracking_gaps. Mark start/end in seconds of the bad stretch.">
              <input type="checkbox" id="gen-improve-repair" /> Repair this span
            </label>
            <label style="width:auto;">From (s)</label>
            <input type="number" id="gen-improve-repair-start" value="0" min="0" step="0.5" style="width:5em;" disabled />
            <label style="width:auto;">To (s)</label>
            <input type="number" id="gen-improve-repair-end" value="0" min="0" step="0.5" style="width:5em;" disabled />
          </div>
        </details>
        <div class="row" style="margin-top:8px;">
          <button id="gen-improve-apply" class="primary" type="button">Improve script</button>
          <span class="hint" id="gen-improve-status" style="margin:0 0 0 8px;"></span>
        </div>
        <div id="gen-audio-segments" class="gen-audio-segments" hidden>
          <div class="gen-audio-segments-head">
            <b>Speech-Hold / Feel segments</b>
            <span class="hint" id="gen-audio-hold-summary"></span>
          </div>
          <p class="hint" style="margin:4px 0 6px;">
            Review taxonomy only — click a block to seek Play. Optional: add as chapter marks (does not change the stroke).
          </p>
          <details class="gen-mark-disclose" id="gen-feel-filters-details">
            <summary>Filters</summary>
            <div class="row gen-audio-seg-filters" style="align-items:center; flex-wrap:wrap; gap:10px; margin:6px 0;">
              <label class="checkbox-row" style="margin:0;"><input type="checkbox" id="gen-seg-f-holding" checked /> Hold</label>
              <label class="checkbox-row" style="margin:0;"><input type="checkbox" id="gen-seg-f-gentle" checked /> Gentle</label>
              <label class="checkbox-row" style="margin:0;"><input type="checkbox" id="gen-seg-f-intense" checked /> Intense</label>
              <label class="checkbox-row" style="margin:0;"><input type="checkbox" id="gen-seg-f-climax" checked /> Climax</label>
              <label class="checkbox-row" style="margin:0;"
                data-help="When on, only dialogue/quiet Hold blocks stay visible in the strip.">
                <input type="checkbox" id="gen-seg-f-speech-only" /> Speech-hold only
              </label>
            </div>
          </details>
          <div id="gen-audio-seg-strip" class="gen-audio-seg-strip" role="list"></div>
          <div class="row" style="margin-top:8px; align-items:center; flex-wrap:wrap; gap:8px;">
            <button type="button" id="gen-audio-seg-chapters">Add visible as chapters</button>
            <span class="hint" id="gen-audio-seg-status"></span>
          </div>
        </div>
      </div>
      <div id="gen-feedback" style="display:none; margin-top:4px; padding:10px;
           border:1px solid var(--border); border-radius:4px;">
        <div style="margin-bottom:6px;">Was the result usable? Your rating helps
          tune quality scoring on real material.</div>
        <div class="row">
          <button data-verdict="brauchbar" type="button">usable</button>
          <button data-verdict="grenzwertig" type="button">borderline</button>
          <button data-verdict="unbrauchbar" type="button">unusable</button>
        </div>
        <input type="text" id="gen-fb-comment" placeholder="Comment (optional) - e.g. what did not fit"
               style="width:100%; margin-top:8px;" />
        <div id="gen-fb-status" class="hint" style="margin-top:6px;"></div>
      </div>
      <div id="gen-quality" style="display:none; margin-top:8px; padding:8px; border-radius:4px;"></div>
    </section>
  `;

  wireDataHelp(root);

  const el = id => root.querySelector(id);
  /** parseInt/parseFloat that keeps 0 (unlike `x || fallback`). */
  function finiteOr(raw, fallback) {
    const n = parseFloat(raw);
    return Number.isFinite(n) ? n : fallback;
  }
  function intOr(raw, fallback) {
    const n = parseInt(raw, 10);
    return Number.isFinite(n) ? n : fallback;
  }
  el('#gen-open-handbook')?.addEventListener('click', () => openHandbook());
  const canvas = el('#roi-canvas');
  const ctx = canvas.getContext('2d');

  let videoPath = null;
  let lastSceneMap = null;
  let sceneMapWinIdx = 0;
  let sceneMapMarks = [];
  let sceneMapMarkMode = null; // 'exclude' | 'source' | 'region' | null
  let sceneMapMarkSeq = 0;
  let img = new Image();
  let nativeW = 0, nativeH = 0;
  let roi = null; // {x,y,w,h} in videopixeln
  let roi2 = null; // zweite Region für Tf/Tj (distance + suction)
  let candidates = []; // TFTJ 4b / MT-Seed: [{x,y,w,h,score,index}, ...] dashed until pick
  let pendingSeed = null; // MT-Seed: { tip, partner } suggest ≠ auto-commit
  let pendingAITarget = null; // strict semantic proposal; Apply required
  let activeAITargetRequest = null; // {requestId, videoPath, expectedClass, timeSec}
  let aiTargetRequestSeq = 0;
  let sceneProposalPath = ''; // loaded .scene.json path (empty = none)
  let sceneProposal = null; // SceneProposalLoad | null (Found view for current seek)
  let extraTargets = []; // extra contact anchors (follow by default when tip path on)
  let maskRois = []; // soft-exclude boxes
  let roi2Mode = false; // Knopf „2. Region“ aktiv
  let markMode = null; // null | 'target' | 'mask'
  // Click-select on preview: tip / contact / extra / scene-map mark.
  // { kind:'tip'|'contact'|'extra'|'scene', index?:number, id?:string }
  let selectedMark = null;
  // Snapshot for Settings → Apply AI setup automatically Undo.
  let aiAppliedUndo = null;
  let createBodyFigure = null;
  let dragging = false, draggingSecond = false, startX = 0, startY = 0, curX = 0, curY = 0;
  let seekSec = 0;
  let generating = false;
  let lastOutputPath = null;
  /** @type {Array<{label?:string,Label?:string,start_ms?:number,StartMs?:number,end_ms?:number,EndMs?:number,speech_hold?:boolean,SpeechHold?:boolean}>} */
  let lastAudioSegments = [];
  let lastSpeechHoldMs = 0;

  const AUDIO_SEG_LABELS = ['holding', 'gentle', 'intense', 'climax'];

  function normalizeAudioSegment(s) {
    if (!s || typeof s !== 'object') return null;
    const label = String(s.label ?? s.Label ?? '').toLowerCase().trim() || 'gentle';
    let start = Number(s.start_ms ?? s.StartMs ?? s.startMs ?? 0);
    let end = Number(s.end_ms ?? s.EndMs ?? s.endMs ?? 0);
    if (!Number.isFinite(start) || !Number.isFinite(end)) return null;
    start = Math.round(start);
    end = Math.round(end);
    if (end < start) {
      const tmp = start;
      start = end;
      end = tmp;
    }
    if (end <= start) return null;
    const speechHold = !!(s.speech_hold ?? s.SpeechHold ?? s.speechHold);
    return { label, startMs: start, endMs: end, speechHold };
  }

  function chapterNameForSegment(s) {
    if (s.label === 'holding') return s.speechHold ? 'Hold (speech)' : 'Hold';
    if (s.label === 'gentle') return 'Gentle';
    if (s.label === 'intense') return 'Intense';
    if (s.label === 'climax') return 'Climax';
    return s.label ? s.label.charAt(0).toUpperCase() + s.label.slice(1) : 'Segment';
  }

  function audioSegFilterOn(label) {
    const id = `#gen-seg-f-${label}`;
    const box = el(id);
    return !box || !!box.checked;
  }

  function visibleAudioSegments() {
    const speechOnly = !!el('#gen-seg-f-speech-only')?.checked;
    return lastAudioSegments.filter((s) => {
      if (!AUDIO_SEG_LABELS.includes(s.label)) return false;
      if (!audioSegFilterOn(s.label)) return false;
      if (speechOnly && !s.speechHold) return false;
      return true;
    });
  }

  function renderAudioSegmentsPanel() {
    const panel = el('#gen-audio-segments');
    const strip = el('#gen-audio-seg-strip');
    const summary = el('#gen-audio-hold-summary');
    if (!panel || !strip) return;
    if (!lastAudioSegments.length) {
      panel.hidden = true;
      strip.innerHTML = '';
      if (summary) summary.textContent = '';
      return;
    }
    panel.hidden = false;
    if (summary) {
      const holdSec = (lastSpeechHoldMs / 1000).toFixed(1);
      summary.textContent = lastSpeechHoldMs > 0
        ? `· Speech-Hold ≈ ${holdSec}s · ${lastAudioSegments.length} segment(s)`
        : `· ${lastAudioSegments.length} segment(s)`;
    }
    const visible = visibleAudioSegments();
    const tMin = Math.min(...lastAudioSegments.map((s) => s.startMs));
    const tMax = Math.max(...lastAudioSegments.map((s) => s.endMs));
    const span = Math.max(1, tMax - tMin);
    strip.innerHTML = '';
    if (!visible.length) {
      const empty = document.createElement('span');
      empty.className = 'hint';
      empty.textContent = 'No segments match the filters.';
      strip.appendChild(empty);
      return;
    }
    visible.forEach((s) => {
      const btn = document.createElement('button');
      btn.type = 'button';
      btn.className = `gen-audio-seg gen-audio-seg-${s.label}` + (s.speechHold ? ' is-speech-hold' : '');
      btn.setAttribute('role', 'listitem');
      const left = ((s.startMs - tMin) / span) * 100;
      const width = Math.max(1.5, ((s.endMs - s.startMs) / span) * 100);
      btn.style.left = `${left}%`;
      btn.style.width = `${width}%`;
      const title = `${chapterNameForSegment(s)} · ${(s.startMs / 1000).toFixed(1)}s–${(s.endMs / 1000).toFixed(1)}s`
        + (s.speechHold ? ' · speech-hold' : '');
      btn.title = title;
      btn.textContent = chapterNameForSegment(s);
      btn.addEventListener('click', () => {
        window.dispatchEvent(new CustomEvent('playback:seek-ms', { detail: { ms: s.startMs } }));
        if (playback && typeof playback.seekMs === 'function') {
          playback.seekMs(s.startMs);
        }
        const st = el('#gen-audio-seg-status');
        if (st) st.textContent = `Seek ${(s.startMs / 1000).toFixed(1)}s — ${chapterNameForSegment(s)}`;
      });
      strip.appendChild(btn);
    });
  }

  function rememberAudioSegmentsFromResult(result) {
    if (!result || typeof result !== 'object') return;
    const segs = result.audioSegments || result.AudioSegments || result.segments;
    const hold = Number(result.speechHoldMs ?? result.SpeechHoldMs ?? result.speech_hold_ms ?? 0);
    if (!Array.isArray(segs)) return;
    lastAudioSegments = segs.map(normalizeAudioSegment).filter(Boolean);
    lastSpeechHoldMs = Number.isFinite(hold) ? hold : 0;
    renderAudioSegmentsPanel();
  }

  async function addVisibleSegmentsAsChapters() {
    const status = el('#gen-audio-seg-status');
    const visible = visibleAudioSegments();
    if (!visible.length) {
      if (status) status.textContent = 'Nothing visible to add — adjust filters.';
      return;
    }
    if (!lastOutputPath) {
      if (status) status.textContent = 'No script path yet.';
      return;
    }
    try {
      if (playback && typeof playback.loadScriptPath === 'function') {
        await playback.loadScriptPath(lastOutputPath, { review: true });
      }
      const existing = await GetScriptChapterMarks().catch(() => []) || [];
      const mapped = visible.map((s) => ({
        name: chapterNameForSegment(s),
        startTime: s.startMs,
        endTime: s.endMs,
      }));
      // Merge: keep existing, append mapped that don't collide within 250ms start.
      const next = existing.slice();
      let added = 0;
      for (const ch of mapped) {
        const clash = next.some((e) => Math.abs((e.startTime ?? e.StartTime ?? 0) - ch.startTime) < 250);
        if (clash) continue;
        next.push(ch);
        added += 1;
      }
      await SaveScriptChapterMarks(next);
      window.dispatchEvent(new CustomEvent('playback:chapters-changed'));
      if (status) {
        status.textContent = added > 0
          ? `Added ${added} chapter mark(s) · Play list refreshed`
          : 'Chapters already present near those times';
      }
      uiInfo(added > 0 ? `Added ${added} audio segment chapter(s)` : 'No new chapters (near-duplicates skipped)', el('#gen-status'));
    } catch (err) {
      if (status) status.textContent = 'Chapters failed: ' + err;
      uiError('Add chapters: ' + err, el('#gen-status'));
    }
  }
  // Everyday FunGen-like: Generate with no ROI → auto-find tip then generate.
  let pendingGenerateAfterRoi = false;
  let userCancelRequested = false;
  let activeCreateSeq = 0;
  EventsOn('generate:started', result => {
    if (typeof result?.seq === 'number' && result.seq >= activeCreateSeq) {
      activeCreateSeq = result.seq;
    }
  });
  let profileSuggestionSeq = 0;
  // Multi-drop batch note — keep visible through auto-find status updates.
  let videoBatchNote = '';
  // Generate queue (DeepFunGen-style batch): remaining paths after the current clip.
  // Entries: { path, status: 'queued'|'running'|'done'|'failed'|'skipped' }
  let generateQueue = [];
  let queueRunning = false;
  let queueOverwrite = false;
  let queueBatchStartedAt = 0;
  let queueDoneCount = 0;
  // FunGen-like 0–100 gauge over the preview (after Generate).
  let genCurvePoints = null; // [{atMs, pos}, ...]
  let genCurveBeforeAIDraft = null; // CSRT Create curve restored on Discard
  let aiDraftCurveActive = false; // true while pending AI draft drives the gauge
  const POS_OVERLAY_PREF = 'samn.genPosOverlay';

  function basename(p) {
    return String(p || '').split(/[\\/]/).pop() || p;
  }

  function renderQueue() {
    const wrap = el('#gen-queue');
    const list = el('#gen-queue-list');
    const summary = el('#gen-queue-summary');
    const runBtn = el('#gen-queue-run');
    const clearBtn = el('#gen-queue-clear');
    if (!wrap || !list || !summary) return;
    if (!generateQueue.length) {
      wrap.hidden = true;
      list.innerHTML = '';
      summary.textContent = '';
      return;
    }
    wrap.hidden = false;
    const pending = generateQueue.filter(q => q.status === 'queued').length;
    const done = generateQueue.filter(q => q.status === 'done').length;
    const failed = generateQueue.filter(q => q.status === 'failed' || q.status === 'skipped').length;
    const total = generateQueue.length;
    summary.textContent = queueRunning
      ? `Queue ${done + 1}/${total} running… (${failed} skipped/failed)`
      : `Queue: ${total} video${total === 1 ? '' : 's'} (${pending} waiting` +
        (done ? `, ${done} done` : '') +
        (failed ? `, ${failed} skipped/failed` : '') + ')';
    if (runBtn) runBtn.disabled = queueRunning || pending === 0;
    if (clearBtn) clearBtn.disabled = queueRunning;
    // Source clips grid (V1 target create.png): one card per clip with a
    // poster frame, name, duration and status. Built with DOM nodes, so a
    // file name is always text (a "<" in a name used to break the list).
    list.replaceChildren(...generateQueue.map((q, i) => {
      const mark = q.status === 'done' ? '✓'
        : q.status === 'running' ? '…'
        : q.status === 'failed' ? '✗'
        : q.status === 'skipped' ? '–'
        : String(i + 1);
      const li = document.createElement('li');
      li.className = 'gen-clip';
      li.dataset.status = q.status;
      li.dataset.path = q.path;
      li.classList.toggle('is-current', q.path === videoPath);
      li.title = q.path;
      const thumb = document.createElement('div');
      thumb.className = 'gen-clip-thumb';
      const img = document.createElement('img');
      img.alt = '';
      img.hidden = true;
      thumb.appendChild(img);
      const markEl = document.createElement('span');
      markEl.className = 'gen-queue-mark';
      markEl.textContent = mark;
      thumb.appendChild(markEl);
      const name = document.createElement('span');
      name.className = 'gen-clip-name';
      name.textContent = basename(q.path);
      const meta = document.createElement('span');
      meta.className = 'hint gen-clip-meta';
      meta.textContent = q.status;
      li.append(thumb, name, meta);
      fillClipCard(li, q);
      return li;
    }));
  }

  // Poster + duration per clip, read once per path (ProbeCreateVideo) and
  // one after the other so a long queue does not start many ffmpeg runs.
  const clipInfo = new Map(); // path -> Promise<CreateVideoInfo|null>
  let clipInfoChain = Promise.resolve();
  function clipInfoFor(path) {
    if (!clipInfo.has(path)) {
      const p = clipInfoChain.then(() => ProbeCreateVideo(path)).catch(() => null);
      clipInfoChain = p.then(() => {}, () => {});
      clipInfo.set(path, p);
    }
    return clipInfo.get(path);
  }
  function fillClipCard(li, q) {
    clipInfoFor(q.path).then(info => {
      if (!li.isConnected || !info) return;
      const img = li.querySelector('img');
      if (info.posterUrl) {
        img.src = info.posterUrl;
        img.hidden = false;
      }
      const meta = li.querySelector('.gen-clip-meta');
      const dur = info.durationSec > 0 ? fmtClock(info.durationSec) : '';
      meta.textContent = dur ? `${dur} · ${q.status}` : q.status;
    });
  }

  el('#gen-queue-list')?.addEventListener('click', e => {
    const li = e.target.closest('.gen-clip');
    if (!li || queueRunning || !li.dataset.path || li.dataset.path === videoPath) return;
    loadVideo(li.dataset.path, 0, null, { keepQueue: true });
  });

  function setQueueFromPaths(paths) {
    const uniq = [];
    const seen = new Set();
    for (const p of (paths || [])) {
      if (!p || seen.has(p)) continue;
      seen.add(p);
      uniq.push(p);
    }
    generateQueue = uniq.map(path => ({ path, status: 'queued' }));
    queueRunning = false;
    queueOverwrite = false;
    queueDoneCount = 0;
    queueBatchStartedAt = 0;
    renderQueue();
  }

  function clearQueueKeepCurrent() {
    generateQueue = [];
    queueRunning = false;
    queueOverwrite = false;
    queueDoneCount = 0;
    queueBatchStartedAt = 0;
    videoBatchNote = '';
    renderQueue();
  }

  function queueProgressPrefix() {
    if (!queueRunning || !generateQueue.length) return '';
    const total = generateQueue.length;
    const idx = Math.min(total, queueDoneCount + 1);
    return `Queue ${idx}/${total} · `;
  }

  function posOverlayWanted() {
    const cb = el('#gen-pos-overlay-toggle');
    if (!cb) return true;
    try {
      const saved = localStorage.getItem(POS_OVERLAY_PREF);
      if (saved === '0') { cb.checked = false; return false; }
      if (saved === '1') { cb.checked = true; return true; }
    } catch (_) { /* ignore */ }
    return !!cb.checked;
  }

  function setPosOverlayVisible(on) {
    const box = el('#gen-pos-overlay');
    if (!box) return;
    const show = on && posOverlayWanted() && genCurvePoints && genCurvePoints.length >= 2;
    box.hidden = !show;
    box.setAttribute('aria-hidden', show ? 'false' : 'true');
    box.classList.toggle('ai-draft-preview', show && aiDraftCurveActive);
  }

  function actionsToCurvePoints(actions) {
    if (!Array.isArray(actions) || actions.length < 2) return null;
    const pts = actions.map((a) => ({
      atMs: a.at ?? a.At ?? 0,
      pos: a.pos ?? a.Pos ?? 0,
    })).filter((p) => Number.isFinite(p.atMs) && Number.isFinite(p.pos));
    return pts.length >= 2 ? pts : null;
  }

  /** S3: show AI draft on the Create 0–100 gauge before Keep (not only in Play). */
  function showAIDraftCurvePreview(actions) {
    const pts = actionsToCurvePoints(actions);
    if (!pts) return false;
    if (!aiDraftCurveActive) {
      genCurveBeforeAIDraft = genCurvePoints;
    }
    genCurvePoints = pts;
    aiDraftCurveActive = true;
    updatePosOverlay();
    return true;
  }

  function clearAIDraftCurvePreview({ restore = true } = {}) {
    if (!aiDraftCurveActive && !genCurveBeforeAIDraft) {
      return;
    }
    aiDraftCurveActive = false;
    if (restore && genCurveBeforeAIDraft && genCurveBeforeAIDraft.length >= 2) {
      genCurvePoints = genCurveBeforeAIDraft;
    } else if (!restore) {
      // Keep: draft curve is the kept script — leave points; drop restore stash.
    } else {
      genCurvePoints = null;
    }
    genCurveBeforeAIDraft = null;
    if (genCurvePoints && genCurvePoints.length >= 2) {
      updatePosOverlay();
    } else {
      setPosOverlayVisible(false);
    }
  }

  function interpGenPos(tMs) {
    const pts = genCurvePoints;
    if (!pts || pts.length < 1) return null;
    if (tMs <= pts[0].atMs) return pts[0].pos;
    const last = pts[pts.length - 1];
    if (tMs >= last.atMs) return last.pos;
    for (let i = 1; i < pts.length; i++) {
      const a = pts[i - 1], b = pts[i];
      if (tMs >= a.atMs && tMs <= b.atMs) {
        if (b.atMs === a.atMs) return b.pos;
        const f = (tMs - a.atMs) / (b.atMs - a.atMs);
        return a.pos + f * (b.pos - a.pos);
      }
    }
    return last.pos;
  }

  function updatePosOverlay() {
    const knob = el('#gen-pos-knob');
    const fill = el('#gen-pos-fill');
    const val = el('#gen-pos-value');
    if (!knob || !fill || !val) return;
    setPosOverlayVisible(true);
    if (el('#gen-pos-overlay')?.hidden) return;
    const pos = interpGenPos(Math.round(seekSec * 1000));
    if (pos == null || Number.isNaN(pos)) {
      val.textContent = '—';
      return;
    }
    const p = Math.max(0, Math.min(100, pos));
    // CSS: bottom = 0, top = 100
    const pct = p; // height from bottom
    knob.style.bottom = `calc(${pct}% - 7px)`;
    fill.style.height = pct + '%';
    val.textContent = String(Math.round(p));
  }

  async function loadGenCurveFromPlay() {
    try {
      const pts = await GetScriptCurve(800);
      if (Array.isArray(pts) && pts.length >= 2) {
        genCurvePoints = pts.map(p => ({
          atMs: p.atMs ?? p.AtMs ?? 0,
          pos: p.pos ?? p.Pos ?? 0,
        }));
        updatePosOverlay();
        return;
      }
    } catch (_) { /* no script loaded yet */ }
    genCurvePoints = null;
    setPosOverlayVisible(false);
  }
  const DISPLAY_W = 560;

  const ROI1_STROKE = '#3dccc0';
  const ROI1_FILL = 'rgba(61,204,192,0.16)';
  const ROI2_STROKE = '#f2b03d';
  const ROI2_FILL = 'rgba(242,176,61,0.18)';
  const TARGET_STROKE = '#e070a0';
  const TARGET_FILL = 'rgba(224,112,160,0.16)';
  const AI_TARGET_STROKE = '#7ee787';
  const AI_TARGET_FILL = 'rgba(126,231,135,0.14)';
  const MASK_STROKE = 'rgba(20,20,24,0.9)';
  const MASK_FILL = 'rgba(0,0,0,0.35)';

  function isTfTj() {
    // Legacy distance profile (CLI / old saves). Product GUI no longer offers it —
    // Contact vibration on Normal is the everyday feel layer (owner 21 Sep).
    const p = (el('#gen-profile').value || '').toLowerCase();
    return p === 'tf' || p === 'tj';
  }

  function normalizeProductProfile() {
    const sel = el('#gen-profile');
    if (!sel) return;
    const p = (sel.value || '').toLowerCase();
    if (p === 'tf' || p === 'tj') {
      sel.value = 'standard';
      sel.dataset.userTouched = '1';
    }
  }

  // KI-Regionssuche (ai_roi.py, lokales ONNX-Modell) ist optional - ohne
  // installiertes onnxruntime oder ohne Modelldatei bleibt es bei der
  // klassischen Rhythmus-Heuristik (auto_roi.py). Einmal beim Öffnen des
  // Tabs geprüft (kostet einen Python-Start), nicht bei jedem videoladen.
  function refreshAIRoiAvailability() {
    CheckAIRoiAvailable().then(available => {
      const checkbox = el('#gen-ai-roi');
      const target = el('#gen-ai-target-class');
      checkbox.disabled = !available;
      if (target) target.disabled = !available || !checkbox.checked;
      const allTags = el('#gen-ai-detections');
      if (allTags) allTags.disabled = !available || !videoPath;
      el('#gen-autoroi-hint').textContent = available
        ? 'AI mode checks the currently displayed frame for the expected body point and never '
          + 'falls back to another class. A matching box remains a proposal until you press Apply — '
          + 'then drag anytime to refine. Use “Show all AI tags” to see nipples/breasts/etc. together.'
        : 'Classic Tip-Find analyzes motion (no AI model) and paints an editable start box — '
          + 'drag on the preview to tighten toward the tip. AI detection: no local ONNX model '
          + 'found (Settings → AI model path, or default folder).';
    }).catch(() => {});
  }
  refreshAIRoiAvailability();
  window.addEventListener('samn-ai-roi-refresh', refreshAIRoiAvailability);

  // Audio-Tempo-Prüfung braucht nur ffmpeg auf dem PATH (Go-native post-hoc
  // oder Python bei PreferPython). Default-on at generate time; Review step
  // exposes the user-facing toggle for improve / re-check.
  CheckAudioCheckAvailable().then(available => {
    const genCheck = el('#gen-audio-check');
    const improveCheck = el('#gen-improve-audio');
    const improveFill = el('#gen-improve-audio-fill');
    if (genCheck) {
      genCheck.disabled = !available;
      if (!available) {
        genCheck.checked = false;
        genCheck.title = 'ffmpeg not found — use portable release or Settings → Install video tools';
      } else {
        genCheck.checked = true;
      }
    }
    if (improveCheck) {
      improveCheck.disabled = !available;
      if (!available) improveCheck.checked = false;
      else improveCheck.checked = true;
    }
    if (improveFill) {
      improveFill.disabled = !available;
      if (!available) improveFill.checked = false;
    }
  }).catch(() => {});

  function setRoi2Mode(on) {
    roi2Mode = !!on;
    if (roi2Mode) markMode = null;
    const btn = el('#gen-roi2-toggle');
    btn.style.outline = roi2Mode ? '2px solid #f2b03d' : '';
    btn.style.background = roi2Mode ? 'rgba(242,176,61,0.22)' : '';
    syncMarkModeButtons();
  }

  function setMarkMode(mode) {
    markMode = mode || null;
    if (markMode) roi2Mode = false;
    const btn = el('#gen-roi2-toggle');
    btn.style.outline = '';
    btn.style.background = '';
    syncMarkModeButtons();
  }

  function syncMarkModeButtons() {
    const tBtn = el('#gen-target-add');
    const mBtn = el('#gen-mask-add');
    if (tBtn) {
      tBtn.style.outline = markMode === 'target' ? '2px solid #e070a0' : '';
      tBtn.style.background = markMode === 'target' ? 'rgba(224,112,160,0.22)' : '';
    }
    if (mBtn) {
      mBtn.style.outline = markMode === 'mask' ? '2px solid rgba(180,180,190,0.9)' : '';
      mBtn.style.background = markMode === 'mask' ? 'rgba(120,120,130,0.25)' : '';
    }
  }

  function updateRoiLabels() {
    el('#gen-roi-label').textContent = roi
      ? `Tip (stroke tracker): x=${roi.x} y=${roi.y} w=${roi.w} h=${roi.h} — CSRT follows this box only; after a big angle change press Find tip`
      : 'No tip box yet (Find tip runs on Create — no motion path to paint)';
    el('#gen-roi2-label').textContent = roi2
      ? secondRegionLabel(roi2, 'gold')
      : 'No contact area marked (optional partner touch)';
    const extras = el('#gen-extras-label');
    if (extras) {
      const parts = [];
      if (extraTargets.length) {
        const named = extraTargets.map((t, i) => t.class ? t.class : `#${i + 1}`).join(', ');
        parts.push(`${extraTargets.length} extra contact (${named}, magenta)`);
      }
      if (maskRois.length) {
        parts.push(`${maskRois.length} soft mask${maskRois.length === 1 ? '' : 's'} (dashed)`);
      }
      extras.style.display = parts.length ? 'block' : 'none';
      extras.textContent = parts.length ? parts.join(' · ') : '';
    }

    // Per-scene ROI is unsupported on the real Tf/Tj two-point distance path.
    // Optional Zone 2 on Stroke is UX-only (not sent in payload) — do NOT
    // disable “Re-find region after each cut” just because Zone 2 is marked.
    const twoPoint = !!roi2 && isTfTj();
    const perScene = el('#gen-perscene');
    perScene.disabled = twoPoint;
    if (twoPoint) perScene.checked = false;
    perScene.title = twoPoint
      ? 'Not available for Tf/Tj tip↔partner distance — region is not re-searched there.'
      : '';
    // Product GUI: CSRT tip only (1-Zone). 4-zone / research backends = CLI.
    const be = el('#gen-backend').value;
    if (be !== 'csrt') {
      el('#gen-backend').value = 'csrt';
    }
    syncMarkSnack();
    updateGenerateEnabled();
  }

  // Outline + scroll Mark contact without arming gold paint. Tip refine stays tip
  // until the user presses the button (auto-arm made body-map paints land as Contact).
  function highlightPartnerMark(scroll) {
    const btn = el('#gen-roi2-toggle');
    if (!btn || roi2Mode) return;
    btn.style.outline = '2px solid #f2b03d';
    btn.style.background = 'rgba(242,176,61,0.12)';
    if (scroll) {
      try { btn.scrollIntoView({ block: 'nearest', inline: 'nearest' }); } catch (_) {}
    }
  }

  function clearPartnerHighlight() {
    if (roi2Mode) return;
    const btn = el('#gen-roi2-toggle');
    if (!btn) return;
    btn.style.outline = '';
    btn.style.background = '';
  }

  // Guided snack for Tip → Partner → Extra (Contact vibe). Everyday still
  // works with tip alone / auto-find — this only clarifies order.
  function syncMarkSnack() {
    const snack = el('#gen-mark-snack');
    const hint = el('#gen-mark-snack-hint');
    if (!snack || !hint) return;
    const vibOn = !!el('#gen-contact-vibration')?.checked;
    const hasTip = !!roi;
    const hasPartner = !!roi2;
    const hasExtra = extraTargets.length > 0;

    // Contact-vib guide only. Off → hide (including after a tip is set).
    if (!vibOn) {
      clearPartnerHighlight();
      snack.style.display = 'none';
      snack.dataset.partnerArmed = '';
      return;
    }
    snack.style.display = '';

    let current = 'tip';
    if (hasTip && !hasPartner) current = 'partner';
    else if (hasTip && hasPartner) current = 'extra';

    snack.querySelectorAll('[data-snack]').forEach((li) => {
      const key = li.getAttribute('data-snack');
      const done = (key === 'tip' && hasTip)
        || (key === 'partner' && hasPartner)
        || (key === 'extra' && hasExtra);
      li.classList.toggle('is-done', done && key !== current);
      li.classList.toggle('is-current', key === current);
    });

    if (!hasTip) {
      snack.dataset.partnerArmed = '';
      clearPartnerHighlight();
      hint.innerHTML = 'Snack <b>1</b>: press <b>Find tip</b> (or drag only if Find missed). '
        + 'That teal box is the stroke tracker — not a whole-frame motion path.';
    } else if (!hasPartner) {
      hint.innerHTML = 'Snack <b>2</b>: <b>Mark contact</b> (gold) on mouth or nipple/breast. '
        + 'Optional — Create works without it. Tip CSRT already owns the stroke.';
      // Highlight only — do not setRoi2Mode. Next canvas drag still refines tip.
      if (snack.dataset.partnerArmed !== '1') {
        snack.dataset.partnerArmed = '1';
        highlightPartnerMark(true);
      } else {
        highlightPartnerMark(false);
      }
    } else if (!hasExtra) {
      snack.dataset.partnerArmed = '1';
      clearPartnerHighlight();
      hint.innerHTML = 'Snack <b>3</b> (optional): <b>+ Another contact</b> for a second nipple/mouth, then Create. '
        + 'Partner fixed (pixels) only if that touch barely moves.';
    } else {
      snack.dataset.partnerArmed = '1';
      clearPartnerHighlight();
      hint.innerHTML = 'Tip + partner + extras set. Ready for Feel → Create. '
        + 'After a big camera-angle change, re-run <b>Find tip</b>.';
    }
  }

  // Product GUI is tip CSRT only (4-zone / research backends stay CLI).
  function forceCsrtBackend(msg) {
    const backend = el('#gen-backend');
    backend.value = 'csrt';
    delete backend.dataset.userTouched;
    if (msg) {
      normalizeProductProfile();
      candidates = [];
      clearPendingSeed();
      setSeedSuggestEnabled(false);
      el('#gen-status').textContent = msg;
    }
    updateGenerateEnabled();
    redraw();
  }

  function contactVibrationOn() {
    return !!el('#gen-contact-vibration')?.checked;
  }

  // Everyday: video alone is enough — Generate auto-finds tip if missing.
  function regionReadyForGenerate() {
    return !!videoPath;
  }

  let tipFindBusy = false; // manual Find tip / auto tip (not only tip-find-then-Create)
  let progressStartedAt = 0;

  function showTipFindProgress(label) {
    const wrap = el('#gen-progress-wrap');
    if (!wrap) return;
    wrap.style.display = 'block';
    el('#gen-progress-bar').style.width = '0%';
    el('#gen-progress-bar').style.opacity = '1';
    el('#gen-progress-text').textContent = label || 'Finding…';
    progressStartedAt = Date.now();
  }

  function setTipFindBusy(on, progressLabel) {
    tipFindBusy = !!on;
    if (on) {
      el('#gen-cancel').disabled = false;
      el('#gen-autoroi').disabled = true;
      el('#gen-candidates').disabled = true;
      const aiTags = el('#gen-ai-detections');
      if (aiTags) aiTags.disabled = true;
      setSeedSuggestEnabled(false);
      showTipFindProgress(progressLabel || 'Finding…');
    } else if (!generating) {
      el('#gen-cancel').disabled = true;
    }
  }

  // Re-enable Find / other spots / AI tags after tip-find, Cancel, or seek.
  function unlockRecognitionControls() {
    tipFindBusy = false;
    if (!generating) el('#gen-cancel').disabled = true;
    if (videoPath) {
      el('#gen-autoroi').disabled = false;
      el('#gen-candidates').disabled = false;
    }
    refreshAIRoiAvailability();
    hideProgress();
  }

  function clearCreateLock() {
    generating = false;
    el('#gen-cancel').disabled = true;
    tipFindBusy = false;
    activeAITargetRequest = null;
    unlockRecognitionControls();
    updateGenerateEnabled();
    updateSceneMapButton();
    syncWorkflowSteps();
  }

  // Tip-find never reached GenerateScript — skip remaining queue entries.
  // C7: tip-find failures name the action and offer a real Find tip retry.
  function showFindTipRetry(reason, level) {
    const status = el('#gen-status');
    const why = String(reason || 'no tip box').replace(/\s+/g, ' ').trim();
    const msg = 'Find tip failed: ' + why + '.';
    if (level === 'warn') uiWarn(msg, null);
    else uiError(msg, null);
    if (!status) return;
    status.replaceChildren();
    const text = document.createElement('span');
    text.textContent = msg + ' ';
    const btn = document.createElement('button');
    btn.type = 'button';
    btn.className = 'secondary status-cta';
    btn.textContent = 'Find tip';
    btn.addEventListener('click', () => {
      if (!videoPath || tipFindBusy || generating) return;
      pendingGenerateAfterRoi = false;
      startAutoFindRegion();
    });
    status.append(text, btn);
  }

  function abortQueueDuringTipFind(statusMsg) {
    if (!queueRunning) return false;
    for (const q of generateQueue) {
      if (q.status === 'queued' || q.status === 'running') q.status = 'skipped';
    }
    queueRunning = false;
    renderQueue();
    el('#gen-status').textContent = statusMsg || 'Queue canceled.';
    return true;
  }

  function startAutoFindRegion() {
    if (!videoPath) return;
    forceCsrtBackend();
    const useAI = el('#gen-ai-roi').checked && !el('#gen-ai-roi').disabled;
    const expectedClass = normalizeClass(el('#gen-ai-target-class')?.value || '');
    // Tip-find-then-Create / queue must auto-continue — classic find only
    // (AI path waits for Apply and cannot finish unattended Create).
    const autoContinue = pendingGenerateAfterRoi;
    if (useAI && !expectedClass && !autoContinue) {
      pendingGenerateAfterRoi = false;
      if (generating) clearCreateLock();
      el('#gen-status').textContent = (
        'Choose the expected body point for strict AI detection, or turn AI off for generic motion search.'
      ) + videoBatchNote;
      el('#gen-ai-target-class')?.focus();
      return;
    }
    if (useAI && !expectedClass && autoContinue) {
      pendingGenerateAfterRoi = false;
      clearCreateLock();
      if (!abortQueueDuringTipFind('Queue stopped — choose an AI body point, or turn AI off, then Create again.')) {
        el('#gen-status').textContent = (
          'Choose the expected body point for strict AI detection, or turn AI off for generic motion search.'
        ) + videoBatchNote;
      }
      el('#gen-ai-target-class')?.focus();
      return;
    }
    clearPendingAITarget();
    redraw();
    const runAI = useAI && !autoContinue;
    const statusMsg = runAI
      ? `Looking only for ${labelFor(expectedClass) || expectedClass} (strict AI; no class fallback)…`
      : (autoContinue && useAI
        ? 'Create uses classic tip-find (AI needs Apply first)…'
        : 'Finding tip region automatically (CSRT)…');
    setTipFindBusy(true, runAI ? 'Strict AI tip…' : 'Finding tip…');
    el('#gen-status').textContent = statusMsg + videoBatchNote;
    if (runAI) {
      const requestId = ++aiTargetRequestSeq;
      activeAITargetRequest = { requestId, videoPath, expectedClass, timeSec: seekSec };
      DetectExpectedTipROI(videoPath, expectedClass, seekSec, requestId);
    } else {
      activeAITargetRequest = null;
      AutoDetectROI(videoPath, 'auto');
    }
  }

  // Default Fix Zone 2 = off when an optional contact partner is marked + vib on.
  function syncRoi2FixedDefault() {
    const fix = el('#gen-roi2-fixed');
    if (!fix || fix.dataset.userTouched) return;
    if (roi2 && contactVibrationOn()) {
      fix.checked = false;
    }
  }

  // Marks S2: extras follow when tip path is recorded (Contact vib soft-ons path).
  // Stay fixed checkbox / no path → static anchor (Tf/Tj careful static targets).
  function contactExtraShouldFollow() {
    if (el('#gen-extra-contact-sticky')?.checked) return false;
    return !!el('#gen-capture-trajectory')?.checked;
  }

  // Progressive steps: next panel appears only when the previous action is done.
  function syncWorkflowSteps() {
    const hasVideo = !!videoPath;
    const hasRoi1 = !!roi;
    const canRun = regionReadyForGenerate();
    const hasResult = !!lastOutputPath;
    const showRegion = hasVideo;
    // Unlock motion/profile once video is loaded (Generate auto-finds tip).
    const showMotion = hasVideo;
    const showRun = canRun || generating;
    const showResult = hasResult;

    const panel = (id, on) => {
      const node = el(id);
      if (!node) return;
      node.hidden = !on;
    };
    panel('#gen-step-region', showRegion);
    panel('#gen-step-motion', showMotion);
    panel('#gen-step-run', showRun);
    panel('#gen-step-result', showResult);

    let current = 1;
    if (showResult) current = 5;
    else if (showRun || generating) current = 4;
    else if (showMotion) current = 3;
    else if (showRegion) current = 2;

    root.querySelectorAll('.gen-step-item').forEach(item => {
      const n = parseInt(item.dataset.step, 10);
      item.classList.toggle('is-current', n === current);
      item.classList.toggle('is-done', n < current);
    });

    const prompt = el('#gen-step-prompt');
    if (!prompt) return;
    if (!hasVideo) {
      prompt.textContent = 'Start here: choose a video. Like FunGen2 — no motion path to paint; we find the tip.';
    } else if (!hasRoi1) {
      prompt.textContent = 'Snack 1: Find tip (auto) — or mark only if Find missed. Tip = stroke box, not whole-scene tracking.';
    } else if (!canRun && !generating) {
      prompt.textContent = 'Snack 2–3 optional: partner touch for Contact vibe, then Feel. Create unlocks when tip is ready.';
    } else if (generating) {
      prompt.textContent = 'Step 4: creating… you can Cancel if needed.';
    } else if (!hasResult) {
      prompt.textContent = 'Step 4: Create Emotion Script — Advanced stays closed for Everyday; open only if needed.';
    } else {
      prompt.textContent = 'Step 5: Improve, then Play — edit dots on the soft curve.';
    }
    syncMarkSnack();
  }

  function updateGenerateEnabled() {
    // Ohne video kein Ziel zum Generieren.
    if (!regionReadyForGenerate()) {
      el('#gen-generate').disabled = true;
      syncWorkflowSteps();
      return;
    }
    el('#gen-generate').disabled = generating;
    syncWorkflowSteps();
  }

  let sceneMapAvailable = false;

  function updateSceneMapButton() {
    const btn = el('#gen-scene-map');
    if (!btn) return;
    // Explicit Advanced control only (Owner § 6) — needs video + OpenCV path.
    btn.disabled = !videoPath || !sceneMapAvailable || generating;
  }

  function activeSceneMapWindow() {
    if (!lastSceneMap || !Array.isArray(lastSceneMap.windows) || !lastSceneMap.windows.length) {
      return null;
    }
    const i = Math.max(0, Math.min(sceneMapWinIdx, lastSceneMap.windows.length - 1));
    return lastSceneMap.windows[i];
  }

  function syncSceneMapTools() {
    const tools = el('#gen-scene-map-tools');
    if (!tools) return;
    const has = !!(lastSceneMap && Array.isArray(lastSceneMap.windows) && lastSceneMap.windows.length);
    tools.style.display = has ? 'block' : 'none';
    if (!has) return;
    const slider = el('#gen-scene-map-win');
    const label = el('#gen-scene-map-win-label');
    if (slider) {
      slider.max = String(Math.max(0, lastSceneMap.windows.length - 1));
      slider.value = String(sceneMapWinIdx);
    }
    const w = activeSceneMapWindow();
    if (label && w) {
      const a = ((w.startMs || 0) / 1000).toFixed(1);
      const b = ((w.endMs || 0) / 1000).toFixed(1);
      label.textContent = `${sceneMapWinIdx + 1}/${lastSceneMap.windows.length} · ${a}–${b}s`;
    }
    const cls = el('#gen-scene-map-mark-class');
    const kind = el('#gen-scene-map-mark-kind')?.value || 'exclude';
    if (cls) {
      cls.style.display = kind === 'region' ? '' : 'none';
      if (!cls.options.length) {
        for (const id of CONTACT_CLASS_ORDER) {
          const opt = document.createElement('option');
          opt.value = id;
          opt.textContent = labelFor(id) || id;
          cls.appendChild(opt);
        }
      }
    }
    updateSceneMapMarksLabel();
  }

  function updateSceneMapMarksLabel() {
    const lab = el('#gen-scene-map-marks-label');
    if (!lab) return;
    if (!sceneMapMarks.length) {
      lab.textContent = 'No marks yet — Paint mark, then drag on preview.';
      return;
    }
    lab.textContent = sceneMapMarks.map((m) => {
      let span = (m.fromMs || 0) === 0 && (m.toMs || 0) === 0
        ? 'whole clip'
        : `${((m.fromMs || 0) / 1000).toFixed(0)}–${((m.toMs || 0) / 1000).toFixed(0)}s`;
      if (m.atMs != null && Number.isFinite(m.atMs)) {
        span = `@${(m.atMs / 1000).toFixed(1)}s`;
      }
      const who = m.kind === 'region' && m.class ? `:${m.class}` : '';
      const kind = m.kind === 'exclude' ? 'ignore' : m.kind;
      const follow = m.follow ? '→follow' : (m.kind === 'exclude' || m.kind === 'source' ? '·fixed' : '');
      let src = '';
      if (m.author === 'suggest') src = '·suggest';
      else if (m.author === 'auto') src = m.reviewed === true ? '·auto✓' : '·auto?';
      return `${kind}${who}@${span}${follow}${src}`;
    }).join(' · ');
    renderAutoCandidatesList();
  }

  function pendingAutoCandidates() {
    return sceneMapMarks.filter((m) =>
      m.author === 'auto' && m.kind === 'region' && m.reviewed !== true);
  }

  function renderAutoCandidatesList() {
    const list = el('#gen-auto-candidates-list');
    if (!list) return;
    const pending = pendingAutoCandidates();
    if (!pending.length) {
      list.innerHTML = '<p class="hint" style="margin:0;">No pending auto candidates.</p>';
      return;
    }
    list.innerHTML = pending.map((m) => {
      const t = m.atMs != null ? `${(m.atMs / 1000).toFixed(1)}s` : '—';
      const id = String(m.id || '').replace(/"/g, '');
      return `<div class="row" data-auto-id="${id}" style="align-items:center;gap:8px;flex-wrap:wrap;margin:4px 0;">
        <span class="hint" style="margin:0;min-width:8em;">${m.class || 'contact'} @ ${t}</span>
        <button type="button" class="secondary gen-auto-seek" data-id="${id}">Seek</button>
        <button type="button" class="primary gen-auto-accept" data-id="${id}"
          data-help="Confirm for P5c YOLO export (reviewed:true).">Accept</button>
        <button type="button" class="secondary gen-auto-reject" data-id="${id}"
          data-help="Delete this teacher candidate from the companion .samn.">Reject</button>
      </div>`;
    }).join('');
  }

  // Rough IoU so L1 suggest does not stack duplicate Ignore boxes.
  function sceneMarkOverlap(a, b) {
    const ar = a?.rect || a;
    const br = b?.rect || b;
    if (!ar || !br) return 0;
    const ax2 = (ar.x || 0) + (ar.w || 0);
    const ay2 = (ar.y || 0) + (ar.h || 0);
    const bx2 = (br.x || 0) + (br.w || 0);
    const by2 = (br.y || 0) + (br.h || 0);
    const ix = Math.max(0, Math.min(ax2, bx2) - Math.max(ar.x || 0, br.x || 0));
    const iy = Math.max(0, Math.min(ay2, by2) - Math.max(ar.y || 0, br.y || 0));
    const inter = ix * iy;
    if (inter <= 0) return 0;
    const uni = (ar.w || 0) * (ar.h || 0) + (br.w || 0) * (br.h || 0) - inter;
    return uni > 0 ? inter / uni : 0;
  }

  // Normalize ROI from Go (may be X/Y/W/H or x/y/w/h).
  function sceneMapRect(r) {
    if (!r) return { x: 0, y: 0, w: 0, h: 0 };
    return {
      x: r.x ?? r.X ?? 0,
      y: r.y ?? r.Y ?? 0,
      w: r.w ?? r.W ?? 0,
      h: r.h ?? r.H ?? 0,
    };
  }

  // Restore Advanced scene-map marks (+ heatmap windows) from companion .samn
  // so Play↔Create keeps exclude/source/region annotations. Does not touch
  // Generate defaults / Everyday path.
  async function restoreSceneMapFromCompanion(path) {
    if (!path || typeof LoadSceneMapForVideo !== 'function') return;
    try {
      const loaded = await LoadSceneMapForVideo(path);
      if (!loaded?.found || videoPath !== path) return;
      if (loaded.map && Array.isArray(loaded.map.windows) && loaded.map.windows.length) {
        lastSceneMap = loaded.map;
        sceneMapWinIdx = 0;
      }
      const rawMarks = Array.isArray(loaded.marks) ? loaded.marks : [];
      sceneMapMarks = rawMarks.map((m) => {
        const kind = m.kind || m.Kind || '';
        const followRaw = m.follow ?? m.Follow;
        // Legacy marks had no follow flag — exclude/source should track by default.
        const follow = followRaw != null ? !!followRaw : (kind === 'exclude' || kind === 'source');
        const rawPath = m.path || m.Path || [];
        const path = Array.isArray(rawPath)
          ? rawPath.map((p) => ({
              ms: p.ms ?? p.Ms ?? 0,
              rect: sceneMapRect(p.rect || p.Rect),
            })).filter((p) => (p.rect?.w || 0) > 0 && (p.rect?.h || 0) > 0)
          : [];
        const out = {
          id: m.id || m.ID || '',
          kind,
          rect: sceneMapRect(m.rect || m.Rect),
          fromMs: m.fromMs ?? m.FromMs ?? 0,
          toMs: m.toMs ?? m.ToMs ?? 0,
          class: m.class || m.Class || '',
          author: m.author || m.Author || 'user',
          follow,
          path,
        };
        const atRaw = m.atMs ?? m.AtMs;
        if (atRaw != null && Number.isFinite(Number(atRaw))) out.atMs = Number(atRaw);
        // M5 / P5c: keep reviewed+confidence through companion restore so a
        // later Generate→.samn write does not drop auto-reviewed flags.
        const conf = m.confidence ?? m.Confidence;
        if (typeof conf === 'number' && conf > 0) out.confidence = conf;
        const rev = m.reviewed ?? m.Reviewed;
        if (typeof rev === 'boolean') out.reviewed = rev;
        return out;
      }).filter((m) => m.kind || m.id);
      let maxSeq = 0;
      for (const m of sceneMapMarks) {
        const n = parseInt(String(m.id || '').replace(/^m/i, ''), 10);
        if (Number.isFinite(n) && n > maxSeq) maxSeq = n;
      }
      sceneMapMarkSeq = maxSeq;
      syncSceneMapTools();
      redraw();
      const n = sceneMapMarks.length;
      const smStatus = el('#gen-scene-map-status');
      if (smStatus && (n || lastSceneMap)) {
        smStatus.textContent = n
          ? `Restored ${n} scene-map mark${n === 1 ? '' : 's'} from companion .samn.`
          : 'Restored scene map from companion .samn.';
      }
    } catch (_) {
      // Missing/corrupt companion is fine — Create starts blank for map marks.
    }
  }

  function heatColor(t) {
    const x = Math.max(0, Math.min(1, t));
    const r = Math.round(255 * Math.min(1, Math.max(0, x * 2)));
    const g = Math.round(255 * Math.min(1, Math.max(0, x < 0.5 ? x * 2 : 2 - x * 2)));
    const b = Math.round(255 * Math.min(1, Math.max(0, 1 - x * 2)));
    return `rgba(${r},${g},${b},0.35)`;
  }

  // Mirror trackcv.sceneMarkActive / sceneMarkRectAt for Create preview scrub.
  function sceneMarkActiveAt(m, atMs) {
    if ((m.fromMs || 0) === 0 && (m.toMs || 0) === 0) return true;
    return atMs >= (m.fromMs || 0) && atMs <= (m.toMs == null ? Infinity : m.toMs);
  }

  // Nearest Follow Path sample at scrub time; static Rect when Path empty (Stay fixed / pre-Create).
  function sceneMarkRectAt(m, atMs) {
    const path = Array.isArray(m.path) ? m.path : [];
    if (!path.length) return m.rect;
    let best = path[0];
    let bestDist = Math.abs(atMs - (best.ms || 0));
    for (let i = 1; i < path.length; i++) {
      const d = Math.abs(atMs - (path[i].ms || 0));
      if (d < bestDist) {
        best = path[i];
        bestDist = d;
      }
    }
    return best.rect || m.rect;
  }

  function drawSceneMapHeatmap() {
    if (!lastSceneMap || !el('#gen-scene-map-overlay')?.checked) return;
    if (!nativeW || !nativeH || !canvas.width) return;
    const win = activeSceneMapWindow();
    if (!win || !Array.isArray(win.score) || !win.score.length) return;
    const cols = lastSceneMap.cols || 16;
    const rows = lastSceneMap.rows || Math.max(1, Math.floor(win.score.length / cols));
    const scaleX = canvas.width / nativeW;
    const scaleY = canvas.height / nativeH;
    const cellW = (lastSceneMap.width || nativeW) / cols;
    const cellH = (lastSceneMap.height || nativeH) / rows;
    ctx.save();
    for (let r = 0; r < rows; r++) {
      for (let c = 0; c < cols; c++) {
        const v = win.score[r * cols + c] || 0;
        if (v < 8) continue;
        ctx.fillStyle = heatColor(v / 255);
        ctx.fillRect(c * cellW * scaleX, r * cellH * scaleY, cellW * scaleX + 0.5, cellH * scaleY + 0.5);
      }
    }
    if (win.chosenCell >= 0) {
      const cc = win.chosenCell % cols;
      const cr = Math.floor(win.chosenCell / cols);
      ctx.strokeStyle = 'rgba(255,255,255,0.9)';
      ctx.lineWidth = 2;
      ctx.strokeRect(cc * cellW * scaleX, cr * cellH * scaleY, cellW * scaleX, cellH * scaleY);
    }
    ctx.restore();
  }

  // S1: draw Ignore/source/region marks at Create scrub time using Follow Path
  // from restored .samn (not the static paint Rect). Heatmap off still shows marks.
  function drawSceneMapMarks() {
    if (!sceneMapMarks.length || !nativeW || !nativeH || !canvas.width) return;
    const atMs = Math.round((seekSec || 0) * 1000);
    for (const m of sceneMapMarks) {
      // Point-in-time auto candidates: show when scrub is within ±1.5 s.
      if (m.atMs != null && Number.isFinite(m.atMs)) {
        if (Math.abs(atMs - m.atMs) > 1500) continue;
      } else if (!sceneMarkActiveAt(m, atMs)) {
        continue;
      }
      const rect = sceneMarkRectAt(m, atMs);
      if (!rect || !(rect.w > 0) || !(rect.h > 0)) continue;
      const pendingAuto = m.author === 'auto' && m.reviewed !== true;
      const confirmedAuto = m.author === 'auto' && m.reviewed === true;
      const stroke = m.kind === 'exclude' ? '#111111'
        : m.kind === 'source' ? '#3dccc0'
          : pendingAuto ? '#c084fc'
            : confirmedAuto ? '#86efac'
              : '#f2b03d';
      const fill = m.kind === 'exclude' ? 'rgba(0,0,0,0.35)'
        : (stroke.length === 7 ? stroke + '33' : 'rgba(0,0,0,0.2)');
      drawNativeRect(rect, stroke, fill, m.kind === 'exclude' || pendingAuto);
    }
  }

  function drawSceneMapOverlay() {
    drawSceneMapHeatmap();
    drawSceneMapMarks();
  }

  function setSceneMapMarkMode(on) {
    sceneMapMarkMode = on ? (el('#gen-scene-map-mark-kind')?.value || 'exclude') : null;
    const btn = el('#gen-scene-map-mark');
    if (btn) btn.classList.toggle('primary', !!sceneMapMarkMode);
    if (on) {
      // Clear other paint modes without requiring Contact-mark DOM nodes.
      markMode = null;
      roi2Mode = false;
      el('#gen-status').textContent =
        `Scene map mark (${sceneMapMarkMode}): drag on preview. Scope = current map window.`;
    }
  }

  async function runSceneMapScan() {
    if (!videoPath || !sceneMapAvailable) return;
    const btn = el('#gen-scene-map');
    const status = el('#gen-scene-map-status');
    if (btn) btn.disabled = true;
    if (status) status.textContent = 'Scanning rhythm map…';
    try {
      const map = await ScanSceneMap(videoPath, 0);
      lastSceneMap = map || null;
      sceneMapWinIdx = 0;
      const n = Array.isArray(map?.windows) ? map.windows.length : 0;
      const grid = map?.cols && map?.rows ? `${map.cols}×${map.rows}` : '';
      if (status) {
        status.textContent = n
          ? `Map ready: ${n} windows${grid ? ` · ${grid}` : ''}`
          : 'Map scan returned no windows.';
      }
      syncSceneMapTools();
      redraw();
      uiInfo(status?.textContent || 'Scene map scan done.', el('#gen-status'));
    } catch (err) {
      lastSceneMap = null;
      if (status) status.textContent = '';
      syncSceneMapTools();
      uiError('Scene map: ' + err, el('#gen-status'));
    } finally {
      updateSceneMapButton();
    }
  }

  let contactUserOverride = false;

  function updateContactVibrationOpts() {
    const on = el('#gen-contact-vibration').checked;
    el('#gen-contact-vibration-opts').style.display = on ? 'block' : 'none';
    const marks = el('#gen-contact-marks-wrap');
    if (marks) {
      marks.style.display = on ? 'block' : 'none';
      marks.hidden = !on;
    }
    if (on) {
      const tip = el('#gen-region-class');
      if (tip && !tip.value && !tip.dataset.userTouched) {
        // Soft default: Glans (tip) — optional, user can clear to (any).
        if ([...tip.options].some((o) => o.value === 'glans')) {
          tip.value = 'glans';
        }
      }
      const c2 = el('#gen-region-class2');
      if (c2 && !c2.value && !c2.dataset.userTouched) {
        // Default contact type: nipples (common dual-side touch target).
        if ([...c2.options].some((o) => o.value === 'nipples')) {
          c2.value = 'nipples';
        }
      }
      const tc = el('#gen-target-class');
      if (tc && !tc.value && !tc.dataset.userTouched) {
        if ([...tc.options].some((o) => o.value === 'nipples')) {
          tc.value = 'nipples';
        }
      }
      // Feel Stage A needs tip trajectory — soft-on with Contact vib.
      const traj = el('#gen-capture-trajectory');
      if (traj && !traj.dataset.userTouched) {
        traj.checked = true;
      }
    } else {
      // Leaving contact-mark mode when vib is off.
      if (roi2Mode) setRoi2Mode(false);
      if (markMode === 'target' || markMode === 'mask') setMarkMode(null);
      const traj = el('#gen-capture-trajectory');
      if (traj && !traj.dataset.userTouched) {
        traj.checked = false;
      }
    }
    syncRoi2FixedDefault();
    syncMarkSnack();
    updateGenerateEnabled();
  }

  function updateContactSpanLabel() {
    const v = parseInt(el('#gen-contact-span').value, 10) || 75;
    const label = el('#gen-contact-span-label');
    if (v <= 50) label.textContent = 'earlier';
    else if (v >= 85) label.textContent = 'deep only';
    else label.textContent = (v / 100).toFixed(2);
  }

  let contactVibPreviewTimer = 0;
  async function refreshContactVibPreview() {
    const out = el('#gen-contact-vib-preview');
    const vibPoly = el('#gen-contact-vib-poly');
    const strokePoly = el('#gen-contact-vib-stroke');
    const peaksG = el('#gen-contact-vib-peaks');
    const badgeActive = el('#gen-cv-badge-active');
    const badgePeak = el('#gen-cv-badge-peak');
    const probe = el('#gen-contact-vib-probe');
    if (!out || !probe) return;
    if (!el('#gen-contact-vibration')?.checked) {
      probe.hidden = true;
      return;
    }
    probe.hidden = false;
    const spanPct = parseInt(el('#gen-contact-span')?.value, 10) || 75;
    const curve = el('#gen-contact-curve')?.value || 'soft';
    try {
      const res = await PreviewContactVibration({
        span: spanPct / 100,
        curve,
      });
      out.textContent = res.hint || res.Hint || 'Feel probe';
      const activePct = Number(res.activePct ?? res.ActivePct ?? 0);
      const peakVib = Number(res.peakVib ?? res.PeakVib ?? 0);
      if (badgeActive) badgeActive.textContent = `${Math.round(activePct)}% active`;
      if (badgePeak) badgePeak.textContent = `peak ${Math.round(peakVib * 100)}`;
      const sample = res.sample || res.Sample || [];
      const w = 320;
      const h = 72;
      const pad = 4;
      if (peaksG) peaksG.innerHTML = '';
      if (Array.isArray(sample) && sample.length >= 2) {
        const t0 = Number(sample[0].atMs ?? sample[0].AtMs ?? 0);
        const t1 = Number(sample[sample.length - 1].atMs ?? sample[sample.length - 1].AtMs ?? 1);
        const span = Math.max(1, t1 - t0);
        const vibOf = (p) => Math.max(0, Math.min(100, Number(p.vib ?? p.Vib ?? 0)));
        const toPts = (key, Key) => sample.map((p) => {
          const t = Number(p.atMs ?? p.AtMs ?? 0);
          const pos = Math.max(0, Math.min(100, Number(p[key] ?? p[Key] ?? 0)));
          const x = pad + ((t - t0) / span) * (w - 2 * pad);
          const y = pad + (1 - pos / 100) * (h - 2 * pad);
          return `${x.toFixed(1)},${y.toFixed(1)}`;
        }).join(' ');
        if (strokePoly) strokePoly.setAttribute('points', toPts('stroke', 'Stroke'));
        if (vibPoly) vibPoly.setAttribute('points', toPts('vib', 'Vib'));
        if (peaksG) {
          for (let i = 1; i < sample.length - 1; i++) {
            const v = vibOf(sample[i]);
            if (v < 15) continue;
            if (v < vibOf(sample[i - 1]) || v < vibOf(sample[i + 1])) continue;
            const t = Number(sample[i].atMs ?? sample[i].AtMs ?? 0);
            const x = pad + ((t - t0) / span) * (w - 2 * pad);
            const y = pad + (1 - v / 100) * (h - 2 * pad);
            const c = document.createElementNS('http://www.w3.org/2000/svg', 'circle');
            c.setAttribute('cx', x.toFixed(1));
            c.setAttribute('cy', y.toFixed(1));
            c.setAttribute('r', '2.4');
            peaksG.appendChild(c);
          }
        }
      } else {
        if (strokePoly) strokePoly.setAttribute('points', '');
        if (vibPoly) vibPoly.setAttribute('points', '');
      }
    } catch (err) {
      out.textContent = 'Feel probe unavailable: ' + err;
      if (strokePoly) strokePoly.setAttribute('points', '');
      if (vibPoly) vibPoly.setAttribute('points', '');
      if (peaksG) peaksG.innerHTML = '';
      if (badgeActive) badgeActive.textContent = '— active';
      if (badgePeak) badgePeak.textContent = '— peak';
    }
  }
  function scheduleContactVibPreview() {
    clearTimeout(contactVibPreviewTimer);
    contactVibPreviewTimer = setTimeout(refreshContactVibPreview, 160);
  }
  function openCreateContactProbe() {
    const d = el('#gen-contact-probe-details');
    if (d) d.open = true;
  }

  function updateProfileUi() {
    normalizeProductProfile();
    // Contact vib is the product feel layer — always shown.
    el('#gen-contact-vibration-wrap').style.display = 'block';
    el('#gen-contact-vibration-row').style.display = 'flex';
    if (!contactUserOverride) {
      el('#gen-contact-vibration').checked = true;
      if (!el('#gen-contact-curve').dataset.userTouched) {
        el('#gen-contact-curve').value = 'soft';
      }
    }
    syncRoi2FixedDefault();
    updateContactVibrationOpts();
    updateGenerateEnabled();
    if (videoPath && roi2) {
      el('#gen-status').textContent = isTfTj()
        ? (contactVibrationOn()
          ? 'Contact area set — tip↔partner distance; tracked unless “Fix contact area” is on.'
          : 'Contact area set (Contact vib off) — tip↔partner distance still uses both regions.')
        : 'Contact area marked — vib = depth and/or tip-near-mark when tip path is recorded.';
    }
  }

  function drawNativeRect(r, stroke, fill, dashed) {
    if (!r || !nativeW || !nativeH) return;
    const scaleX = canvas.width / nativeW, scaleY = canvas.height / nativeH;
    const dx0 = r.x * scaleX, dy0 = r.y * scaleY, dw = r.w * scaleX, dh = r.h * scaleY;
    ctx.save();
    if (dashed) ctx.setLineDash([6, 4]);
    ctx.strokeStyle = stroke;
    ctx.lineWidth = 2;
    ctx.strokeRect(dx0, dy0, dw, dh);
    ctx.fillStyle = fill;
    ctx.fillRect(dx0, dy0, dw, dh);
    ctx.restore();
  }

  function drawSelectedOutline(r) {
    if (!r || !nativeW || !nativeH || !canvas.width) return;
    const scaleX = canvas.width / nativeW, scaleY = canvas.height / nativeH;
    ctx.save();
    ctx.strokeStyle = 'rgba(255,255,255,0.95)';
    ctx.lineWidth = 3;
    ctx.setLineDash([4, 3]);
    ctx.strokeRect(r.x * scaleX - 2, r.y * scaleY - 2, r.w * scaleX + 4, r.h * scaleY + 4);
    ctx.restore();
  }

  function pointInNativeRect(nx, ny, r) {
    return !!(r && r.w > 0 && r.h > 0
      && nx >= r.x && ny >= r.y && nx <= r.x + r.w && ny <= r.y + r.h);
  }

  function selectedMarkNativeRect() {
    if (!selectedMark) return null;
    if (selectedMark.kind === 'tip') return roi;
    if (selectedMark.kind === 'contact') return roi2;
    if (selectedMark.kind === 'extra') return extraTargets[selectedMark.index] || null;
    if (selectedMark.kind === 'scene') {
      const m = sceneMapMarks.find((x) => x.id === selectedMark.id);
      if (!m) return null;
      const atMs = Math.round((seekSec || 0) * 1000);
      return sceneMarkRectAt(m, atMs);
    }
    return null;
  }

  function updateSelectedMarkUI() {
    const wrap = el('#gen-selected-mark');
    const lab = el('#gen-selected-mark-label');
    if (!wrap || !lab) return;
    if (!selectedMark) {
      wrap.hidden = true;
      lab.textContent = '';
      createBodyFigure?.setActive?.(
        el('#gen-region-class')?.value || el('#gen-region-class2')?.value || '');
      return;
    }
    // Body map + Delete live under Mark options — open when a mark is selected.
    const opts = el('#gen-mark-options-details');
    if (opts) opts.open = true;
    wrap.hidden = false;
    let text = 'Selected: ';
    if (selectedMark.kind === 'tip') {
      const cls = regionClass1Value();
      text += `Tip${cls ? ` (${labelFor(cls) || cls})` : ''} — click body map to set Tip class`;
      createBodyFigure?.setActive?.(cls);
    } else if (selectedMark.kind === 'contact') {
      const cls = regionClass2Value();
      text += `Contact${cls ? ` (${labelFor(cls) || cls})` : ''} — click body map for Contact type`;
      createBodyFigure?.setActive?.(cls);
    } else if (selectedMark.kind === 'extra') {
      const t = extraTargets[selectedMark.index];
      const cls = normalizeClass(t?.class || '');
      text += `Extra contact #${(selectedMark.index || 0) + 1}${cls ? ` (${labelFor(cls) || cls})` : ''}`;
      createBodyFigure?.setActive?.(cls);
    } else if (selectedMark.kind === 'scene') {
      const m = sceneMapMarks.find((x) => x.id === selectedMark.id);
      const kind = m?.kind || 'mark';
      const cls = normalizeClass(m?.class || '');
      text += `Scene ${kind}${cls ? ` (${labelFor(cls) || cls})` : ''}`
        + (kind === 'region' ? ' — body map sets Region class' : '');
      createBodyFigure?.setActive?.(cls);
    }
    lab.textContent = text;
  }

  function setSelectedMark(sel) {
    selectedMark = sel;
    updateSelectedMarkUI();
    redraw();
  }

  function hitPaintedMark(canvasX, canvasY) {
    if (!nativeW || !nativeH || !canvas.width) return null;
    const nx = canvasX * nativeW / canvas.width;
    const ny = canvasY * nativeH / canvas.height;
    const atMs = Math.round((seekSec || 0) * 1000);
    for (let i = sceneMapMarks.length - 1; i >= 0; i--) {
      const m = sceneMapMarks[i];
      if (m.atMs != null && Number.isFinite(m.atMs)) {
        if (Math.abs(atMs - m.atMs) > 1500) continue;
      } else if (!sceneMarkActiveAt(m, atMs)) {
        continue;
      }
      const rect = sceneMarkRectAt(m, atMs);
      if (pointInNativeRect(nx, ny, rect)) {
        return { kind: 'scene', id: m.id };
      }
    }
    for (let i = extraTargets.length - 1; i >= 0; i--) {
      if (pointInNativeRect(nx, ny, extraTargets[i])) {
        return { kind: 'extra', index: i };
      }
    }
    if (pointInNativeRect(nx, ny, roi2)) return { kind: 'contact' };
    if (pointInNativeRect(nx, ny, roi)) return { kind: 'tip' };
    return null;
  }

  function ensureSelectOption(sel, classId) {
    if (!sel || !classId) return;
    if (![...sel.options].some((o) => o.value === classId)) {
      const opt = document.createElement('option');
      opt.value = classId;
      opt.textContent = labelFor(classId) || classId;
      sel.appendChild(opt);
    }
    sel.value = classId;
  }

  function applyBodyClassToMark(classId) {
    const n = normalizeClass(classId);
    if (!n) return;
    if (selectedMark?.kind === 'tip') {
      ensureSelectOption(el('#gen-region-class'), n);
      if (el('#gen-region-class')) el('#gen-region-class').dataset.userTouched = '1';
      updateRoiLabels();
      updateSelectedMarkUI();
      el('#gen-status').textContent = `Tip class → ${labelFor(n) || n}`;
      return;
    }
    if (selectedMark?.kind === 'contact') {
      ensureSelectOption(el('#gen-region-class2'), n);
      if (el('#gen-region-class2')) el('#gen-region-class2').dataset.userTouched = '1';
      updateRoiLabels();
      updateSelectedMarkUI();
      el('#gen-status').textContent = `Contact type → ${labelFor(n) || n}`;
      return;
    }
    if (selectedMark?.kind === 'extra') {
      const t = extraTargets[selectedMark.index];
      if (t) t.class = n;
      ensureSelectOption(el('#gen-target-class'), n);
      if (el('#gen-target-class')) el('#gen-target-class').dataset.userTouched = '1';
      updateRoiLabels();
      updateSelectedMarkUI();
      el('#gen-status').textContent = `Extra contact class → ${labelFor(n) || n}`;
      redraw();
      return;
    }
    if (selectedMark?.kind === 'scene') {
      const m = sceneMapMarks.find((x) => x.id === selectedMark.id);
      if (m && (m.kind === 'region' || m.kind === 'source')) {
        m.class = n;
        m.kind = 'region';
        ensureSelectOption(el('#gen-scene-map-mark-class'), n);
        updateSceneMapMarksLabel();
        updateSelectedMarkUI();
        el('#gen-status').textContent = `Scene region class → ${labelFor(n) || n}`;
        redraw();
        return;
      }
      el('#gen-status').textContent = 'Ignore marks have no body-part class — use Region kind to label.';
      return;
    }
    if (sceneMapMarkMode === 'region') {
      ensureSelectOption(el('#gen-scene-map-mark-class'), n);
      el('#gen-status').textContent = `Next Scene Region paint → ${labelFor(n) || n}`;
      return;
    }
    if (markMode === 'target' || roi2Mode) {
      ensureSelectOption(el('#gen-target-class'), n);
      ensureSelectOption(el('#gen-region-class2'), n);
      if (el('#gen-region-class2')) el('#gen-region-class2').dataset.userTouched = '1';
      if (el('#gen-target-class')) el('#gen-target-class').dataset.userTouched = '1';
      el('#gen-status').textContent = `Next contact paint → ${labelFor(n) || n}`;
      return;
    }
    // Default: tip class (anchor model for Everyday tip).
    ensureSelectOption(el('#gen-region-class'), n);
    if (el('#gen-region-class')) el('#gen-region-class').dataset.userTouched = '1';
    createBodyFigure?.setActive?.(n);
    el('#gen-status').textContent = `Tip class → ${labelFor(n) || n} (or click a mark first)`;
  }

  function deleteSelectedMark() {
    if (!selectedMark) return;
    if (selectedMark.kind === 'tip') {
      roi = null;
      el('#gen-status').textContent = 'Tip mark cleared — Find tip area or paint again.';
    } else if (selectedMark.kind === 'contact') {
      roi2 = null;
      el('#gen-status').textContent = 'Contact mark cleared.';
    } else if (selectedMark.kind === 'extra') {
      extraTargets.splice(selectedMark.index, 1);
      el('#gen-status').textContent = 'Extra contact removed.';
    } else if (selectedMark.kind === 'scene') {
      sceneMapMarks = sceneMapMarks.filter((m) => m.id !== selectedMark.id);
      updateSceneMapMarksLabel();
      el('#gen-status').textContent = 'Scene map mark removed.';
    }
    setSelectedMark(null);
    updateRoiLabels();
    updateGenerateEnabled();
  }

  function showAiAppliedChip(lines) {
    const wrap = el('#gen-ai-applied');
    const lab = el('#gen-ai-applied-label');
    if (!wrap || !lab) return;
    if (!lines || !lines.length) {
      wrap.hidden = true;
      lab.textContent = '';
      return;
    }
    wrap.hidden = false;
    lab.textContent = 'AI applied: ' + lines.join(' · ') + ' — Undo restores your previous empty slots.';
  }

  function clearAiAppliedChip() {
    aiAppliedUndo = null;
    showAiAppliedChip([]);
  }

  async function maybeAutoApplySceneProposal() {
    try {
      const s = await getSettingsCache();
      if (!s?.applyAISetupAutomatically) return;
      if (!sceneProposal?.found) return;
      const p = sceneProposal.proposal || {};
      const primary = sceneProposalBox(p.primary || p.Primary);
      const partner = sceneProposalBox(p.partner || p.Partner);
      const snap = {
        roi: roi ? { ...roi } : null,
        roi2: roi2 ? { ...roi2 } : null,
        tipClass: el('#gen-region-class')?.value || '',
        contactClass: el('#gen-region-class2')?.value || '',
      };
      const applied = [];
      // User values win — only fill empty Tip / contact (Owner 28 Sep).
      if (!roi && primary && primary.w > 0 && primary.h > 0) {
        applyScenePrimary();
        applied.push('Tip');
      }
      if (!roi2 && partner && partner.w > 0 && partner.h > 0) {
        applyScenePartner();
        applied.push('contact');
      }
      if (applied.length) {
        aiAppliedUndo = snap;
        showAiAppliedChip(applied);
        el('#gen-status').textContent =
          `AI setup applied (${applied.join(', ')}) — Settings opt-in. Undo available.`;
      }
    } catch (_) { /* settings optional in tests */ }
  }

  function undoAiApplied() {
    if (!aiAppliedUndo) return;
    const snap = aiAppliedUndo;
    roi = snap.roi ? { ...snap.roi } : null;
    roi2 = snap.roi2 ? { ...snap.roi2 } : null;
    if (el('#gen-region-class')) el('#gen-region-class').value = snap.tipClass || '';
    if (el('#gen-region-class2')) el('#gen-region-class2').value = snap.contactClass || '';
    clearAiAppliedChip();
    updateRoiLabels();
    updateGenerateEnabled();
    el('#gen-status').textContent = 'AI apply undone — Tip/contact restored.';
    redraw();
  }

  // MT-Seed: IoU gate so Tip+2nd suggestions stay spatially distinct.
  function boxesOverlap(a, b, iouThresh = 0.25) {
    if (!a || !b) return false;
    const ax2 = a.x + a.w, ay2 = a.y + a.h;
    const bx2 = b.x + b.w, by2 = b.y + b.h;
    const ix = Math.max(0, Math.min(ax2, bx2) - Math.max(a.x, b.x));
    const iy = Math.max(0, Math.min(ay2, by2) - Math.max(a.y, b.y));
    const inter = ix * iy;
    if (inter <= 0) return false;
    const union = a.w * a.h + b.w * b.h - inter;
    return union > 0 && (inter / union) >= iouThresh;
  }

  function regionClass2Value() {
    return normalizeClass(el('#gen-region-class2')?.value || '');
  }

  function regionClass1Value() {
    return normalizeClass(el('#gen-region-class')?.value || '');
  }

  /** Apply optional body-part class on Zone 2 (suggest ≠ invent Partner). */
  function applySecondClassFromCandidate(c) {
    const sel = el('#gen-region-class2');
    if (!sel) return '';
    const fromCand = normalizeClass(c?.class || '');
    if (fromCand) {
      if (![...sel.options].some(o => o.value === fromCand)) {
        const opt = document.createElement('option');
        opt.value = fromCand;
        opt.textContent = labelFor(fromCand) || fromCand;
        sel.appendChild(opt);
      }
      sel.value = fromCand;
      return fromCand;
    }
    return regionClass2Value();
  }

  function secondRegionLabel(roiBox, via) {
    const cls = regionClass2Value();
    const clsTag = cls ? `, ${labelFor(cls) || cls}` : '';
    return `Contact area: x=${roiBox.x} y=${roiBox.y} w=${roiBox.w} h=${roiBox.h}`
      + ` (video pixels${via ? `, ${via}` : ''}${clsTag})`;
  }

  function nudgeZone2ClassIfEmpty() {
    const sel = el('#gen-region-class2');
    if (!sel || sel.value) return '';
    sel.style.outline = '2px solid rgba(242,176,61,0.85)';
    setTimeout(() => { if (sel) sel.style.outline = ''; }, 2400);
    return ' Pick partner class (mouth · nipples · breasts) — one contact mark, not a stroke path.';
  }

  /** Ranked list → Tip (#1) + first non-overlapping partner. Suggest only. */
  function suggestTipPartnerPair(list) {
    if (!list || list.length < 2) return null;
    const tip = list[0];
    for (let i = 1; i < list.length; i++) {
      if (!boxesOverlap(tip, list[i])) return { tip, partner: list[i] };
    }
    return { tip, partner: list[1] };
  }

  function setSeedSuggestEnabled(on) {
    const btn = el('#gen-seed-suggest');
    if (!btn) return;
    btn.hidden = !on;
    btn.disabled = !on;
  }

  function clearPendingSeed() {
    pendingSeed = null;
    const status = el('#gen-seed-status');
    if (status) status.textContent = '';
  }

  const SCENE_TYPE_LABELS = {
    blowjob: 'Blowjob',
    handjob: 'Handjob',
    titjob: 'Titjob',
    penetration: 'Penetration',
  };

  function clearSceneProposal() {
    sceneProposalPath = '';
    sceneProposal = null;
    const chip = el('#gen-scene-type-chip');
    if (chip) { chip.style.display = 'none'; chip.textContent = ''; }
    const actions = el('#gen-scene-proposals-actions');
    if (actions) actions.style.display = 'none';
    const status = el('#gen-scene-proposals-status');
    if (status) status.textContent = '';
    const partnerBtn = el('#gen-scene-apply-partner');
    if (partnerBtn) partnerBtn.hidden = true;
  }

  function sceneProposalBox(c) {
    if (!c) return null;
    return {
      x: c.x ?? c.X ?? 0,
      y: c.y ?? c.Y ?? 0,
      w: c.w ?? c.W ?? 0,
      h: c.h ?? c.H ?? 0,
      score: c.score ?? c.Score ?? 0,
      index: c.index ?? c.Index ?? 0,
      class: normalizeClass(c.class || c.Class || ''),
    };
  }

  function openMarkAiDetails() {
    const d = el('#gen-mark-ai-details');
    if (d) d.open = true;
  }

  function renderSceneProposalUI() {
    const chip = el('#gen-scene-type-chip');
    const actions = el('#gen-scene-proposals-actions');
    const status = el('#gen-scene-proposals-status');
    const primaryLab = el('#gen-scene-primary-label');
    const partnerLab = el('#gen-scene-partner-label');
    const partnerBtn = el('#gen-scene-apply-partner');
    if (!sceneProposal || !sceneProposal.found) {
      if (chip) { chip.style.display = 'none'; chip.textContent = ''; }
      if (actions) actions.style.display = 'none';
      if (partnerBtn) partnerBtn.hidden = true;
      return;
    }
    openMarkAiDetails();
    const p = sceneProposal.proposal || {};
    const sceneType = (p.scene_type || p.sceneType || '').toLowerCase();
    const conf = typeof p.confidence === 'number' ? p.confidence : 0;
    const startMs = p.start_ms ?? p.startMs ?? 0;
    const endMs = p.end_ms ?? p.endMs ?? 0;
    const typeLabel = SCENE_TYPE_LABELS[sceneType] || (sceneType || 'Unknown');
    if (chip) {
      chip.style.display = '';
      chip.textContent = `${typeLabel} · ${Math.round(conf * 100)}%`
        + (endMs > startMs ? ` · ${(startMs / 1000).toFixed(0)}–${(endMs / 1000).toFixed(0)}s` : '');
    }
    if (actions) actions.style.display = 'block';
    const primary = sceneProposalBox(p.primary || p.Primary);
    const partner = sceneProposalBox(p.partner || p.Partner);
    if (primaryLab) {
      const cls = primary?.class ? (labelFor(primary.class) || primary.class) : 'primary';
      primaryLab.textContent = `Primary: ${cls}`;
    }
    if (partner && partner.w > 0 && partner.h > 0) {
      if (partnerLab) {
        const cls = partner.class ? (labelFor(partner.class) || partner.class) : 'partner';
        partnerLab.textContent = `Partner: ${cls} (proposal only)`;
      }
      if (partnerBtn) partnerBtn.hidden = false;
    } else {
      if (partnerLab) partnerLab.textContent = '';
      if (partnerBtn) partnerBtn.hidden = true;
    }
    if (status && sceneProposalPath) {
      status.textContent = sceneProposalPath.split(/[\\/]/).pop()
        + (sceneProposal.count ? ` · ${sceneProposal.count} window(s)` : '');
    }
  }

  function drawSceneProposalOverlay() {
    if (!sceneProposal || !sceneProposal.found || !nativeW || !nativeH) return;
    const p = sceneProposal.proposal || {};
    const primary = sceneProposalBox(p.primary || p.Primary);
    const partner = sceneProposalBox(p.partner || p.Partner);
    const prev = pendingSeed;
    pendingSeed = {
      tip: primary ? { index: primary.index || 1 } : null,
      partner: partner && partner.w > 0 ? { index: partner.index || 2 } : null,
    };
    if (primary && primary.w > 0 && primary.h > 0) {
      drawCandidate({ ...primary, index: primary.index || 1 });
    }
    if (partner && partner.w > 0 && partner.h > 0) {
      drawCandidate({ ...partner, index: partner.index || 2 });
    }
    pendingSeed = prev;
  }

  async function refreshSceneProposalAtSeek() {
    if (!sceneProposalPath) return;
    const atMs = Math.round((seekSec || 0) * 1000);
    try {
      const loaded = await LoadSceneProposalAt(sceneProposalPath, atMs);
      sceneProposal = loaded && loaded.found ? loaded : null;
      if (loaded && !loaded.found) {
        const status = el('#gen-scene-proposals-status');
        if (status) status.textContent = 'No proposal at this time.';
      }
      renderSceneProposalUI();
      redraw();
    } catch (err) {
      uiError('Scene proposals: ' + err, el('#gen-scene-proposals-status'));
    }
  }

  async function adoptSceneProposalLoad(loaded, sourceLabel) {
    if (!loaded || !loaded.found) {
      sceneProposal = null;
      renderSceneProposalUI();
      const status = el('#gen-scene-proposals-status');
      if (status) {
        status.textContent = sourceLabel
          ? `${sourceLabel}: none found`
          : 'No scene proposals found.';
      }
      redraw();
      return false;
    }
    sceneProposalPath = loaded.path || sceneProposalPath;
    sceneProposal = loaded;
    renderSceneProposalUI();
    el('#gen-status').textContent =
      'Scene proposal loaded — Apply as Tip to use primary (partner stays proposal-only unless Apply AI setup automatically).';
    redraw();
    await maybeAutoApplySceneProposal();
    return true;
  }

  function applyScenePrimary() {
    if (!sceneProposal || !sceneProposal.found) return;
    const p = sceneProposal.proposal || {};
    const primary = sceneProposalBox(p.primary || p.Primary);
    if (!primary || primary.w <= 0 || primary.h <= 0) return;
    forceCsrtBackend();
    roi = { x: primary.x, y: primary.y, w: primary.w, h: primary.h };
    const tipCls = normalizeClass(
      sceneProposal.regionClass || sceneProposal.region_class || primary.class || '');
    if (tipCls && el('#gen-region-class')) {
      const sel = el('#gen-region-class');
      if (![...sel.options].some(o => o.value === tipCls)) {
        const opt = document.createElement('option');
        opt.value = tipCls;
        opt.textContent = labelFor(tipCls) || tipCls;
        sel.appendChild(opt);
      }
      sel.value = tipCls;
    }
    updateRoiLabels();
    updateGenerateEnabled();
    const sceneType = (p.scene_type || p.sceneType || '').toLowerCase();
    const typeLabel = SCENE_TYPE_LABELS[sceneType] || sceneType || 'scene';
    const stamp = () => {
      el('#gen-roi-label').textContent =
        `Tip (stroke tracker): scene primary — x=${roi.x} y=${roi.y} w=${roi.w} h=${roi.h}`;
    };
    stamp();
    const msg = `Tip applied from scene proposal (${typeLabel}) — partner not applied (no silent ROI2).`;
    el('#gen-status').textContent = msg;
    redraw();
    autoApplyPipeline().then(() => { stamp(); el('#gen-status').textContent = msg; });
  }

  function applyScenePartner() {
    if (!sceneProposal || !sceneProposal.found) return;
    const p = sceneProposal.proposal || {};
    const partner = sceneProposalBox(p.partner || p.Partner);
    if (!partner || partner.w <= 0 || partner.h <= 0) return;
    pickCandidate({
      x: partner.x, y: partner.y, w: partner.w, h: partner.h,
      score: partner.score, index: partner.index || 2, class: partner.class,
    }, 2);
    el('#gen-status').textContent =
      'Contact mark applied from scene partner — correct by hand if needed.';
  }

  function clearPendingAITarget() {
    pendingAITarget = null;
    const status = el('#gen-ai-target-status');
    if (status) status.textContent = '';
  }

  // Cancel any in-flight tip-find / candidates / AI tags / strict AI request.
  // Always calls CancelROIDetection (not only when activeAITargetRequest is set)
  // so classic AutoDetectROI cannot overwrite a paint or a seek frame.
  function cancelAnyRoiRequest(statusMsg) {
    const wasBusy = tipFindBusy || !!activeAITargetRequest;
    const tipFindCreate = wasBusy && pendingGenerateAfterRoi;
    activeAITargetRequest = null;
    if (wasBusy) pendingGenerateAfterRoi = false;
    CancelROIDetection().catch(() => {});
    if (!wasBusy) return;
    if (tipFindCreate && generating) {
      clearCreateLock();
    } else {
      unlockRecognitionControls();
    }
    if (statusMsg) el('#gen-status').textContent = statusMsg;
  }

  function cancelActiveAITargetRequest() {
    cancelAnyRoiRequest();
  }

  function renderPendingAITarget() {
    const status = el('#gen-ai-target-status');
    if (!status) return;
    status.textContent = '';
    if (!pendingAITarget) return;
    const cls = labelFor(pendingAITarget.matchedClass) || pendingAITarget.matchedClass;
    const confidence = Math.round((pendingAITarget.confidence || 0) * 100);
    status.appendChild(document.createTextNode(
      `Detected ${cls} · ${confidence}% — `));
    const apply = document.createElement('button');
    apply.type = 'button';
    apply.textContent = 'Apply target';
    apply.addEventListener('click', () => {
      if (!pendingAITarget) return;
      roi = {
        x: pendingAITarget.x, y: pendingAITarget.y,
        w: pendingAITarget.w, h: pendingAITarget.h,
      };
      const selectedClass = normalizeClass(pendingAITarget.matchedClass || pendingAITarget.expectedClass);
      const classSelect = el('#gen-region-class');
      if (classSelect && selectedClass) classSelect.value = selectedClass;
      pendingAITarget = null;
      status.textContent = `${cls} target applied — CSRT starts here. Optional target-locked rhythm remains under Advanced.`;
      updateRoiLabels();
      updateGenerateEnabled();
      redraw();
      autoApplyPipeline();
    });
    const dismiss = document.createElement('button');
    dismiss.type = 'button';
    dismiss.textContent = 'Dismiss';
    dismiss.style.marginLeft = '6px';
    dismiss.addEventListener('click', () => {
      clearPendingAITarget();
      redraw();
    });
    status.appendChild(apply);
    status.appendChild(dismiss);
  }

  function renderPendingSeedStatus() {
    const status = el('#gen-seed-status');
    if (!status) return;
    status.textContent = '';
    if (!pendingSeed) return;
    const tip = pendingSeed.tip, partner = pendingSeed.partner;
    status.appendChild(document.createTextNode(
      `Suggest Tip #${tip.index} + 2nd #${partner.index} — `));
    const applyBtn = document.createElement('button');
    applyBtn.type = 'button';
    applyBtn.textContent = 'Apply';
    applyBtn.addEventListener('click', () => applyPendingSeed());
    const dismissBtn = document.createElement('button');
    dismissBtn.type = 'button';
    dismissBtn.textContent = 'Dismiss';
    dismissBtn.style.marginLeft = '6px';
    dismissBtn.addEventListener('click', () => {
      clearPendingSeed();
      el('#gen-status').textContent =
        'Suggestion dismissed — Everyday tip-CSRT if you skip the contact mark.';
      redraw();
    });
    status.appendChild(applyBtn);
    status.appendChild(dismissBtn);
  }

  function applyPendingSeed() {
    if (!pendingSeed) return;
    const { tip, partner } = pendingSeed;
    roi = { x: tip.x, y: tip.y, w: tip.w, h: tip.h };
    roi2 = { x: partner.x, y: partner.y, w: partner.w, h: partner.h };
    const tipCls = normalizeClass(tip.class || '');
    if (tipCls && el('#gen-region-class')) {
      const sel = el('#gen-region-class');
      if (![...sel.options].some(o => o.value === tipCls)) {
        const opt = document.createElement('option');
        opt.value = tipCls;
        opt.textContent = labelFor(tipCls) || tipCls;
        sel.appendChild(opt);
      }
      sel.value = tipCls;
    }
    applySecondClassFromCandidate(partner);
    clearPendingSeed();
    setRoi2Mode(false);
    updateRoiLabels();
    updateProfileUi();
    updateGenerateEnabled();
    const stampSeedLabels = () => {
      el('#gen-roi-label').textContent =
        `Tip candidate #${tip.index} — x=${tip.x} y=${tip.y} w=${tip.w} h=${tip.h}`;
      el('#gen-roi2-label').textContent =
        `2nd candidate #${partner.index} — x=${partner.x} y=${partner.y} w=${partner.w} h=${partner.h}`;
    };
    stampSeedLabels();
    autoApplyPipeline().then(stampSeedLabels);
    const nudge = nudgeZone2ClassIfEmpty();
    el('#gen-status').textContent =
      `Tip #${tip.index} + contact #${partner.index} applied — correct by hand if needed.`
      + ` Contact mark was never auto-filled.${nudge}`;
    redraw();
  }

  function drawCandidate(c) {
    if (!c || !nativeW || !nativeH) return;
    const scaleX = canvas.width / nativeW, scaleY = canvas.height / nativeH;
    const dx0 = c.x * scaleX, dy0 = c.y * scaleY, dw = c.w * scaleX, dh = c.h * scaleY;
    const isTip = pendingSeed && pendingSeed.tip && pendingSeed.tip.index === c.index;
    const isPartner = pendingSeed && pendingSeed.partner && pendingSeed.partner.index === c.index;
    ctx.save();
    ctx.setLineDash([5, 4]);
    if (isTip) {
      ctx.strokeStyle = 'rgba(61, 204, 192, 0.98)';
      ctx.fillStyle = 'rgba(61, 204, 192, 0.22)';
    } else if (isPartner) {
      ctx.strokeStyle = 'rgba(242, 176, 61, 0.98)';
      ctx.fillStyle = 'rgba(242, 176, 61, 0.22)';
    } else {
      ctx.strokeStyle = 'rgba(120, 200, 255, 0.95)';
      ctx.fillStyle = 'rgba(120, 200, 255, 0.12)';
    }
    ctx.lineWidth = 2;
    ctx.strokeRect(dx0, dy0, dw, dh);
    ctx.fillRect(dx0, dy0, dw, dh);
    let label = '#' + (c.index || '?');
    if (isTip) label += ' Tip';
    else if (isPartner) label += ' 2nd';
    const cls = normalizeClass(c.class || '')
      || (isPartner ? regionClass2Value() : '')
      || (isTip ? regionClass1Value() : '');
    if (cls) label += ' ' + (labelFor(cls) || cls);
    ctx.setLineDash([]);
    ctx.font = '600 13px system-ui, sans-serif';
    ctx.fillStyle = 'rgba(10, 20, 30, 0.75)';
    ctx.fillRect(dx0 + 2, dy0 + 2, ctx.measureText(label).width + 8, 18);
    ctx.fillStyle = isPartner ? '#ffe9c2' : '#dff3ff';
    ctx.fillText(label, dx0 + 6, dy0 + 15);
    ctx.restore();
  }

  function hitCandidate(canvasX, canvasY) {
    if (!candidates.length || !nativeW || !nativeH) return null;
    const scaleX = canvas.width / nativeW, scaleY = canvas.height / nativeH;
    const vx = canvasX / scaleX, vy = canvasY / scaleY;
    // Prefer smallest containing box (most specific).
    let best = null, bestArea = Infinity;
    for (const c of candidates) {
      if (vx >= c.x && vx <= c.x + c.w && vy >= c.y && vy <= c.y + c.h) {
        const area = c.w * c.h;
        if (area < bestArea) {
          best = c;
          bestArea = area;
        }
      }
    }
    return best;
  }

  /** zone 1 = tip, zone 2 = body-part / contact. Never fills the other zone (issue #8). */
  function pickCandidate(c, zone = 1) {
    if (!c) return;
    if (zone === 2) {
      roi2 = { x: c.x, y: c.y, w: c.w, h: c.h };
      applySecondClassFromCandidate(c);
      setRoi2Mode(false);
      updateRoiLabels();
      updateGenerateEnabled();
      el('#gen-roi2-label').textContent =
        secondRegionLabel(roi2, `candidate #${c.index}`);
      const nudge = nudgeZone2ClassIfEmpty();
      const msg = `2nd mark set from candidate #${c.index} — correct by hand if needed.${nudge}`;
      el('#gen-status').textContent = msg;
      redraw();
      // Re-assert status after pipeline hint (SuggestPipeline → updateProfileUi).
      autoApplyPipeline().then(() => { el('#gen-status').textContent = msg; });
      return;
    }
    roi = { x: c.x, y: c.y, w: c.w, h: c.h };
    const tipCls = normalizeClass(c.class || '');
    if (tipCls && el('#gen-region-class')) {
      const sel = el('#gen-region-class');
      if (![...sel.options].some(o => o.value === tipCls)) {
        const opt = document.createElement('option');
        opt.value = tipCls;
        opt.textContent = labelFor(tipCls) || tipCls;
        sel.appendChild(opt);
      }
      sel.value = tipCls;
    }
    // Never auto-fill Zone 2 from a Zone 1 pick (issue #8 / TFTJ 4b / MT-Seed).
    updateRoiLabels();
    // Keep the candidate index in the label (MT-Seed tests + user feedback).
    el('#gen-roi-label').textContent =
      `Tip (stroke tracker): candidate #${c.index} — x=${roi.x} y=${roi.y} w=${roi.w} h=${roi.h}`;
    updateGenerateEnabled();
    const msg =
      `Tip set from candidate #${c.index} — optional: Shift-click a 2nd body-part mark, or Suggest Tip+2nd. Everyday tip alone is fine.`;
    el('#gen-status').textContent = msg;
    redraw();
    autoApplyPipeline().then(() => { el('#gen-status').textContent = msg; });
  }

  function drawDragRect(stroke, fill, dashed) {
    const dx0 = Math.min(startX, curX), dy0 = Math.min(startY, curY);
    const dw = Math.abs(curX - startX), dh = Math.abs(curY - startY);
    ctx.save();
    if (dashed) ctx.setLineDash([6, 4]);
    ctx.strokeStyle = stroke;
    ctx.lineWidth = 2;
    ctx.strokeRect(dx0, dy0, dw, dh);
    ctx.fillStyle = fill;
    ctx.fillRect(dx0, dy0, dw, dh);
    ctx.restore();
  }

  function redraw() {
    ctx.clearRect(0, 0, canvas.width, canvas.height);
    if (img.src) ctx.drawImage(img, 0, 0, canvas.width, canvas.height);
    drawSceneMapOverlay();
    drawSceneProposalOverlay();
    for (const c of candidates) drawCandidate(c);
    if (pendingAITarget) drawNativeRect(pendingAITarget, AI_TARGET_STROKE, AI_TARGET_FILL, true);
    const dragRoi2 = dragging && draggingSecond && !markMode && !sceneMapMarkMode;
    const dragRoi1 = dragging && !draggingSecond && !markMode && !sceneMapMarkMode;
    if (roi && !dragRoi1) drawNativeRect(roi, ROI1_STROKE, ROI1_FILL);
    if (roi2 && !dragRoi2) drawNativeRect(roi2, ROI2_STROKE, ROI2_FILL);
    for (const t of extraTargets) drawNativeRect(t, TARGET_STROKE, TARGET_FILL);
    for (const m of maskRois) drawNativeRect(m, MASK_STROKE, MASK_FILL, true);
    const selRect = selectedMarkNativeRect();
    if (selRect) drawSelectedOutline(selRect);
    if (dragging) {
      if (sceneMapMarkMode === 'exclude') drawDragRect('#111111', 'rgba(0,0,0,0.35)', true);
      else if (sceneMapMarkMode === 'source') drawDragRect('#3dccc0', 'rgba(61,204,192,0.2)');
      else if (sceneMapMarkMode === 'region') drawDragRect('#f2b03d', 'rgba(242,176,61,0.2)');
      else if (markMode === 'mask') drawDragRect(MASK_STROKE, MASK_FILL, true);
      else if (markMode === 'target') drawDragRect(TARGET_STROKE, TARGET_FILL);
      else if (draggingSecond) drawDragRect(ROI2_STROKE, ROI2_FILL);
      else drawDragRect(ROI1_STROKE, ROI1_FILL);
    }
  }

  canvas.addEventListener('mousedown', e => {
    cancelAnyRoiRequest();
    clearPendingAITarget();
    const r = canvas.getBoundingClientRect();
    startX = curX = e.clientX - r.left;
    startY = curY = e.clientY - r.top;
    dragging = true;
    draggingSecond = !markMode && (roi2Mode || e.shiftKey);
  });
  window.addEventListener('keydown', (e) => {
    if (e.key === 'Escape' && selectedMark) {
      setSelectedMark(null);
      el('#gen-status').textContent = 'Mark deselected.';
    }
  });
  // Track drag on window so painting still works when the pointer leaves the
  // canvas (scrolled preview / tip-find copy pushing the frame down).
  window.addEventListener('mousemove', e => {
    if (!dragging) return;
    const r = canvas.getBoundingClientRect();
    curX = Math.max(0, Math.min(canvas.width, e.clientX - r.left));
    curY = Math.max(0, Math.min(canvas.height, e.clientY - r.top));
    redraw();
  });
  window.addEventListener('mouseup', () => {
    if (!dragging) return;
    dragging = false;
    const wasSecond = draggingSecond;
    const mode = markMode;
    const smMode = sceneMapMarkMode;
    draggingSecond = false;
    const w = Math.abs(curX - startX), h = Math.abs(curY - startY);
    // Tiny press: pick a motion candidate if shown (TFTJ 4b / MT-Seed).
    // Click → Tip (Zone 1). Shift / Zone-2 mode → Partner (Zone 2). Never auto-fills the other.
    // Else: click an existing painted mark to select (body map sets class).
    if (w < 8 && h < 8) {
      if (!mode && !smMode && candidates.length) {
        const hit = hitCandidate(startX, startY);
        if (hit) {
          pickCandidate(hit, wasSecond ? 2 : 1);
          return;
        }
      }
      if (!mode && !smMode) {
        const painted = hitPaintedMark(startX, startY);
        setSelectedMark(painted);
        if (painted) {
          el('#gen-status').textContent =
            'Mark selected — click body map to set class, or Delete selected.';
        }
        return;
      }
      redraw();
      return;
    }
    setSelectedMark(null);
    const scaleX = nativeW / canvas.width, scaleY = nativeH / canvas.height;
    const x0 = Math.min(startX, curX), y0 = Math.min(startY, curY);
    const box = {
      x: Math.round(x0 * scaleX), y: Math.round(y0 * scaleY),
      w: Math.round(w * scaleX), h: Math.round(h * scaleY),
    };
    if (smMode) {
      const win = activeSceneMapWindow();
      sceneMapMarkSeq += 1;
      const sticky = !!el('#gen-scene-map-mark-sticky')?.checked;
      // Ignore/source follow the subject by default (Owner: marks must move).
      const follow = smMode === 'region' ? false : !sticky;
      sceneMapMarks.push({
        id: `m${sceneMapMarkSeq}`,
        kind: smMode,
        rect: box,
        fromMs: win?.startMs || 0,
        toMs: win?.endMs || 0,
        class: smMode === 'region' ? (el('#gen-scene-map-mark-class')?.value || '') : '',
        author: 'user',
        follow,
      });
      setSceneMapMarkMode(false);
      updateSceneMapMarksLabel();
      const followHint = follow ? ', follows subject' : ', stay fixed';
      el('#gen-status').textContent =
        `Scene map ${smMode === 'exclude' ? 'ignore (black)' : smMode} mark added (${((win?.startMs || 0) / 1000).toFixed(0)}–${((win?.endMs || 0) / 1000).toFixed(0)}s${followHint}). Click body map if Region.`;
      setSelectedMark({ kind: 'scene', id: `m${sceneMapMarkSeq}` });
      return;
    }
    if (mode === 'target') {
      const cls = el('#gen-target-class')?.value || '';
      const follow = contactExtraShouldFollow();
      extraTargets.push({ ...box, fixed: !follow, class: cls });
      setMarkMode(null);
      const tag = cls ? ` (${cls})` : '';
      const modeHint = follow ? ', follows partner path' : ', stay fixed';
      el('#gen-status').textContent = `Extra contact #${extraTargets.length}${tag} added${modeHint}.`;
      setSelectedMark({ kind: 'extra', index: extraTargets.length - 1 });
      updateRoiLabels();
      updateGenerateEnabled();
      return;
    } else if (mode === 'mask') {
      // Soft mask → whole-clip black ignore that follows the subject.
      sceneMapMarkSeq += 1;
      sceneMapMarks.push({
        id: `mask${sceneMapMarkSeq}`,
        kind: 'exclude',
        rect: box,
        fromMs: 0,
        toMs: 0,
        author: 'user',
        follow: true,
      });
      setMarkMode(null);
      updateSceneMapMarksLabel();
      el('#gen-status').textContent = `Ignore region (black) #${sceneMapMarks.filter(m => m.kind === 'exclude').length} added — follows subject; not used for recognition.`;
      setSelectedMark({ kind: 'scene', id: `mask${sceneMapMarkSeq}` });
      updateRoiLabels();
      updateGenerateEnabled();
      return;
    } else if (wasSecond) {
      roi2 = box;
      setRoi2Mode(false);
      // Zone 2 is optional for Contact vib — do not switch to legacy Tf/Tj.
      el('#gen-status').textContent = contactVibrationOn()
        ? 'Snack 2 done — partner touch (gold). Optional: + Another, or continue to Feel → Create.'
        : 'Optional contact zone set.';
      setSelectedMark({ kind: 'contact' });
    } else {
      roi = box;
      setSelectedMark({ kind: 'tip' });
      el('#gen-status').textContent = contactVibrationOn()
        ? 'Snack 1 done — tip box is the stroke tracker (CSRT). Next: Mark contact (gold), or skip to Create.'
        : 'Tip box set — CSRT follows this look; Find tip again after a big camera-angle change.';
    }
    updateRoiLabels();
    updateGenerateEnabled();
    autoApplyPipeline();
    redraw();
  });

  async function autoApplyPipeline() {
    const w = roi?.w || 0, h = roi?.h || 0;
    const w2 = roi2?.w || 0, h2 = roi2?.h || 0;
    try {
      const s = await SuggestPipeline(w, h, w2, h2);
      if (!s) return;
      // Manual backend/profile choices survive ROI redraws; only auto-fill
      // when the user has not touched the dropdowns yet.
      const backendTouched = el('#gen-backend').dataset.userTouched === '1';
      const profileTouched = el('#gen-profile').dataset.userTouched === '1';
      if (s.Backend && !backendTouched) el('#gen-backend').value = s.Backend;
      if (s.Profile && !profileTouched) {
        const prof = (s.Profile === 'tf' || s.Profile === 'tj') ? 'standard' : s.Profile;
        el('#gen-profile').value = prof;
        updateProfileUi();
      }
      const pipe = el('#gen-pipeline-auto');
      if (pipe) {
        pipe.textContent = (s.Reason || '') + (s.GoPath ? ' · Go path' : ' · Python path');
      }
      updateGenerateEnabled();
    } catch (err) {
      const pipe = el('#gen-pipeline-auto');
      if (pipe) pipe.textContent = 'Pipeline hint unavailable';
    }
  }

  // Fallengelassenes video Apply. Teilt sich den Ladeweg mit der
  // Dateiauswahl, damit beide Wege garantiert dasselbe tun.
  // paths[] = full multi-drop list (current first); queue holds all clips.
  // Workspace (V1 W3): offer the Play video in Create step 1 when it differs.
  function playerVideoPath() {
    return document.getElementById('tab-playback')?.dataset.pbVideoPath || '';
  }
  function syncUsePlayerVideo() {
    const row = el('#gen-use-player-row');
    if (!row) return;
    const pv = playerVideoPath();
    const show = !!pv && pv !== videoPath && !generating;
    row.hidden = !show;
    if (show) el('#gen-use-player-name').textContent = pv.split(/[\\/]/).pop();
  }
  el('#gen-use-player-video')?.addEventListener('click', () => {
    const pv = playerVideoPath();
    if (pv && pv !== videoPath) loadVideo(pv);
  });
  const pbRoot = document.getElementById('tab-playback');
  if (pbRoot) {
    new MutationObserver(syncUsePlayerVideo).observe(pbRoot, { attributes: true, attributeFilter: ['data-pb-video-path'] });
  }
  window.addEventListener('ws:view', syncUsePlayerVideo);
  syncUsePlayerVideo();

  window.addEventListener('drop:video', e => {
    const path = e.detail.path;
    const paths = (e.detail.paths && e.detail.paths.length)
      ? e.detail.paths
      : [path];
    const rest = paths[0] === path ? paths.slice(1) : paths.filter(p => p !== path);
    loadVideo(path, Math.max(0, paths.length - 1), rest);
  });
  el('#gen-queue-clear')?.addEventListener('click', () => {
    clearQueueKeepCurrent();
    if (videoPath) {
      el('#gen-status').textContent = 'Queue cleared — current clip kept.';
    }
  });
  el('#gen-queue-run')?.addEventListener('click', () => startQueueRun());

  let postPreviewTimer = 0;
  function probePointsToAttr(sample, t0, span, w, h, pad) {
    return sample.map((p) => {
      const t = Number(p.atMs ?? p.AtMs ?? 0);
      const pos = Math.max(0, Math.min(100, Number(p.pos ?? p.Pos ?? 0)));
      const x = pad + ((t - t0) / span) * (w - 2 * pad);
      const y = pad + (1 - pos / 100) * (h - 2 * pad);
      return `${x.toFixed(1)},${y.toFixed(1)}`;
    }).join(' ');
  }
  async function refreshPostprocessPreview() {
    const out = el('#gen-postprocess-preview');
    const poly = el('#gen-postprocess-poly');
    const rawPoly = el('#gen-postprocess-raw');
    const peaksG = el('#gen-postprocess-peaks');
    if (!out) return;
    const prominenceRaw = parseFloat(el('#gen-prominence')?.value);
    try {
      const res = await PreviewPostprocess({
        // intOr (not ||): Smooth/Peakdist sliders allow min=0; package API
        // treats 0 as "use default" — do not coerce to 11/150 in the GUI.
        smoothWindow: intOr(el('#gen-smooth')?.value, 11),
        minPeakDistanceMs: intOr(el('#gen-peakdist')?.value, 150),
        peakProminence: Number.isFinite(prominenceRaw) && prominenceRaw > 0 ? prominenceRaw : 0,
        rdpTolerance: finiteOr(el('#gen-rdp')?.value, 0),
        adaptiveKeyframeError: el('#gen-adaptive')?.checked ? 6 : 0,
        maxSpeed: finiteOr(el('#gen-maxspeed')?.value, 0),
      });
      const hz = (res.meanHz || res.MeanHz || 0);
      const kf = res.keyframeCount ?? res.KeyframeCount ?? 0;
      const peaksN = res.peakCount ?? res.PeakCount ?? 0;
      const valleysN = res.valleyCount ?? res.ValleyCount ?? 0;
      const badge = (id, text) => { const b = el(id); if (b) b.textContent = text; };
      badge('#gen-pp-badge-kf', `${kf} kf`);
      badge('#gen-pp-badge-peaks', `${peaksN} peaks`);
      badge('#gen-pp-badge-valleys', `${valleysN} valleys`);
      badge('#gen-pp-badge-hz', hz > 0 ? `~${hz.toFixed(2)} Hz` : '— Hz');
      const hzBit = hz > 0 ? ` · ~${hz.toFixed(2)} Hz` : '';
      out.textContent = (res.hint || res.Hint || 'Postprocess probe') + hzBit;
      const sample = res.sample || res.Sample || [];
      const raw = res.rawSample || res.RawSample || [];
      const peaks = res.peaks || res.Peaks || [];
      const w = 320;
      const h = 72;
      const pad = 4;
      const timeBase = sample.length >= 2 ? sample : raw;
      if (Array.isArray(timeBase) && timeBase.length >= 2) {
        const t0 = Number(timeBase[0].atMs ?? timeBase[0].AtMs ?? 0);
        const t1 = Number(timeBase[timeBase.length - 1].atMs ?? timeBase[timeBase.length - 1].AtMs ?? 1);
        const span = Math.max(1, t1 - t0);
        if (rawPoly) {
          rawPoly.setAttribute('points', Array.isArray(raw) && raw.length >= 2
            ? probePointsToAttr(raw, t0, span, w, h, pad) : '');
        }
        if (poly) {
          poly.setAttribute('points', Array.isArray(sample) && sample.length >= 2
            ? probePointsToAttr(sample, t0, span, w, h, pad) : '');
        }
        if (peaksG) {
          peaksG.innerHTML = '';
          if (Array.isArray(peaks)) {
            peaks.forEach((p) => {
              const t = Number(p.atMs ?? p.AtMs ?? 0);
              const pos = Math.max(0, Math.min(100, Number(p.pos ?? p.Pos ?? 0)));
              const x = pad + ((t - t0) / span) * (w - 2 * pad);
              const y = pad + (1 - pos / 100) * (h - 2 * pad);
              const c = document.createElementNS('http://www.w3.org/2000/svg', 'circle');
              c.setAttribute('cx', x.toFixed(1));
              c.setAttribute('cy', y.toFixed(1));
              c.setAttribute('r', '2.4');
              peaksG.appendChild(c);
            });
          }
        }
      } else {
        if (poly) poly.setAttribute('points', '');
        if (rawPoly) rawPoly.setAttribute('points', '');
        if (peaksG) peaksG.innerHTML = '';
      }
    } catch (err) {
      out.textContent = 'Postprocess probe unavailable: ' + err;
      if (poly) poly.setAttribute('points', '');
      if (rawPoly) rawPoly.setAttribute('points', '');
      if (peaksG) peaksG.innerHTML = '';
    }
  }
  function schedulePostprocessPreview() {
    clearTimeout(postPreviewTimer);
    postPreviewTimer = setTimeout(refreshPostprocessPreview, 180);
  }
  function syncExpertKnobLabels() {
    const map = [
      ['#gen-smooth', '#gen-smooth-val', (v) => String(Math.round(Number(v) || 0))],
      ['#gen-peakdist', '#gen-peakdist-val', (v) => String(Math.round(Number(v) || 0))],
      ['#gen-prominence', '#gen-prominence-val', (v) => {
        const n = Number(v);
        if (!Number.isFinite(n) || n === 0) return '0';
        return String(Math.round(n * 100) / 100);
      }],
      ['#gen-rdp', '#gen-rdp-val', (v) => {
        const n = Number(v);
        if (!Number.isFinite(n) || n === 0) return '0';
        return String(Math.round(n * 10) / 10);
      }],
      ['#gen-maxspeed', '#gen-maxspeed-val', (v) => String(Math.round(Number(v) || 0))],
    ];
    for (const [inpSel, labSel, fmt] of map) {
      const inp = el(inpSel);
      const lab = el(labSel);
      if (inp && lab) lab.textContent = fmt(inp.value);
    }
  }
  syncExpertKnobLabels();
  ['#gen-smooth', '#gen-peakdist', '#gen-prominence', '#gen-rdp', '#gen-maxspeed'].forEach(sel => {
    el(sel)?.addEventListener('input', () => {
      syncExpertKnobLabels();
      schedulePostprocessPreview();
    });
    el(sel)?.addEventListener('change', () => {
      syncExpertKnobLabels();
      schedulePostprocessPreview();
    });
  });
  el('#gen-smooth')?.addEventListener('change', e => {
    saveSetting('generator.smooth_window', intOr(e.target.value, 11));
  });
  el('#gen-peakdist')?.addEventListener('change', e => {
    saveSetting('generator.min_peak_distance_ms', intOr(e.target.value, 150));
  });
  el('#gen-prominence')?.addEventListener('change', e => {
    saveSetting('generator.peak_prominence', finiteOr(e.target.value, 0));
  });
  el('#gen-rdp')?.addEventListener('change', e => {
    saveSetting('generator.rdp_tolerance', finiteOr(e.target.value, 0));
  });
  el('#gen-maxspeed')?.addEventListener('change', e => {
    saveSetting('generator.max_speed', finiteOr(e.target.value, 0));
  });
  el('#gen-adaptive')?.addEventListener('change', e => {
    schedulePostprocessPreview();
    saveSetting('generator.adaptive_keyframe', !!e.target.checked);
  });
  el('#gen-advanced-expert')?.addEventListener('toggle', () => {
    if (el('#gen-advanced-expert')?.open) refreshPostprocessPreview();
  });

  // Restore Create Feel + Expert knobs from settings (defaults match HTML).
  getSettingsCache().then((s) => {
    if (!s) return;
    if (s.genContactVibrationSpan != null && el('#gen-contact-span')) {
      const span = Math.round(Number(s.genContactVibrationSpan));
      if (Number.isFinite(span) && span >= 40 && span <= 95) {
        el('#gen-contact-span').value = String(span);
        updateContactSpanLabel();
      }
    }
    const curve = s.genContactVibrationCurve;
    if (curve && el('#gen-contact-curve')
        && ['linear', 'soft', 'peak', 'impulse'].includes(curve)) {
      el('#gen-contact-curve').value = curve;
      el('#gen-contact-curve').dataset.userTouched = '1';
      const impulse = el('#gen-contact-impulse');
      if (impulse) impulse.checked = curve === 'impulse';
    }
    if (s.genSmoothWindow != null && el('#gen-smooth')) {
      el('#gen-smooth').value = String(intOr(s.genSmoothWindow, 11));
    }
    if (s.genMinPeakDistanceMs != null && el('#gen-peakdist')) {
      el('#gen-peakdist').value = String(intOr(s.genMinPeakDistanceMs, 150));
    }
    if (s.genPeakProminence != null && el('#gen-prominence')) {
      el('#gen-prominence').value = String(finiteOr(s.genPeakProminence, 0));
    }
    if (s.genRDPTolerance != null && el('#gen-rdp')) {
      el('#gen-rdp').value = String(finiteOr(s.genRDPTolerance, 0));
    }
    if (s.genMaxSpeed != null && el('#gen-maxspeed')) {
      el('#gen-maxspeed').value = String(finiteOr(s.genMaxSpeed, 0));
    }
    if (typeof s.genAdaptiveKeyframe === 'boolean' && el('#gen-adaptive')) {
      el('#gen-adaptive').checked = s.genAdaptiveKeyframe;
    }
    syncExpertKnobLabels();
    scheduleContactVibPreview();
  }).catch(() => { /* settings optional in tests */ });

  async function startQueueRun() {
    if (queueRunning || !generateQueue.some(q => q.status === 'queued')) return;
    // Ask once for overwrite policy for the whole batch.
    let needOverwriteAsk = false;
    try {
      for (const q of generateQueue) {
        if (q.status !== 'queued') continue;
        if (await ScriptExistsForVideo(q.path)) {
          needOverwriteAsk = true;
          break;
        }
      }
      if (videoPath && await ScriptExistsForVideo(videoPath)) {
        needOverwriteAsk = true;
      }
    } catch (_) { /* prefer not to overwrite */ }
    queueOverwrite = false;
    if (needOverwriteAsk) {
      if (!confirm('One or more scripts already exist beside queued videos. Replace existing Emotion Scripts / funscripts for this queue?')) {
        // Still run, but skip existing (overwrite=false → backend refuses; we mark skipped).
        queueOverwrite = false;
      } else {
        queueOverwrite = true;
      }
    }
    queueRunning = true;
    queueDoneCount = generateQueue.filter(q => q.status === 'done').length;
    queueBatchStartedAt = Date.now();
    renderQueue();
    // If current video is set and matches first queued, or no current — advance.
    const next = generateQueue.find(q => q.status === 'queued');
    if (!next) {
      queueRunning = false;
      renderQueue();
      return;
    }
    // Start with current video if it is the next queued item; else load next.
    if (videoPath === next.path) {
      next.status = 'running';
      renderQueue();
      pendingGenerateAfterRoi = true;
      generating = true;
      el('#gen-generate').disabled = true;
      el('#gen-cancel').disabled = false;
      el('#gen-status').textContent = queueProgressPrefix() + 'Finding tip, then creating…';
      syncWorkflowSteps();
      if (roi) {
        generate({ fromQueue: true });
      } else {
        startAutoFindRegion();
      }
      return;
    }
    await loadVideo(next.path, 0, null, { fromQueueAdvance: true });
    const entry = generateQueue.find(q => q.path === next.path);
    if (entry) entry.status = 'running';
    renderQueue();
    pendingGenerateAfterRoi = true;
    generating = true;
    el('#gen-generate').disabled = true;
    el('#gen-cancel').disabled = false;
    el('#gen-status').textContent = queueProgressPrefix() + 'Finding tip, then creating…';
    syncWorkflowSteps();
    startAutoFindRegion();
  }

  async function advanceQueueAfterDone(ok, errMsg) {
    if (!queueRunning) return;
    const cur = generateQueue.find(q => q.status === 'running')
      || generateQueue.find(q => q.path === videoPath);
    if (cur) {
      if (ok) {
        cur.status = 'done';
        queueDoneCount++;
      } else if (errMsg && /already exists/i.test(errMsg) && !queueOverwrite) {
        cur.status = 'skipped';
        queueDoneCount++;
      } else {
        cur.status = 'failed';
        queueDoneCount++;
      }
    }
    renderQueue();
    if (userCancelRequested) {
      for (const q of generateQueue) {
        if (q.status === 'queued' || q.status === 'running') q.status = 'skipped';
      }
      queueRunning = false;
      renderQueue();
      el('#gen-status').textContent = 'Queue canceled.';
      return;
    }
    const next = generateQueue.find(q => q.status === 'queued');
    if (!next) {
      queueRunning = false;
      renderQueue();
      const failed = generateQueue.filter(q => q.status === 'failed' || q.status === 'skipped').length;
      const done = generateQueue.filter(q => q.status === 'done').length;
      const elapsed = queueBatchStartedAt
        ? Math.round((Date.now() - queueBatchStartedAt) / 1000)
        : 0;
      el('#gen-status').textContent =
        `Queue done — ${done} created` +
        (failed ? `, ${failed} skipped/failed` : '') +
        (elapsed ? ` · ${elapsed < 60 ? elapsed + ' s' : Math.round(elapsed / 60) + ' min'} total` : '') +
        '. Review the last clip below.';
      return;
    }
    // Continue batch: load next + auto-find + generate.
    await loadVideo(next.path, 0, null, { fromQueueAdvance: true });
    next.status = 'running';
    renderQueue();
    pendingGenerateAfterRoi = true;
    generating = true;
    el('#gen-generate').disabled = true;
    el('#gen-cancel').disabled = false;
    el('#gen-status').textContent = queueProgressPrefix() + 'Finding tip, then creating…';
    syncWorkflowSteps();
    startAutoFindRegion();
  }

  async function chooseVideo() {
    const path = await PickVideoFile();
    // Fokus zurück ins Fenster - siehe gleichnamige Behandlung in
    // playback.js: nach dem nativen Datei-Dialog bleibt der Tastaturfokus
    // sonst am Button hängen.
    if (document.activeElement && document.activeElement.blur) {
      document.activeElement.blur();
    }
    window.focus();
    if (!path) return;
    await loadVideo(path);
  }

  // Project header (V1 target create.png): real facts from ffprobe and a
  // poster frame. Values the probe cannot read are left out, never guessed.
  let projectHeadSeq = 0;
  function fmtClock(sec) {
    const s = Math.max(0, Math.round(sec));
    const h = Math.floor(s / 3600), m = Math.floor((s % 3600) / 60), r = s % 60;
    const two = n => String(n).padStart(2, '0');
    return h ? `${h}:${two(m)}:${two(r)}` : `${two(m)}:${two(r)}`;
  }
  async function refreshProjectHead(path) {
    const seq = ++projectHeadSeq;
    const head = el('#gen-project-head');
    const poster = el('#gen-project-poster');
    if (!head) return;
    head.hidden = !path;
    if (!path) return;
    el('#gen-project-name').textContent = path.split(/[\\/]/).pop();
    el('#gen-project-meta').textContent = 'Reading video details…';
    poster.hidden = true;
    poster.removeAttribute('src');
    try {
      const info = await ProbeCreateVideo(path);
      if (seq !== projectHeadSeq) return;
      const parts = [];
      if (info.width > 0 && info.height > 0) parts.push(`${info.width} × ${info.height}`);
      if (info.fps > 0) parts.push(`${Number(info.fps.toFixed(2))} fps${info.vfr ? ' (variable)' : ''}`);
      if (info.durationSec > 0) parts.push(fmtClock(info.durationSec));
      if (info.codec) parts.push(String(info.codec).toUpperCase());
      el('#gen-project-meta').textContent = parts.join('  ·  ');
      if (info.posterUrl) {
        poster.src = info.posterUrl;
        poster.hidden = false;
      }
    } catch (err) {
      if (seq !== projectHeadSeq) return;
      el('#gen-project-meta').textContent = 'Video details not readable — ' + (err?.message || err);
    }
  }

  async function loadVideo(path, extraCount = 0, remainingPaths = null, opts = {}) {
    cancelActiveAITargetRequest();
    videoPath = path;
    refreshProjectHead(path);
    // Workspace (V1 W3): Play lists this clip under its recent videos, and
    // the "Use Player video" offer hides once both use the same file.
    window.dispatchEvent(new CustomEvent('gen:video', { detail: { path } }));
    syncUsePlayerVideo();
    lastSceneMap = null;
    sceneMapWinIdx = 0;
    sceneMapMarks = [];
    sceneMapMarkMode = null;
    clearSceneProposal();
    const smStatus = el('#gen-scene-map-status');
    if (smStatus) smStatus.textContent = '';
    syncSceneMapTools();
    const loadSuggestionSeq = ++profileSuggestionSeq;
    seekSec = 0;
    el('#gen-seek').value = '0';
    el('#gen-seek').disabled = false;
    el('#gen-seek-btn').disabled = false;
    el('#gen-seek-plus').disabled = false;
    el('#gen-seek-plus5').disabled = false;
    el('#gen-video-path').textContent = path.split(/[\\/]/).pop();
    const emptyCta = el('#gen-empty-cta');
    if (emptyCta) emptyCta.hidden = true;
    el('#gen-status').textContent = 'Loading preview frame…';
    roi = null;
    roi2 = null;
    clearPendingAITarget();
    setAIDraftPending(null);
    aiDraftCurveActive = false;
    genCurveBeforeAIDraft = null;
    genCurvePoints = null;
    setPosOverlayVisible(false);
    extraTargets = [];
    maskRois = [];
    setMarkMode(null);
    // Neues video: Pipeline-Vorschläge wieder erlauben.
    delete el('#gen-backend').dataset.userTouched;
    delete el('#gen-profile').dataset.userTouched;
    setRoi2Mode(false);
    el('#gen-generate').disabled = true;
    updateRoiLabels();
    // Multi-drop → queue (DeepFunGen UX). remainingPaths = videos after current.
    // fromQueueAdvance: keep existing queue entries; only refresh status UI.
    // keepQueue: a clip card was clicked — same, but auto-find still runs.
    if (!opts.fromQueueAdvance && !opts.keepQueue) {
      if (Array.isArray(remainingPaths) && remainingPaths.length) {
        // Full batch including the clip just loaded (DeepFunGen-style queue).
        setQueueFromPaths([path, ...remainingPaths]);
        videoBatchNote = ` (${remainingPaths.length} more in queue)`;
      } else if (extraCount > 0 && !remainingPaths) {
        // Legacy: count only — cannot rebuild paths.
        videoBatchNote = ` (${extraCount} more video${extraCount === 1 ? '' : 's'} — re-drop to queue)`;
        generateQueue = [];
        renderQueue();
      } else {
        videoBatchNote = '';
        if (!queueRunning) {
          generateQueue = [];
          renderQueue();
        }
      }
    } else {
      videoBatchNote = '';
      renderQueue();
    }
    const batchNote = videoBatchNote;
    try {
      await showFrame(path, 0);
      candidates = [];
      clearPendingSeed();
      setSeedSuggestEnabled(false);
      // Region buttons stay disabled until generate:autoroi (auto-find owns them).
      el('#gen-autoroi').disabled = true;
      el('#gen-candidates').disabled = true;
      el('#gen-suggest-profile').disabled = false;
      el('#gen-label-scene').disabled = false;
      updateSceneMapButton();
      const spLoad = el('#gen-scene-proposals-load');
      const spBeside = el('#gen-scene-proposals-beside');
      if (spLoad) spLoad.disabled = false;
      if (spBeside) spBeside.disabled = false;
      el('#gen-suggest-status').textContent = '';
      el('#gen-status').textContent = (
        generateQueue.length
          ? 'Everyday path: tip region for CSRT. Use Create queue for the batch, or Create for this clip only.'
          : 'Everyday path: finding tip region for CSRT. Contact marks appear when Contact vib is on.'
      ) + batchNote;
      lastOutputPath = null;
      genCurvePoints = null;
      setPosOverlayVisible(false);
      el('#gen-feedback').style.display = 'none';
      el('#gen-improve').style.display = 'none';
      el('#gen-improve-status').textContent = '';
      el('#gen-quality').style.display = 'none';
      syncWorkflowSteps();
      refreshAIScriptWriterUI();
      // Soft suggestion: show Apply like the click path — never auto-Apply.
      SuggestProfile(path).then(result => {
        if (!result || !result.found || !videoPath || videoPath !== path
          || loadSuggestionSeq !== profileSuggestionSeq) return;
        renderProfileSuggestion(el('#gen-suggest-status'), result);
      }).catch(() => {});
      // P4 follow-up: restore sceneMapMarks (+ map windows) from companion .samn.
      await restoreSceneMapFromCompanion(path);
      // Scene2: soft-load companion .scene.json if present (Apply still required).
      try {
        const beside = await LoadSceneProposalsBesideVideo(path, 0);
        if (beside && beside.found) {
          await adoptSceneProposalLoad(beside, 'Beside video');
        }
      } catch (_) { /* optional companion */ }
      // FunGen-like: auto-find tip after preview loads (CSRT first choice).
      // Queue advance starts auto-find from advanceQueueAfterDone / startQueueRun.
      if (!opts.fromQueueAdvance) {
        startAutoFindRegion();
      }
    } catch (err) {
      // Keep batchNote so multi-drop queue hint stays visible even if preview fails.
      cancelAnyRoiRequest();
      unlockRecognitionControls();
      uiError('Load video: ' + err + batchNote, el('#gen-status'));
    }
  }

  async function showFrame(path, sec) {
    const preview = sec > 0 ? await LoadFrameAt(path, sec) : await LoadFirstFrame(path);
    nativeW = preview.width; nativeH = preview.height;
    const displayH = Math.round(DISPLAY_W * nativeH / nativeW);
    canvas.width = DISPLAY_W; canvas.height = displayH;
    rememberNativeSize(canvas, nativeW, nativeH);
    await new Promise((resolve, reject) => {
      img.onload = () => { redraw(); resolve(); };
      img.onerror = () => reject(new Error('Preview image could not be decoded'));
      img.src = 'data:image/png;base64,' + preview.pngBase64;
    });
  }

  async function seekTo(sec) {
    if (!videoPath) return;
    cancelAnyRoiRequest();
    clearPendingAITarget();
    seekSec = Math.max(0, sec);
    el('#gen-seek').value = String(seekSec);
    el('#gen-status').textContent = `Loading frame at ${seekSec}s…`;
    try {
      await showFrame(videoPath, seekSec);
      if (sceneProposalPath) await refreshSceneProposalAtSeek();
      el('#gen-status').textContent = genCurvePoints
        ? (aiDraftCurveActive
          ? `Frame at ${seekSec}s — 0–100 gauge shows AI draft (Keep or Discard).`
          : `Frame at ${seekSec}s — 0–100 gauge follows the curve.`)
        : `Frame at ${seekSec}s — mark region.`;
      updatePosOverlay();
    } catch (err) {
      uiError('Seek failed: ' + err, el('#gen-status'));
    }
  }

  async function checkDeps() {
    try {
      await CheckGeneratorDependencies();
      uiInfo('Python and required packages are available.', el('#gen-status'));
    } catch (err) {
      uiError('Dependencies: ' + err, el('#gen-status'));
    }
  }

  async function generate(genOpts = {}) {
    if (!videoPath) return;
    const fromQueue = !!genOpts.fromQueue || queueRunning;
    if (activeAITargetRequest) {
      el('#gen-status').textContent = 'Wait for the body-point check, or change/dismiss it before creating.';
      return;
    }
    if (pendingAITarget) {
      el('#gen-status').textContent = 'Review the detected body point first: Apply target, or Dismiss to keep the current region.';
      return;
    }
    normalizeProductProfile();

    // Everyday: no tip yet → auto-find then continue.
    if (!roi) {
      const strictAI = el('#gen-ai-roi').checked && !el('#gen-ai-roi').disabled;
      if (strictAI) {
        pendingGenerateAfterRoi = false;
        el('#gen-status').textContent = 'Find the expected body point, review it, then press Apply target before Create.';
        startAutoFindRegion();
        return;
      }
      pendingGenerateAfterRoi = true;
    generating = true;
    el('#gen-generate').disabled = true;
    el('#gen-cancel').disabled = false;
    updateSceneMapButton();
      el('#gen-status').textContent = queueProgressPrefix() + 'No tip yet — finding region, then creating…';
      syncWorkflowSteps();
      startAutoFindRegion();
      return;
    }

    // Do not silently overwrite an existing script beside the video.
    let overwrite = false;
    if (fromQueue) {
      overwrite = queueOverwrite;
    } else {
      try {
        if (await ScriptExistsForVideo(videoPath)) {
          const target = videoPath.replace(/\.[^.\\/]+$/, '');
          if (!confirm(`A script already exists for this video. Creating again can replace these files:\n${target}.samn (Emotion Script)\n${target}.funscript (copy for other apps)\n\nReplace existing files?`)) {
            return;
          }
          overwrite = true;
        }
      } catch (err) {
        // Existence check failed — prefer not to overwrite; backend will refuse cleanly.
      }
    }

    if (fromQueue) {
      const entry = generateQueue.find(q => q.path === videoPath);
      if (entry) entry.status = 'running';
      renderQueue();
    }

    el('#gen-generate').disabled = true;
    el('#gen-cancel').disabled = false;
    generating = true;
    userCancelRequested = false;
    syncWorkflowSteps();
    updateSceneMapButton();
    el('#gen-status').textContent = queueProgressPrefix() + 'Creating…';
    el('#gen-log').textContent = '';
    {
      const wrap = el('#gen-progress-wrap');
      wrap.style.display = 'block';
      el('#gen-progress-bar').style.width = '0%';
      el('#gen-progress-bar').style.opacity = '1';
      el('#gen-progress-text').textContent = queueProgressPrefix() + 'Starting…';
      const tip = el('#gen-preview-steer-tip');
      if (tip) tip.textContent = '';
      progressStartedAt = Date.now();
    }
    // Tip ROI required for CSRT; placeholder {0,0,0,0} is unused on the
    // product path (research backends that ignore ROI stay CLI-only).
    const effectiveRoi = roi || { x: 0, y: 0, w: 0, h: 0 };
    const prominenceRaw = parseFloat(el('#gen-prominence')?.value);
    const payload = {
      videoPath,
      x: effectiveRoi.x, y: effectiveRoi.y, w: effectiveRoi.w, h: effectiveRoi.h,
      invert: el('#gen-invert').checked,
      smoothWindow: intOr(el('#gen-smooth').value, 11),
      minPeakDistanceMs: intOr(el('#gen-peakdist').value, 150),
      peakProminence: Number.isFinite(prominenceRaw) && prominenceRaw > 0 ? prominenceRaw : 0,
      disableCameraCompensation: !el('#gen-camcomp').checked,
      disableSceneCutDetection: !el('#gen-scenecut').checked,
      perSceneRoi: el('#gen-perscene').checked,
      adaptiveKeyframeError: el('#gen-adaptive').checked ? 6 : 0,
      autoRetry: el('#gen-retry').checked,
      backend: el('#gen-backend').value || 'csrt',
      dynamicRangeMs: el('#gen-dynrange').checked ? 3000 : 0,
      profile: el('#gen-profile').value || 'standard',
      axis: el('#gen-axis').value,
      rdpTolerance: finiteOr(el('#gen-rdp').value, 0),
      maxSpeed: finiteOr(el('#gen-maxspeed')?.value, 0),
      overwrite,
      contactVibration: el('#gen-contact-vibration').checked,
      contactVibrationSpan: el('#gen-contact-vibration').checked
        ? (parseInt(el('#gen-contact-span').value, 10) || 75) / 100
        : 0,
      contactVibrationCurve: el('#gen-contact-vibration').checked
        ? (el('#gen-contact-impulse')?.checked
          ? 'impulse'
          : (el('#gen-contact-curve').value || 'linear'))
        : '',
      autoOZoneMarker: el('#gen-auto-ozone').checked,
      audioCheck: el('#gen-audio-check').checked,
      captureTrajectory: !!el('#gen-capture-trajectory')?.checked,
      rhythmGrid: !!el('#gen-rhythm-grid')?.checked,
      contactPointsFile: (
        !!el('#gen-rhythm-grid')?.checked
        && !!el('#gen-contact-points')?.checked
        && (el('#gen-contact-points-path')?.value || '').trim()
      ) || '',
      // Hybrid verify: fixed measured K=1.5 when switch on + points path present.
      contactVerifyK: (
        !!el('#gen-rhythm-grid')?.checked
        && !!el('#gen-contact-points')?.checked
        && !!(el('#gen-contact-points-path')?.value || '').trim()
        && !!el('#gen-contact-verify')?.checked
      ) ? 1.5 : 0,
      startTimeSec: seekSec > 0 ? seekSec : 0,
    };
    // Contact marks: persist when Contact vib is on (or legacy Tf/Tj distance).
    // Everyday stroke still uses tip-only CSRT — marks go to metadata.contact_marks
    // for feel / later feel-decouple (native pipeline stamps them; drive_stroke=false).
    if (roi2 && (contactVibrationOn() || isTfTj())) {
      payload.x2 = roi2.x;
      payload.y2 = roi2.y;
      payload.w2 = roi2.w;
      payload.h2 = roi2.h;
      payload.roi2Fixed = !!el('#gen-roi2-fixed')?.checked;
    }
    payload.regionClass = el('#gen-region-class')?.value || '';
    payload.regionClass2 = el('#gen-region-class2')?.value || '';
    if (extraTargets.length) {
      payload.extraTargets = extraTargets.map(t => ({
        x: t.x, y: t.y, w: t.w, h: t.h,
        // Explicit bool — omitempty Fixed:false must still mean follow on Play.
        fixed: !!t.fixed,
        class: t.class || '',
      }));
    }
    if (maskRois.length) {
      payload.maskRois = maskRois.map(m => ({ x: m.x, y: m.y, w: m.w, h: m.h }));
    }
    if (sceneMapMarks.length) {
      payload.sceneMapMarks = sceneMapMarks.map(m => {
        const out = {
          id: m.id || '',
          kind: m.kind || '',
          rect: { x: m.rect.x, y: m.rect.y, w: m.rect.w, h: m.rect.h },
          fromMs: m.fromMs || 0,
          toMs: m.toMs || 0,
          class: m.class || '',
          author: m.author || 'user',
          follow: !!m.follow,
        };
        if (m.atMs != null && Number.isFinite(m.atMs)) out.atMs = m.atMs;
        if (typeof m.confidence === 'number' && m.confidence > 0) {
          out.confidence = m.confidence;
        }
        if (typeof m.reviewed === 'boolean') {
          out.reviewed = m.reviewed;
        }
        if (Array.isArray(m.path) && m.path.length) {
          out.path = m.path.map((p) => ({
            ms: p.ms || 0,
            rect: {
              x: p.rect?.x || 0, y: p.rect?.y || 0,
              w: p.rect?.w || 0, h: p.rect?.h || 0,
            },
          }));
        }
        return out;
      });
    }
    GenerateScript(payload);
  }

  el('#gen-cancel').addEventListener('click', () => {
    userCancelRequested = true;
    const tipFindOnly = pendingGenerateAfterRoi;
    const manualTipFind = tipFindBusy && !generating;
    pendingGenerateAfterRoi = false;
    // Drop in-flight tip-find / candidates / AI tags / Smarter tip find.
    activeAITargetRequest = null;
    CancelROIDetection().catch(() => {});
    CancelContactPoints().catch(() => {});
    CancelGenerate();
    // Tip-find-then-Create never gets generate:done until tracking starts —
    // clear the Create lock here so Cancel always unlocks the UI.
    if (generating) {
      clearCreateLock();
      // Queue tip-find never reached GenerateScript — tear down now or
      // Clear stays disabled waiting forever for generate:done.
      if (tipFindOnly && abortQueueDuringTipFind('Queue canceled.')) {
        return;
      }
      el('#gen-status').textContent = tipFindOnly
        ? 'Tip find canceled.'
        : (queueRunning ? 'Cancel requested — stopping queue…' : 'Cancel requested…');
      return;
    } else if (manualTipFind) {
      unlockRecognitionControls();
      el('#gen-status').textContent = 'Tip find canceled.';
      return;
    }
    el('#gen-status').textContent = queueRunning
      ? 'Cancel requested — stopping queue…'
      : 'Cancel requested…';
  });

  const handleProgressLine = line => {
    const log = el('#gen-log');
    if (log) {
      log.textContent += line + '\n';
      log.scrollTop = log.scrollHeight;
    }
    // Tip-find keeps the friendly status; raw detector lines stay in the log.
    if (!(tipFindBusy && !generating)) {
      el('#gen-status').textContent = line;
    }
    const wrap = el('#gen-progress-wrap');
    if (wrap && wrap.style.display === 'none') {
      wrap.style.display = 'block';
      progressStartedAt = Date.now();
    }
    // Stage B steers (stroke preview): tip only — do NOT flip Advanced
    // checkboxes. Go already applied opts for this run; checking the box
    // here sticky-enables PerSceneROI for every later Create and forces the
    // Python path (#338 CSRT-not-available on Windows portable).
    const s = String(line);
    if (/STROKE_PREVIEW:.*enabling .Re-find region after each cut/i.test(s)) {
      const tip = el('#gen-preview-steer-tip');
      if (tip) tip.textContent = 'Stroke preview: enabled Re-find region after each cut for this run (high cut rate). Uncheck Advanced → Re-find… next time to stay on Go CSRT.';
    }
    if (/STROKE_PREVIEW:.*enabling camera motion compensation/i.test(s)) {
      const tip = el('#gen-preview-steer-tip');
      if (tip) tip.textContent = 'Stroke preview: enabled camera motion compensation for this run (pan-like energy).';
    }
    const pipe = el('#gen-pipeline');
    if (!pipe) return;
    if (/Go-Pipeline|Go-native|trackcv/i.test(s) && !/simpletrack|NCC/i.test(s)) {
      pipe.textContent = 'Path: Go CSRT';
    } else if (/PreferSimpletrack|simpletrack|NCC/i.test(s)) {
      pipe.textContent = 'Path: Go simpletrack (experimental)';
    } else if (/Python CSRT|product path|using Python/i.test(s) && /Python/i.test(s)) {
      pipe.textContent = 'Path: Python';
    }
  };
  EventsOn('generate:progress', handleProgressLine);
  EventsOn('generate:tip-progress', event => {
    const request = activeAITargetRequest;
    if (!request || Number(event?.requestId) !== request.requestId
      || event?.videoPath !== request.videoPath) return;
    handleProgressLine(String(event?.line || ''));
  });

  EventsOn('generate:tip-detection', result => {
    const request = activeAITargetRequest;
    if (!request || request.videoPath !== videoPath) return;
    const selected = normalizeClass(el('#gen-ai-target-class')?.value || '');
    const expected = normalizeClass(result.expectedClass || '');
    if (Number(result.requestId) !== request.requestId
      || !el('#gen-ai-roi').checked || selected !== request.expectedClass
      || expected !== request.expectedClass
      || (result.videoPath && result.videoPath !== request.videoPath)) return;
    activeAITargetRequest = null;
    unlockRecognitionControls();
    pendingGenerateAfterRoi = false;
    generating = false;
    el('#gen-cancel').disabled = true;
    if (result.error || !result.match) {
      clearPendingAITarget();
      redraw();
      const wanted = labelFor(expected || selected) || expected || selected || 'target';
      const reason = result.errorCode || result.status || 'target_not_detected';
      const guidance = ['manifest_missing', 'manifest_invalid', 'class_unresolved', 'class_conflict'].includes(reason)
        ? 'Check that classes.json beside the ONNX model contains this class.'
        : 'Mark the intended point manually or train/correct more examples.';
      showFindTipRetry(`no confirmed ${wanted} (${reason}). ${guidance} Existing region kept`, 'warn');
      updateGenerateEnabled();
      return;
    }
    const matched = normalizeClass(result.matchedClass || '');
    if (!matched || matched !== expected || !(result.w > 0 && result.h > 0)) {
      clearPendingAITarget();
      redraw();
      uiWarn('AI result did not match the requested body point. Existing region kept; mark manually.', el('#gen-status'));
      updateGenerateEnabled();
      return;
    }
    pendingAITarget = { ...result, matchedClass: matched, expectedClass: expected };
    renderPendingAITarget();
    el('#gen-status').textContent =
      `${labelFor(matched) || matched} found with ${Math.round((result.confidence || 0) * 100)}% confidence — Apply target to use it.`;
    redraw();
    updateGenerateEnabled();
  });

  // Ergebnis der automatischen Regionssuche Apply - die ROI wird
  // genauso gesetzt, als hätte der Nutzer sie gezogen, und lässt sich
  // danach frei korrigieren.
  EventsOn('generate:autoroi', result => {
    // Drop stale finds before touching UI — a late result for video A must not
    // hide progress / unlock buttons while video B is still searching.
    if (result.videoPath && videoPath && result.videoPath !== videoPath) {
      return;
    }
    unlockRecognitionControls();
    if (result.error) {
      const wasPending = pendingGenerateAfterRoi;
      pendingGenerateAfterRoi = false;
      generating = false;
      el('#gen-cancel').disabled = true;
      updateGenerateEnabled();
      updateSceneMapButton();
      syncWorkflowSteps();
      if (/cancel/i.test(String(result.error || ''))) {
        el('#gen-status').textContent = 'Tip find canceled.';
      } else {
        showFindTipRetry(result.error, 'error');
      }
      // Queue tip-find failed — advance/fail entry (do not leave queue stuck).
      if (queueRunning && wasPending) {
        advanceQueueAfterDone(false, result.error);
      }
      return;
    }
    candidates = [];
    clearPendingSeed();
    setSeedSuggestEnabled(false);
    // Everyday first choice: tip CSRT.
    if (!el('#gen-backend').dataset.userTouched) {
      el('#gen-backend').value = 'csrt';
      forceCsrtBackend();
    }
    if (!el('#gen-profile').dataset.userTouched) {
      el('#gen-profile').value = 'standard';
    }
    roi = { x: result.x, y: result.y, w: result.w, h: result.h };
    const hasRoi2 = result.w2 > 0 && result.h2 > 0;
    if (hasRoi2) {
      // Optional contact suggestion only — do not switch to legacy Tf/Tj.
      roi2 = { x: result.x2, y: result.y2, w: result.w2, h: result.h2 };
    }
    updateRoiLabels();
    const via = result.engine === 'ai' ? 'AI detection' : 'classic auto';
    updateProfileUi();
    updateGenerateEnabled();
    el('#gen-roi-label').textContent =
      `Tip (stroke tracker): ${via} found — x=${roi.x} y=${roi.y} w=${roi.w} h=${roi.h}`;
    let status = hasRoi2
      ? `Tip + contact found (${via}) — drag either box to refine (editable).`
      : `Tip region found (${via}) — start box only; drag on the preview to refine.`;
    if (result.verifyWarning) {
      status += ' ⚠ ' + result.verifyWarning;
      uiWarn(result.verifyWarning, el('#gen-status'));
    }
    el('#gen-status').textContent = status + videoBatchNote;
    redraw();
    const shouldGenerate = pendingGenerateAfterRoi;
    pendingGenerateAfterRoi = false;
    if (shouldGenerate && roi) {
      generate({ fromQueue: queueRunning });
    }
  });
  // Fortschritt: das Backend schickt 0-100, oder -1 wenn die Frame-Anzahl
  // des videos unknown war. In dem Fall wird ein unbestimmter
  // Balken gezeigt statt eines erfundenen Prozentwerts.
  const handleProgressPercent = pct => {
    const wrap = el('#gen-progress-wrap');
    const bar = el('#gen-progress-bar');
    const text = el('#gen-progress-text');
    if (wrap.style.display === 'none') {
      wrap.style.display = 'block';
      progressStartedAt = Date.now();
    }
    if (pct < 0) {
      bar.style.width = '100%';
      bar.style.opacity = '0.35';
      text.textContent = 'Running… (could not determine video length)';
      return;
    }
    bar.style.opacity = '1';
    bar.style.width = pct + '%';
    const elapsed = (Date.now() - progressStartedAt) / 1000;
    let rest = '';
    if (pct >= 3 && elapsed > 2) {
      const total = elapsed / (pct / 100);
      const remaining = Math.max(0, Math.round(total - elapsed));
      rest = `  ·  ~${remaining < 60 ? remaining + ' s' : Math.round(remaining / 60) + ' min'} left`;
      // Batch ETA: extrapolate from finished clips + current clip remainder.
      if (queueRunning && generateQueue.length > 1 && queueBatchStartedAt) {
        const finished = queueDoneCount;
        const totalQ = generateQueue.length;
        if (finished > 0 && pct >= 5) {
          const batchElapsed = (Date.now() - queueBatchStartedAt) / 1000;
          const perClip = batchElapsed / (finished + pct / 100);
          const batchLeft = Math.max(0, Math.round(perClip * (totalQ - finished - pct / 100)));
          rest += `  ·  batch ~${batchLeft < 60 ? batchLeft + ' s' : Math.round(batchLeft / 60) + ' min'}`;
        }
      }
    }
    text.textContent = `${queueProgressPrefix()}${pct} %${rest}`;
  };
  EventsOn('generate:percent', handleProgressPercent);
  EventsOn('generate:tip-percent', event => {
    const request = activeAITargetRequest;
    if (!request || Number(event?.requestId) !== request.requestId
      || event?.videoPath !== request.videoPath) return;
    handleProgressPercent(Number(event?.percent));
  });

  function hideProgress() {
    el('#gen-progress-wrap').style.display = 'none';
    el('#gen-progress-bar').style.width = '0%';
    el('#gen-progress-text').textContent = '';
  }

  // Urteil abschicken. Der Bereich erscheint erst nach einem erfolgreichen
  // Lauf - vorher gibt es nichts zu beurteilen.
  root.querySelectorAll('#gen-feedback button[data-verdict]').forEach(btn => {
    btn.addEventListener('click', async () => {
      if (!lastOutputPath) return;
      const status = el('#gen-fb-status');
      try {
        await SubmitFeedback({
          outputPath: lastOutputPath,
          verdict: btn.dataset.verdict,
          comment: el('#gen-fb-comment').value || '',
        });
        status.textContent = `Thanks - saved as "${btn.textContent.trim()}".`;
        el('#gen-fb-comment').value = '';
      } catch (err) {
        status.textContent = 'Could not save: ' + err;
      }
    });
  });

  function syncImproveRepairInputs() {
    const on = !!el('#gen-improve-repair')?.checked;
    const a = el('#gen-improve-repair-start');
    const b = el('#gen-improve-repair-end');
    if (a) a.disabled = !on;
    if (b) b.disabled = !on;
  }
  function syncImproveHealRhythm() {
    const healOn = !!el('#gen-improve-heal')?.checked;
    const rhythm = el('#gen-improve-heal-rhythm');
    if (!rhythm) return;
    rhythm.disabled = !healOn;
    if (!healOn) rhythm.checked = false;
  }
  el('#gen-improve-repair')?.addEventListener('change', syncImproveRepairInputs);
  el('#gen-improve-heal')?.addEventListener('change', syncImproveHealRhythm);
  syncImproveRepairInputs();
  syncImproveHealRhythm();

  function collectImproveRepairSpans() {
    if (!el('#gen-improve-repair')?.checked) return [];
    const startSec = parseFloat(el('#gen-improve-repair-start')?.value) || 0;
    const endSec = parseFloat(el('#gen-improve-repair-end')?.value) || 0;
    if (!(endSec > startSec) || endSec <= 0) return [];
    return [{ start_ms: Math.round(startSec * 1000), end_ms: Math.round(endSec * 1000) }];
  }

  el('#gen-improve-apply')?.addEventListener('click', async () => {
    if (!lastOutputPath) return;
    const status = el('#gen-improve-status');
    const btn = el('#gen-improve-apply');
    btn.disabled = true;
    status.textContent = 'Improving…';
    try {
      const audioOn = !!el('#gen-improve-audio')?.checked;
      // Keep generate-time hidden flag in sync for any re-run.
      if (el('#gen-audio-check')) el('#gen-audio-check').checked = audioOn;
      const healOn = !!el('#gen-improve-heal')?.checked;
      const result = await ImproveGeneratedScript({
        path: lastOutputPath,
        videoPath: videoPath || '',
        startSec: parseFloat(el('#gen-improve-start')?.value) || 0,
        endSec: parseFloat(el('#gen-improve-end')?.value) || 0,
        fillGaps: !!el('#gen-improve-fill')?.checked,
        healTrackingGaps: healOn,
        healRhythm: healOn && !!el('#gen-improve-heal-rhythm')?.checked,
        repairSpans: collectImproveRepairSpans(),
        maxGapMs: 0,
        audioCheck: audioOn,
        useAudioForFill: !!el('#gen-improve-audio-fill')?.checked,
      });
      status.textContent = result.message || 'Done';
      if (result.audioWarnings && result.audioWarnings.length) {
        status.textContent += ' — ' + result.audioWarnings[0];
      }
      rememberAudioSegmentsFromResult(result);
      // Do not label all PointsAdded as "fill" — Heal bridges count too (#348).
      const improveBits = [];
      const healedN = result.windowsHealed || result.WindowsHealed || 0;
      const repairedN = result.spansRepaired || result.SpansRepaired || 0;
      const rhythmN = result.rhythmBridged || result.RhythmBridged || 0;
      const addedN = result.pointsAdded || result.PointsAdded || 0;
      if (healedN > 0) improveBits.push(`healed ${healedN}`);
      if (repairedN > 0) improveBits.push(`repaired ${repairedN}`);
      if (rhythmN > 0) improveBits.push(`${rhythmN} rhythm`);
      if (addedN > 0) improveBits.push(`+${addedN} pts`);
      if (result.trimmed || result.Trimmed) improveBits.push('trimmed');
      el('#gen-status').textContent =
        `Improved: ${result.afterCount ?? result.AfterCount ?? '?'} points` +
        (improveBits.length ? ` (${improveBits.join(' · ')})` : '') +
        ' — open Play to edit dots/curve.';
      const reloadPath = result.path || lastOutputPath;
      if (reloadPath && playback && typeof playback.loadScriptPath === 'function') {
        await playback.loadScriptPath(reloadPath, { review: true });
        await loadGenCurveFromPlay();
      }
    } catch (err) {
      status.textContent = 'Improve failed: ' + err;
      uiError('Improve script: ' + err, el('#gen-status'));
    } finally {
      btn.disabled = false;
    }
  });

  ['holding', 'gentle', 'intense', 'climax'].forEach((lab) => {
    el(`#gen-seg-f-${lab}`)?.addEventListener('change', () => renderAudioSegmentsPanel());
  });
  el('#gen-seg-f-speech-only')?.addEventListener('change', () => renderAudioSegmentsPanel());
  el('#gen-audio-seg-chapters')?.addEventListener('click', () => {
    addVisibleSegmentsAsChapters();
  });

  EventsOn('generate:done', result => {
    // Ignore stale cancel from a superseded run (new Create already active).
    if (result && result.cancelled && !userCancelRequested) {
      return;
    }
    if (typeof result?.seq === 'number') {
      if (result.seq < activeCreateSeq) return;
      activeCreateSeq = result.seq;
    }
    const cancelByUser = userCancelRequested;
    userCancelRequested = false;
    hideProgress();
    el('#gen-cancel').disabled = true;
    generating = false;
    lastOutputPath = result.path || null;
    el('#gen-fb-status').textContent = '';
    el('#gen-improve-status').textContent = '';
    if (!result.audioSegments && !result.AudioSegments) {
      // Clear stale strip until improve / generate payload brings new segments.
      lastAudioSegments = [];
      lastSpeechHoldMs = 0;
      renderAudioSegmentsPanel();
    }
    el('#gen-feedback').style.display = lastOutputPath ? 'block' : 'none';
    el('#gen-improve').style.display = lastOutputPath ? 'block' : 'none';
    updateGenerateEnabled();
    updateSceneMapButton();
    refreshAIScriptWriterUI();
    syncWorkflowSteps();
    if (result.error) {
      if (result.cancelled) {
        el('#gen-status').textContent = 'Canceled.';
        if (queueRunning) {
          userCancelRequested = cancelByUser || true;
          advanceQueueAfterDone(false, 'canceled');
          userCancelRequested = false;
        }
        return;
      }
      el('#gen-status').textContent = 'Failed: ' + result.error;
      el('#gen-improve').style.display = 'none';
      if (queueRunning) advanceQueueAfterDone(false, result.error);
      return;
    }
    el('#gen-status').textContent = queueProgressPrefix() + 'Done — Emotion Script ready in Play.';
    const pipe = el('#gen-pipeline');
    if (pipe) {
      pipe.textContent = '';
    }
    if (typeof result.oZoneMarkerStartMs === 'number') {
      const s = Math.round(result.oZoneMarkerStartMs / 1000);
      const e = Math.round(result.oZoneMarkerEndMs / 1000);
      el('#gen-status').textContent += ` — O-marker set: ${s}s–${e}s`;
    }
    // S1 discoverability (GUI load): Export classical run lives under Advanced.
    el('#gen-status').textContent +=
      ' — Advanced → Export classical run saves a local AI-training sample (S1).';

    const qualityBox = el('#gen-quality');
    if (typeof result.qualityScore === 'number') {
      const pct = Math.round(result.qualityScore * 100);
      // Das Urteil kommt vom Quality Doctor selbst, nicht aus dem Score: er
      // kennt harte Ausschlusskriterien, die trotz Score >= 0.5 zum Scheitern
      // führen. Nur bei älteren Skripten ohne das Feld auf den Score zurückfallen.
      const ok = typeof result.qualityPassed === 'boolean'
        ? result.qualityPassed
        : result.qualityScore >= 0.5;
      qualityBox.style.display = 'block';
      qualityBox.style.background = ok ? 'rgba(61,216,117,0.12)' : 'rgba(216,77,77,0.12)';
      qualityBox.style.border = `1px solid ${ok ? 'var(--ok)' : 'var(--danger)'}`;
      let html = `<b>Signal Quality (Quality Doctor): ${pct}% ${ok ? '(within normal)' : '(review recommended)'}</b>`;
      html += '<br><span style="opacity:0.7;">Technical signal quality — not motion fidelity '
        + '(Does the curve match the video?).</span>';
      if (result.qualityWarnings && result.qualityWarnings.length > 0) {
        html += '<ul style="margin:6px 0 0 18px; padding:0;">' +
          result.qualityWarnings.map(w => `<li>${w}</li>`).join('') + '</ul>';
      }
      if (result.aiOpinionVerdict) {
        html += `<div style="margin-top:8px; padding-top:8px; border-top:1px solid var(--border);">`
          + `<b>AI second opinion: ${result.aiOpinionVerdict}</b>`
          + (result.aiOpinionReason ? `<br>${result.aiOpinionReason}` : '')
          + `</div>`;
      }
      if (result.trackingGapCount > 0 || result.trackingReason) {
        html += `<div style="margin-top:8px; padding-top:8px; border-top:1px solid var(--border);">`
          + `<b>Tracking:</b> `;
        const bits = [];
        if (result.trackingReason === 'tracker_lost_heavy') {
          bits.push('heavy tracker loss — Tip/Partner often lost; contact feel may mute in gaps');
        } else if (result.trackingReason === 'tracker_lost_elevated') {
          bits.push('elevated tracker loss — some Tip/Partner gaps');
        } else if (result.trackingReason) {
          bits.push(String(result.trackingReason));
        }
        if (result.trackingGapCount > 0) {
          bits.push(`${result.trackingGapCount} gap window(s) (contact vib muted there on Tf/Tj)`);
        }
        html += bits.join(' · ') + `</div>`;
      }
      if (result.audioCheckWarnings && result.audioCheckWarnings.length > 0) {
        html += `<div style="margin-top:8px; padding-top:8px; border-top:1px solid var(--border);">`
          + `<b>Audio tempo check:</b>`
          + '<ul style="margin:6px 0 0 18px; padding:0;">'
          + result.audioCheckWarnings.map(w => `<li>${w}</li>`).join('') + '</ul>'
          + `</div>`;
      }
      rememberAudioSegmentsFromResult(result);
      if (lastAudioSegments.length) {
        const holdSec = (lastSpeechHoldMs / 1000).toFixed(1);
        html += `<div style="margin-top:8px; padding-top:8px; border-top:1px solid var(--border);">`
          + `<b>Speech-Hold / Feel:</b> ${lastAudioSegments.length} segment(s)`
          + (lastSpeechHoldMs > 0 ? ` · hold ≈ ${holdSec}s` : '')
          + ` — see Review strip below (filter · seek · chapters).`
          + `</div>`;
      }
      qualityBox.innerHTML = html;
    } else {
      qualityBox.style.display = 'none';
    }

    // Right after Generate: fill gaps (auto + tighter second pass in Go),
    // then open Play with dots. Review Improve can re-run with trim/audio.
    (async () => {
      let path = result.path;
      try {
        el('#gen-status').textContent += ' — filling gaps…';
        if (el('#gen-improve-status')) {
          el('#gen-improve-status').textContent = 'Auto fill-gaps after generate…';
        }
        // Force fill + heal on; honor Review audio toggles (defaults checked).
        if (el('#gen-improve-fill')) el('#gen-improve-fill').checked = true;
        if (el('#gen-improve-heal')) el('#gen-improve-heal').checked = true;
        const polished = await ImproveGeneratedScript({
          path,
          videoPath: videoPath || '',
          startSec: 0,
          endSec: 0,
          fillGaps: true,
          healTrackingGaps: true,
          healRhythm: false, // GapFill rhythm is opt-in manual Improve only
          repairSpans: [],
          maxGapMs: 0, // auto + second pass @ 400ms
          audioCheck: !!(el('#gen-improve-audio')?.checked || el('#gen-audio-check')?.checked),
          useAudioForFill: el('#gen-improve-audio-fill')
            ? !!el('#gen-improve-audio-fill').checked
            : true,
        });
        if (polished && polished.path) path = polished.path;
        const msg = (polished && polished.message) || 'Gaps checked';
        rememberAudioSegmentsFromResult(polished || {});
        const healed = polished && polished.windowsHealed > 0;
        const filled = polished && (polished.pointsAdded > 0 || polished.gapsFilled > 0);
        if (filled || healed) {
          const bits = [];
          if (healed) bits.push(`healed ${polished.windowsHealed}`);
          if (polished.pointsAdded > 0) bits.push(`+${polished.pointsAdded} pts`);
          else if (polished.gapsFilled > 0) bits.push(`filled ${polished.gapsFilled}`);
          el('#gen-status').textContent += ` (${bits.join(' · ')})`;
        } else {
          el('#gen-status').textContent += ' (gaps ok)';
        }
        if (el('#gen-improve-status')) {
          el('#gen-improve-status').textContent = 'After generate: ' + msg;
        }
      } catch (err) {
        // Non-fatal — still open Play with the raw generate output.
        console.warn('post-generate fill gaps:', err);
        if (el('#gen-improve-status')) {
          el('#gen-improve-status').textContent = 'Auto fill skipped: ' + err;
        }
      }
      lastOutputPath = path;
      refreshAIScriptWriterUI();
      el('#gen-status').textContent += ' — loaded in Play (dots + Edit curve).';
      try {
        await playback.loadScriptPath(path, { review: true });
        await loadGenCurveFromPlay();
      } catch (err) {
        console.warn('post-generate Play/overlay:', err);
        playback.loadScriptPath(path, { review: true });
      }
      if (queueRunning) {
        await advanceQueueAfterDone(true);
      }
    })();
  });

  el('#gen-pos-overlay-toggle')?.addEventListener('change', () => {
    const on = !!el('#gen-pos-overlay-toggle').checked;
    try { localStorage.setItem(POS_OVERLAY_PREF, on ? '1' : '0'); } catch (_) { /* ignore */ }
    setPosOverlayVisible(on);
    if (on) updatePosOverlay();
  });
  // Restore toggle preference once DOM is ready.
  posOverlayWanted();
  setPosOverlayVisible(false);
  el('#gen-choose').addEventListener('click', chooseVideo);
  el('#gen-check-deps').addEventListener('click', checkDeps);
  el('#gen-generate').addEventListener('click', generate);
  el('#gen-scene-map')?.addEventListener('click', runSceneMapScan);
  el('#gen-scene-map-win')?.addEventListener('input', () => {
    sceneMapWinIdx = parseInt(el('#gen-scene-map-win').value, 10) || 0;
    syncSceneMapTools();
    redraw();
  });
  el('#gen-scene-map-overlay')?.addEventListener('change', () => redraw());
  el('#gen-scene-map-mark-kind')?.addEventListener('change', () => {
    syncSceneMapTools();
    if (sceneMapMarkMode) setSceneMapMarkMode(true);
  });
  el('#gen-scene-map-mark')?.addEventListener('click', () => {
    setSceneMapMarkMode(!sceneMapMarkMode);
  });
  el('#gen-scene-map-marks-clear')?.addEventListener('click', () => {
    sceneMapMarks = [];
    updateSceneMapMarksLabel();
    redraw();
  });
  el('#gen-scene-map-export')?.addEventListener('click', async () => {
    const status = el('#gen-scene-map-export-status');
    if (!videoPath) {
      if (status) status.textContent = 'Load a video first.';
      uiWarn('Export for learning needs a video.', el('#gen-status'));
      return;
    }
    if (status) status.textContent = 'Exporting…';
    try {
      const res = await ExportSceneMapLearning(videoPath);
      const msg = `Exported → ${res.outDir || res.OutDir || 'scene_map_learning'} `
        + `(${res.windows ?? res.Windows ?? 0} windows, `
        + `${res.negatives ?? res.Negatives ?? 0} negatives, `
        + `${res.autoCandidates ?? res.AutoCandidates ?? 0} auto).`;
      if (status) status.textContent = msg;
      uiInfo(msg, el('#gen-status'));
    } catch (err) {
      if (status) status.textContent = '';
      uiError('Export for learning: ' + err, el('#gen-status'));
    }
  });
  el('#gen-scene-map-suggest')?.addEventListener('click', async () => {
    const status = el('#gen-scene-map-export-status');
    if (!nativeW || !nativeH) {
      if (status) status.textContent = 'Load a video first.';
      uiWarn('Suggest ignores needs a loaded video frame.', el('#gen-status'));
      return;
    }
    if (typeof SuggestExcludePriors !== 'function') {
      uiWarn('Suggest ignores not available in this build.', el('#gen-status'));
      return;
    }
    if (status) status.textContent = 'Suggesting from learning…';
    try {
      const res = await SuggestExcludePriors(nativeW, nativeH);
      const note = res.note || res.Note || '';
      const raw = res.suggestions || res.Suggestions || [];
      let added = 0;
      for (const s of raw) {
        const m = s.mark || s.Mark;
        if (!m) continue;
        const rect = sceneMapRect(m.rect || m.Rect);
        if ((rect.w || 0) < 4 || (rect.h || 0) < 4) continue;
        const dup = sceneMapMarks.some((ex) =>
          ex.kind === 'exclude' && sceneMarkOverlap(ex, { rect }) >= 0.45);
        if (dup) continue;
        sceneMapMarkSeq += 1;
        sceneMapMarks.push({
          id: m.id || m.ID || `suggest${sceneMapMarkSeq}`,
          kind: 'exclude',
          rect,
          fromMs: m.fromMs ?? m.FromMs ?? 0,
          toMs: m.toMs ?? m.ToMs ?? 0,
          author: 'suggest',
          follow: m.follow ?? m.Follow ?? true,
          path: [],
        });
        added += 1;
      }
      updateSceneMapMarksLabel();
      redraw();
      const msg = added
        ? `Added ${added} suggested Ignore mark${added === 1 ? '' : 's'} — review on the map; Clear removes them.`
        : (note || 'No suggestions yet.');
      if (status) status.textContent = msg;
      if (added) uiInfo(msg, el('#gen-status'));
      else uiWarn(msg, el('#gen-status'));
    } catch (err) {
      if (status) status.textContent = '';
      uiError('Suggest ignores: ' + err, el('#gen-status'));
    }
  });
  SceneMapAvailable().then((ok) => {
    sceneMapAvailable = !!ok;
    updateSceneMapButton();
  }).catch(() => {
    sceneMapAvailable = false;
    updateSceneMapButton();
  });

  let pendingAIDraft = null; // { actions, qdPassed, qdScore, notes }

  function setAIDraftPending(draft) {
    pendingAIDraft = draft;
    const keep = el('#gen-ai-script-keep');
    const discard = el('#gen-ai-script-discard');
    const has = !!(draft && Array.isArray(draft.actions) && draft.actions.length >= 2);
    if (keep) {
      keep.hidden = !has;
      keep.disabled = !has;
    }
    if (discard) {
      discard.hidden = !has;
      discard.disabled = !has;
    }
  }

  function refreshAIScriptWriterUI() {
    const draft = el('#gen-ai-script-draft');
    const exportBtn = el('#gen-ai-script-export');
    const status = el('#gen-ai-script-status');
    const hint = el('#gen-ai-script-hint');
    const canExport = !!lastOutputPath;
    if (exportBtn) {
      exportBtn.disabled = !canExport;
    }
    if (!draft) return;
    AIScriptWriterStatus().then((st) => {
      const available = !!(st && (st.available || st.Available));
      const reason = (st && (st.reason || st.Reason)) || '';
      const stage = (st && (st.stage || st.Stage)) || 'S0';
      const samples = (st && (st.sampleCount || st.SampleCount)) || 0;
      draft.disabled = !available || !videoPath;
      if (status) {
        const exportBit = canExport
          ? 'Export classical run ready'
          : 'Export classical run after Create';
        const draftBit = available
          ? `draft Ready (${stage}${samples ? `, ${samples} sample(s)` : ''})`
          : (reason || `draft not available (${stage})`);
        status.textContent = `${exportBit} · ${draftBit}`;
      }
      if (hint && !canExport && !available) {
        hint.textContent =
          'Everyday Create still uses CSRT. After a good Create, Export classical run builds a local imitation library; then AI draft becomes available. Keep required — CSRT path unchanged.';
      } else if (hint && canExport && !available) {
        hint.textContent = reason
          || 'Create finished — Export classical run saves a local training sample (S1). Draft unlocks after ≥1 export.';
      } else if (hint && available) {
        hint.textContent = reason
          || 'Imitation library ready. AI draft stretches the best duration + tip-aspect match — review QD, then Keep or Discard. Everyday CSRT unchanged.';
      }
    }).catch(() => {
      draft.disabled = true;
      if (status) {
        status.textContent = canExport
          ? 'Export classical run ready · AI draft status unavailable'
          : 'Export classical run after Create · AI draft status unavailable';
      }
    });
  }
  refreshAIScriptWriterUI();
  el('#gen-ai-script-export')?.addEventListener('click', async () => {
    const status = el('#gen-ai-script-status');
    const path = lastOutputPath;
    if (!path) {
      uiWarn('Create a script first, then export the classical run.', el('#gen-status'));
      return;
    }
    if (status) status.textContent = 'Exporting training sample…';
    try {
      const tip = roi && roi.w > 0 && roi.h > 0 ? roi : { x: 0, y: 0, w: 0, h: 0 };
      const res = await ExportAIScriptImitation(
        path, videoPath || '', tip.x || 0, tip.y || 0, tip.w || 0, tip.h || 0);
      if (status) status.textContent = (res && (res.message || res.Message)) || 'Exported';
      uiInfo((res && (res.message || res.Message)) || 'Training sample exported', el('#gen-status'));
      refreshAIScriptWriterUI();
    } catch (err) {
      if (status) status.textContent = '';
      uiError('Export classical run: ' + err, el('#gen-status'));
    }
  });
  el('#gen-ai-script-draft')?.addEventListener('click', async () => {
    const status = el('#gen-ai-script-status');
    if (!videoPath) {
      uiWarn('Choose a video first.', el('#gen-status'));
      return;
    }
    if (status) status.textContent = 'Drafting from imitation library…';
    setAIDraftPending(null);
    clearAIDraftCurvePreview({ restore: true });
    try {
      const tip = roi && roi.w > 0 && roi.h > 0 ? roi : { x: 0, y: 0, w: 0, h: 0 };
      const res = await DraftAIScript(
        videoPath, tip.x || 0, tip.y || 0, tip.w || 0, tip.h || 0, 0);
      const actions = (res && (res.actions || res.Actions)) || [];
      const qdPassed = !!(res && (res.qdPassed ?? res.QDPassed));
      const qdScore = (res && (res.qdScore ?? res.QDScore)) ?? null;
      const notes = (res && (res.notes || res.Notes)) || '';
      if (!actions || actions.length < 2) {
        throw new Error('draft returned no actions');
      }
      setAIDraftPending({ actions, qdPassed, qdScore, notes });
      const previewed = showAIDraftCurvePreview(actions);
      const scoreBit = qdScore != null ? `QD ${Number(qdScore).toFixed(2)} (${qdPassed ? 'pass' : 'fail'})` : 'QD n/a';
      const previewBit = previewed
        ? ' Scrub Time/Frame — 0–100 gauge shows the draft before Keep.'
        : '';
      if (status) status.textContent = `Draft ready — ${scoreBit}. Keep or Discard.`;
      uiInfo(`AI draft ready — ${scoreBit}. Review${previewed ? ' on the gauge' : ''}, then Keep draft or Discard.${previewBit}`, el('#gen-status'));
    } catch (err) {
      setAIDraftPending(null);
      clearAIDraftCurvePreview({ restore: true });
      if (status) status.textContent = '';
      uiError('AI draft: ' + err, el('#gen-status'));
    }
  });
  el('#gen-ai-script-keep')?.addEventListener('click', async () => {
    const status = el('#gen-ai-script-status');
    if (!pendingAIDraft || !videoPath) {
      uiWarn('Run AI draft script first.', el('#gen-status'));
      return;
    }
    if (status) status.textContent = 'Keeping draft…';
    try {
      const res = await KeepAIScriptDraft(videoPath, pendingAIDraft.actions);
      const path = (res && (res.path || res.Path)) || '';
      const msg = (res && (res.message || res.Message)) || 'Draft kept';
      setAIDraftPending(null);
      clearAIDraftCurvePreview({ restore: false }); // keep draft points on gauge
      if (path) {
        lastOutputPath = path;
        el('#gen-feedback').style.display = 'block';
        el('#gen-improve').style.display = 'block';
        if (playback && typeof playback.loadScriptPath === 'function') {
          try {
            await playback.loadScriptPath(path, { review: true });
            await loadGenCurveFromPlay();
          } catch (_) {
            // Gauge already shows the kept draft actions.
          }
        }
      }
      if (status) status.textContent = msg;
      uiInfo(msg, el('#gen-status'));
      refreshAIScriptWriterUI();
    } catch (err) {
      if (status) status.textContent = '';
      uiError('Keep AI draft: ' + err, el('#gen-status'));
    }
  });
  el('#gen-ai-script-discard')?.addEventListener('click', () => {
    setAIDraftPending(null);
    clearAIDraftCurvePreview({ restore: true });
    const status = el('#gen-ai-script-status');
    if (status) status.textContent = 'Draft discarded';
    uiInfo('AI draft discarded — Everyday Create result unchanged.', el('#gen-status'));
    refreshAIScriptWriterUI();
  });

  el('#gen-seek-btn').addEventListener('click', () => seekTo(parseFloat(el('#gen-seek').value) || 0));
  el('#gen-seek-plus').addEventListener('click', () => seekTo(seekSec + 1));
  el('#gen-seek-plus5').addEventListener('click', () => seekTo(seekSec + 5));
  el('#gen-roi2-toggle').addEventListener('click', () => {
    setRoi2Mode(!roi2Mode);
    if (roi2Mode) {
      el('#gen-status').textContent = 'Contact area: drag on preview (gold).';
    }
  });
  el('#gen-target-add')?.addEventListener('click', () => {
    if (markMode === 'target') {
      setMarkMode(null);
      return;
    }
    setMarkMode('target');
    el('#gen-status').textContent = 'Another contact area: drag on preview (magenta).';
  });
  el('#gen-mask-add')?.addEventListener('click', () => {
    if (markMode === 'mask') {
      setMarkMode(null);
      return;
    }
    setMarkMode('mask');
    el('#gen-status').textContent = 'Ignore region (black): drag on preview — Create tracks it; not used for recognition.';
  });
  el('#gen-extras-clear')?.addEventListener('click', () => {
    extraTargets = [];
    maskRois = [];
    sceneMapMarks = sceneMapMarks.filter((m) => !(m.id || '').startsWith('mask'));
    updateSceneMapMarksLabel();
    setMarkMode(null);
    updateRoiLabels();
    redraw();
    el('#gen-status').textContent = 'Extra targets and ignore regions cleared.';
  });
  el('#gen-profile').addEventListener('change', () => {
    el('#gen-profile').dataset.userTouched = '1';
    normalizeProductProfile();
    updateProfileUi();
  });
  el('#gen-contact-vibration').addEventListener('change', () => {
    contactUserOverride = true;
    updateContactVibrationOpts();
  });
  el('#gen-capture-trajectory')?.addEventListener('change', () => {
    el('#gen-capture-trajectory').dataset.userTouched = '1';
  });
  el('#gen-contact-impulse')?.addEventListener('change', () => {
    const on = !!el('#gen-contact-impulse').checked;
    const curve = el('#gen-contact-curve');
    if (!curve) return;
    if (on) {
      curve.value = 'impulse';
      curve.dataset.userTouched = '1';
    } else if (curve.value === 'impulse') {
      curve.value = 'soft';
    }
    scheduleContactVibPreview();
    saveSetting('generator.contact_vibration_curve', curve.value || 'soft');
  });
  el('#gen-contact-curve').addEventListener('change', () => {
    el('#gen-contact-curve').dataset.userTouched = '1';
    const impulse = el('#gen-contact-impulse');
    if (impulse) impulse.checked = el('#gen-contact-curve').value === 'impulse';
    openCreateContactProbe();
    scheduleContactVibPreview();
    saveSetting('generator.contact_vibration_curve', el('#gen-contact-curve').value || 'soft');
  });
  el('#gen-contact-span').addEventListener('input', () => {
    updateContactSpanLabel();
    openCreateContactProbe();
    scheduleContactVibPreview();
  });
  el('#gen-contact-span').addEventListener('change', e => {
    saveSetting('generator.contact_vibration_span', intOr(e.target.value, 75));
  });
  el('#gen-contact-vibration')?.addEventListener('change', scheduleContactVibPreview);
  // Initial probe when Feel panel is shown with Contact vib on.
  scheduleContactVibPreview();
  updateContactSpanLabel();
  el('#gen-backend').addEventListener('change', () => {
    el('#gen-backend').dataset.userTouched = '1';
        normalizeProductProfile();
    updateGenerateEnabled();
  });
  el('#gen-autoroi').addEventListener('click', () => {
    if (!videoPath) return;
    pendingGenerateAfterRoi = false;
    startAutoFindRegion();
  });

  el('#gen-candidates').addEventListener('click', () => {
    if (!videoPath) return;
    forceCsrtBackend();
    clearPendingSeed();
    setTipFindBusy(true, 'Finding spots…');
    el('#gen-status').textContent = 'Finding motion candidates (nothing applied until you click / Apply)…';
    SuggestROICandidates(videoPath);
  });

  el('#gen-ai-detections')?.addEventListener('click', () => {
    if (!videoPath) return;
    forceCsrtBackend();
    clearPendingSeed();
    setTipFindBusy(true, 'Listing AI tags…');
    el('#gen-status').textContent = 'Listing all AI class tags on this frame (nipples/breasts/glans/…)…';
    ListAIDetections(videoPath, seekSec || 0);
  });

  el('#gen-scene-proposals-load')?.addEventListener('click', async () => {
    if (!videoPath) return;
    try {
      const path = await PickSceneProposalsFile();
      if (!path) return;
      sceneProposalPath = path;
      const atMs = Math.round((seekSec || 0) * 1000);
      const loaded = await LoadSceneProposalAt(path, atMs);
      await adoptSceneProposalLoad(loaded, path.split(/[\\/]/).pop());
    } catch (err) {
      uiError('Scene proposals: ' + err, el('#gen-scene-proposals-status'));
    }
  });

  el('#gen-scene-proposals-beside')?.addEventListener('click', async () => {
    if (!videoPath) return;
    try {
      const atMs = Math.round((seekSec || 0) * 1000);
      const loaded = await LoadSceneProposalsBesideVideo(videoPath, atMs);
      if (loaded?.path) sceneProposalPath = loaded.path;
      await adoptSceneProposalLoad(loaded, 'Beside video');
    } catch (err) {
      uiError('Scene proposals: ' + err, el('#gen-scene-proposals-status'));
    }
  });

  el('#gen-scene-apply-primary')?.addEventListener('click', () => applyScenePrimary());
  el('#gen-scene-apply-partner')?.addEventListener('click', () => applyScenePartner());
  el('#gen-scene-proposals-dismiss')?.addEventListener('click', () => {
    clearSceneProposal();
    redraw();
    el('#gen-status').textContent = 'Scene proposal dismissed — Tip/ROI2 unchanged.';
  });

  el('#gen-seed-suggest')?.addEventListener('click', () => {
    if (candidates.length < 2) {
      el('#gen-status').textContent = 'Need at least two motion candidates for Tip+2nd.';
      return;
    }
    const pair = suggestTipPartnerPair(candidates);
    if (!pair) {
      el('#gen-status').textContent = 'Could not form a Tip+2nd pair — pick by hand.';
      return;
    }
    pendingSeed = pair;
    renderPendingSeedStatus();
    el('#gen-status').textContent =
      `Tip #${pair.tip.index} + 2nd #${pair.partner.index} suggested — Apply to seed, or Dismiss. Tip alone = Everyday.`;
    redraw();
  });

  EventsOn('generate:roi-candidates', result => {
    // Drop stale / cancelled / wrong-video results.
    if (result.videoPath && videoPath && result.videoPath !== videoPath) {
      return;
    }
    unlockRecognitionControls();
    if (result.error) {
      const cancelled = /cancelled/i.test(String(result.error || ''));
      if (cancelled) {
        el('#gen-status').textContent = 'Tip find canceled.';
      } else {
        uiError('Motion candidates: ' + result.error, el('#gen-status'));
      }
      setSeedSuggestEnabled(false);
      clearPendingSeed();
      return;
    }
    const list = Array.isArray(result.candidates) ? result.candidates : [];
    candidates = list.map((c, i) => ({
      x: c.x, y: c.y, w: c.w, h: c.h,
      score: c.score || 0,
      index: c.index || (i + 1),
      class: normalizeClass(c.class || c.label || ''),
    }));
    clearPendingSeed();
    if (!candidates.length) {
      setSeedSuggestEnabled(false);
      el('#gen-status').textContent = 'No motion candidates — mark primary by hand.';
      redraw();
      return;
    }
    setSeedSuggestEnabled(candidates.length >= 2);
    el('#gen-status').textContent = candidates.length >= 2
      ? `${candidates.length} motion candidates — click Tip; optional Shift-click 2nd body-part or Suggest Tip+2nd (Apply). Tip alone = Everyday.`
      : `1 motion candidate — click to set Tip. Contact mark never auto-filled.`;
    redraw();
  });

  EventsOn('generate:ai-detections', result => {
    if (result.videoPath && videoPath && result.videoPath !== videoPath) {
      return;
    }
    unlockRecognitionControls();
    if (result.error) {
      const cancelled = /cancelled/i.test(String(result.error || ''));
      if (cancelled) {
        el('#gen-status').textContent = 'Tip find canceled.';
      } else {
        uiError('AI tags: ' + result.error, el('#gen-status'));
      }
      setSeedSuggestEnabled(false);
      clearPendingSeed();
      return;
    }
    const list = Array.isArray(result.detections) ? result.detections : [];
    candidates = list.map((d, i) => ({
      x: d.x, y: d.y, w: d.w, h: d.h,
      score: d.confidence || 0,
      index: d.index != null ? d.index + 1 : (i + 1),
      class: normalizeClass(d.className || d.class || ''),
    }));
    clearPendingSeed();
    if (!candidates.length) {
      setSeedSuggestEnabled(false);
      el('#gen-status').textContent = 'No AI class tags on this frame — train more samples or seek.';
      redraw();
      return;
    }
    const names = [...new Set(candidates.map((c) => labelFor(c.class) || c.class || '?'))];
    setSeedSuggestEnabled(candidates.length >= 2);
    el('#gen-status').textContent =
      `${candidates.length} AI tags (${names.slice(0, 6).join(', ')}${names.length > 6 ? '…' : ''}) — click Tip; Shift-click contact. Not tip-only.`;
    redraw();
  });

  const PROFILE_VALUES = ['standard', 'weich', 'autotune'];
  const PROFILE_DISPLAY = { standard: 'Normal', weich: 'Soft', autotune: 'Autotune' };

  function normalizeSuggestedProfile(label) {
    // Legacy "tf"/"tj" scene labels map to Normal — Contact vib is the feel layer now.
    if (label === 'tj' || label === 'tf') return 'standard';
    return label;
  }

  function profileSuggestionVia(result) {
    if (result.kind === 'local_model') {
      return `local Go model, confidence ${Math.round((result.confidence || 0) * 100)}%`;
    }
    if (result.kind === 'ai') {
      return `AI server, confidence ${Math.round((result.confidence || 0) * 100)}%`;
    }
    if (result.kind === 'measured' && typeof result.confidence === 'number') {
      return `measured, distance ${result.confidence.toFixed(3)}`;
    }
    return 'saved scene';
  }

  /** Mount Suggest → Apply (never auto-apply). Shared by load soft-suggest + click. */
  function renderProfileSuggestion(status, result) {
    if (!result || !result.found) {
      status.textContent = 'No suggestion (no similar saved scene, AI server unreachable).';
      return;
    }
    const via = profileSuggestionVia(result);
    const label = normalizeSuggestedProfile(result.label);
    const display = PROFILE_DISPLAY[label] || label;
    if (PROFILE_VALUES.includes(label)) {
      status.textContent = `Suggestion: “${display}” (${via}) — `;
      const applyBtn = document.createElement('button');
      applyBtn.textContent = 'Apply';
      applyBtn.addEventListener('click', () => {
        el('#gen-profile').value = label;
        updateProfileUi();
        status.textContent = `Profile “${display}” applied (${via}).`;
      });
      status.appendChild(applyBtn);
    } else {
      status.textContent = `Similar to saved scene “${result.label}” (${via}) — no `
        + 'direct profile name; not applied automatically.';
    }
  }

  el('#gen-suggest-profile').addEventListener('click', async () => {
    if (!videoPath) return;
    const requestSeq = ++profileSuggestionSeq;
    const requestPath = videoPath;
    const status = el('#gen-suggest-status');
    status.textContent = 'Comparing to saved scenes…';
    el('#gen-suggest-profile').disabled = true;
    try {
      const result = await SuggestProfile(videoPath);
      if (requestSeq !== profileSuggestionSeq || requestPath !== videoPath) return;
      renderProfileSuggestion(status, result);
    } catch (err) {
      if (requestSeq === profileSuggestionSeq) status.textContent = 'Error: ' + err;
    } finally {
      if (requestSeq === profileSuggestionSeq) el('#gen-suggest-profile').disabled = false;
    }
  });

  el('#gen-label-scene').addEventListener('click', async () => {
    if (!videoPath) return;
    const label = el('#gen-scene-label').value.trim();
    if (!label) {
      uiWarn('Enter a name for the scene.', el('#gen-suggest-status'));
      return;
    }
    el('#gen-label-scene').disabled = true;
    try {
      const profile = el('#gen-profile').value || 'standard';
      await LabelSceneWithProfile(videoPath, label, profile);
      el('#gen-suggest-status').textContent = `Scene "${label}" saved with Style “${profile}”. `
        + 'Retrain the Go profile model in AI training to include it.';
    } catch (err) {
      uiError('Remember scene: ' + err, el('#gen-suggest-status'));
    } finally {
      el('#gen-label-scene').disabled = false;
    }
  });

  // Contact-vibe Create: tip + partner minimal sets only (no full taxonomy leftovers).
  // AI tip / Scene-map Region keep broader lists via orderedCanonical.
  const fillClassSelect = (selId, preferIds, { strict = false } = {}) => {
    const sel = el(selId);
    if (!sel) return;
    const parts = strict ? onlyCanonical(preferIds) : orderedCanonical(preferIds);
    for (const p of parts) {
      const opt = document.createElement('option');
      opt.value = p.id;
      opt.textContent = p.label;
      sel.appendChild(opt);
    }
  };
  fillClassSelect('#gen-region-class', TIP_CLASS_ORDER, { strict: true });
  fillClassSelect('#gen-region-class2', CONTACT_CLASS_ORDER, { strict: true });
  fillClassSelect('#gen-target-class', CONTACT_CLASS_ORDER, { strict: true });
  fillClassSelect('#gen-ai-target-class', AI_TIP_CLASS_ORDER);
  fillClassSelect('#gen-scene-map-mark-class', SCENE_REGION_CLASS_ORDER);

  createBodyFigure = mountBodyFigure(el('#gen-body-figure'), {
    allowedClasses: CONTACT_VIBE_IDS,
    heading: 'Contact vibe map',
    hint: 'Tip (glans/penis) + one partner (mouth or nipple/breast). Not a stroke path.',
    onSelect: applyBodyClassToMark,
    getActive: () => {
      if (selectedMark?.kind === 'tip') return regionClass1Value();
      if (selectedMark?.kind === 'contact') return regionClass2Value();
      if (selectedMark?.kind === 'extra') {
        return normalizeClass(extraTargets[selectedMark.index]?.class || '');
      }
      if (selectedMark?.kind === 'scene') {
        const m = sceneMapMarks.find((x) => x.id === selectedMark.id);
        return normalizeClass(m?.class || '');
      }
      if (roi2Mode || markMode === 'target') {
        return normalizeClass(el('#gen-region-class2')?.value || el('#gen-target-class')?.value || '');
      }
      return regionClass1Value() || regionClass2Value();
    },
  });

  el('#gen-selected-mark-delete')?.addEventListener('click', () => deleteSelectedMark());
  el('#gen-ai-applied-undo')?.addEventListener('click', () => undoAiApplied());

  el('#gen-ai-roi')?.addEventListener('change', () => {
    const enabled = el('#gen-ai-roi').checked && !el('#gen-ai-roi').disabled;
    const target = el('#gen-ai-target-class');
    if (target) target.disabled = !enabled;
    cancelAnyRoiRequest();
    clearPendingAITarget();
    redraw();
    el('#gen-autoroi-hint').textContent = enabled
      ? 'Choose the exact expected body point on the displayed frame. AI fails closed instead of choosing another class. Result stays editable — drag to refine.'
      : 'Classic Tip-Find is active (editable start box). Enable AI plus an expected body point for strict matching.';
  });
  el('#gen-ai-target-class')?.addEventListener('change', () => {
    cancelAnyRoiRequest();
    clearPendingAITarget();
    redraw();
    const cls = normalizeClass(el('#gen-ai-target-class').value || '');
    el('#gen-ai-target-status').textContent = cls
      ? `Strict target: ${labelFor(cls) || cls}. Press Find tip area.`
      : 'Choose an expected body point before strict AI detection.';
  });
  el('#gen-region-class')?.addEventListener('change', () => {
    const sel = el('#gen-region-class');
    if (sel) sel.dataset.userTouched = '1';
    createBodyFigure?.setActive?.(sel?.value || '');
    updateSelectedMarkUI();
  });
  el('#gen-region-class2')?.addEventListener('change', () => {
    const sel = el('#gen-region-class2');
    if (sel) {
      sel.dataset.userTouched = '1';
      sel.style.outline = '';
    }
    if (roi2) {
      el('#gen-roi2-label').textContent = secondRegionLabel(roi2);
      redraw();
    }
    createBodyFigure?.setActive?.(sel?.value || '');
    updateSelectedMarkUI();
  });
  el('#gen-target-class')?.addEventListener('change', () => {
    const sel = el('#gen-target-class');
    if (sel) sel.dataset.userTouched = '1';
  });
  el('#gen-roi2-fixed')?.addEventListener('change', () => {
    el('#gen-roi2-fixed').dataset.userTouched = '1';
  });

  function syncContactPointsUi() {
    const rhythmOn = !!el('#gen-rhythm-grid')?.checked;
    const usePts = el('#gen-contact-points');
    const row = el('#gen-contact-points-row');
    const path = el('#gen-contact-points-path');
    const pick = el('#gen-contact-points-pick');
    const genBox = el('#gen-contact-points-gen');
    const verify = el('#gen-contact-verify');
    const verifyRow = el('#gen-contact-verify-row');
    const verifyHint = el('#gen-contact-verify-hint');
    if (!usePts) return;
    usePts.disabled = !rhythmOn;
    if (!rhythmOn) {
      usePts.checked = false;
    }
    const show = rhythmOn && usePts.checked;
    if (row) row.style.display = show ? 'flex' : 'none';
    if (path) path.disabled = !show;
    if (pick) pick.disabled = !show;
    // Hybrid verify only when Use contact points is on (path may still be empty).
    if (verify) {
      verify.disabled = !show;
      if (!show) verify.checked = false;
    }
    if (verifyRow) verifyRow.style.display = show ? 'flex' : 'none';
    if (verifyHint) verifyHint.style.display = show ? 'block' : 'none';
    // Teacher generate is available whenever Rhythm-robust is on (path optional until Use is checked).
    if (genBox) genBox.style.display = rhythmOn ? 'block' : 'none';
  }
  el('#gen-rhythm-grid')?.addEventListener('change', syncContactPointsUi);
  el('#gen-contact-points')?.addEventListener('change', syncContactPointsUi);
  el('#gen-contact-points-pick')?.addEventListener('click', async () => {
    try {
      const p = await PickContactPointsFile();
      if (p && el('#gen-contact-points-path')) {
        el('#gen-contact-points-path').value = p;
      }
    } catch (err) {
      uiError('Choose contact points: ' + err, el('#gen-status'));
    }
  });
  el('#gen-contact-points-run')?.addEventListener('click', async () => {
    const status = el('#gen-contact-points-gen-status');
    const btn = el('#gen-contact-points-run');
    if (btn?.dataset.running === '1') {
      CancelContactPoints().catch(() => {});
      if (status) status.textContent = 'Canceling…';
      return;
    }
    if (!videoPath) {
      if (status) status.textContent = 'Load a video first.';
      return;
    }
    const nudenet = !!el('#gen-cp-nudenet')?.checked;
    const teachers = [];
    if (el('#gen-cp-ollama')?.checked) teachers.push('ollama:qwen2.5vl:7b');
    if (el('#gen-cp-lmstudio')?.checked) teachers.push('lmstudio:local-vision');
    if (!nudenet && !teachers.length) {
      if (status) status.textContent = 'Check at least one teacher.';
      return;
    }
    if (status) status.textContent = 'Generating…';
    if (btn) {
      btn.dataset.running = '1';
      btn.textContent = 'Cancel';
    }
    try {
      await GenerateContactPointsForVideo(videoPath, { nudenet, teachers, onnx: [], stepS: 0, out: '' });
    } catch (err) {
      if (btn) {
        btn.dataset.running = '';
        btn.textContent = 'Generate contact points';
      }
      if (status) status.textContent = '';
      uiError('Generate contact points: ' + err, el('#gen-status'));
    }
  });
  EventsOn('contactpoints:progress', (line) => {
    const status = el('#gen-contact-points-gen-status');
    if (status && line) status.textContent = String(line).slice(0, 120);
  });
  EventsOn('contactpoints:done', async (payload) => {
    const status = el('#gen-contact-points-gen-status');
    const btn = el('#gen-contact-points-run');
    if (btn) {
      btn.dataset.running = '';
      btn.textContent = 'Generate contact points';
    }
    if (payload?.cancelled || (payload?.error && /cancelled/i.test(String(payload.error)))) {
      if (status) status.textContent = 'Canceled.';
      return;
    }
    if (payload?.error) {
      if (status) status.textContent = 'Failed: ' + payload.error;
      uiError('Generate contact points: ' + payload.error, el('#gen-status'));
      return;
    }
    const path = payload?.path || '';
    if (path && el('#gen-contact-points-path')) {
      el('#gen-contact-points-path').value = path;
      const use = el('#gen-contact-points');
      if (use && !use.disabled) use.checked = true;
      syncContactPointsUi();
    }
    if (status) status.textContent = path ? `Wrote ${path}` : 'Done.';
    uiInfo(path ? `Contact points → ${path}` : 'Contact points done.', el('#gen-status'));
  });

  el('#gen-import-contact-candidates')?.addEventListener('click', async () => {
    const status = el('#gen-auto-candidates-status');
    if (!videoPath) {
      if (status) status.textContent = 'Load a video first.';
      return;
    }
    try {
      const contactPath = await PickContactPointsFile();
      if (!contactPath) return;
      if (status) status.textContent = 'Importing…';
      const n = await ImportContactCandidatesForVideo(videoPath, contactPath);
      await restoreSceneMapFromCompanion(videoPath);
      if (status) status.textContent = `Imported ${n} candidate${n === 1 ? '' : 's'}.`;
      uiInfo(`Imported ${n} teacher contact candidate${n === 1 ? '' : 's'}.`, el('#gen-status'));
      redraw();
    } catch (err) {
      if (status) status.textContent = '';
      uiError('Import candidates: ' + err, el('#gen-status'));
    }
  });

  el('#gen-auto-candidates-list')?.addEventListener('click', async (ev) => {
    const btn = ev.target?.closest?.('button[data-id]');
    if (!btn || !videoPath) return;
    const id = btn.getAttribute('data-id') || '';
    if (!id) return;
    const status = el('#gen-auto-candidates-status');
    if (btn.classList.contains('gen-auto-seek')) {
      const m = sceneMapMarks.find((x) => x.id === id);
      if (m?.atMs != null && Number.isFinite(m.atMs)) {
        seekTo(m.atMs / 1000);
      }
      return;
    }
    const accept = btn.classList.contains('gen-auto-accept');
    const reject = btn.classList.contains('gen-auto-reject');
    if (!accept && !reject) return;
    if (status) status.textContent = accept ? 'Accepting…' : 'Rejecting…';
    try {
      await ReviewAutoContactCandidate(videoPath, id, accept);
      if (accept) {
        const m = sceneMapMarks.find((x) => x.id === id);
        if (m) m.reviewed = true;
      } else {
        sceneMapMarks = sceneMapMarks.filter((x) => x.id !== id);
      }
      updateSceneMapMarksLabel();
      redraw();
      if (status) {
        status.textContent = accept
          ? `Accepted ${id} (reviewed:true).`
          : `Rejected ${id}.`;
      }
    } catch (err) {
      if (status) status.textContent = '';
      uiError((accept ? 'Accept' : 'Reject') + ': ' + err, el('#gen-status'));
    }
  });

  syncContactPointsUi();
  renderAutoCandidatesList();

  updateContactVibrationOpts();
  syncWorkflowSteps();
}
