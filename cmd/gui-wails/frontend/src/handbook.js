// In-app user handbook (Emotion GUI). Source of truth for agents also lives in
// docs/USER_HANDBOOK.md — keep the FAQ here in sync when product behavior changes.

const SECTIONS = [
  {
    title: 'Everyday Create (recommended)',
    body: `1. Open Generator → choose a video.
2. Find tip area (or paint the box). Optional: Smarter tip find if an AI ROI model is set in Settings.
3. Leave Contact vibration on (Feel: Sensitivity / Curve show a live vib probe with active%/peak badges).
   Optionally mark nipples/mouth for feel later.
4. Click Create / Generate. Optional Expert knobs show a rich live probe
   (raw vs postprocess + peak badges — synthetic only).
5. Review & improve: trim · Fill gaps · Heal tracking gaps (+ opt-in Rhythm bridge) · Repair this span · audio check
   (Speech-Hold / Feel segment strip + optional chapters).
6. Open Play — soft curve + dots; if the script has audio_check segments,
   the same Speech-Hold / Feel strip appears (seek · filters · optional chapters)
   and filtered bands can paint on the heatmap (Show on heatmap).
   Edit if needed. Connect device when ready.

Auto after Generate: fill gaps + heal known tracker-loss windows (linear bridge only).
Default stroke writer is classical CSRT — AI never silently invents the curve.`,
  },
  {
    title: 'Speech-Hold & Feel segments',
    body: `After Audio check (ffmpeg), Review can show holding / gentle / intense / climax blocks from classical audio energy — same path as the tempo check. Play shows the same strip when the loaded script already has metadata.audio_check (including companion .funscript for .samn).

• Click a block to seek Play
• Filters hide labels; “Speech-hold only” keeps dialogue/quiet Hold cues
• “Show on heatmap” paints filtered bands on the Play heatmap (thin curve underlay)
• “Add visible as chapters” merges chapter marks (does not rewrite the stroke)

Audio never invents 0–100 actions. Everyday Create stays Go CSRT.`,
  },
  {
    title: 'Is everything in the GUI?',
    body: `Almost for everyday use. Create, Play, Device, Training, Settings, Scene map (Advanced), learning export, classical AI-training export, Load project / Scale range, Optimize for Neo 2 — yes.

Still CLI / advanced / not Everyday GUI:
• Whole-frame 4-zone stroke mode (CLI only — removed from Create GUI)
• Flow tracker as Generate default (CLI)
• ONNX vision AI draft writer (S2+ model inference — not shipped; imitation draft is available after Export classical run)`,
  },
  {
    title: 'Fill gaps vs Heal tracking gaps',
    body: `Fill gaps — linear points across long time holes between actions.
Heal tracking gaps — uses tracking_gaps from Generate: strips junk inside loss windows, bridges only those windows, clears metadata so Contact vib is not muted forever. Opt-in Rhythm bridge continues the stroke rhythm (line fallback). Repair this span rewrites a user-marked range the same way without clearing tracking_gaps.

Neither re-runs CSRT. Neither invents motion from audio (audio only spaces steps).`,
  },
  {
    title: 'Can AI write the Funscript?',
    body: `Path is open, opt-in, local only:
• S0 plan + stub — shipped
• S1 Export classical run (Generator → Advanced) — after Create, status reminds you; builds a local imitation library
• S2-imitation — with ≥1 export, AI draft script stretches the best duration + tip-aspect match → Quality Doctor → Keep/Discard (experimental; not Everyday default)
• ONNX vision draft writer — not shipped yet

Cloud “ChatGPT writes positions” is out of product. Everyday Create stays CSRT.`,
  },
  {
    title: 'Play — quick map',
    body: `Play / Stop — device + curve (default). Video ▶ can also start device (toggle).
Edit curve — drag dots; click empty = add; double-click = delete (≥2 remain).
Bake & share (details) — Bake feel channels / Share for other apps
Optimize for Neo 2 — import path: fill/heal gaps → contact on → bake vibe/suction → .samn
Script check & analysis (details) — summary + Check script (Script Doctor)
Load / Save project — .snp.json
Curve edit (details) — Cap / Speed-cap / Scale / Delete / Speed HL / FPS-Snap / BPM (opens when you mark a heatmap range)
Video sync — Device follows video; pause/buffer zeros the device immediately (soft-start on resume)
Speed-cap selection — stretch too-fast segments in the marked range (Cap intensity slider; default 400)
Speed highlights — red “too fast” bands on the curve (HL threshold slider + show/hide; display only)
Scale selection — soften/boost marked range on the active Curve axis (factor slider; optional Soft edges)
BPM grid — optional beat lines on the curve (BPM field or audio tempo); display only
Curve zoom — Zoom in/out / selection / reset (+ wheel) synced with heatmap; display/nav only
Contact vibration — Strength / Sensitivity / Curve + live SVG probe (synthetic; optional Save to script)
Playlist — queue multiple scripts
O-markers / Bookmarks / Chapters — each behind a details panel (O-markers & Chapters open on heatmap selection)
Bookmarks — named times: add at playhead, seek, rename, remove
Chapters — named ranges: mark heatmap, add from selection, seek, rename, remove
Heatmap / markers — seek, loop, Extended-O, O-markers (secondary Intensity range + readout)
OFS tools — Cap / HL / Scale / FPS-Snap / BPM grid (range + readout where applicable)
Playback Advanced — Tick / Max-Speed / Smoothing / Soft-Start + Extended-O Amplitude/Hold/Restore (range + readout; device feel only)

Keyboard: Space · ←/→ · ,/. · 1–9 · +/− offset · L loop · E Extended-O · O marker.`,
  },
  {
    title: 'Device & Training',
    body: `Device — connect Sam Neo 2 / Handy / etc.; connection test optional in Settings.
Intiface Central — start the server, then Via Intiface Central (empty address = this machine). After a single socket drop, one automatic reconnect is tried; if it stays down, tap Connect again.
Training — practice patterns without a video (technique, channel; Cycles/Ramp/Hold/Rest + Peak/Plateau/Progression range + readout).
AI Train — label tip ROIs / train ONNX, and Train Go profile model (helpers only — not the stroke writer).`,
  },
  {
    title: 'Settings that matter',
    body: `Open user handbook — this FAQ.
AI ROI model path — enables Smarter tip find. Use Open AI training to jump to the AI Train tab, then Check availability.
Collect learning data — allows Scene map Export for learning.
Colibri / AI server URL — optional local prose for Suggest profile (see docs/COLIBRI_SETUP.md). Use Test AI server after coli serve.

AI draft model path is not in Settings yet (reserved for a future ONNX writer). Until then, use Export classical run → AI draft script from the imitation library.`,
  },
  {
    title: 'Also in the GUI',
    body: `Play Playlist — queue multiple scripts.
Create Review — Align fill to audio tempo (optional when filling gaps).
Create Advanced — groups: Tracking & polarity, Long-clip anti-drift (hybrid assist on CSRT), Scene map, AI assist (nested AI draft — never Everyday), Signal & quality. Expert tuning (closed): axis / adaptive / Re-find / smooth / peak / RDP / max speed (range + readout) / rich live probe (raw underlay + postprocess + peak badges) / CSRT-only note. Impulse vib: Feel → Curve (Advanced mirror under Expert). Everyday base = tip → Go CSRT; Hybrid = assist/verify only.`,
  },
  {
    title: 'Use contact points (Advanced)',
    body: `Opt-in after VLM1 engine: when Rhythm-robust signal is on, Advanced → Use contact points loads a teachers JSON. The rhythm grid may search near those points only when the tip box is far away (>3 cells). Empty/off = bit-identical Everyday Create.

Optional hybrid: Advanced → Verify with the engine (hybrid, K=1.5) next to Use contact points. Keeps a teacher point only where the engine's own rhythm is ≥1.5× stronger than at its chosen cell (same as CLI --contact-verify 1.5). Default off.

Build the JSON in the GUI: Advanced → teacher checkboxes (NudeNet / Ollama / LM Studio) → Generate contact points (writes .contact.json and fills the path). CLI contact_points.py still works.

Review teacher candidates: after Import candidates (or a map with author:auto marks), Advanced scene map lists pending autos — Accept sets reviewed:true (eligible for P5c YOLO), Reject deletes. Unreviewed autos stay out of export.

Settings → Local AI setup → Check AI setup probes GPU/packages/train venv/local servers; Install teachers / train / models run ai_setup.py profiles without touching Everyday CSRT OpenCV.`,
  },
  {
    title: 'Troubleshooting',
    body: `Flat / stuck curve — re-find tip · Invert motion · Heal tracking gaps · check tracking_gaps muted Contact.
Feels inverted — Advanced → Invert motion direction.
Impulse peaks-only vib — Feel → Curve → Impulse (Advanced → Expert has a synced mirror).
Camera pans drift — Camera motion compensation · Fix contact area (static) off.
Long-clip drift — Advanced → Rhythm-robust signal (opt-in). Optional: Use contact points (teachers JSON) when Rhythm-robust is on — Generate contact points or Choose… a file. Optional: Verify with the engine (hybrid K=1.5) so weak teacher points are dropped.
Audio “wrong tempo” — warn only; fix ROI/axis; does not rewrite the curve.
AI draft greyed out — Export classical run once after Create (≥1 sample), then draft enables. ONNX model path is not required for imitation.`,
  },
];

let overlay = null;

function closeHandbook() {
  if (overlay) {
    overlay.remove();
    overlay = null;
  }
}

/** Open the in-app handbook modal. */
export function openHandbook() {
  closeHandbook();
  overlay = document.createElement('div');
  overlay.className = 'handbook-overlay';
  overlay.setAttribute('role', 'dialog');
  overlay.setAttribute('aria-modal', 'true');
  overlay.setAttribute('aria-label', 'User handbook');

  const panel = document.createElement('div');
  panel.className = 'handbook-panel';

  const head = document.createElement('header');
  head.className = 'handbook-head';
  head.innerHTML = `
    <div>
      <p class="handbook-kicker">Emotion Script</p>
      <h2>User handbook</h2>
      <p class="handbook-sub">How to use Create, Play, gaps, and the opt-in AI path. Default curve writer stays CSRT.</p>
    </div>
    <button type="button" class="handbook-close" aria-label="Close handbook">Close</button>
  `;

  const nav = document.createElement('nav');
  nav.className = 'handbook-nav';
  const body = document.createElement('div');
  body.className = 'handbook-body';

  SECTIONS.forEach((sec, i) => {
    const id = 'hb-sec-' + i;
    const a = document.createElement('button');
    a.type = 'button';
    a.className = 'handbook-nav-btn' + (i === 0 ? ' is-active' : '');
    a.textContent = sec.title;
    a.addEventListener('click', () => {
      nav.querySelectorAll('.handbook-nav-btn').forEach(b => b.classList.remove('is-active'));
      a.classList.add('is-active');
      const el = body.querySelector('#' + id);
      if (el) el.scrollIntoView({ behavior: 'smooth', block: 'start' });
    });
    nav.appendChild(a);

    const article = document.createElement('article');
    article.className = 'handbook-section';
    article.id = id;
    const h = document.createElement('h3');
    h.textContent = sec.title;
    const p = document.createElement('pre');
    p.className = 'handbook-text';
    p.textContent = sec.body.trim();
    article.appendChild(h);
    article.appendChild(p);
    body.appendChild(article);
  });

  const foot = document.createElement('p');
  foot.className = 'handbook-foot hint';
  foot.textContent = 'Full markdown for agents: docs/USER_HANDBOOK.md · Look: dark ink + lilac accent';

  panel.appendChild(head);
  panel.appendChild(nav);
  panel.appendChild(body);
  panel.appendChild(foot);
  overlay.appendChild(panel);
  document.body.appendChild(overlay);

  head.querySelector('.handbook-close').addEventListener('click', closeHandbook);
  overlay.addEventListener('click', (e) => {
    if (e.target === overlay) closeHandbook();
  });
}

document.addEventListener('keydown', (e) => {
  if (e.key === 'Escape' && overlay) {
    e.stopPropagation();
    closeHandbook();
  }
});
