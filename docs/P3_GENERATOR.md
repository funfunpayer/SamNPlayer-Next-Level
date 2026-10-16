# P3: the Generator (with its Editor) in C++

**Owner (16 Oct 2026):** the Generator must do everything 0.7's does, with the Editor built in, and the detection must get **better**. C++ has the stronger libraries for that, so detection moves there.

The full list of 0.7's Generator functions is in [`PARITY.md`](PARITY.md) §4 (63 items). This file is the plan to get there and beyond. Decisions: D-133 to D-136 in [`DECISIONS.md`](DECISIONS.md).

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
| Backends (flow, grid LK, region fusion, fusion) | C++ core on OpenCV (`calcOpticalFlowPyrLK`, DIS / Farneback flow) | They were Python only because 0.7's core was Go. In C++ they run several times faster and can run **together** per frame (see Better detection). |
| Post-processing, quality, contact, scene map, audio | C++ core | Pure maths; ported with their Go/Python tests (parity, as P1). |
| AI inference (YOLO body parts, auto tip, contact detector) | C++ core via OpenCV `dnn` on ONNX models | No Python runtime for users; same models (0.7 already exports ONNX: `export_yolo_onnx.py`). |
| AI training, VLM experiments, dataset tools | **Python stays** (`tools/ai/`), calling the core through `samn_native` | Training libraries are Python; users never need it. |
| Generator UI with Editor | QML + C++ controllers, sharing the Player's `CurveItem`, lanes, heatmap, marks | One Editor for the Player's Edit view and the Generator's Review step (0.7 `gen_editor.js` borrowed the Player's tools the same way). |

## Better detection (beyond 0.7)

Each item is measured on the golden clips before it becomes a default. The Owner provides the clips; they never enter the repo. A change that does not beat 0.7 on the benchmark stays off.

1. **Run the backends together.** CSRT on the tip, a dense flow field in the box, and an LK point grid run on the same frame, followed by a confidence-weighted fusion. In 0.7 they were alternatives chosen beforehand. One failing source (blur, occlusion) no longer breaks the stroke.
2. **AI-assisted re-acquisition.** When CSRT and appearance memory lose the tip, the ONNX body-part detector proposes where it went, limited to plausible boxes near the last position. 0.7 could only coast or ask the user.
3. **Auto tip and contact from AI by default** (opt-in in 0.7, Python only). It is fast in C++ and always a proposal: the user confirms it, as in 0.7.
4. **Sub-frame timing.** Peaks are refined between frames (parabolic fit on the signal), so strokes on 24/25/30 fps clips land closer to the real turn points. 0.7 snapped to whole frames.
5. **Kalman smoothing of the tip path** in place of fixed Savitzky-Golay windows on noisy segments. It adapts to speed and is checked against the Quality Doctor score.
6. **GPU where available.** OpenCV DNN with CUDA or OpenCL when present, and the CPU otherwise. The results are the same, only faster.

## Steps (each one a PR with tests; parity first, then improvements)

| Step | Content | Done when |
|---|---|---|
| P3.0 | OpenCV in the build (Linux apt `libopencv-dev libopencv-contrib-dev`; Windows vcpkg `opencv4[contrib,dnn,ffmpeg]` with binary cache), `samn/video/FrameReader` | CI green on both systems; frame-exact seek test on a generated test-pattern clip |
| P3.1 | CSRT single-box track + scene cuts + camera compensation (port of `trackcv/track.go`) | Positions match 0.7's Go path on synthetic clips (generated in the test, moving test patterns) within ±1 position |
| P3.2 | Post-processing (`posttrack/*`) → actions, auto O-zone | Go tests ported; whole-pipeline output equals 0.7 on the synthetic clips |
| P3.3 | Generator page: steps 1–5, marking canvas, run with progress and cancel, queue; Review step opens the result in the built-in Editor | Every §4.1–4.6 item that needs no AI works |
| P3.4 | Two-point / multi-point, appearance memory, guards, rhythm grid, tracking gaps | Go tests ported; equal on the synthetic two-point clips |
| P3.5 | Backends in C++ (flow, grid LK, region fusion) + fusion (item 1) | Python tests ported as C++ tests; benchmark on golden clips |
| P3.6 | Quality Doctor, contact, scene map, audio check / segments / tempo | Tests ported; Player BPM "From audio" and Speech-Hold get their data |
| P3.7 | AI inference via ONNX (auto tip, body parts, contact detector) + re-acquisition (item 2) | Same detections as 0.7's Python on the sample frames, from C++ |
| P3.8 | Comparison controller (the Owner's "Vergleich Controller"): runs 0.7 and SamNPlayer 2 on the same clips and reports score, peaks, timing differences | A report per clip in `docs/benchmarks/` (numbers only, no media) |
| P3.9 | Better detection items 3–6, each behind a switch until the benchmark shows it beats 0.7 | Benchmark |

The Editor itself (dot editing, range tools, Undo, multi-axis, E1–E10) is P4. P3.3 already opens the Generator's result in it, so the Editor is built once and shared by the Player's Edit view and the Generator's Review step.

## Rules carried over

- Synthetic test clips are generated in the tests (moving test patterns). No real media in the repo.
- A generated script never overwrites an existing one without asking (0.7 overwrite guard).
- AI is a proposal: the user confirms (0.7 and D-129).
- Measure before choosing a default (0.7 lesson: "gemessen, nicht geraten").
