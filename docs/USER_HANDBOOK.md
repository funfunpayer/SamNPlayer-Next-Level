# Emotion Script — User handbook

**Product:** SamNPlayer Emotion GUI · **Format:** `.samn` (Emotion Script) · Funscript = share/import only.  
**Audience:** Owner + everyday users. English product copy.  
**Last updated:** 27 Sep 2026 (Contact points Advanced GUI + handbook; local helpers; Test AI server).

**In the app:** Settings → **Open user handbook** · Create → **User handbook** (same FAQ modal).  
Related engineering docs: `EVERYDAY_GENERATE.md`, `AI_SCRIPT_WRITER.md`, `CONTENT_SOURCES.md`, `AUDIO_WORKFLOW.md`.  
**Owner guides (Deutsch):** [`ANLEITUNG.md`](ANLEITUNG.md) · [`owner/`](owner/).

---

## 1. What you get

| Piece | Meaning |
|-------|---------|
| **Create** | Video → tip track (CSRT) → Emotion Script (`.samn`) + companion `.funscript` |
| **Play** | Watch curve on video, Neo 2 feel, edit dots, markers |
| **Device** | Connect Sam Neo 2 / Handy / etc. |
| **Training** | Practice patterns (no video required) |
| **AI Train** | Label scenes / tip ROIs for *helpers* (not the stroke writer) |
| **Settings** | Models, Collect learning data, logs, **Open user handbook** |

**Default stroke writer is classical CSRT.** AI never silently invents the curve.

---

## 2. Everyday Create (recommended path)

1. Open **Create** → choose a video.  
2. **Find tip area** (or paint the box). Optional: *Smarter tip find* if an AI ROI model is set in Settings.  
3. Leave **Contact vibration** on (default). Feel shows a live vib probe for
   Sensitivity / Curve (synthetic bounce — not your clip). Optionally mark
   nipples/mouth (gold / magenta) for feel later.  
4. Click **Create** / Generate. Optional Expert knobs show a rich live probe
   (raw vs postprocess curves + peak badges — synthetic only).
5. **Review & improve:** trim · **Fill gaps** · **Heal tracking gaps** · audio check
   (Speech-Hold / Feel segment strip · optional chapters — never rewrites stroke).
6. Open **Play** — soft curve + dots; if the script has `audio_check` segments,
   the same Speech-Hold / Feel strip appears (seek · filters · optional chapters)
   and filtered bands can paint on the heatmap (“Show on heatmap”).
   Edit if needed. Connect device when ready.

Auto after Generate: fill gaps + heal known tracker-loss windows (linear bridge only).

---

## 3. FAQ

### Is everything in the GUI?
**Almost for everyday use.** Create, Play, Device, Training, Settings, Scene map (Advanced), learning export, classical AI-training export, Load project / Scale range, Optimize for Neo 2, **in-app handbook** — yes.

Still **CLI / advanced / not Everyday GUI:**
- Whole-frame **4-zone** stroke mode (CLI-only — weaker on measured clips; not in Create GUI)
- Flow tracker as Generate default (CLI)
- ONNX vision AI **draft script** writer (S2+ model inference — not shipped; imitation draft is available after **Export classical run**)

### Why is Create better than importing a random Funscript?
Create tracks the tip in *your* video. Import + **Optimize for Neo 2** polishes foreign scripts (fill gaps → contact → bake) but cannot invent missing tracking.

### What is Heal tracking gaps vs Fill gaps?
| Control | Does |
|---------|------|
| **Fill gaps** | Linear points across *long time holes* between actions |
| **Heal tracking gaps** | Uses `tracking_gaps` from Generate: strips junk *inside* loss windows, bridges **only those windows**, clears metadata so Contact vib is not muted forever |

Neither re-runs CSRT. Neither invents motion from audio (audio only spaces steps).

### Can AI write the Funscript?
**Path is open, opt-in, local only** (`docs/AI_SCRIPT_WRITER.md`):
- **S0** plan + stub — shipped  
- **S1** **Export classical run** (Create → **Advanced**) — after a good Create, status reminds you; builds a local imitation library  
- **S2-imitation** — with ≥1 export, **AI draft script** stretches the best duration + tip-aspect match → Quality Doctor → Keep/Discard (experimental; not Everyday default)  
- ONNX vision draft writer — not shipped yet

Cloud “ChatGPT writes positions” is **out of product**.

### Smarter tip find / Suggest profile — do they change the curve?
No. They **propose** tip box or style. You Apply. Curve still comes from CSRT Create.

### Local helpers — setup path (optional)
All local, deletable, off until you wire them. Full guide: `docs/LOCAL_MODEL_SETUP.md` (Colibri: `docs/COLIBRI_SETUP.md`).

1. **Tip ROI model** — AI Train → label tip boxes → train ONNX → Settings → **Open AI training** / set path → **Check availability** → Create offers *Smarter tip find* (Apply still required).  
2. **Style suggestion** — Create → remember scenes with Style → AI Train → **Train Go profile model** → Create **Suggest profile** → **Apply** (soft suggestion on load also shows Apply; never auto-applies).  
3. **AI draft script** — Create → **Export classical run** (≥1) → Advanced **AI draft script** → Keep/Discard. No Settings model path yet.  
4. **Colibri (optional)** — Settings AI server URL for prose Suggest / quality opinion; measured scene similarity works without it.

### Scene map?
Advanced → **Show scene map**: rhythm heatmap + marks (exclude/source/region). Explicit only — never auto before Create. Optional **Export for learning** needs Settings → Collect learning data.

### Contact vibration vs tip path?
Contact vib follows stroke depth by default. With **Record tip path** + contact marks, Play can also buzz when the tip grazes a mark (Feel Stage A).

### Use contact points (Advanced)?
Opt-in after the VLM1 engine. With **Rhythm-robust signal** on, Create → Advanced → **Use contact points** loads a teachers JSON. The rhythm grid may search near those points only when the tip box is far away (>3 cells). Empty/off = bit-identical Everyday Create.

Optional hybrid: Advanced → **Verify with the engine (hybrid, K=1.5)** (next to Use contact points). Keeps a teacher point only where the engine's own rhythm is ≥1.5× stronger than at its chosen cell (same as CLI `--contact-verify 1.5`). Default **off**.

**Build the JSON in the GUI:** Advanced → teacher checkboxes (NudeNet / Ollama / LM Studio) → **Generate contact points** (writes `.contact.json` and fills the path). CLI `contact_points.py` still works.

**Review teacher candidates:** after Import candidates (or a map with `author:auto` marks), Advanced scene map lists pending autos — **Accept** sets `reviewed:true` (eligible for P5c YOLO), **Reject** deletes. Unreviewed autos stay out of export.

### Settings Check AI setup?
Settings → **Local AI setup** → **Check AI setup** probes GPU/packages/train venv/local servers. **Install teachers / train / models** run the matching `ai_setup.py` profiles (training stays in a separate venv; Everyday CSRT OpenCV untouched).

### Mac / Linux / Windows?
Windows portable is the primary smoke target. See release notes for your build.

### Does it cost anything? Do I need a key?
No. SamNPlayer is freeware for private, non-commercial use — no key, no trial
limits. Accept the EULA on first start (Settings → Licence shows it again). If
it helps you, Settings → *Support SamNPlayer* has the donation links.

---

## 4. Play — quick map

| Control | Use |
|---------|-----|
| Play / Stop | Device + curve (default). Video ▶ can also start device (toggle). |
| Edit curve | Drag dots; click empty = add; double-click = delete (≥2 remain). |
| Optimize for Neo 2 | Import path: fill/heal gaps → contact on → bake vibe/suction → `.samn` |
| Load / Save project | `.snp.json` — script, video, offset, seek, loop |
| Scale selection | Soften/boost marked range on the active Curve axis (factor slider; optional Soft edges) |
| Heatmap / markers | Seek, loop, Extended-O, O-markers |
| **Bookmarks** | Named times in script — add at playhead, seek, remove |
| **Chapters** | Named ranges — mark heatmap range, add, seek, remove (stored replaces auto summary) |
| Contact strength / curve | Live feel without rewriting file |

Keyboard: Space · ←/→ · ,/. · 1–9 · +/− offset · L loop · E Extended-O · O marker — see Play help chip.

---

## 5. Device & Training

| Tab | Use |
|-----|-----|
| **Device** | Connect Sam Neo 2 / Handy / etc. Optional short connect-test pulse in Settings. **Intiface:** start Intiface Central → Via Intiface Central (empty = this machine). After one socket drop, SamNPlayer tries **one** auto-reconnect; then tap Connect again. |
| **Training** | Practice patterns without video (technique, channel, cycles, ramp/hold/rest). |
| **AI Train** | Label tip ROIs / train ONNX; **Train Go profile model** — helpers only, not the stroke writer. |

---

## 6. Settings that matter

| Setting | Effect |
|---------|--------|
| **Open user handbook** | In-app FAQ (same content as this doc’s Everyday sections) |
| AI ROI model path | Enables *Smarter tip find* |
| **Open AI training** | Jumps to the AI Train tab (label → train → back here for Check availability) |
| Collect learning data | Allows Scene map **Export for learning** |
| AI server URL (Colibri) | Optional local prose for Suggest profile / quality opinion — **Test AI server** probes `/v1/models` |
| **Check AI setup** / Install teachers·train·models | Settings → Local AI setup — probe GPU/packages/servers; install profiles never touch Everyday CSRT OpenCV |

**Not in Settings yet:** AI draft model path — reserved Go key `generator.aiScriptModelPath` for a future ONNX writer. Until then, use **Export classical run** → **AI draft script** from the imitation library (no Settings path needed).

---

## 7. Files on disk

| File | Role |
|------|------|
| `*.samn` | Emotion Script (source of truth) |
| `*.funscript` | Community companion / export |
| `*.snp.json` | Playback project |
| `…/roi_training_dataset/scene_map_learning/` | Scene-map collect (opt-in) |
| `…/roi_training_dataset/ai_script_imitation/` | Classical runs for AI draft training (S1) |

---

## 8. Troubleshooting

| Symptom | Try |
|---------|-----|
| Flat / stuck curve | Re-find tip · Invert motion · Heal tracking gaps · Check tracking_gaps muted Contact |
| Feels inverted | Advanced → **Invert motion direction** |
| Camera pans drift | Camera motion compensation · Fix contact area (static) off |
| Long-clip drift | Advanced → **Rhythm-robust signal** (opt-in); optional **Use contact points** + **Verify with the engine** (hybrid K=1.5) |
| Audio “wrong tempo” | Warn only — fix ROI/axis; does not rewrite curve |
| AI draft greyed out | **Export classical run** once after Create (≥1 sample), then draft enables — ONNX model path not required for imitation |

---

## 9. Also in the GUI (quick)

| Control | Where |
|---------|-------|
| **Playlist** | Play — queue multiple scripts |
| **Align fill to audio tempo** | Create → Review (optional when filling gaps) |
| **Sliding dynamics / Auto-Retry / Suggest O-markers** | Create → Advanced |
| **Export classical run** | Create → Advanced (S1 training sample; needs a Create result) |
| **Use contact points** | Create → Advanced (needs Rhythm-robust; teachers JSON — Generate or Choose…) |
| **Verify with the engine** | Create → Advanced (with Use contact points; hybrid K=1.5; default off) |
| **Generate contact points** | Create → Advanced (teacher checkboxes → `.contact.json`) |
| **Accept / Reject** auto candidates | Create → Advanced scene map (`author:auto`; P5c gate) |
| **Import candidates…** | Create → Advanced scene map (from `.contact.json`) |
| **Check AI setup** | Settings → Local AI setup |

---

## 10. What we optimized for (honest)

- **Reliable Everyday CSRT** over flashy AI curve writers  
- **Opt-in** AI helpers (ROI, profile suggest, scene map, future draft)  
- **GUI load rule:** shipped features must appear in Emotion UI  
- Look: dark ink + **lilac accent** + teal secondary (Owner preference over honey orange); more motion on tabs / Create / handbook / CTAs (`prefers-reduced-motion` safe)

Not yet the “best possible” recognition endgame — that needs measured gates (Owner: speed-cap, ≥4–5 rhythm clips, G1.3, AI S2 metrics). This handbook matches **what ships today**.
