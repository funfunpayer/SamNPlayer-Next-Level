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

## 2. The better way: teachers, a label editor, our own student

The old repository already went further than the 0.7 page (`docs/VLM_TEACHER_PLAN.md`,
`docs/VLM_MODELS.md`, `contact_points.py`, `contact_detector.py`): **teachers**
propose the boxes, several of them vote, the user only confirms or corrects, and
**our own small model (the student)** learns from the confirmed boxes. Measured
there with NudeNet alone, fully automatic, through the real tracker: on the
multi-person clip the correlation went from 0.304 to **0.406**; the golden clips
stayed the same. The Owner (19 Oct): build this in, and make marking better than
0.7's twelve still boxes, "editor-like": scrub through the clip, watch the boxes
move, and drag them back where they drift.

### 2.1 Teachers

| Teacher | Runs | Delivery (Owner, 19 Oct) |
|---|---|---|
| **NudeNet 320n** (18 body-part classes, fast) | ONNX Runtime in C++, CPU or GPU | one click in Settings (its weights were trained with YOLOv8: licence unclear, so not bundled) |
| **Vision-language models** (Qwen2.5-VL 7B, Qwen3-VL, InternVL …) | an OpenAI-compatible server: Ollama, LM Studio, or a machine in the user's network such as a Mac with larger models | the user installs and starts the server and enters its address (D-031); nothing goes to the internet |
| **Our student** (below), once it wins the benchmark | ONNX Runtime in C++ | trained here |
| The engine itself (CSRT box, rhythm grid) | built in | built in |

Every point carries **agree**: how many teachers put a box within 1.5 cells and
1.5 s of it. Only consensus boxes are offered as proposals; a wrong teacher can
not pull a working track away (the old gate: the engine uses a point only where
the tracker is more than 3 cells from it).

### 2.2 The label editor (replaces the 12 still boxes)

The centre is the Player (the shared stage and transport, D-154); a label is a
**track through the clip**, not a box on one picture.

- **Classes and objects.** You mark a class (hand, breast, nipple, mouth …)
  and add as many objects of it as the scene has: *hand 1*, *hand 2*, a third
  hand of a second person. The number is given by the app; the model learns
  the class (Owner, 19 Oct: "hand 1 and hand 2 make no sense … more logical
  what one marks and how"). 0.7's fixed `hand_1` / `hand_2` and plural
  `breasts` / `nipples` are gone.
- **Lanes like the timeline.** One lane per object (glans, mouth, hand 1 …) under
  the video: where the label is present, where it is confirmed, where it is
  absent, and the keyframes as dots. Colour shows the teachers' agreement.
- **Scrub and drag.** Play or scrub; the boxes ride along on the video. Where a
  box drifts, drag it back: that frame becomes a confirmed keyframe, and the
  tracker re-runs from it to the next keyframe (forward and backward, the two
  results blended), so one correction fixes the whole stretch.
- **Not in the picture (`X`)** marks where an object is gone until it shows
  again; those frames become negative samples, not wrong boxes.
- **There but covered (`H`)** marks where an object is hidden by a hand, a body
  or the edge (Owner, 19 Oct: "manchmal sieht man nicht alles auf einem
  Bild"); those frames are left out of training, so the model never learns
  "nothing here" where something is. Partly visible: mark the part you see.
- **Contact point.** A class `contact` marks where the stroke touches: the
  point the contact vibration follows (0.8 teacher plan).
- **Not this (exclusion zones).** A class `exclude` marks what must not be
  tracked: a thigh that looks like the tip (0.7's "thigh wins" error), a
  foreign hand, a second person (Owner, 19 Oct: "markieren, was nicht
  getrackt wird"). Red and hatched in the editor, as many as the scene needs,
  and they move and vanish like any other object. Exported as a class of its
  own, so the model learns "this is not it"; the Generator keeps its tip
  search out of these boxes (`ExcludeZonesAt`, replaces 0.7's black
  "+ Ignore region").
- **Next uncertain.** A button and a key jump to the next frame where the
  teachers disagree or the tracker is unsure (active learning): the user spends
  time where it matters.
- **Keys.** Space play, `,` `.` one frame, `1`–`9` pick the label, drag to draw,
  `Delete` removes the keyframe, `X` absent, `N` next uncertain, Ctrl+Z undo.
- **Images too.** A still image is a clip of one frame.
- **Export.** Every N-th frame of the confirmed stretches (and the absent ones as
  negatives) becomes a sample, split by clip so validation never sees a training
  clip; the sound can be kept.

### 2.3 Our student

- **RF-DETR** (Apache-2.0, code and pretrained weights), fine-tuned on the
  confirmed samples, exported to one ONNX file. It runs in C++ with ONNX
  Runtime, so playing and creating need no Python.
- Training needs Python and PyTorch: a separate environment that Settings sets up
  with one click (as 0.7's `ai_setup.py install train`; it never touches the
  app's own OpenCV). Devices: CUDA, DirectML, Apple MPS, CPU.
- 0.7's `ultralytics` path (AGPL-3.0, its weights too) is not used.
- When the student beats the teachers on the benchmark, it becomes the fast
  teacher and the Generator's tip finding uses it.

### 2.4 What is in the package

Bundled (licence-clean): ONNX Runtime (MIT) and the RF-DETR base weights
(Apache-2.0). One click in Settings: NudeNet, the training environment. The
user's own servers: the vision-language models. Datasets and models stay on the
user's computer (D-027, D-028).

### 2.5 Where it lives

| Part | Where |
|---|---|
| The mode | the fourth tab *AI Train* (plugin), shown when *Settings → General → Plugins → AI Train (preview)* is on |
| Label tracks, keyframes, interpolation, export | C++ core (with tests), QML editor |
| Tracking between keyframes | `vision/` (OpenCV CSRT, already there) |
| Teachers | C++: ONNX Runtime for NudeNet and the student, Qt Network for the vision servers |
| Training | Python, a separate process with progress and Cancel |
| Benchmark | the second page of the mode |

### 2.6 Phases (D-157)

A clip has a story, not only objects. A **phase lane** sits above the object
lanes; its stretches are dragged and named like chapters:

| Phase | What it is | What the device does |
|---|---|---|
| `intro` | titles, talk, undressing before the action | holds at 0; the Training's video stage skips it |
| `empty` | black, a title card, nobody in the picture | holds at 0; skipped like the intro |
| `oral` | blowjob | the Oral feel: a steady seal, a pulse per stroke |
| `hand` | handjob | the Hand feel: the grip follows speed |
| `breast` | titjob | the Breast feel: softer, follows depth |
| `intercourse` | penetration | the Intercourse feel: follows the rhythm |
| `climax` | the peak, the cumshot | builds up to the peak; in Training the moment to pause or cool down |
| `cool-down` | after the peak | fades out softly |

- **Rules first.** Without any model the app proposes phases: dark or nearly
  still pictures (empty), the start before the first steady rhythm (intro),
  the stroke tempo and loudness rising to a peak and falling after it (climax,
  cool-down), the scene type from the contact class (mouth → oral).
- **Then learned.** Confirmed phases teach a small scene model of our own
  (a classifier over frames and sound); teacher votes may join (§2.1).
- **Used everywhere.** The Editor's scenes and their feel (Scene Map, D-145),
  the Generator's *Optimize for*, and the Training's video stage. Suggestions
  only: nothing changes a script or a session until the user confirms it.

## 3. Steps (one PR each, tests with synthetic clips only)

1. **Plugin frame** (done): the `Plugins` singleton, the Settings switch, the
   fourth tab and the page with its two parts.
2. **Label editor core** (done): label tracks in the core (`label_track.h`:
   classes with several objects each, box and absent keys, interpolation,
   confirmed keys and reviewed stretches, `<clip>.labels.json`, sample export
   with negatives, YOLO lines), `LabelController` (undo, autosave, export of
   images / labels / classes.txt) and `LabelEditor.qml` (classes and their
   objects, drawing and moving boxes on the frame, scrubbing and play, lanes,
   keys `1`–`9`, `,` `.`, arrows, `X`, `Delete`, Ctrl+Z).
2d. **Phases** (D-157, §2.6): the phase lane in the editor and in
   `<clip>.labels.json`, export as `phases.csv` for the scene model (done:
   pick a phase, drag over the lane, the badge on the frame); the rule-based
   suggestions (done: `phase_rules.h` over brightness and change per 500 ms
   from `vision/picture_signals.h` and the loudness, *Suggest phases* in the
   editor; they only fill stretches nobody set and stay pale until
   confirmed); the Editor's scenes (done: the feel phases suggest a scene's
   feel, *Scenes from phases* adds a scene where each confirmed phase
   begins); the device shaping per phase at playback (done, Owner 20 Oct:
   "beim Abspielen": the recipe's `phase_ranges` in the `.samn`, applied
   after the mapping in `PlaybackFrames`: rest over intro and empty with a
   1 s edge, the climax lifts towards full, the cool-down fades to 0; the
   script stays as it is; Editor *Phases shape playback* / *Play without
   phases*); the Training's video stage (done: the film's confirmed phases
   from its `.labels.json`; intro and empty are skipped, at the climax the
   session pauses by default, or cools down, or carries on, Owner 20 Oct:
   "einstellbar, Standard Pause").
3. **Tracking between keyframes** (done): *Track between keys* (`T`) runs
   CSRT forward from one confirmed box and backward from the next
   (`engine/label_tracking.h`, frames at most 640 px, at most 1500 frames per
   stretch, else "add a key in between"); `BlendTracks` blends the two runs
   (the forward one weighs more near its start) into unconfirmed keys every
   200 ms, marked uncertain where they disagree (IoU under 0.5) or one lost
   the object; orange keys in the lane, *Next uncertain* (`N`) jumps there.
   One undo step; confirmed keys are never overwritten. Next: re-run the
   stretch on its own after a drag.
4. **Teachers:** ONNX Runtime in the build, NudeNet (one-click download),
   vision servers (Ollama, LM Studio, any OpenAI-compatible address, e.g. a
   Mac), consensus into proposed tracks.
5. **Student:** the training environment (one click), RF-DETR training and ONNX
   export with progress and Cancel; the student as a teacher and in the
   Generator's tip finding.
6. **Benchmark:** clip cutter, script compare with labels, golden-clip run and
   history; the gate for every model and engine change.
7. **The smaller learners from 0.7:** the learned generation profile and the
   quality model (logistic regression over the run's numbers, kept only when it
   beats the rules in cross-validation).

Each step ticks its lines in `docs/PARITY.md` §6.

## 4. Later improvements

- **Learning from corrections** everywhere: a corrected box in the Generator
  or an edit after Create can become a sample (opt-in, D-031).
- **Pose** (MediaPipe / RTMPose, 0.8 research) as one more teacher, once it
  passes the benchmark.
- **Personal motion profiles** (CAP-08): opt-in, with rollback.

## 5. The Owner's answers (19 Oct)

1. **The switch:** off by default; *Settings → General → AI Train (preview)*
   shows the fourth tab.
2. **Benchmark:** in the same mode, as its second page.
3. **The teacher:** a local model, or a model server in the user's own
   network, for example a Mac that runs larger models (LM Studio or another
   OpenAI-compatible server). The server is used only when the user enters
   its address (D-031); nothing goes to the internet.
4. **The models:** bundle what is licence-clean, the rest with one click.
5. **Marking:** keep marking by hand as a second source, but make it better
   than twelve still boxes: a label editor where you scrub through the clip,
   see the boxes and drag them where they drift (§2.2).
