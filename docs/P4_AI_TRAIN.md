# P4 — AI Train and Benchmark as a plugin mode (D-155)

Owner, 19 Oct: AI training was in the released 0.7, so it comes back in full,
as **its own plugin with its own mode**. It is in every build now and is
released to users later. First it is ported exactly as 0.7 had it; after that
come the improvements that were planned for it. This is the plan; the build
starts with step 1; the Owner's answers are in §5.

## 1. What 0.7 shipped (release commit `73bf0b6`)

0.7 had about 4 000 lines for this: `roi_training.js` (1 881), `app_roi_training.go`
(691), `generator/roi_training.go` (483), `generator/roi_still.go` (455),
`benchmark.js` (628), plus the Python helpers `bootstrap_yolo_dataset.py`,
`train_yolo_model.py`, `export_yolo_onnx.py` and `contact_detector.py` with
their tests. The full checklist is `docs/PARITY.md` §6 (16 items).

**AI Train** (Tools → AI Train), four steps:

1. **Collect.** Open a video or a still image, go to a time, draw up to 12
   named boxes (face, mouth, breasts, nipples, hand_1, hand_2, penis, glans,
   vagina or a custom tag; a tag may repeat). *Use for training* tracks every
   box through the clip with CSRT and writes YOLO samples (every N-th frame,
   box scale, optional audio as WAV). Progress, log and Cancel.
2. **Review.** A grid of the new samples with their boxes; *Correct box* opens
   a review editor (zoom 0.2–8×, pan, select, relabel, redraw, add, delete,
   discard). The tracker can be wrong on real footage, so review is part of
   the flow, not an extra.
3. **Dataset.** Class counts per split and "ready to train".
4. **Train.** Epochs (100), device (Auto, CUDA, DirectML, Apple MPS, CPU), start
   and cancel; the result is `roi_detector.onnx` plus `classes.json` in the AI
   model folder, which the Generator's tip finding then uses.

Also there: the **learned generation profile** (a small model trained from
remembered scenes with a style, which suggests a profile at Create), the setup
check and one-click install of the Python packages, and writing the
requirement files for a manual install.

**Benchmark** (Tools → Bench): cut a clip (in / out, width, no audio), compare a
candidate script against a reference (verdict GUT / REVIEW / …, save the label
for AI), run the golden-clip manifest (success, Quality Doctor pass, average
score and correlation per clip) and keep the history of runs.

## 2. How it fits SamNPlayer Next

| Part | Where | Why |
|---|---|---|
| The mode | a fourth tab **AI Train** after Training, loaded as a plugin (`plugins/aitrain`), on only when *Settings → General → AI Train (preview)* is on | the Owner's "own mode, as a plugin"; in every build, hidden until it is released |
| Collect, review, dataset, the profile model | C++ (`vision/`, OpenCV CSRT is already there) and QML | no Python needed to gather and check data |
| Training and ONNX export | Python, as in 0.7 (`vision/training/*.py`), started as a separate process with progress lines and Cancel | PLAN rule 1: Python only where it is clearly better; YOLO training lives in Python |
| Running the model | ONNX Runtime in C++ (already planned for tip finding, ENG-05) | fast, no Python at play time |
| Benchmark | a second page in the same mode | it is the gate every engine change must pass (ROADMAP "gate") |

**Licence.** The trainer of 0.7 is `ultralytics` (AGPL-3.0). SamNPlayer Next
does not bundle it: the user's Python installs it, as in 0.7, and the app talks
to it as a separate program. A model trained with it carries its own licence
and is listed like code (LICENSING rule 4). If a later step needs AGPL code
inside the app, the project moves to AGPL-3.0-or-later in the same PR (D-143).

**Data.** Datasets, models and benchmark recordings stay on the user's
computer (D-027, D-028); nothing is uploaded, nothing is downloaded without a
click. Test media for our own tests stay in `SamNPlayer-TestMedia` (D-150).

## 3. Steps (one PR each, tests with synthetic clips only)

1. **Plugin frame** (done). `Plugins` (a QML singleton) lists the plugin modes
   that are on; *Settings → General → Plugins → AI Train (preview)*
   (`plugins.aitrain.enabled`, default off) adds the fourth tab; the page
   (`AiTrainPage.qml`) has the AI Train and Benchmark pages with the 0.7
   texts and the steps still to come.
2. **Collect.** Video / still, 12 named boxes, tag list and custom tags, track
   with CSRT, write YOLO samples and the audio; progress and Cancel.
3. **Review.** The grid and the review editor; *Discard*, *Correct box*.
4. **Dataset summary** and the *learned generation profile* (train, status,
   suggestion at Create).
5. **Python setup and training.** Find Python, check and install the packages
   (with the CSRT restore of 0.7, issues #94/#95/#119), list devices, train,
   export ONNX, Cancel; the Generator's tip finding loads the model.
6. **Benchmark.** Clip cutter, script compare with labels, golden-clip run and
   history.

Each step ticks its lines in `docs/PARITY.md` §6.

## 4. Planned improvements (after the port)

- **A teacher helps.** A stronger model (local, e.g. via LM Studio or a large
  ONNX detector) proposes boxes on the sampled frames; the user only confirms
  or corrects. Fewer clicks per sample, and the small model learns from the
  big one (ROADMAP ENG-05, "teachers JSON", hybrid verify K = 1.5).
- **Learning from corrections.** Every *Correct box* in Review and every edit
  after Create can become a training sample (opt-in, D-031).
- **Benchmark as the gate.** A model or engine change only becomes the default
  when the golden clips get better and none gets worse (ROADMAP "Owner gates").
- **Active learning.** The review grid shows the frames the model is least sure
  about first.
- **Personal motion profiles** (CAP-08): opt-in, with rollback.

## 5. The Owner's answers (19 Oct)

1. **The switch:** off by default; *Settings → General → AI Train (preview)*
   shows the fourth tab.
2. **Benchmark:** in the same mode, as its second page.
3. **The teacher:** a local model, or a model server in the user's own
   network, for example a Mac that runs larger models (LM Studio or another
   OpenAI-compatible server). The server is used only when the user enters
   its address (D-031); nothing goes to the internet.
