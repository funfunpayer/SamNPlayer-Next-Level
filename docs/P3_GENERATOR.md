# P3: the Generator (with its Editor) in C++

**Owner (16 Oct 2026):** the Generator must do everything 0.7's does, with the Editor built in, and the detection must get **better**. C++ has the stronger libraries for that, so detection moves there.

The full list of 0.7's Generator functions is in [`PARITY.md`](PARITY.md) §4 (63 items). This file is the plan to get there and beyond. Decisions: D-133 to D-137 and D-139 to D-141 in [`DECISIONS.md`](DECISIONS.md). Lessons: [`LESSONS.md`](LESSONS.md) §4 and §11.

## What 0.7 has (the reference)

| Part | 0.7 code | Size | What it does |
|---|---|---|---|
| Pipeline driver | `generator/generator.go`, `native.go` | 2.4 k Go | Runs a Create: Go-native path (CSRT via cgo) or the Python script; progress, cancel, queue, overwrite guard, auto O-zone. |
| Tracking | `generator/trackcv/*` (`cv.cpp` wrapper, `track.go`, `track_two.go`, `track_multi.go`, `appearance_memory.go`, `dispguard.go`, `multi_recover.go`, `rhythm_grid.go`, `tip_distance.go`, `scene_map_scan.go`) | 2 k Go + 0.2 k C++ | CSRT on the tip box, scene-cut detection, camera compensation, appearance memory (re-find after loss), two-point / multi-point tip→partner distance, displacement guard, rhythm grid. |
| Fallback tracker | `generator/simpletrack/*` | Go | NCC template tracking without OpenCV. |
| Python backends | `generate_funscript.py` (3 k), `flow_backend.py`, `grid_lk_backend.py`, `region_fusion_backend.py`, `fusion.py`, `motion_signature.py`, `support_signals.py` | 21 k Python in total | Optical flow (no ROI), grid of LK points, 2×2 region fusion, multi-source fusion, signals. |
| Post-processing | `generator/posttrack/*` | Go | Filters, Savitzky-Golay, normalisation (incl. moving average), peaks, keyframes, conversion to actions, preview. |
| Quality | `quality_doctor.py`, `ai_quality.py`, `tracking_gaps.go`, `stroke_preview_phase.go`, `strokepreview/*` | | Rule-based quality score, gaps, stroke preview hints. |
| Contact | `contact_*.go/.py`, `distance_partners.go`, `tf_tj_meta.py` | | Contact candidates, points, marks, review; partner distance. |
| Scene map | `scene_map*.go`, `scene_proposals.go`, `scene_roles.py` | | Rhythm windows per cell, proposals, roles. |
| Audio | `audiocheck.go`, `audio_check.py`, `audio_segments.go`, `audio_vibration.go`, `beatbar*.go` | | Audio tempo, segments, Speech-Hold, vibration from sound, beat bar. |
| AI | `ai_roi.py`, `auto_roi.py`, `bodyparts*`, `contact_detector.py`, `pose_observer.py`, `vlm_*.py`, `train_yolo_model.py`, `export_yolo_onnx.py`, `aiscript/*`, `profilemodel/*` | | YOLO body parts, auto tip, pose, VLM scoring, training, AI script drafts, profile model. |
| UI | `generator.js`, `gen_editor.js`, `gen_feel.js`, `gen_video.js`, `postgen.js`, `roi_help.js`, `create_layout.js` | | Steps 1–5, marking canvas, feel, run and progress, review in the built-in editor. |

## Where each part goes (best language per part, D-102)

| Part | SamNPlayer 2 | Why |
|---|---|---|
| Video frames for analysis | C++ core, OpenCV `VideoCapture` (FFmpeg backend) | Frame-exact seek and fast decode in one process, no ffmpeg subprocess per frame. |
| Tracking (CSRT, scene cuts, camera compensation, appearance memory, 2/N points, guards, rhythm grid) | C++ core `samn/track/*` on OpenCV `tracking` + `video` | 0.7 already called the same C++ library through cgo; now the logic is C++ too, with no cgo boundary per frame. |
| Backends (flow, grid LK, region fusion, fusion) | C++ core on OpenCV (`calcOpticalFlowPyrLK`, DIS / Farneback flow) | They were Python only because 0.7's core was Go. In C++ they can run **together** per frame (see Better detection). Faster is a hypothesis, not a fact: 0.7's profile put ~99 % of the time in OpenCV/FFmpeg (D-070) and ours puts 93 % in CSRT's update (`docs/benchmarks/P3_PROFILE.md`); decode, tracking, checkers, post-processing and the whole run are measured separately before any speed is claimed, and a faster isolated part is not a faster product (0.7: parallel tf/tj trackers 1.68x in isolation, no end-to-end gain). |
| Post-processing, quality, contact, scene map, audio | C++ core | Pure maths; ported with their Go/Python tests (parity, as P1). |
| AI inference (YOLO body parts, auto tip, contact detector) | C++ core via OpenCV `dnn` on ONNX models | No Python runtime for users; same models (0.7 already exports ONNX: `export_yolo_onnx.py`). |
| AI training, VLM experiments, dataset tools | **Python stays** (`tools/ai/`), calling the core through `samn_native` | Training libraries are Python; users never need it. |
| Generator UI with Editor | QML + C++ controllers, sharing the Player's `CurveItem`, lanes, heatmap, marks | One Editor for the Player's Edit view and the Generator's Review step (0.7 `gen_editor.js` borrowed the Player's tools the same way). |

## Better detection (beyond 0.7)

Each item is measured on the golden clips before it becomes a default (D-135). The Owner provides the clips; they never enter the repo. A change that does not beat 0.7 on the benchmark stays off. **Revised 17 Oct after `LESSONS.md` (D-139):** the first plan put "run the backends together and fuse them" first; 0.7 measured exactly that and rejected it as an idea, not because of its stack (REJ-05, REJ-19, GD-01). The list below keeps only what the lessons allow.

**One box writes the stroke (GD-01).** The tip box tracked by CSRT is the only writer of the curve. Other sources (flow, LK grid, zones, detectors) may *check* or *re-anchor* that box; they never write or blend into the curve. A segment whose range collapses is flagged, not filled with invented amplitude.

1. **Hybrid check instead of fusion (GD-16, REJ-05).** Flow and an LK grid run beside CSRT on the same frame (cheap in C++) as a *disagreement detector*: when they disagree with the box, the frame is marked low-confidence and the re-acquisition below may run. Fusion stayed between its sources (clean 1.000/0.926 → 0.985, occluded 0.293/0.747 → 0.620); a disagreement signal is all it gave.
2. **AI-assisted re-acquisition, fail closed (GD-10, GD-16, REJ-20).** When CSRT and appearance memory lose the tip, the ONNX body-part detector proposes boxes near the last position; the engine verifies a proposal (appearance against the *original* seed, displacement guard) before it becomes the search anchor. No verified box → the gap stays a gap ("lost" is not "wrong", GD-19). Periodic template re-anchoring was measured worse (10 jumps up to 924 px) and is not repeated. Acceptance (0.7 PR #248): a confirmed target is never taken over by a neighbour region that moves more (in-phase, anti-phase and quarter-phase neighbours), a scene cut re-anchors only through a proposal, and a late result never lands on another video (the run id / video check that Find tip and the runner already have).
3. **Auto tip and contact from AI as proposals (GD-17, GD-18).** Faster in C++, still a proposal the user confirms with Apply; on by default only after a golden win. Licence of code and weights checked first, ONNX boxes compared with the original backend on identical frames.
4. **Sub-frame peaks.** Peaks refined between frames (parabolic fit) so strokes on 24/25/30 fps clips land closer to the turn points. Measured with the timing method of GD-05/GD-06 (windowed r, aliasing flag), never with a per-clip best offset.
5. **Kalman smoothing of the tip path** in place of fixed Savitzky-Golay windows on noisy segments; a before/after run on the same clip (GD-13), amplitude statistics compared, not only r (GD-24).
6. **GPU for the detector only.** OpenCV DNN with CUDA or OpenCL when present; there is no GPU CSRT in OpenCV (REJ-11), so tracking stays on the CPU. Same results, only faster.

### Where C++ changes the picture (retests allowed, through the gate)

Some 0.7 results were limited by the stack, not the idea (`LESSONS.md` §12). These may be re-measured in C++, each against 0.7 on the goldens:

| 0.7 result | Limit came from | What C++ may try |
|---|---|---|
| Two CSRT trackers in parallel for distance profiles: no end-to-end gain (REJ-13) | Python threading and hand-off | Two trackers on two threads with no interpreter; measure end to end, not a microbenchmark. |
| Frame sharpening lost before any shader (REJ-16) | extracting frames from a browser `<video>` | Native frames from the decoder; unsharp masking re-measured against reference agreement, not lost-frame rate (REJ-15, REJ-32). |
| Flow backend 1.9x faster, not 4x (REJ-18) | the Python/cv2 loop | Speed may improve; its quality finding (0.55 on a busy scene) stands, so flow stays a checker (item 1), not a writer. |
| CUDA unusable (REJ-11) | pip wheels without CUDA | A CUDA/OpenCL build for DNN; CSRT still CPU. |

### Do not repeat (measured in 0.7)

KCF / MOSSE instead of CSRT (99 % lost on a small tip box, REJ-12) · fusion or four-zone fusion as the writer (REJ-05, REJ-19) · flow as the default writer (REJ-18) · upscaling or unsharp masking as a fix for hard boxes without an agreement check (REJ-14, REJ-15) · camera estimate on a half-size picture (r 0.94 → 0.71, REJ-24) · periodic template re-anchoring, scale lock, slower learning (REJ-20, REJ-21) · automatic second region by grid pairs or an invented region (REJ-23) · audio as a position source (REJ-26) · a language model as the stroke writer (REJ-30) · the tracker's own lost rate or quality as proxy for agreement (REJ-32). A retry needs a new hypothesis for *why* it should differ, written in the PR.

## Own engine and C++ toolbox (Owner 17 Oct, D-140, D-141)

The detection engine is our own C++: algorithms are free to implement from their published descriptions; only other projects' code carries a licence. Libraries are building blocks, never the engine.

| Part | Built ourselves | Building blocks (licence) | Measured by |
|---|---|---|---|
| Video frames | `FrameReader` (PTS, exact seek) | OpenCV videoio (Apache-2.0); FFmpeg libav directly if the timestamps need it (LGPL build, shared) | frame-exact tests (P3.0) |
| Tracking | the CSRT box loop, scene cuts, camera compensation, appearance memory, guards, hybrid check (D-139), sub-frame peaks, Kalman | OpenCV core/imgproc/video/tracking (Apache-2.0) | Go parity (P3.1–P3.4), then P3.8 |
| Newer trackers | only as a challenger to CSRT, never a silent swap | OpenCV TrackerNano / TrackerVit (OpenCV 4.7+), ONNX trackers (check each licence) | P3.8 against CSRT on the goldens (REJ-12 is the warning) |
| Optical flow (checker only, D-139) | the disagreement signal | OpenCV DIS / Farneback; RAFT via ONNX (BSD) | P3.8 |
| AI inference | the gating, verification and fail-closed rules | ONNX Runtime (MIT; DirectML on any Windows GPU, CUDA, CPU), OpenCV dnn | same boxes as 0.7 on identical frames (GD-17), then P3.8 |
| Models | own training data and training (0.7 pipeline, GD-18) | models with a permissive licence only; licence of weights checked first | golden win before any default |
| Audio onsets, tempo, beats | spectral flux, tempogram, beat tracking with dynamic programming, segments (replaces 0.7 `audiocheck` / `audio_segments`) | pocketfft or KissFFT (BSD), libsamplerate (BSD), FFmpeg for decoding | 0.7 audio tests ported (P3.6), then P3.8 |
| Speech (Speech-Hold) | the hold logic | Silero VAD via ONNX (MIT), whisper.cpp (MIT) | labelled synthetic and Owner clips |

**The engine is a built-in plugin (D-141).** `engine/` (library `samn_engine`, P3.1 on) implements the detector plugin interface and is linked into every build; the Generator page calls it in-process on a worker thread. One interface for every detector:
- input: media path, time range, marks and options;
- output: progress, then results (positions, gaps, marks, beats, onsets) in video time;
- a cancel token and a run sequence id on every result; the run stops when the app quits; a failure is reported, never blocks the UI (GD-25). Because it runs in-process, the engine is held to the core's rules: RAII, no exceptions across the interface, covered by the sanitizer job.

External plugins may use the same interface out of process (JSON lines on stdin/stdout). `vision/` (frame reading, the tracker factory, the self-check) holds the generic building blocks; the tracking logic, parameters and models live in `engine/`. Everything is GPL-3.0-or-later, so GPL code (OpenFunscripter, ScriptPlayer, aubio, Essentia …) may be used anywhere (`LICENSING.md`). A plugin's result is a candidate like any other: it has to beat our engine and 0.7 at the P3.8 gate before the UI offers it by default. The 0.7 `pluginhost` (drop folder plus manifest, stage H1) is the starting point for discovery.

## Steps (each one a PR with tests; parity first, then improvements)

| Step | Content | Done when |
|---|---|---|
| P3.0 (started 17 Oct: `samn_vision`, FrameReader with PTS, one tracker factory, self-check, CI on both systems, sanitizers) | OpenCV in the build (Linux apt `libopencv-dev libopencv-contrib-dev`; Windows vcpkg `opencv4[contrib,dnn,ffmpeg]` with binary cache), `samn/video/FrameReader` returning `{frame, pts_us}` (GD-07), one `CreateTracker()` with a startup self-check and a CI test that CSRT runs on both systems, no silent fallback (GD-02); the sanitizer job covers the tracking tests (GD-23); RAII for every tracker and capture | CI green on both systems; frame-exact seek test and a start-offset test on a generated test-pattern clip |
| P3.1 (done 17 Oct: `engine/` `TrackBox`, bit-identical to 0.7 on 16 runs incl. 5 min, `docs/benchmarks/P3_1_TRACK_PARITY.md`; appearance memory, jump guard and coast came along because TrackROI uses them) | CSRT single-box track + scene cuts + camera compensation (port of `trackcv/track.go`) | Positions match 0.7's Go path on synthetic clips (generated in the test, moving test patterns, known ground truth, GD-22) within ±1 position; a 5-minute synthetic clip for drift (GD-11) |
| P3.2 (17 Oct: `engine/` `PositionsToActions`, `LimitSpeed`, filters, all 13 Python goldens equal; RDP in core `samn/simplify.h`; `signal_chain.h` with profile recipes and the default detrend (GD-12); O-zone suggestions in core `samn/ozone.h`; whole chain against 0.7 in `docs/benchmarks/P3_2_PIPELINE_PARITY.md`. Quality Doctor and the signal auto-retry follow with P3.6) | Post-processing (`posttrack/*`) → actions, auto O-zone; default detrend for stroke profiles (GD-12); stage order of 0.7 `FUNSCRIPT_ALGOS.md` (GD-13) | Go tests ported; whole-pipeline output equals 0.7 on the synthetic clips |
| P3.8 (17 Oct: `samn_compare` + `samn/engine/compare.h`, Motion Fidelity in core `samn/motion_fidelity.h`, SHA-256 manifest; first run `docs/benchmarks/P3_8_FIRST_RUN.md`; open: the 3+5-run gate, Quality as the second number with P3.6, the Owner's golden clips) | Comparison controller (the Owner's "Vergleich Controller"): runs 0.7 and SamNPlayer 2 on the same clips; absolute timeline, ±1000 ms lag window, boundary-pinned and low-confidence flags, windowed r, aliasing flag, same `n`, manifest with hashes, 3+5-run gate (GD-05, GD-06); quality and agreement as two separate numbers (GD-04) | A report per clip in `docs/benchmarks/` (numbers only, no media); the synthetic known-lag characterization test passes |
| P3.3 | Generator page: steps 1–5, marking canvas, run with progress from the first frame (GD-21), cancel token and run sequence id on every event, single-instance guard, children killed on quit, "canceled" is not an error (GD-25), queue; overwrite guard; Review step opens the result in the built-in Editor | Every §4.1–4.6 item that needs no AI works; a spy-stage test per option proves it reaches the output (GD-03) |
| P3.4 | Two-point / multi-point (GD-08), appearance memory and reacquire failing closed (GD-10), guards, rhythm grid opt-in (GD-15), tracking gaps (GD-19) | Go tests ported; equal on the synthetic two-point clips |
| P3.5 | Backends in C++ (flow, grid LK, region fusion) as checkers for the hybrid check (item 1), not as writers | Python tests ported as C++ tests; benchmark on golden clips |
| P3.6 | Quality Doctor, contact, scene map, audio check / segments / tempo; metrics as windowed fractions on the detrended signal (GD-14) | Tests ported; Player BPM "From audio" and Speech-Hold get their data |
| P3.7 | AI inference via ONNX (auto tip, body parts, contact detector) + verified re-acquisition (item 2) | Same detections as 0.7's Python on the sample frames, from C++ |
| P3.9 | Better detection items 3–6 and the C++ retests, each behind a switch until the benchmark shows it beats 0.7 | Benchmark |

The Generator and the Editor are one mode (Owner 16 Oct, D-137): the same page creates a script and edits it, and a script opened for editing lands there with editing on. The full Editor (dot editing, range tools, Undo, multi-axis, E1–E10) is P4 and is built into that page; P3.3 already edits the Generator's result there.

## Rules carried over

- Synthetic test clips are generated in the tests (moving test patterns). No real media in the repo.
- A generated script never overwrites an existing one without asking (0.7 overwrite guard).
- AI is a proposal: the user confirms (0.7 and D-129).
- Measure before choosing a default (0.7 lesson: "measured, not guessed").
- Read `LESSONS.md` §4 (GD-01..GD-25) and §11 before touching an area of the Generator; each step above names the lessons it must follow.

## Editor create tools (Owner, 18 Oct)

The Editor creates curves as well as editing them, and writes them into the
.samn (all axes) as well as the .funscript:

- **Beat-bar scripts** (Cock Hero / Fap Hero): detect the beat bar and turn
  beats into strokes (`samn/beatbar.h`, `samn/beat_strokes.h`, from 0.7).
- **Vibration and suction from the stroke** with the 0.7 feel algorithms
  (recipes, suction/vibration balance, contact vibration).
- **Vibration that follows the sound** (`samn/audio_envelope.h`, 0.7 "Write
  as vibration curve").

Each tool writes through the Editor's history (one undo step) and is checked
against 0.7 with the same inputs before it becomes a default.

