# Scene map — rhythm preview, user marks, learning data (plan)

Status: **P5a–P5c done** — P5c [#297](https://github.com/funfunpayer/SamNPlayer/pull/297) reviewed YOLO export; P5b #256 / `524d23b`; P5a #255 / `c57b910`; P4b #254; P4 #253; P3 #251; P2 v0.5.31; P1 #246; plan #243.
Audit issue [#290](https://github.com/funfunpayer/SamNPlayer/issues/290) is **closable / closed** on tip (P5c shipped; P5e parked; P5d not duplicated).
Implementation: Cursor (per Owner). Claude reviews the engine parts and runs
the golden-clip measurements. **Next:** P5e parked; Claude M3 gate / Owner rhythm clips before P6. Do not reopen P5c.

Owner goal (paraphrased): *build it so it makes Generate better **and** yields
training data, until an AI knows our whole engine — what moves, how, where,
and who (which body part).*

This doc plans the mechanisms and their order. It does not change any
default. Every stage has a measurement gate. The standing rules still hold:
- No silent defaults (`AGENT_COORD` rule 4).
- AI suggests; it never writes scripts (`KI_TRAINING.md`).
- Local-first, deletable, and learning can be switched off
  (`SAM_ARCHITECTURE.md` § Privacy).

---

## 1. The idea in one picture

During the first pass over a clip, the engine already computes a
**rhythm map**: every 8s window, each grid cell gets a score for how strongly
it moves at the stroke tempo. This is the heatmap from the #233 prototype.
The owner looks at that map, sees at a glance what the engine "sees", and
marks what is wrong. For example:

- "not the thigh"
- "the stroke is here"
- "this is the mouth"

The marks do two jobs:

1. **Better Generate now.** The rhythm grid never picks a masked cell. A hint
   narrows the search.
2. **Learning data later.** Every mark, plus every engine decision next to it,
   is stored. That gives labelled examples:

   | Question | Label |
   |---|---|
   | who | region class |
   | where | box / cells over time |
   | what | stroke source vs. excluded |
   | how | tempo, orientation, drift events |

   These examples feed the existing local YOLO trainer and, later, a learned
   cell scorer.

---

## 2. What already exists (reuse, don't rebuild)

| Piece | Where | State |
|---|---|---|
| Rhythm grid: Farneback flow → 16×9 cells, 8s/2s windows, tempo f0 0.6–2.5 Hz, score E²/total, best cell within 3 cells of the CSRT box, sign from CSRT with continuity below \|r\| 0.1 | `generator/trackcv/rhythm_grid.go`, `FlowCells` in `cv.cpp` | **Shipped opt-in** #233; Advanced checkbox #236; v0.5.30 |
| Soft masks (`MaskROIs`, GUI **+ Mask**) | `generator.go` `MaskROIs`, `generator.js` `maskRois` | Shipped. With **RhythmGrid** on, masks stay Go-eligible (converted to exclude marks; P3 #251). Without RhythmGrid, masks still force Python (`native.go`) |
| Body-region taxonomy (9 English IDs) + roles `tracked` / `fixed` / `mask` | `docs/BODY_REGIONS.md`, `generator/bodyparts` | Shipped |
| `metadata.contact_marks` (tip / primary / extras boxes + class) | `funscript/contact_marks.go`, `generator/contact_marks.go` | Shipped; persisted in `.samn` companion |
| `metadata.trajectory` (per-frame tip path, opt-in) | `generator/native.go`, `samn_write.go` | Shipped |
| Local YOLO training (bootstrap → train → ONNX) | `generator/bootstrap_yolo_dataset.py`, `train_yolo_model.py`, `docs/KI_TRAINING.md` | Shipped; box labels are hand-made today |
| Stroke preview Stage A / B (quick probe, cut → PerSceneROI, pan → camera) | `strokepreview.RunQuick`, `applyStrokePreviewSteers` | Shipped #177 / #239 |

**Gaps this plan closed (pre-P1 → historical; map/marks/collect shipped through P5c):**
- ~~No map is ever shown to the user.~~ → P1/P2 Show scene map
- ~~Masks don't reach the Go path.~~ → P3 Go mask eligibility
- ~~Marks are not time-bounded.~~ → P2 scene-scoped marks
- ~~Engine decisions are not recorded.~~ → P5a collect / export-learning
- ~~There is no dataset writer that turns sessions into labels automatically.~~ → P5a–P5c (reviewed YOLO export on #297)

---

## 3. Mechanisms

### M1 — Scene map scan (engine, Go)

**What:** a `SceneMap` result that the GUI can draw over the video.

```go
// generator/trackcv (sketch — names are proposals)
type SceneMap struct {
    Version        int
    Cols, Rows     int          // 16 × rhythmGridRows(w,h)
    Width, Height  int          // video px
    Windows        []MapWindow
}
type MapWindow struct {
    StartMs, EndMs int64
    TempoHz        float64      // local f0
    Score          []uint8      // Cols*Rows, per-window normalized 0–255 (E²/total)
    ChosenCell     int          // -1 if the grid fell back to the tracker
    BoxCX, BoxCY   float64      // CSRT box centre at window mid (full-run only)
    SignRule       string       // "tracker" | "continuity" (full-run only)
    TrackerR       float64      // |r| cell vs tracker (full-run only)
}
```

**Two ways to produce it:**

1. **Quick scan (before Generate).** `ScanSceneMap(video, n)`:
   - Samples *n* windows (default 6 × 8s, spread over the clip, skipping
     scene cuts).
   - Runs only `FlowCells` + `rhythmScores`. No CSRT, no box, no ChosenCell.
   - Cost estimate from #233 numbers: Farneback ≈ 10 ms/frame at 320 px, so
     6 × 192 frames ≈ 12–20 s including seeks. Same budget class as stroke
     preview.
   - This is the owner's "first pass" view.
2. **By-product of a real run.** When `RhythmGrid` is on, `TrackROI` already
   computes every window's scores. It returns the full map (all windows,
   ChosenCell, box, sign rule) at no extra cost. `rhythmGridPositions` only
   needs to also return its per-window decisions.

**Refactor needed:** split `rhythmGridPositions` into:
- `scoreWindows` (map)
- `chooseAndStitch` (curve)

The map and the curve must use the same numbers. Unit-test that splitting
changes nothing: output bit-identical to today on the synthetic tests and on
cached `clip_ausschnitt` flow.

**Size:** 280 s → 140 windows × 144 cells × 1 byte ≈ 20 KB. Fine for `.samn`
(base64) and for a Wails round trip.

### M2 — Marks on the map (GUI)

**Show:**
- A window slider (or follow the video playhead).
- The heatmap over the current video frame, in the same look as the #233
  prototype.
- The CSRT box and the chosen cell (full-run map only).

**Mark kinds.** Reuse the existing box drawing, and add optional cell
painting that snaps to the grid:

| Kind | Meaning | Engine effect (M3) |
|---|---|---|
| `exclude` | "never take the signal from here" (thigh, background, face) | cells inside → removed from candidates |
| `source` | "the stroke is here" | search restricted to hint ∪ normal radius; ties go to the hint |
| `region` | body-part label (`penis`, `mouth`, `hand_1`, …) with a role | label only (who / where) — no engine effect in phase 1 |

**Time scope per mark:** whole clip (default), current scene (cut to cut), or
`fromMs–toMs`. Bodies move, so a mask on the thigh at 140 s is not
automatically right at 20 s.

**Everyday stays simple.** The map and marks live in **Advanced** (or a
"Review map" step after Generate). No new Everyday controls.

### M3 — Engine honours marks (Go)

1. **Candidate filter** in the rhythm grid:
   - A cell is excluded when its centre lies in an `exclude` mark that is
     active at the window's midpoint.
   - `source` marks: if a hint overlaps the search radius, pick the best cell
     inside the hint unless its score is below `0.5 ×` the best outside.
     The factor is a starting value and must be measured.
2. **Go eligibility.** Masks used to force Python. P3 (#251) shipped: when
   `RhythmGrid` is on, masks are allowed on the Go single-ROI path.
   - Not silent for mask users: CHANGELOG entry, board line, progress log
     `"TRACK: masks honoured by rhythm grid (Go)"`.
   - Without `RhythmGrid`, keep Python behaviour (no change for those users).
3. **Camera features.** `EstimateCameraMotionY` excludes only the tracked box
   today. To match Python's punch-out semantics it needs a list of exclude
   rects (C wrapper change in `cv.cpp` / `cv.h`). Masked moving body parts
   must not count as camera motion.
4. **Metadata:** record which marks were active per window (`MapWindow.Marks`
   ids) so the learning data knows what the engine obeyed.

**Measurement gate (Claude):** on `clip_voll`, with the **same** exclude marks
applied to both runs:
- Compare the grid with and without the thigh / left-leg exclude marks
  (visible at 140 s and 170 s in the prototype heatmap).
- Metric: windowed r and orientation vs **both** FunGen references, judged on
  windows where the references agree (see `AGENT_COORD` Decision log,
  23 Sep).
- Ship only if marks don't regress either clip.

### M4 — Persist in `.samn` (who / what / where / how)

New optional `metadata.scene_map` block, written only when a map exists:

```jsonc
"scene_map": {
  "version": 1,
  "video": { "durationMs": 280125, "width": 1280, "height": 720, "sha256_head": "…" },
  "grid": { "cols": 16, "rows": 9 },
  "windows": [ { "startMs": 0, "endMs": 8000, "tempoHz": 1.0,
                 "score_b64": "…", "chosen": 88, "signRule": "tracker",
                 "trackerR": 0.41, "box": [650, 540], "marks": ["m1"] } ],
  "marks": [
    { "id": "m1", "kind": "exclude", "rect": [60, 560, 260, 160],
      "fromMs": 120000, "toMs": 180000, "author": "user", "createdAt": "…" },
    { "id": "m2", "kind": "region", "class": "penis", "role": "tracked",
      "rect": [560, 430, 170, 220], "atMs": 0, "author": "user" },
    { "id": "a7", "kind": "region", "class": "penis", "role": "tracked",
      "rect": [540, 470, 160, 200], "atMs": 96000, "author": "auto",
      "confidence": 0.82, "reviewed": false }
  ],
  "events": [ { "atMs": 174875, "kind": "reacquire", "fromBox": [...], "toBox": [...] } ]
}
```

- **Additive and optional.** Old readers ignore it, and `.funscript` export
  never carries it (size).
- **`author: "auto"` marks** always carry `confidence` and `reviewed`. Only
  `reviewed: true` or high-confidence auto marks may become training labels
  (M5).
- **`sha256_head`** hashes the first MiB of the video. It lets labels be
  matched to the right clip without storing any media.
- **Existing fields stay:** `contact_marks` and `trajectory` are unchanged.
  `scene_map.marks` of kind `region` can be seeded from `contact_marks`, so
  users don't mark twice.

### M5 — Learning-data writer (local)

New step "Export for training", as a GUI button in the AI tab and a CLI
command. It walks `.samn` files with a `scene_map` and writes into the
existing dataset folder (`KI_TRAINING.md` § Folders):

1. **YOLO box labels (who / where)**
   - Source: `region` marks → frame JPEG + YOLO txt, the same layout
     `bootstrap_yolo_dataset.py` already writes.
   - **Auto-labels:** CSRT box and chosen grid cell agree (box within one
     cell of the chosen cell), for ≥ 3 consecutive windows with |r| ≥ 0.3.
     The tip box on those frames is written as class `glans` / `penis` with
     `author: auto`.
   - This is the "frames where CSRT and the grid agree" idea from the drift
     work. It gives labels without hand-labelling, but only where two
     independent signals agree.
   - The owner can review / reject them in the existing training review UI.
2. **Negative regions (what not)**
   - Source: `exclude` marks.
   - Written as a per-clip JSON of "not the stroke source" boxes. The first
     consumer is M6-L1 priors; a later one is hard-negative mining.
3. **Engine trace (how) — JSONL, one line per window**
   - Contents: tempo, per-cell features (score, distance to box, flow
     magnitude, region label of the cell if any), which cell was chosen, and
     sign rule.
   - Plus the outcome where a reference exists: windowed r vs FunGen / a
     user-corrected script.
   - This is the training table for a future **learned cell scorer** (M6-L3).

Privacy: everything stays local and deletable:
- One "Delete learning data" button.
- A Settings switch "Collect learning data" (default **off** until the Owner
  decides otherwise).
- No upload path.

### M6 — Learning stages (each gated, each optional)

| Stage | What learns | From | Gate before it may influence Generate |
|---|---|---|---|
| **L0 collect** | nothing — data only | M4 / M5 | none (no behaviour change) |
| **L1 priors** | simple statistics, no ML: e.g. "user excludes the lower-left corner on 80% of POV clips" → pre-filled exclude **suggestion** on the map | M5 negatives | suggestion only; user confirms |
| **L2 region detector** | existing YOLO → ONNX, now trained on marks + reviewed auto-labels | M5 box labels | suggests `region` / `exclude` marks and the tip ROI; never writes the script; golden-clip ROI accuracy vs today's `ai_roi` |
| **L3 learned cell scorer** | small model (e.g. gradient boosting / tiny MLP, ONNX) that replaces hand-made `E²/total` for picking the source cell | M5 engine trace + outcomes | must beat the hand scorer on ≥ 4–5 clips, windowed r **and** orientation, no clip regressing; opt-in first |
| **L4 Perception v1 fusion** | combines CSRT, rhythm grid, region detector (and pose later) with confidences | all of the above | `SAM_ARCHITECTURE.md` § Perception v1 rules |

"The AI knows our whole engine" means reaching L3/L4: the model has seen, per
window:
- **who** — region labels
- **where** — cells / boxes
- **what** — the source vs the excluded
- **how** — tempo, sign, drift events, outcome

Each stage only suggests until its own measurement gate is passed.

---

## 4. Order of work and ownership

| Phase | Content | Suggested owner | Depends on | Done when |
|---|---|---|---|---|
| **P1** | M1 refactor (`scoreWindows` / `chooseAndStitch`), `SceneMap` from full runs, `ScanSceneMap` quick scan; Wails binding | Cursor (Claude reviews) | — | bit-identical curve on existing tests; scan ≤ ~20 s on `clip_voll`; unit tests for map shape / normalization |
| **P2** | M2 map view + mark tools (exclude / source / region, time scope) in Advanced | Cursor | P1 | owner can paint the thigh out at 140 s and see it persisted |
| **P3** | M3 candidate filter + source hints + Go eligibility with masks + camera exclude list; CHANGELOG + board note | Cursor (engine); Claude measures | P1, P2 | **code done** (#251) — gate in M3 still Claude on both goldens |
| **P4** | M4 `metadata.scene_map` in `.samn` (writer + reader + schema doc in `SAMN_FORMAT.md`) | Cursor | P1 | round-trip test; old files still load; `.funscript` export unchanged |
| **P5** | M5 export (YOLO labels, auto-labels, negatives, JSONL trace) + privacy switch / delete | Cursor; Claude reviews the auto-label rule | P4 | export of `clip_voll` yields reviewed-able auto labels; delete removes all |
| **P6+** | M6 L1 → L2 → L3 | later lanes | enough clips | per-stage gate in the table above |

P1 + P3 are the parts that make Generate better right away. P4 + P5 start
collecting data. Nothing learns before P6, and nothing becomes a default
without the Owner.

---

## 5. Measurement rules (apply to every phase)

- **Goldens:** `clip_voll` (280 s) + `clip_ausschnitt` (50 s) with both
  FunGen references. Use the same harness as #230 / #233: cached `TrackROI`
  → production posttrack → windowed 30 s best-lag r.
- **Orientation:** judge it against the YOLO reference, or only on windows
  where both references agree. The two references contradict each other in
  `clip_voll` 120–180 s and in `clip_ausschnitt` 0–30 s.
- **Default flips** (rhythm grid on, masks on Go, any learned stage) need the
  Owner's ≥ 4–5 clips gate (#234) — r up **and** no orientation regression on
  every clip.
- **Rejected / negative results** go into the `AGENT_COORD` Decision log, so
  nobody retries them.

---

## 6. Owner decisions — DECIDED (24 Sep 2026)

These four product decisions are now **closed**. Cursor does not need to choose defaults for them during implementation.

1. **Quick scan timing — explicit button, not automatic.**
   - `ScanSceneMap` is started explicitly from Advanced via **Show scene map**.
   - Do **not** add the estimated 10–20 s scan before every Generate.
   - A real run with `RhythmGrid` still returns the full map as a by-product.
   - Measure scan time and usage before any later proposal to make it automatic.

2. **Collect learning data — default OFF, explicit opt-in.**
   - `Collect learning data` defaults to **off**.
   - Offer a one-time, clear opt-in in the AI / training area; do not prompt before every Generate.
   - Scene-map state needed for the feature itself may be persisted in `.samn`; dataset collection/export remains separately opt-in and local.
   - Keep **Delete learning data** as specified in M5.

3. **Auto-labels — review required before training.**
   - Auto candidates may be generated using the M5 agreement/confidence rule, but start as `author: "auto", reviewed: false`.
   - Training consumes user labels and `reviewed: true` auto-labels only.
   - No confidence threshold may bypass review yet. Reconsider that only after L2 has been measured once and the Owner explicitly opens a new gate.
   - This prevents tracker/grid errors from becoming self-reinforcing ground truth.

4. **Time-scope default for marks — current scene (cut to cut).**
   - New marks default to the current scene's `sceneStartMs–sceneEndMs`.
   - **Whole clip** and an explicit custom range remain available choices.
   - The engine applies only marks active at the map window midpoint.
   - The GUI should make the current scope visible without forcing the user to configure it for every mark.

### Implementation go-ahead (historical)

**P1 was approved and shipped** (#246). Order **P1 → P2 → P3** held; P2/P3 were not pulled into P1.

P1 split rhythm-grid scoring vs selection/stitching, exposed `SceneMap`, and added `ScanSceneMap` + Wails binding with curve bit-identity vs baseline `e9e697e`.

Shipped after P1's gate:
- **P2:** Advanced map UI and `exclude` / `source` / `region` marks (current-scene default scope).
- **P3:** marks affect Generate (source/exclude, Go masks, camera excludes).
- **P4/P5:** learning collection stays opt-in/off by default; unreviewed auto-labels do not enter training (P5c reviewed-only YOLO on #297).

## 7. Pointers

- Rhythm grid: `generator/trackcv/rhythm_grid.go`, `track.go`
  (`Options.RhythmGrid`), `cv.cpp` `Gray_FlowCells`.
- Masks: `generator/generator.go` (`MaskROIs`), `generator/native.go`
  (eligibility), `cmd/gui-wails/frontend/src/generator.js` (`maskRois`).
- Persistence: `generator/samn_write.go`, `funscript/contact_marks.go`,
  `docs/SAMN_FORMAT.md`.
- Training: `generator/bootstrap_yolo_dataset.py`, `train_yolo_model.py`,
  `docs/KI_TRAINING.md`, `docs/BODY_REGIONS.md`.
- Measurements and rejected variants: `docs/AGENT_COORD.md` Decision log
  (23 Sep: CSRT drift round 1 / 2, rhythm grid, orientation).
