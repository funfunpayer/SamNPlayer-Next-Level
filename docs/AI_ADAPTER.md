# Local AI as an adapter on top of the existing system

This file records how a local AI enters the generator without replacing the
measurable, non-AI system — a request from the chat on September 14, 2026:
"the generator should run with local AI, and for that should be able to use
the results/tools of the non-AI system; they should work together."

## Principle

The AI proposes, the existing system measures. This is not a new rule but
the line already followed in `docs/TEAM_STAND.md` (section 8) and
`quality_model.py`: a learned model is only used once it outperforms the
fixed rules in cross-validation, and a human confirms the result. The same
applies to the AI extension:

- The AI produces a **proposal** (region, profile, judgment).
- The proposal runs through **the same** classical pipeline (CSRT/flow
  tracking, Quality Doctor, mapper) as a manually marked one — there is no
  second, AI-only output path that produces a `.funscript` on its own.
- A generic AI suggestion may fall back to the non-AI method. An explicit
  **expected body point** is different: strict target matching fails closed
  when that class is missing, weak or ambiguous. It never substitutes another
  class or silently invokes motion auto-ROI; the user may keep the existing
  region or mark the intended point manually.
- Both engines are **optional** and run **locally** — no model in the
  repository, no automatic download, no telemetry. Anyone who does not
  install or start them notices nothing of their existence.

## Two engines, two jobs

Deliberately **not** routed through a gateway (Bifrost/LM Studio), but two
independent, lightweight local engines — each for the job it is built for:

| Engine | Job | Why this one |
|---|---|---|
| **ONNX Runtime** | Vision: propose a body-part region | Small (a few MB of model plus runtime), CPU/GPU; fits the existing detector/export path |
| **Go motion-profile model** | Learn Normal/Soft/Autotune from confirmed motion signatures | Go training/classification with no ML runtime, GPU, server or network; signature measurement still uses local Python/OpenCV |
| **Colibri** ([JustVugg/colibri](https://github.com/JustVugg/colibri)) | Large local language models for judgments with a reason attached (quality, profile choice in prose) | Pure C, no CUDA/PyTorch needed, runs on ordinary hardware, `coli serve` speaks the OpenAI `/v1/chat/completions` API — a plain HTTP client is enough to connect it, no SDK required. Owner setup: [`COLIBRI_SETUP.md`](COLIBRI_SETUP.md); all local helpers: [`LOCAL_MODEL_SETUP.md`](LOCAL_MODEL_SETUP.md) |

Both are swappable: the backend register (`backends.py`) and the
`roi_finder` injection point in `track_by_scenes()` exist for exactly this
purpose — register a function with a fixed contract, nothing in the
pipeline changes.

## Where "ONNX like FunGen 2" comes from — and what was NOT taken from it

FunGen 2 is a closed binary (FunGen 1 is PolyForm Strict), see
`HANDOFF.md`, "Third-party code policy". No source code was read. Publicly
described (fungen.app, GitHub README): FunGen detects objects with a YOLO
model and tracks from there. Only the **idea** — "detection proposes a
region, tracking measures from it" — was carried over. The model contract
in `ai_roi.py` (input `1x3xHxW`, output `(N,6)` normalized) is an
independently designed interface, not a reimplementation of a specific
export format.

## Order (agreed with the user: region → profile → quality)

### 1. Propose a region — **implemented**

- `generator/ai_roi.py`: `find_roi(video, start_frame, end_frame)` with the
  same contract as `auto_roi.find_roi` — both are interchangeable
  `roi_finder` implementations.
- CLI: `--per-scene-roi --roi-finder ai --ai-model-path model.onnx`
  (default stays `auto`, so behavior is unchanged without the new option).
- `decode_detections()` and `select_best_box()` are pure functions and
  testable without a model or onnxruntime (`generator/ai_roi_test.py`).
  `select_best_box` / `select_two_best_boxes` accept optional
  `preferred_class_ids` so a multi-class ROI model can prefer e.g.
  `hand`/`breast` over other detections (falls back to all classes when
  none of the preferred ids appear in the frame).
- CLI: `--preferred-classes hand,breast` (or `0,1`) with `--classes-json`
  pointing at the dataset or model directory. After training,
  `classes.json` is copied next to the `.onnx` automatically.
- GUI: Settings → **Preferred classes**; Generate uses that preference for
  AI ROI proposals.
- Generate also has a stricter, user-selected **expected body point** path.
  `ai_roi.py --expected-class … --strict-class` resolves that canonical class
  through the `classes.json` beside the ONNX model. It returns a typed result
  with matched class and confidence, or a stable failure code such as
  `manifest_missing`, `manifest_invalid`, `class_conflict`,
  `target_not_detected`, `below_confidence` or `ambiguous_target`. The Wails
  event remains only a proposal: Create draws a
  dashed box and requires **Apply target** before replacing the active ROI.
  This path has no preferred-class fallback and no coordinate-only success. It
  evaluates a PNG produced by the same `DumpFrameAt` FFmpeg path and timestamp
  as the preview currently shown in Create, instead of re-seeking through
  OpenCV or pooling detections from different scenes or people. Changing the video,
  preview time, expected class or AI mode cancels the superseded local process;
  stale result and progress events are sequence-gated.
- When the optional Rhythm Grid writes the signal, its first cell must lie in
  the selected start box and remains the signal cell for that entire shot.
  Rhythm similarity alone is deliberately not allowed to switch cells: a
  stronger adjacent limb can be perfectly in-phase or anti-phase. FFT windows
  are clipped to shot boundaries, sign continuity is reset there, and the next
  shot re-seeds from its tracker position. Until enough frames exist in the new
  shot, the signal falls back to the tracker rather than mixing two scenes or
  inventing a different target.
- `onnxruntime` lives in `generator/requirements-ai.txt`, NOT in
  `requirements.txt`.
- **GUI wiring done:** `generator.go` gained `FindROIAIWithProgress`/
  `AIRoiAvailable`, sharing the stdout/stderr protocol with the classical
  path via `findROIViaScript`. The generator tab shows a "KI-Erkennung
  (ONNX)" checkbox next to "Region automatisch finden", enabled only when
  `CheckAIRoiAvailable()` says so. Still no bundled/recommended ONNX model
  in the repository or the binary — that stays deliberate (see "Two
  engines, two jobs" above) — but tooling now exists to produce one
  yourself:

  - `generator/bootstrap_yolo_dataset.py` turns the CSRT tracking the
    project already has into a YOLO-format training set — no manual
    annotation needed for the bootstrap pass. `--class-name` tags each
    clip with a content category (e.g. `blowjob`, `tf_tj_mix`); repeated
    calls into the same `--output-dir` accumulate across clips AND
    categories (`classes.json` remembers name→ID, `data.yaml` is
    rewritten). GUI: tab **KI-Trainingssystem** (see `docs/KI_TRAINING.md`).
  - Train via GUI (**Abhängigkeiten installieren** embeds
    `requirements-ai-train.txt` even in release builds) or CLI:

    ```bash
    python generator/train_yolo_model.py \
      --dataset-dir ~/.config/SamNPlayer/roi_training_dataset \
      --output ~/.config/SamNPlayer/models/roi_detector.onnx \
      --epochs 100 --device auto
    ```

    (Older docs mentioned hand-running `yolo detect train … yolo11n.pt` —
    the supported path is `train_yolo_model.py` with base `yolov8n.pt`.)
  - `generator/export_yolo_onnx.py` exports a trained checkpoint (any
    Ultralytics YOLO version — the script only calls `YOLO(pt_path)`, it
    is not tied to a specific release) to ONNX and rewrites the graph so
    coordinates are normalized 0..1, matching `ai_roi.py`'s contract
    exactly. This step is necessary: verified empirically that
    Ultralytics' own `export(nms=True)` output has the right `(N,6)`
    shape but pixel-space coordinates, not normalized ones.
  - Both scripts are available from the GUI/release binary via embedded
    `requirements-ai-train.txt` (Install-Knopf) as well as from a source
    checkout. `onnx`/`ultralytics` stay out of `requirements-ai.txt`
    (which remains just `onnxruntime` to *run* a finished model).
  - **Required convention:** train and export at `imgsz=640` — `ai_roi.py`
    hardcodes `input_size=640` in `_run_model()` and does not expose it
    via `find_roi()`.
  - CPU-only training works for small smoke tests but is impractical for
    a real dataset; the intended flow is bootstrap/export on any machine,
    then `yolo detect train data=data.yaml model=yolo11n.pt epochs=... \
    imgsz=640 device=cuda` on a machine with a GPU (e.g. the user's own
    Windows 11 PC). Manual annotation refinement (correcting frames where
    CSRT lost or misjudged the region — see the module docstring for why
    that matters more than raw frame count) works with any standard
    YOLO-format tool (LabelImg, CVAT, makesense.ai).
  - No pretrained/bundled weights are provided or planned — training
    remains something each user runs against their own material, matching
    this file's "no model in the repository" principle. FunGen's own
    trained weights were deliberately not considered as a shortcut here:
    FunGen 1 is PolyForm Strict-licensed (personal/noncommercial only, no
    redistribution) and FunGen 2 is closed-source, so their weights
    aren't ours to reuse — only the freely available open-source
    Ultralytics tooling they also build on was used.

### 2. Propose a profile (standard/weich/autotune) — **implemented, local learning added**

Decided in favor of the Colibri direction, and only as a fallback behind
the existing classical tool — not a replacement for it:

- `generator/motion_signature.py` already had `extract()`, `save_labelled()`,
  `load_labelled()`, and `find_similar()`, but nothing called them
  (`HANDOFF.md` listed this as "not yet connected"). `--label-scene NAME`
  and `--suggest-profile` in `generate_funscript.py` now wire them up.
- `--suggest-profile` tries the classical, deterministic tool **first**:
  `find_similar()` against saved examples. Below its distance threshold
  (0.15) that alone decides the suggestion — `ai_profile.py` is not even
  imported, let alone called.
- Only when no saved example is close enough does it ask a local Colibri
  server (`ai_profile.suggest_profile`) to judge the same eight signature
  numbers plus the nearest saved examples, and return a profile guess with
  a one-sentence reason — or `"unsure"` rather than force a guess.
- `generator/profilemodel` now adds a deliberately small **pure-Go learned
  classifier** on top of those same eight values. `Remember scene + style`
  stores the user-confirmed Style as `parameters.profile`; the AI training tab
  reports usable samples and writes `motion_profile_model.json` atomically.
  The model uses per-profile centroids plus observed spread, rejects distant
  scenes and near-ties, and therefore has an inspectable reason for every
  accepted class instead of forcing a guess.
- The local Go model is tried first. If it has no safe answer (or has not been
  trained), the existing nearest-scene and optional Colibri paths remain as
  fallbacks. This gives us useful learning before a large corpus exists without
  making a large language model responsible for frame-level tracking.
- Neither path touches `--profile` automatically — both print a suggestion
  only, matching `docs/NEXT.md`'s "do not switch profiles automatically
  without validation".
- Colibri's `/v1/chat/completions` is called with the stdlib
  `urllib.request` — no new pip dependency. `ai_profile.available()`
  returns `False` (not an exception) when no server is reachable at
  `--ai-base-url` (default `http://127.0.0.1:8080`), which is the expected
  state until someone runs `coli serve` locally.
- Tests: `generator/ai_profile_test.py` (prompt building and response
  parsing, no network) and `generator/profile_suggestion_test.py` (the CLI
  wiring end to end via a real subprocess call, proving the classical path
  wins when it has a confident match and that the AI path is skipped
  entirely in that case).
- **GUI wiring done:** Create stores scene + selected Style, profile suggestions
  still require **Apply**, and AI training has status/refresh/train controls plus
  a direct navigation button back to Create. **Still open:** field data is needed
  to tune rejection thresholds and compare the learned model to Colibri.

Large local models such as Qwen can still be connected through an
OpenAI-compatible local server for explanations, metadata normalization or an
additional opinion. They are **not** the Everyday stroke writer.

**Owner 26 Sep — second path:** an **opt-in local AI draft** that *does* write
candidate stroke actions is now allowed as an experimental lane
(`docs/AI_SCRIPT_WRITER.md`). Rules: off by default; local model only; Quality
Doctor + explicit Keep; no cloud LLM inventing per-frame positions from chat
tokens. CSRT Create remains the product default until Owner gates a later stage.

### 3. Quality judgment — **implemented**

Extends, does not replace, `quality_model.py` — and does not try to beat
its cross-validation gate, because it isn't a trained classifier and isn't
competing with one:

- `--ai-quality-opinion` runs `quality_doctor.evaluate()` exactly as
  before, then — only if a Colibri server answers at `--ai-base-url` —
  asks it to read the SAME `passed`/`score`/`metrics`/`warnings` output
  and reply with one verdict from the project's own existing vocabulary
  (`REPORT_VERDICTS` in `generate_funscript.py`: `brauchbar` /
  `grenzwertig` / `unbrauchbar`) plus one sentence of reasoning.
- The opinion is printed and, with `--report`, stored as
  `quality.ai_opinion` next to the Doctor's own numbers — it never writes
  `quality["passed"]` or `quality["score"]`, and it does not call
  `--feedback` or `quality_model.train()` on its own. Turning an opinion
  into real training data stays a human decision, same as today.
- `generator/colibri_client.py` is a small shared HTTP client
  (`/v1/chat/completions`, stdlib `urllib`) that both `ai_profile.py` and
  `ai_quality.py` now use, instead of each opening its own connection.
- Tests: `generator/colibri_client_test.py` (the shared transport),
  `generator/ai_quality_test.py` (prompt building and response parsing,
  no network), and `generator/ai_quality_cli_test.py` (the CLI wiring
  through the real pipeline via subprocess, proving `quality.passed`/
  `quality.score` stay identical with and without the flag when no server
  answers).
- **GUI display done:** a "KI-Zweitmeinung zur Qualität einholen" checkbox
  feeds `aiQualityOpinion` into `GenerateOptions`; the result shows as
  `aiOpinionVerdict`/`aiOpinionReason` in the generation result. **Still
  open:** — same caveat as step 2 — no field data yet on how useful it
  actually is without a running Colibri server to test against.

All three steps from the original plan (region, profile, quality) are now
implemented AND wired into the GUI end-to-end (region proposal, profile
suggestion, quality second opinion). **Product rule (unchanged):** Everyday
stroke writer stays **Go CSRT tip**; every AI/local-model path is
**suggest → Apply** (profile, semantic target, MT-Seed). No silent ROI2 /
curve rewrite from AI.

What remains for the adapter as a whole: nobody has yet trained a real region
model with the new bootstrap/export tooling (step 1) on a real, multi-clip,
manually-refined dataset — the tooling was only proven against a tiny CPU smoke
run — and there is still no field data on step 2/3 usefulness from a real
Colibri server. No bundled ONNX model is planned regardless (see step 1 above):
training stays something each user runs locally against their own material.
SceneMap P5 learning collect is opt-in local JSON only (not YOLO train write).

### 4. Depth / pose supporting signals — experimental

Monocular depth and pose are **not** a fourth output path. They may only
soft-rank existing proposals (today: optional bonus inside
`ai_roi.select_best_box` / `select_two_best_boxes` when
`SAMNPLAYER_DEPTH_RANK=1` or `use_depth_rank=True`).

- `generator/support_signals.py`: classical relative-depth **proxy** (numpy/
  OpenCV only — not metric depth), optional ONNX hooks (`load_onnx_session`,
  `propose_pose_boxes` stub), and `depth_roi_confidence` for ranking hints.
- No bundled model, no auto-download, no telemetry. Missing onnxruntime or
  model files → empty lists / classical-only; the CSRT/flow pipeline is
  unchanged.
- CLI probe: `python3 generator/support_signals.py --video clip.mp4 --frame 0`
  prints JSON (`--check` for availability flags only). Operator notes:
  `docs/DEPTH_POSE.md`.
- **Not default:** golden-clip bake-off required before any depth/pose signal
  influences shipped defaults (`docs/KI_TRAINING.md`, `docs/ROADMAP.md` item 4
  after v0.5.6). Still proposes only — never writes a `.funscript` alone.

## Not part of this change

- No action detector — unchanged principle from `docs/TEAM_STAND.md`.
- No requirement to install AI — both engines remain optional additions,
  like the Intiface path for the device (`device/intiface.go`): a second
  option, not a replacement for the existing one.
- No cloud connection, no gateway — both engines run exclusively on
  `127.0.0.1`.
