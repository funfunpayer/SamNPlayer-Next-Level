# Classical Generate heuristics (G1 package)

Design for the **classical-only** heuristics package on the production
roadmap (`docs/PRODUCTION_ROADMAP.md` phase **G1**). Not AI. Goal: one
coherent set of rules that turn CSRT observations into better funscripts
for Sam Neo 2 — measurable, tunable, and safe to teach AI later.

Related: `docs/FUNSCRIPT_ALGOS.md`, `docs/AUDIO_WORKFLOW.md`,
`docs/SIGNAL_VS_FIDELITY.md`, `docs/ENGINE.md`, `docs/SELF_BUILD.md`.

---

## Why this exists

0.5.11 locked **one CSRT path**. Next quality wins are not “another
tracker in the dropdown,” but **stable post-track decisions**: axis,
peaks, smooth, RDP, speed, weak-ROI hints, audio plausibility.

We learn for AI by first producing **good classical scripts**. AI must
not write the 0–100 curve (`docs/AI_ADAPTER.md`).

---

## Workflow (ship this order)

```text
  1. ROI
        manual mark (product default)
        optional: classic/AI find-region → VerifyROI warn-only
  2. Track
        CSRT only (Go trackcv when linked; else Python CSRT)
  3. Heuristics package (this doc) — posttrack + peak/RDP/speed/axis
  4. Quality Doctor          → Signal Quality (label it)
  5. Audio tempo check       → warn only (never invent positions)
  6. Write .funscript + .samn (+ optional .samn quality sidecar fields)
```

Do **not** put audio or AI before a tracked curve exists (except an
optional tempo *hint* that only biases peak distance — G1.3, gated).

---

## Package contents (v1)

| Knob | Role | Product rule |
|------|------|--------------|
| Axis (`x`/`y`/auto) | Pick dominant motion | Auto OK; surface choice in log; allow force |
| Smooth / Savgol | Kill tracker jitter | Profile-driven; no silent over-smooth |
| Peak distance | Stroke tempo | Device-safe min gap (~100 ms); preview may *hint* (advisory) |
| RDP | Keyframe density | Keep feel; avoid “stair” from over-reduce |
| Speed cap | Device compat | Autotune defaults 400; Normal/Soft off unless set |
| Detrend | Kill baseline drift | **On by default for stroke** (#230); Tf/Tj untouched; `<0` = off |
| Weak-ROI / tiny span | Hint, not rewrite | “Check ROI / axis” — never invent motion |
| Tracking gaps | Metadata + warning | Visible in Quality Doctor / report |
| Audio Hz vs script Hz | Plausibility | Post-hoc; fail open; see `AUDIO_WORKFLOW.md` |
| **Stroke preview (A+B)** | Sparse extrema + cut/pan steers | Timing/flags only; Stage B may enable PerSceneROI / camera for *this run* (#239) |

Implementation homes today: `generator/posttrack`,
`generate_funscript.py`, `generator/audiocheck.go`, Quality Doctor,
`generator/strokepreview` + `generator/stroke_preview_phase.go`,
`generator/detrend_default.go`.
G1 work is **unify + document + tune as one package**, not three
scattered knobs.

---

## Signal Quality vs Motion Fidelity

| Stage | Reports |
|-------|---------|
| Quality Doctor / Script Doctor | **Signal Quality** only |
| FunGen / phase / golden correlation | **Motion Fidelity** only |
| Heuristics changes | Must not mix the two into one marketed score |

A smoother script (higher Signal Quality) that loses phase is a
**regression**. Gate with goldens (`docs/GOLDEN_CLIPS.md`).

---

## Audio (classical)

| Mode | Status | Rule |
|------|--------|------|
| Post-check script Hz vs audio energy Hz | Shipped; keep default-on when ffmpeg present | Warn only |
| QD fail + clear audio Hz → English ROI/axis hint | **Shipped** G1.2 (#this) | Warn only; no curve rewrite |
| Pre-pass tempo hint → bias peak distance | G1.3 optional | Only after goldens show help > harm |
| “No motion → write from audio” | **Rejected** | Audio is not a position curve |

---

## Exit criteria for “heuristics v1 done”

1. One English GUI/docs explanation of the workflow above (Generate help).
2. Same fixed-ROI golden set: Motion Fidelity not worse; Signal Quality
   equal or better on average.
3. Audio check never changes actions; weak-track + clear audio → English
   hint only.
4. No new GUI tracker backends; no AI auto-commit of ROI2.

---

## Out of scope here (later phases)

| Topic | Phase |
|-------|-------|
| Windows in-binary CSRT | G0 |
| Neo 2 raw-value profile / Intent→device | G2 |
| AI ROI / train / fusion | G3 |
| License enforcement | G4 |

---

## Implementation note

Prefer extending existing posttrack + Quality Doctor + audiocheck over
new packages. Every behavior change needs a regression that fails without
it (`CONTRIBUTING.md`). Clip tests beat vibes (`docs/SELF_BUILD.md`).

---

## Inventory — where each knob lives (23 Sep 2026 / v0.5.30+)

Cursor G1next lane: map reality before tune. **No behavior change in this
inventory** — only documentation of what ships today vs what G1.1 still
owes.

### Pipeline order (GenerateWithContext)

```text
  applyStrokePreview     → Stage A probe + Stage B steers (audio / PerSceneROI / camera)
  applyDefaultDetrend    → stroke profiles only (#230)
  Track (CSRT / …)       → positions
  posttrack.Convert      → smooth → detrend → normalize → peaks → RDP → …
  AudioCheck (optional)  → metadata warn only
  Quality Doctor         → Signal Quality score
```

### Knob → code → GUI default → status

| Knob | Code | Everyday / GUI default | Status |
|------|------|------------------------|--------|
| Axis | `trackcv.Options.Axis` (`auto`/`x`/`y`); GUI `#gen-axis` | Automatic | **Shipped** |
| Smooth | `posttrack.Options.SmoothWindow`; GUI `#gen-smooth` | 11 frames | **Shipped** |
| Peak distance | `MinPeakDistanceMs`; GUI `#gen-peakdist` | 150 ms (Autotune path may use 200) | **Shipped**; preview *suggests* only (not applied) |
| RDP | `RDPTolerance`; GUI `#gen-rdp` | 0 = off | **Shipped** |
| Speed cap | `MaxSpeed`; GUI `#gen-maxspeed`; Autotune forces 400 if unset | 0 (off) on Normal/Soft | **Partial** — “always on for Neo 2” not true for Normal |
| Detrend | `applyDefaultDetrend` → `DetrendWindowMs` 2× stroke period (1.5–4 s) | On for stroke; Tf/Tj off | **Shipped** #230 (table above was stale) |
| Dynamic range | `DynamicRangeMs`; GUI `#gen-dynrange` | 3000 ms on | **Shipped** |
| Weak-ROI hint | Quality Doctor / VerifyROI | warn-only | **Shipped** |
| Tracking gaps | `tracking_gaps` metadata | stamped when flags present | **Shipped** |
| Audio post-check | `audiocheck.go`; Stage B may force on | on when ffmpeg + preview weak | **Shipped** |
| QD fail + clear audio Hz hint (G1.2) | `AppendQualityAudioHint` / `append_quality_audio_hint` | warn on Generate when QD failed | **Shipped** |
| Peak bias from audio/preview (G1.3) | preview `suggested_min_peak_distance_ms` | advisory progress only | **Open** — measure before applying |
| Stroke preview A | `strokepreview.RunQuick` | always (unless `SkipStrokePreview`) | **Shipped** #177 |
| Stroke preview B | `applyStrokePreviewSteers` | cut→PerSceneROI; pan→camera (this run) | **Shipped** #239 |
| Rhythm grid | `Options.RhythmGrid` / Advanced checkbox | **off** | **Opt-in** #233/#236 — default gate = Owner clips |

### Gaps to close for “heuristics v1 done” (G1.1+)

1. **Speed cap product rule** — Owner still decides: leave Normal uncapped (current;
   HW smoke 24 Sep OK without forcing a cap), or Neo-2-safe default with opt-out.
   **Not silent.** Cursor keeps Normal/Soft uncapped until that decision.
2. **G1.3 peak-distance bias** — only after goldens show help > harm; today Stage A
   suggests, Convert ignores. Needs Owner clips / measure first.
3. ~~One Generate-help blurb~~ — **DONE** in `EVERYDAY_GENERATE.md` + Review audio `data-help`.
4. **Python `generate_funscript.py` parity** — G1.2 hint mirrored; emergency path only.

### Explicitly not G1 inventory work

- Rhythm-grid default-on (Owner clips)
- F-003 tier-2 lag tie-break (board: don’t)
- New trackers / YOLO-as-Stroke / Pose Stage B
- Changing Everyday Contact-vib or Stroke backend defaults
- Applying G1.3 peak bias without golden evidence

**G1 status (24 Sep, without Owner):** package knobs shipped + documented; G1.2
hint shipped; G1.3 + speed-cap default + rhythm default-on remain Owner-gated.
Function preserved — no silent default changes.