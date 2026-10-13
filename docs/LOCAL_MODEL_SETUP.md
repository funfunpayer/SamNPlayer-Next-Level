# Local models in SamNPlayer — Owner setup

How to set up **in-app local helpers** (not a Cursor Cloud agent model). No
bundled GGUF/ONNX weights and no auto-download. Everyday Create stays CSRT;
every AI path is suggest → Apply (or Keep for drafts).

Details by subsystem: `docs/AI_ADAPTER.md`, `docs/KI_TRAINING.md`,
`docs/AI_SCRIPT_WRITER.md`, `docs/COLIBRI_SETUP.md`.

## What exists today

| Role | Runtime | Writes Funscript? | Owner can set up? |
|------|---------|-------------------|-------------------|
| Region / tip proposal | YOLO → **ONNX** (`onnxruntime`) | No — box → Apply | **Yes** — train or place `.onnx` |
| Profile suggestion | Go `motion_profile_model.json` (+ optional Colibri) | No — Suggest → Apply | **Yes** — Remember scenes → Train |
| Quality second opinion | Local LLM server (**Colibri** / OpenAI-compatible) | No — prose only | **Yes** if `coli serve` runs |
| AIWrite draft | Imitation library (JSON); ONNX writer later | Draft → QD → Keep | **Yes** without ONNX after Export classical |
| Virtual Person chat | Planned local OpenAI-compatible | No | Not yet (Host H1 only; no Chat GUI) |

## A) ONNX region model (main “local model” expectation)

1. **AI training** → **Install AI train deps** (`ultralytics` / `onnx`; inference needs `onnxruntime` from `requirements-ai.txt`).
2. Pick a video → draw boxes → **Use for training** → discard bad samples.
3. **Start training** → writes roughly:
   - Windows: `%LOCALAPPDATA%\SamNPlayer\models\roi_detector.onnx`
   - Linux/macOS: `~/.config/SamNPlayer/models/roi_detector.onnx`
   (+ `classes.json`).
4. **Settings → AI region detection** — use **Open AI training** to jump to the AI Train tab if needed; empty path = default; or set your `.onnx` → **Check availability**.
5. **Create** → Smarter tip find → review → **Apply target**.

CLI:

```bash
python generator/train_yolo_model.py \
  --dataset-dir ~/.config/SamNPlayer/roi_training_dataset \
  --output ~/.config/SamNPlayer/models/roi_detector.onnx \
  --epochs 100 --device auto
```

## B) Go profile model

Create → remember scene + Style → **AI training** → **Train Go profile model** →
Create **Suggest profile** → **Apply**.  
File: `…/SamNPlayer/models/motion_profile_model.json`.

## C) Colibri (optional LLM beside the app)

See **`docs/COLIBRI_SETUP.md`**. Short path: `coli serve` → Settings AI server
URL (empty = `http://127.0.0.1:8080`) → **Test AI server** → Create Suggest /
quality opinion.

## D) AIWrite (imitation, no ONNX writer yet)

1. Good Create → Advanced → **Export classical run** (library under
   `…/roi_training_dataset/ai_script_imitation`).
2. **AI draft script** → review gauge → **Keep draft** / Discard.

ONNX vision draft path is not implemented (`docs/AI_SCRIPT_WRITER.md`).

## E) Virtual Person

Settings → Host Enable / Give dildo — **no Chat UI** yet; Echo backend.
Real local LLM chat is a later milestone.

## F) Contact points / VLM teachers (opt-in)

Local teachers (NudeNet and/or `vlm_probe`) → JSON → rhythm grid search
hint. Everyday Create stays bit-identical when off/empty. Details:
`docs/VLM_MODELS.md`, `docs/VLM_TEACHER_PLAN.md`, handbook FAQ.

1. Create → Advanced → turn on **Rhythm-robust signal**.
2. Check teacher boxes (NudeNet / Ollama / LM Studio) → **Generate contact
   points** (writes `<clip>.contact.json` and fills the path), **or** build
   via CLI `python generator/contact_points.py …` then **Choose…**.
3. Check **Use contact points**. Create as usual.
4. Optional review: **Import candidates…** into the scene map, then **Accept**
   (`reviewed:true` for P5c) / **Reject** pending `author:auto` marks.

## G) AI setup check / install (one command)

Settings → **Local AI setup**:
- **Check AI setup** — GPU, packages, training venv, Ollama / LM Studio /
  Colibri; shows next steps (changes nothing).
- **Install teachers / train / models** — matching `ai_setup.py` profiles.
  NudeNet installs without touching CSRT OpenCV; training uses a separate
  venv.

CLI still works: `python3 generator/ai_setup.py check` and
`install teachers|train|models --yes`.

Details and the hardware notes are in `docs/VLM_MODELS.md`.

## Intentionally missing

| Expectation | Reality |
|-------------|---------|
| Release ships a ready `.onnx` | No — train locally or bring your own |
| “ChatGPT inside the app” | Colibri URL + VP stub only |
| AIWrite = FunGen vision curve | Imitation stretch today; ONNX writer later |
| Pose/Depth Everyday defaults | Stubs / Owner bake-off (`docs/DEPTH_POSE.md`) |

**One-liner:** local helpers are largely wired (ONNX ROI, Go profile, optional
Colibri, AIWrite imitation). Not built: bundled language/vision weights and
AIWrite ONNX inference. Everyday remains CSRT.
