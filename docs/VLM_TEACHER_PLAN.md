# Local VLM teacher → our own detector (plan)

Status: V0 probe/scorer/labels merged (#293, #305). **VLM1 engine + GUI shipped** (#312 engine, #316 Advanced Use contact points, #318 handbook). **V2/V3 tooling (candidates import, dataset, RF-DETR train, ONNX teacher): this PR.** Open: V1 mark-review UI (Cursor), first training run (Owner GPU).
Owner go, 27 Sep 2026: *"The system has to get better. Start if you can,
check whether it holds up. Best later: our own model that uses the data we
generated with Qwen & co. Everything is approved — coordinate with Cursor."*
Owner hardware: one NVIDIA card with **16 GB**.

This doc plans the mechanisms and their order. It changes no default. It
extends `docs/SCENE_MAP_PLAN.md` (marks, learning export, L2 detector) and
follows `docs/AI_ADAPTER.md` (the AI proposes, the existing system measures).

---

## 1. Why

Rhythm-grid numbers (windowed 30 s best-lag r vs FunGen ohne / mit YOLO):

| State | `clip_voll` | `clip_ausschnitt` |
|---|---|---|
| CSRT only | 0.386 / 0.552 | 0.449 / 0.712 |
| Grid, best 23 Sep (no lock) | 0.411 / 0.767 | 0.466 / 0.877 |
| Grid + identity lock (#248) | 0.427 / 0.626 | 0.383 / 0.666 |
| Grid + lock release (#287, main) | 0.455 / 0.752 | 0.486 / 0.887 |

Every fix so far tunes *where the motion is*. None of them knows *what* is
moving:
- CSRT follows a pixel patch.
- The rhythm grid follows the cell that moves at the stroke tempo.

That is why the known failures happen:
- The ROI starts on the breast or a hand next to the stroke (IdLock root cause).
- The thigh rocks at the same tempo.
- CSRT drifts off the shaft.

A human (or a vision-language model looking at the frame) sees at once
where the contact point is. The engine needs that knowledge, but it needs it
per frame, fast, and local. A 7B VLM is too slow for that and too big to
ship. So:

> **The VLM is the teacher, not the engine.** It labels keyframes offline.
> A person confirms the labels. Our own small detector (ONNX, the existing
> L2 path) learns from them and runs per frame in Generate.

---

## 2. Pipeline

```
video ─► keyframes (per shot + every N s)
          │
          ▼
   local VLM (Qwen-VL via Ollama / LM Studio, OpenAI-compatible API)
          │  boxes: contact / penis / glans / hand / breasts / thigh / face …
          ▼
   V0 probe JSON  ──► measurement on goldens (does it hold up?)
          │
          ▼
   V1 SceneMap marks  author:"vlm", reviewed:false  ──► user accepts / rejects
          │
          ▼
   V2 dataset (SceneMap M5 export; reviewed only) + CSRT propagation
          │
          ▼
   V3 own detector (YOLO-type → ONNX, ai_roi.py contract)
          │
          ▼
   anchor for rhythm grid / CSRT seed / exclude, per frame, in Generate
```

These rules hold throughout:
- **Local only.** Frames never leave the machine. There is no cloud VLM and
  no telemetry. No model ships in the repo and nothing downloads
  automatically; the same rule already applies to `ai_roi.py` and Colibri.
- **Review before training.** This is SceneMap § 6 decision 3. VLM boxes
  start as `reviewed:false` and never enter training unreviewed.
- **The AI never writes the script.** VLM boxes only choose *where* the
  engine looks. The curve still comes from flow and tracking.

---

## 3. Runtime and models (16 GB NVIDIA)

| Model | VRAM (approx.) | Licence (check at download) | Role |
|---|---|---|---|
| **Qwen2.5-VL-7B-Instruct**, Q4_K_M / Q8 | ~6 / ~9 GB | Apache-2.0 | **default teacher** |
| Qwen3-VL ~8B (if the local runtime has it) | ~6–10 GB | check | second opinion / later swap |
| Qwen2.5-VL-3B-Instruct | ~3–4 GB | Qwen *research* licence (non-commercial) | plumbing tests only, **not** for product data |

- **Runtime:** Ollama (`ollama pull qwen2.5vl:7b`, port 11434) or LM Studio
  (port 1234). Both speak `/v1/chat/completions` with `image_url`
  base64 parts. The probe reuses `generator/colibri_client.py` for the HTTP call.
- **Coordinates differ per model family.** Qwen2.5-VL returns absolute
  pixels of the image it was sent. Qwen3-VL is documented as 0–1000
  relative. Others return 0–1. The probe therefore sends frames already at
  a model-friendly size (multiples of 28, long side ≤ 896), so the model
  does not resize them. It also has a **calibration mode**: a synthetic image
  with known coloured rectangles, whose answer shows which convention the
  model uses. The probe never guesses the convention.
- **Explicit content is the main unknown.** General VLMs are often weak on,
  or evasive about, explicit anatomy. V0 measures the refusal rate and box
  quality before anything is built on top.

---

## 4. Phases

### V0 — probe: does it hold up? (Claude, this PR)

Tool: `generator/vlm_probe.py`. It is opt-in and has no dependencies beyond
the generator's own.

The tool does four things:
- Samples keyframes: the middle of each shot (`detect_scene_boundaries`)
  plus one frame every `--every-s` seconds.
- Sends each keyframe with one fixed, versioned prompt. The prompt asks for
  JSON boxes with labels from a small vocabulary: the canonical body regions
  plus `thigh` and `contact`.
- Writes `*.vlm.json`. Per keyframe it holds: time, size sent, latency, raw
  answer, `refused`, and normalised boxes 0..1.
- Optionally writes overlay JPEGs, so a person can check the boxes by eye.

**Exemplar mode (Owner idea, 27 Sep).** The Owner asked: *"tell Qwen what
you marked; it should watch that and mark it."*
- `--exemplar-json <labels> --exemplar-count K` (or
  `--exemplar t_ms:x0,y0,x1,y1`) sends K reference frames of the same clip
  first:
  - the contact region is drawn in green;
  - no-go regions (another person, hands, thighs) are drawn in red.
- The model then finds the same region, meaning the same people and body
  parts, in the last image.
- `--follow` also shows the model's own previous answer as a yellow box, so
  it can see where the region was a few seconds earlier.
- In the product, the reference is simply the user's first mark, which is
  the same step as drawing the ROI today. On multi-person clips this removes
  the "which person?" guess.
- Reference frames are never asked about again, and `vlm_score.py` skips
  them.
- Multi-image requests need a runtime that accepts several `image_url` parts
  (Ollama / LM Studio with Qwen2.5-VL). If yours does not, the first answer
  fails and is recorded as an error.

**Oracle ceiling on the two goldens (measured 27 Sep, before any Qwen run).**
Claude labelled the keyframes itself: 28 on `clip_voll` (every 10 s) and
10 on `clip_ausschnitt` (every 5 s). Each label is a `contact` box plus
thigh / hand `exclude` boxes. The labels are committed as
`testdata/golden_clips/*/vlm_oracle.json`. They were fed to the engine as
SceneMap marks in the offline harness, which reproduces #287:
0.456 / 0.752 and 0.482 / 0.888.

| Variant (r ohne / mit) | clip_voll | clip_ausschnitt |
|---|---|---|
| #287 baseline (no marks) | 0.456 / 0.752 | 0.482 / 0.888 |
| contact box as `source` mark | 0.393 / 0.662 | 0.454 / 0.867 |
| thighs / hands as `exclude` | = baseline (never chosen anyway) | = baseline |
| ROI seeded on the first contact box | 0.393 / 0.662 | 0.501 / 0.848 |
| re-seed whenever the locked cell leaves the contact box | 0.444 / 0.770 | 0.454 / 0.867 |

What this shows:
- **"Where" is already solved on these two clips.** Since #287 the chosen
  cell lies inside the hand-labelled contact box in 118 / 140 windows
  (`clip_voll`) and 24 / 25 (`clip_ausschnitt`). The misses are
  neighbouring cells around shot changes.
- **A `source` mark only acts at a lock (re-)seed.** The identity lock
  overrides it afterwards (the M3 caveat in `rhythm_grid.go`). Seeding on a
  box edge cell (120 / 136) instead of 119 costs about 0.09.
- **The references cap what we can measure.** The two FunGen references
  agree with each other at only r ≈ 0.45, windowed:
  - `clip_voll`: 0.45, with windows ranging from 0.85 down to 0.15.
  - `clip_ausschnitt`: 0.45.

  Our r against ohne (0.46 / 0.48) is already at that level.

So a VLM cannot show a gain on these two goldens, even a perfect one. Its
value is **robustness**: clips where today's ROI or lock lands on the wrong
body part, shots without a user ROI, and multi-person scenes. The gate
therefore changes to:

1. **Box quality vs the reference labels:**
   `vlm_score.py <clip>.vlm.json <golden>/vlm_oracle.json`.
   - Contact hit rate ≥ 80 % (the probe's contact centre lies in the
     labelled box; refusals and missing boxes count as misses).
   - Contact-on-exclude ≤ 5 %.
   - Refusal rate < 20 %.
2. **End to end on clips where the baseline fails.**
   - These are the Owner's new clips. At least one has to show the ROI or
     lock on the wrong body part, which the labels identify.
   - The requirement is a gain there, with no regression on the two goldens.
   - This needs references for the new clips. A hand-corrected script is
     best, because FunGen output is only a ~0.45 proxy.

**Multi-person clip: where "where" matters (measured 27 Sep).**
- The clip is an Owner upload: 642 s, two women, POV, 256×144. The
  reference is a FunGen-style script.
- Claude labelled 27 keyframes (every 20 s), committed as
  `testdata/vlm_labels/multi_person_642s.json`.
- The #287 engine starts from a centre ROI and then sits on the heads (cells
  17–21 / 33). Its chosen cell is on the real contact in only **3 of 237
  windows**.
- All numbers below come from the offline harness, which is bit-identical to
  Go `TrackROI` on this clip (max diff 0.0).

| Variant | contact hit | r (detrend 1500) |
|---|---|---|
| #287 baseline | 1 % | 0.304 |
| labels as SceneMap `source`/`exclude` marks (today's M3 path) | 1 % | 0.305 |
| + re-seed when the locked cell leaves the box | 0 % | 0.264 |
| **search anchor = label contact centre** (instead of the CSRT box) | 62 % | **0.425** |
| anchor + marks + re-seed | 85 % | 0.417 |

The same anchor on the goldens:
- `clip_voll`: 0.467 / 0.771, against 0.456 / 0.752.
- `clip_ausschnitt`: unchanged at 0.482 / 0.888.

Anchor + marks costs `clip_ausschnitt` 0.03.

Conclusion:
- A correct *where* lifts the multi-person clip by **+0.12 r (+40 %)** and
  does not regress either golden.
- Marks cannot deliver it. The grid only searches within 3 cells of the
  CSRT box, and the lock ignores sources after the seed.
- **What the teacher (and later our own detector) must feed is the grid's
  search anchor, per frame.** The VLM contact box centre replaces the CSRT
  box centre while a box is active.
- That is the engine hook for V1 / V3. It needs its own opt-in PR and its
  own gate on ≥ 4–5 clips.

**Where V0 runs:** on the Owner's PC. The cloud session cannot download
weights, because its network policy blocks huggingface.co. The Owner runs
one command and sends back the `.vlm.json` (text only, no frames).

### VLM1 — contact points in the engine (Claude, 27 Sep; Owner + Cursor OK)

This is what the multi-person evidence asked for, and it is now built,
opt-in:
- `trackcv.Options.ContactPoints` (normalized, video time) plus
  `ContactHoldMs` (default 1 s).
- Per frame, the nearest point replaces the rhythm grid's search centre,
  **only while the CSRT box is further than the search radius (3 cells)
  from it**.
- With no points, the input is returned untouched.
- `generator.Options.ContactPointsFile` / `ContactPointsMinAgree`, and the
  CLI flags `generate --rhythm-grid --contact-points <file>
  [--contact-min-agree N]`. GUI: Create → Advanced → **Use contact points**
  when Rhythm-robust is on (#316); empty/off = bit-identical.
- `generator/contact_points.py` asks the teachers (NudeNet and any number
  of `vlm_probe` results), counts agreement, and writes the file. The model
  list is in `docs/VLM_MODELS.md`.

Measured with NudeNet alone, fully automatic, through real Go `TrackROI`:
- multi-person clip: r 0.304 → **0.406**; the chosen cell is on the real
  contact in 46 % of windows (was 1 %).
- `clip_voll`: 0.455 / 0.752, unchanged.
- `clip_ausschnitt`: bit-identical to the run without points.

The ≥ 4–5 clip gate still applies before this is ever on by default.

### V1 — VLM proposals as marks (Cursor, after V0 passes)

> **Status 10 Oct (Claude, took over Cursor's lane):** superseded as
> planned. VLM0 showed that teacher output has to drive the rhythm-grid
> *search anchor*, not SceneMap source marks (marks: 1 % hit, anchor:
> 62 %). That path is built: `contact_points.py --vlm/--teacher` feeds
> both the anchor (`--contact-points`) and the reviewable candidates
> (`import-contact-candidates`, Accept / Reject in the map view). A
> separate `.vlm.json` → marks importer is not needed. The original plan
> stays below for reference.

- Import `.vlm.json` as SceneMap marks:
  - `contact` becomes `source`.
  - `thigh` and `hand_*` become `exclude`.
  - The body regions become `region`.
  - All of them get `author:"vlm"` and `reviewed:false`.
  - The time scope is the current shot (§ 6 decision 4).
- The map view shows them in a distinct style, with accept / reject per mark
  and per shot.
- Generate honours only accepted marks. Unreviewed VLM marks count only if
  the user ticks "use VLM suggestions" (off by default).
- Claude measures the accepted-marks run on both goldens.

### V2 — dataset (ChatGPT P5c + Cursor)

- SceneMap M5 export writes the reviewed boxes as YOLO labels, using the
  existing `bootstrap_yolo_dataset.py` layout.
- **Propagation:** a reviewed keyframe box is carried along its shot with
  CSRT, which gives labels on the in-between frames. Those labels get
  `author:"vlm+track"` and are kept only while CSRT confidence is high and
  the box stays within one grid cell of the chosen rhythm cell. This is the
  M5 agreement rule.
- Negatives: exclude boxes go to hard-negative mining.

### V3 pipeline — built (Claude, 28 Sep)

The tooling for V2 and V3 exists. The steps and commands are in
`docs/VLM_MODELS.md` § "The training loop, as built":
- `import-contact-candidates` (Go CLI) turns teacher consensus into
  reviewable marks.
- `contact_detector.py dataset|train` builds the dataset and trains
  RF-DETR, then exports ONNX.
- `contact_points.py --onnx` uses the trained model as a teacher.

Still open:
- **Cursor:** accept/reject UI for `author:auto` contact candidates in the
  map view.
- **Owner:** clips, confirmations, and a GPU run.

### V3 — our own detector (Owner GPU)

- `train_yolo_model.py` → `export_yolo_onnx.py` → `ai_roi.py` contract. A
  nano or small detector trains fine on 16 GB.
- **Gate (SceneMap L2):**
  - Box accuracy on held-out, reviewed frames.
  - End-to-end windowed r on **≥ 4–5 clips**, no clip regressing, and
    orientation judged against the YOLO reference.
  - It stays opt-in until the Owner flips it.
- Then per frame:
  - A detector box seeds and anchors the rhythm-grid search, instead of the
    ROI centre.
  - It excludes hand and thigh cells.
  - It re-seeds CSRT after drift.

### V4 — later

- The learned cell scorer (L3) uses detector labels as features.
- Perception v1 fusion (L4).

---

## 5. Ownership

| Who | Does |
|---|---|
| **Claude** | V0 tool + all measurements (V0 gate, V1 run, V3 gate); reviews the propagation rule |
| **Cursor** | V1 import + map-view review UI + "use VLM suggestions" switch; V2 propagation in the export |
| **ChatGPT** | proposed: P5c reviewed-label export (the V2 sink) — answers the E-ask on the board |
| **Owner** | runs V0/V3 on the 16 GB GPU; **4–5 more clips with references** (still the biggest lever); licence decision below |

---

## 6. Open Owner decisions

1. **Detector framework licence.**
   - `ultralytics` (used by `train_yolo_model.py`) is AGPL-3.0, and
     Ultralytics applies it to trained weights too. For a paid product that
     means either an Ultralytics licence or an Apache-2.0 detector (for
     example YOLOX or D-FINE / RT-DETR in a permissive implementation).
   - This only has to be decided before V3 ships, not for V0–V2.
   - **Claude's recommendation (10 Oct): RF-DETR (Apache-2.0)** — it is
     already what `contact_detector.py` trains, it is at least as accurate
     as YOLO on small objects in public benchmarks, code *and* weights are
     Apache-2.0, and it removes the AGPL question for paid builds. Keep
     `ultralytics` only for the existing optional AI Train path, never
     for shipped weights. **Owner confirmed 10 Oct ("Ja zu allem").**
2. **Runtime default:** Ollama or LM Studio. Proposal: document both, and
   default the probe to Ollama.
3. **Optional:** allow `huggingface.co` in the cloud environment's network
   policy, so that Claude can run the 3B model on CPU for plumbing tests.
   This is not needed for V0 itself.

---

## 7. Risks

- **Refusal or poor boxes on explicit frames.** V0 measures this first.
- **Hallucinated boxes.** Mandatory review, plus the agreement rule for
  propagated labels.
- **Over-fitting to two goldens.** The ≥ 4–5 clip gate applies to every
  default flip.
- **Runtime cost.** A keyframe every 5 s on a 280 s clip is about 60 VLM
  calls. At roughly 1–3 s each on a 16 GB card, that is minutes, once per
  clip, offline.
