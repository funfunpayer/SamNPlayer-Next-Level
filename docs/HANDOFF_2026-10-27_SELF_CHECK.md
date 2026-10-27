# Handoff 27 Oct 2026: the self-checking engine

Everything the session of 26–27 Oct knew about the self-checking engine:
where it lives, what is built, what was measured, how to reproduce it, and
how to go on. The design with all numbers is
[`P3_SELF_CHECK_ENGINE.md`](P3_SELF_CHECK_ENGINE.md); this file is the map.

## 1. Where things are

| What | Where |
|---|---|
| Design, mathematics, every measurement | `docs/P3_SELF_CHECK_ENGINE.md` (§2–§3.5 results, §4 lanes, §5 checkers c1–c9, §5a AI blocks, §5b–§5c research, §7 slices) |
| Sample gate (SC-1), window verifier (SC-3) | `engine/include/samn/engine/self_check.h`, `engine/src/self_check.cpp` |
| Checker signals recorded while tracking (SC-2) | `TrackOptions::record_checks` → `TrackResult::checks` (`track_box.h`, `track_box.cpp`, class `CheckRecorder`) |
| Original look for the memory | `TrackOptions::seed_gray` / `seed_box` |
| Adaptive scene-cut test (off) | `TrackOptions::adaptive_cuts`, class `AdaptiveSceneCut` (`engine/src/track_parts.h`, `scene_cut.cpp`) |
| Verified repair (SC-5) | `engine/include/samn/engine/repair.h`, `engine/src/repair.cpp` |
| Camera trust (RANSAC inlier share) | `CameraMotionY(..., double* inlier_fraction)` (`track_parts.h`, `camera_motion.cpp`) |
| Tests | `tests/engine/self_check_test.cpp`, `track_checks_test.cpp`, `repair_test.cpp`, the adaptive-cut test in `track_box_test.cpp` |
| Command-line tool | `engine/tools/samn_selfcheck.cpp` (target `samn_selfcheck`, §3 below) |
| Model routing for agents | `.claude/` + `CLAUDE-ROUTING.md` (#134) |
| Synthetic scenes, bench scripts (Python) | private `funfunpayer/SamNPlayer-TestMedia`, branch `claude/next-project-prep-o1ckxh`, folder `synthetic/motion-bench-2026-10/` (**not merged into its main**; the Owner may merge it or leave it on the branch) |
| Owner clip | private test media, `clips/owner-av1-256x144/` (main): video, 0.7 script with 0.7's tip path in `metadata.trajectory.tip`, `manifest.json` (box 52/33/47/45 from 305 s), README |

Nothing of this is wired into the Generator yet: every new behaviour is off by
default or a separate call, and 0.7 parity is unchanged.

## 2. What was measured (short)

| Question | Answer | Where |
|---|---|---|
| Is the document's method (MOG2 + largest blob) a writer? | No: 0.92 with one mover and a still camera, 0.02–0.52 with a second body, camera, zoom or flicker; the auto chain 0.87–0.99 | design §2 |
| Do the checkers find bad windows on synthetic scenes? | Yes: real C++ chain 13 of 13, 0 false alarms, pauses "still" | §3.3 |
| Do they on the Owner clip? | Not yet: "fail" windows agree with 0.7 about as well as "pass" ones | §3.3, §3.4 |
| 0.7's cut rule on the Owner clip | 1992 cuts in 570 s; the adaptive test: 2. But without the false cuts the tracker drifts (15 → 144 px from 0.7's box), because each false cut re-anchored it through the appearance memory | §3.4 |
| Verified repair on the Owner clip | strict (look ≥ 0.75): 0 of 67 repaired; lenient (0.55): 30 repaired but worse against 0.7 (\|r\| 0.72 → 0.53). The look against the start crop cannot verify identity at 256×144 | §3.5 |
| Speed | repair ≈ 40 min for 570 s (Find tip + up to 3 retries per failed stretch) | §3.5 |

Lessons worth keeping (also pinned by tests):
- MOG2 learns a regular stroke into its background within a few cycles: never
  use it as "does the scene move"; scene motion comes from flow.
- The seed template does not follow a zoom: appearance (c7) is a trigger, not
  a verdict on its own.
- A quiet written axis with motion on the other axis is a wrong axis, not a
  pause. The automatic axis choice uses the raw range (`Ptp`) and one jump can
  flip it.
- Own references (median over the clip's own windows) fail when the track is
  wrong from the start: a box on still background looks "normal" against
  itself. Compare against the scene (flow), not against the box's own history.
- A false scene cut can be doing useful work (re-anchoring); remove it only
  when something else takes over that job.

## 3. How to reproduce

Local build (Linux, no Qt needed):

```bash
sudo apt-get update && sudo apt-get install -y libopencv-dev libopencv-contrib-dev
cmake -S . -B build -G Ninja -DSAMN_BUILD_APP=OFF -DSAMN_BUILD_PYTHON=OFF
cmake --build build --target engine_tests samn_selfcheck
build/tests/engine_tests          # 86 engine tests at the time of writing
```

The Owner clip is AV1; OpenCV's FFmpeg on Linux has no software AV1 decoder,
so transcode first (the Windows package has dav1d since #131):

```bash
ffmpeg -c:v libdav1d -i clips/owner-av1-256x144/clip.mp4 -an -c:v libx264 -crf 12 clip_h264.mp4
build/engine/samn_selfcheck clip_h264.mp4 52 33 47 45 --start-ms 305000 --out out/      # verdicts
build/engine/samn_selfcheck clip_h264.mp4 52 33 47 45 --start-ms 305000 --repair --out out/   # + repair (~40 min)
```

`out/track.csv`, `windows.csv`, `repair.csv` are numbers only. Compare them with
0.7's script and tip path from `clip.funscript` (the Python snippets used are in
the test-media branch, `synthetic/motion-bench-2026-10/owner-clip/`).

Synthetic scenes: `python3 -I synth.py clips` in the bench folder renders the
ten scenes with ground truth; `samn_selfcheck` runs on them the same way (Find
tip box from 0.7's `auto_roi.find_roi`).

## 4. How to go on (in this order)

1. **Reference clips (Owner).** 4–5 clips with a script the Owner trusts
   (hand-made or carefully corrected), in the private test media like the Owner
   clip. Without them no threshold can be calibrated on real material (D-135).
   Until then: do not tune c1–c9 on the one Owner clip.
2. **Calibrate the verifier** on those clips: per window, agreement of the
   first run with the reference vs. the verdict; pick thresholds that separate,
   pinned from both sides with tests.
3. **Identity check that works on real clips** (the repair's blocker). Measure,
   in this order: continuity with the last verified window (start within one box
   of the good track, same stroke frequency); the rhythm-grid cell (GD-15); a
   detector class (YOLO-family / RF-DETR via ONNX, design §5a). Keep the repair
   strict until one of them separates right from wrong retries.
4. **Then retire the false cuts**: with a working re-anchor, turn
   `adaptive_cuts` on behind the golden gate.
5. **Speed:** cache the flow per clip (as FunAutoScripter does), shorter
   retries, Find tip only once per failed stretch; target well under the run
   time of the first track.
6. **Robust axis:** the principal motion axis (PCA of the box displacement per
   window, design §5b) instead of `Ptp` x/y; measured against x/y on the
   reference clips.
7. **Wire into the Generator** behind a switch: `GateSamples` in front of the
   P3.2 chain, verdicts into the quality report, gaps shown in the Editor.
8. **Fast lane (SC-4)** and the AI blocks of §5a, each through the same gate.
9. Planned outside the self-check: vibration texture for strokers
   (`P3_GENERATOR.md` "Editor create tools"), multi-axis from measured motion
   (design §5c).

## 5. Rules that bit this session

- No model identifiers in code, docs or commit messages (D-120, scanned by
  `tests/check_identifiers.cmake`); commit trailers use plain
  `Co-Authored-By: Claude`. Four early commits of #133 carried a versioned name
  in the trailer; they are squashed into `main` now.
- MSVC: `cv::Rect::contains({x, y})` does not compile; write `cv::Point(x, y)`.
- Never force-push in this repo; an old branch with closed history
  (`claude/next-project-prep-o1ckxh`, PR #2) stays as it is, new work goes on a
  new `claude/*` branch.
- Agent routing: `.claude/agents/` lanes load only when the session starts in
  this repo; otherwise use an inline model choice for the same lanes.

## 6. Research behind the design (Owner links, 26–27 Oct)

Summaries and what we take from each are in design §5b–§5c:
MotionScript Generator document (background subtraction), Motion Tracking
Funscript Generator workflow, MakeVibrations / MakeVibrationsExt, FunAutoScripter
(MIT, `pingasAtumas/FunAutoScripter`), a beat-bar generator (MIT,
`Rantark/beat-bar-video-creator-with-funscript`), the OFS multi-axis tutorial,
the Slibowitz hand-scripting tutorial, GPAC (LGPL; candidate for checking MP4
timestamps only, not a decoder), claude-router (MIT, in `.claude/`).
