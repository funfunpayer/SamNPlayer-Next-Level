# Local teachers and models — what we can use, how to plug it in

Companion to `docs/VLM_TEACHER_PLAN.md`. Everything here runs **locally**:
- No frame leaves the machine.
- No model ships in the repo.
- Nothing downloads by itself.

The Owner installs what they want to try. Researched 27 Sep 2026; check
licences again at download time.

## How a teacher reaches the engine

```
video ─► teacher(s) ─► contact_points.py ─► <clip>.contact.json
                                              │
                    ┌─────────────────────────┴─────────────────────────┐
                    │ CLI: generate --rhythm-grid --contact-points …    │
                    │ GUI: Create → Advanced → Rhythm-robust →          │
                    │      Generate contact points (or Choose JSON) →  │
                    │      Use contact points                          │
                    └─────────────────────────┬─────────────────────────┘
                                              │
        rhythm grid searches around the contact point only where the CSRT
        box is > 3 cells away (its own search radius) — elsewhere unchanged
```

Build the JSON via **Generate contact points** in Create → Advanced (teacher
checkboxes) or CLI; then enable **Use contact points** (handbook FAQ).

- `contact_points.py --nudenet --vlm a.vlm.json --vlm b.vlm.json` asks
  several teachers per video (the Owner's idea).
- Every point carries `agree`: how many distinct teachers put a point within
  1.5 cells and 1.5 s of it.
- `--contact-min-agree 2` makes the engine use consensus points only.
- The same consensus points are the training-data candidates for our own
  detector (reviewed first, SceneMap § 6 decision 3).

## Teachers

| Teacher | What it gives | Runtime / install | Licence | Status here |
|---|---|---|---|---|
| **NudeNet** 320n | Boxes per frame: exposed/covered breasts, genitalia, buttocks, face (male/female) … (18 classes). Fast (320×320). | `pip install nudenet --no-deps onnxruntime`. Model bundled in the package. | Package MIT. Weights are YOLOv8-trained: re-check before we *bundle* anything; a user install is fine. | **Measured.** Penis found in only 7–37 % of keyframes (often occluded), but precise. The penis → cleavage rule hits 64 % / 80 % / 44 %, correct 18/18, 8/8, 12/16. As contact points (gated): multi-person r 0.304 → **0.406**; goldens unchanged (`clip_ausschnitt` bit-identical, `clip_voll` same r). |
| **Qwen2.5-VL 7B** | Boxes via prompt, pixel coordinates. | Ollama `qwen2.5vl:7b`, LM Studio. About 6 GB at Q4. | Apache-2.0 (3B: research-only). | `vlm_probe.py`. Waiting for the Owner's GPU run. |
| **Qwen3-VL 4B / 8B** | Boxes via prompt, **0–1000** coordinates. Also points. | Ollama `qwen3-vl:4b`, `qwen3-vl:8b`. | Check at download. | Probe calibration detects 0–1000 automatically. |
| Qwen2.5-VL abliterated | Same model with refusals removed. Box quality on explicit frames unknown. | Ollama `huihui_ai/qwen2.5-vl-abliterated`. | Derivative of Qwen2.5-VL. | Try if the plain model refuses (the V0 refusal-rate gate). |
| **InternVL3.5 8B** | Strong grounding (RefCOCO ~90 %). Boxes as `<box>[[…]]</box>`, 0–1000. | LM Studio / lmdeploy / vLLM (OpenAI-compatible). | Check at download. | Needs a `<box>` parser in the probe before use. |
| **Moondream 3** | Purpose-built `detect` (boxes) / `point`. 2B active parameters. | Moondream Station (own local API, not OpenAI chat). | Check at download. | Needs a small probe backend for its `/detect` API. |
| Florence-2 / OWLv2 / Grounding DINO | Open-vocabulary boxes from text (MIT / Apache). | transformers / ONNX. | MIT / Apache-2.0. | Probably weak on explicit content. Useful for non-explicit classes (hands, thighs, faces). |
| Engine itself | CSRT box + rhythm-grid cell. | built in | — | Already a teacher: the M5 auto-label agreement rule. |

Every new teacher gets the same treatment:
1. Run it on the keyframes that have labels.
2. Score it with `vlm_score.py`, against `testdata/golden_clips/*/vlm_oracle.json`
   and `testdata/vlm_labels/multi_person_642s.json`.
3. Run it end to end through `contact_points.py` and Generate.
4. Only then does it join the consensus.

## Our own model (student)

- **RF-DETR** (Roboflow) or **D-FINE**. Code *and* pretrained weights are
  Apache-2.0.
  - Fine-tunes on COCO/YOLO-format data.
  - Exports a single ONNX file (`model.export(format="onnx")`) for ONNX
    Runtime, like `ai_roi.py`.
- `ultralytics` (today's `train_yolo_model.py`) is AGPL-3.0, including trained
  weights by Ultralytics' reading. It is fine for local experiments, but not
  what we ship. This answers VLM plan § 6.1.
- Data: consensus points and boxes from the teachers above, reviewed by the
  user, plus the SceneMap marks the user draws, through the P5c export.

Gate (SceneMap L2):
- Box quality on held-out reviewed frames.
- End-to-end r on ≥ 4–5 clips.

## The training loop, as built (V3)

1. **Teachers → points:**
   `contact_points.py --video V --nudenet [--vlm a.vlm.json …] [--onnx contact_detector.onnx]`
   writes `V.contact.json`.
2. **Points → candidates:**
   `SamNPlayer-cli import-contact-candidates V.samn --contact V.contact.json [--min-agree 2]`
   - Consensus points become `contact` region marks with `author:"auto"`,
     `reviewed:false`, at most one per 2 s.
   - A re-import replaces unconfirmed candidates and keeps confirmed ones.
   - Region marks have no effect on Generate.
3. **Candidates → confirmed:** the user confirms (`reviewed:true`) or deletes
   them in the scene map, and may draw their own `contact` region marks,
   which count as confirmed.
4. **Confirmed → export:** SceneMap learning export (P5c, opt-in) writes only
   confirmed marks to `<learning>/<clip>/reviewed_yolo/`.
5. **Export → dataset:**
   `contact_detector.py dataset --learning-dir <learning> --out ds [--classes contact]`
   - Merges clips and remaps the per-clip class ids.
   - Splits by clip once there are 3 or more clips, so validation never sees
     training clips.
6. **Dataset → model:**
   `contact_detector.py train --dataset ds --out contact_detector.onnx --size nano --device cuda`
   - Needs `pip install "rfdetr[train]"`.
   - Writes `contact_detector.onnx` plus the model card `contact_detector.json`.
7. **Model → teacher:** `contact_points.py --onnx contact_detector.onnx`.
   - Runs through onnxruntime on CPU or GPU with no PyTorch.
   - Uses the same decode as RF-DETR's own ONNX helper.
   - Once it wins on the gate below, it can be the only teacher: fast, local,
     no big models needed.

## Runtimes and several teachers in one command

- `vlm_probe.py --backend ollama|lmstudio|colibri|vllm` sets the URL and
  timeout presets; `--base-url` overrides them.
- `contact_points.py --teacher backend:model` (repeatable) asks each VLM
  directly and caches the result per video. Example:

  ```bash
  python3 generator/contact_points.py --video clip.mp4 --nudenet \
      --teacher ollama:qwen2.5vl:7b --teacher ollama:qwen3-vl:8b \
      --teacher colibri:glm-5.3-flash \
      --teacher-exemplar clip.labels.json --teacher-follow
  ```

  - A teacher whose server is not running is skipped.
  - The consensus uses whatever answered.
- **Colibri** runs very large mixture-of-experts models on consumer
  hardware. It keeps the dense part in RAM and streams the experts from
  NVMe. Two of its families see images: GLM-5.3-Flash (321B) and DeepSeek
  V4.1 Flash (552B).
  - They are slow per image, but a few dozen keyframes per clip is fine
    (probe timeout 30 min).
  - The upstream README documents port 8000. SamNPlayer's Settings prose
    helpers assume 8080, so start `coli serve` on the port you configure.
  - The upstream README lists no GGUF support; it uses its own int4/int8
    checkpoints.

## Setup check and install (Owner PC)

```bash
python3 generator/ai_setup.py check                  # what is there, what is missing, next steps
python3 generator/ai_setup.py install teachers --yes # onnxruntime(-gpu) + NudeNet (--no-deps: keeps CSRT OpenCV)
python3 generator/ai_setup.py install train --yes    # separate venv: PyTorch CUDA + rfdetr[train]
python3 generator/ai_setup.py install models --yes   # ollama pull of the vision models that fit the GPU
```

- Nothing runs without `--yes`.
- Training lives in its own venv (`%LOCALAPPDATA%\SamNPlayer\ai-train-venv`
  or `~/.config/SamNPlayer/ai-train-venv`). RF-DETR pulls `opencv-python`,
  which would break the app's CSRT OpenCV.
- The same functions are exposed to the GUI: `generator.CheckAISetup`,
  `InstallAISetup(profile)` and `GenerateContactPoints(video, teachers)`.
  The buttons are Cursor's.

## Hardware

What the current NVIDIA 16 GB card already covers:
- NudeNet.
- Our own RF-DETR training (Nano / Small, batch 4–8).
- Qwen2.5-VL-7B and Qwen3-VL-8B at Q4–Q8.

It is enough to start the whole loop.

If you buy:

| Goal | What matters | Suggestion |
|---|---|---|
| Bigger VLM teachers (Qwen2.5-VL-32B), faster training | GPU memory | NVIDIA with **24–32 GB**: RTX 5090 (32 GB), RTX 4090 (24 GB), or a used RTX 3090 (24 GB) as the budget route. Stay on NVIDIA: CUDA is what PyTorch, RF-DETR and onnxruntime-gpu support first. |
| Colibri's giant MoE models | RAM and disk speed, not the GPU | 64–128 GB RAM plus a fast PCIe 4/5 NVMe with 1–2 TB free for model files |
| All of it on one box | both | 24–32 GB NVIDIA, 64–128 GB RAM, 2–4 TB NVMe, a good PSU and cooling for long training runs |

- A Mac with 64–128 GB unified memory runs large VLMs well. Training with
  RF-DETR/PyTorch is weaker there, so it is not the main box.
- Prices and stock change quickly; check at purchase time.

## Ideas — towards one or two systems that run everywhere

1. **Our own RF-DETR detector as the everyday teacher.**
   - ONNX runs through onnxruntime on CPU, CUDA, or DirectML (AMD/Intel
     under Windows).
   - A small model (Nano has about 30 M parameters) and fast enough for
    every frame on a GPU.
   - No big model needed at run time. The big VLMs stay optional
     "teachers of the teacher".
2. **Pick frames where teachers disagree for review (active learning).**
   - Frames where NudeNet, the VLMs and our detector disagree teach the
     model the most per confirmation.
   - Cheap to add to `import-contact-candidates` as a second candidate kind.
3. **Retrain from the GUI:** "Retrain contact detector" runs `dataset` and
   `train` in the train venv, then swaps the model when validation improves.
4. **Contact points automatically in Create.** When the own model exists,
   Create can generate points itself before Generate (one checkbox), with
   no separate step.
5. **Per-frame detector in Go later** (onnxruntime C API) if the Python hop
   becomes a bottleneck. The engine hook (`ContactPoints`) already takes the
   points.

## Adding a model to the probe

1. It has to speak OpenAI `/v1/chat/completions` with `image_url` parts.
   Ollama, LM Studio, vLLM and lmdeploy all do.
2. Run `vlm_probe.py --calibrate --model <id>`. The calibration shows its
   coordinate convention: pixels, 0–1000 or 0–1.
3. Run `vlm_probe.py --video … --model <id> [--exemplar-json … --follow]`.
4. Run `vlm_score.py <clip>.vlm.json <labels>`.
5. Feed it to `contact_points.py --vlm <clip>.vlm.json` together with the
   other teachers.
