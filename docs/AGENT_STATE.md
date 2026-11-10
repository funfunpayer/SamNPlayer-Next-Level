# Agent state

**Read this first** (new session, new agent, after a context loss), then [`MODULES.md`](MODULES.md), [`PLAN.md`](PLAN.md) and [`DECISIONS.md`](DECISIONS.md). Update this file in **every PR**, finished or not. **Push often:** the repo is always the current state.

**Last update:** 10 Nov 2026, Cursor, branch `cursor/gen-improve-panel-635a`: Generator review-card Improve script (Start/End trim + Fill gaps + Heal tracking gaps). Quality first. Do not touch HealRhythm / RepairSpans / StampTrackingGaps / audio check / Speech-Hold / feel_heatbands / handbook / `.sam` / intensity-arc.

## Handover: Generator Improve panel (10 Nov)

PARITY_REMAINING item 10 (the panel pieces only). Review card on Generator Create: Start (s) / End (s) trim (0 = unbound), Fill gaps (default on), Heal tracking gaps (default on), button Improve script. `EditorController::improveScript` wraps `samn::ImproveScript` (trim → heal → fill); same save/reload as Optimize for. Status `Improved: N points (healed N · +N pts · trimmed) — open Play to edit dots/curve.` Left open: Align fill to audio tempo, audio check, Rhythm bridge, Repair this span.

- **App:** `EditorController::improveScript(startSec, endSec, fillGaps, healTrackingGaps)`; QML `improvePanel` on `GeneratorPage` after `qualityBox`. HelpChips: `generator.start-s` / `generator.end-s` / `gen-improve-fill` / `gen-improve-heal`.
- **Tests:** `editor_controller_test` `improveScriptTrimsFillsAndHeals`; `generator_qml_test` `improvePanelShowsAndRuns`.

## Handover: device diagnostics history (10 Nov)

PARITY_REMAINING item 3 (the history piece only). After each finished `RunDiagnostics` (including interrupted) the worker appends one JSONL line. Empty `device.diagnosticsHistoryPath` → `config_dir()/device_diagnostics_history.jsonl`. `DeviceController::getDiagnosticsHistory` returns newest-first; Settings → Device lists `date (Mock|device) — N commands, M errors[, interrupted]`. Name/kind are stamped from the session (the report JSON has none). No Choose… path field (override is the settings key, tested in C++). Left open: `generator.reportPath`, feel_heatbands.

- **Core:** `diagnostics.h` `DiagnosticsHistoryEntry`, `AppendDiagnosticsHistory` / `LoadDiagnosticsHistory`, `HistoryEntryFromReport`. Tests in `device_diagnostics_test.cpp`.
- **App:** `DeviceWorker::RunDiagnostics` appends; `getDiagnosticsHistory` / `diagnosticsHistoryPath` / `setDiagnosticsHistoryPath`. QML under the Device diagnostics card (`diagHistoryLine`). Tests: `diagnosticsOnMock`, `diagnosticsCanBeCancelled`, `diagnosticsHistoryPathDefaultAndOverride`, `settingsDeviceTabTestsTheMockDevice`.

## Handover: axis legend chips (10 Nov, #228)

- **GUI:** `PlayerTimeline` horizontal axis legend chips above the ruler; `LaneName` Left/Right/Home/End via `Keys.onPressed` (Qt 6 has no `onHomePressed`/`onEndPressed`). Chip `MouseArea` at `z:10`; Repeater captures `laneId`.
- **Tests:** `player_page_qml_test` delivers press/release to the chip `MouseArea` (window `QTest` clicks miss stacked feel-stage areas).
- **Docs:** README Player scenes/thumbs/legend/Alt+B; PARITY axis-legend tick.

## Handover: script analysis (10 Nov, merged #229)

- **Core:** `motion_classify.h` (0.7 `motionx.Classify`), `motion_chapters.h` (`Chapters`), `script_analysis.h` (`AnalyzeScript` / `summarizeStates`). An empty `ClassifyOptions` does not smooth; only `AnalyzeScriptActions` sets `SmoothRadius` 2. `ScriptChapters` classifies with smoothing off.
- **Player:** `scriptAnalysisSummary` after a script with at least 4 points, on the Playback card "Script check & analysis" (title gains "· summary ready"). Stored `metadata.chapters` replace the automatic labels (empty name → "chapter", end at or before the start → start+1). Nothing is written back.
- **Tests:** `tests/core/motion_classify_test.cpp`, `motion_chapters_test.cpp`, `script_analysis_test.cpp`, `tests/app/player_controller_test.cpp` `scriptAnalysisOnOpen`.



## Handover: `claude/samn2-label-vfr` (9 Nov, label tracking: VFR fix and long-GOP window)

Follow-up to #198 (label tracking memory), which was merged before its review finished. **#198 broke variable-frame-rate clips** (phone footage): the backward decode seeked by the nominal rate and required the exact frame time, so it failed with "the video changed between two reads" where the old code worked. This PR fixes that and the rest of the review. Below: the whole current design.

Owner priority: CPU/RAM efficient AI. `TrackBetweenKeys` (`engine/src/label_tracking.cpp`) kept every small frame of the stretch in a `std::vector<cv::Mat>` (about 0.7 MB per frame at 640 px wide, up to `max_frames` = 1500, so about 1 GB) because the backward run needs the frames in reverse.

- **Change:** the forward run streams (read, track, drop; frames nothing will track are only grabbed). The backward run decodes the stretch in chunks (24 frames, growing to at most 96 when seeks are measured to be costly, i.e. a long GOP) and decodes the next chunk on another thread into a second reused buffer; at most two windows are alive. The wanted frame is found by its time (seek, grab until the time, retry further back if the seek landed after it), so variable-frame-rate clips work. A lost track stops the backward run. New `FrameReader::Grab` / `Retrieve` (Read is both). `BetweenKeysOptions` got `window_frames`, `max_window_frames`, `stats`; `samn_engine` links Threads.
- **Result (synthetic, `docs/benchmarks/LABEL_MEMORY_2026-11-08.md`):** GOP 12 720p: RSS 758 to 137 MB (5.5x), CPU -2 %. Long GOP (H.264, GOP 250): RSS 769 to 243 MB (720p, 3.2x, CPU +5 %) and 802 to 281 MB (1080p, 2.9x, CPU +18 %). The acceptance of 5x and +10 % holds for the short GOP only; the long GOP is the second decode plus the seeks. Boxes bit-identical in every case.
- **Tools/tests:** `samn_label_mem` (`--make`, `--track`; by hand, `EXCLUDE_FROM_ALL`); engine tests against a copy of the old algorithm: bounded window, cut clip, cancel after k polls, box-vs-stretch check order, VFR and long-GOP clips (these use the ffmpeg program and say SKIPPED where it is missing; CI has it).
- **VFR:** fixed and tested: the engine test makes VFR clips with ffmpeg; on main's code (#198) it fails with "the video changed between two reads", with this change boxes equal the old algorithm's.
- **Not verified:** Windows/MSVC, real clips (phones: B-frames, GOP 30 to 60; only synthetic VFR and GOP made by ffmpeg were tried), the Qt `label_controller` (not built here; its call site is unchanged).
- **Ideas not done:** keep the forward run's last chunk instead of decoding it again; a `FrameReader` that reports the GOP.

## Handover: `claude/samn2-intensity-arc` (6-8 Nov, D-165 first slice: intro and climaxes)

Owner: find the intro and the climaxes ("Höhepunkte") of a video from its sound and samples of its picture; cinema-grade, clean, lean, measured before and after, little CPU and memory. This slice is core only: no decoder change, no FFT, no Generator change. PR #168, reviewed and refuted three times; this state answers the third review (empty stretches, scene cuts without sound, the loud-opening guard, test bounds, the documented limits, `steps()` overflow).

- **Core:** `intensity_arc.h` / `intensity_arc.cpp`, `ComputeArc(PhaseSignals, ArcOptions)` -> `IntensityArc` (`detail` 4 s and `arc` 20 s curves, `intro_end_ms`, `peaks` with `t_ms`, `start_ms`, `end_ms`, `prominence`, `confidence`; the primary first). Design in D-165. Each constant has its reason next to it, and the fitted ones say so.
- **The design in one line each:**
  - A climax is a rise **within the action** after the intro, never against the intro.
  - It is measured **per input against that input's own level**, so a climax in the sound alone counts; but the arc blends 0.6 / 0.4, so it needs a much larger rise than one in the picture.
  - Scene cuts and flashes are taken out of the motion by a running median over 5 steps (also at the clip's ends).
  - Empty steps (black, or still and silent) count in none of the levels, flanks or noise; `SuggestPhases` gives the arc only the span from the first to the last picture.
  - It stands over the rest of the action (`climax_gain`) and over the action beside it: 1.6x, `climax_gain` behind a sudden edge, 1.15x for a sudden last step held to the clip's end.
  - It takes less than half the action.
  - Second and third climaxes only in a clip that is calm outside its climaxes.
  - An intro must be calm (motion or sound at most half its level in the action).
  - A clip that opens at its top gets no climax suggestion there (conservative); without a clear rise from its left flank, the arc must fall halfway towards the action beside it, clear of the noise, before the clip's start.
- **`SuggestPhases`:** keeps its empty-stretch rule and takes intro and climaxes from the arc. Every climax is the longest free stretch of its peak (never cut by black, never merged past `climax_max_ms`). The cool-down follows the last climax when it ends calmer than the action before it. `min_intro_ms` scales below two minutes. `PhaseSignals::tempo` and `PhaseRuleOptions::active_share` are removed (no caller set them).
- **Measured** (`docs/benchmarks/INTENSITY_ARC_2026-11-06.md`):
  - 24 scenarios: climax hits 369 -> 560 of 600, false climaxes 54 -> 10, intro hits 567 -> 600.
  - 17 random-arc families (five new: peak beside a black end, loud opening, empty middle, empty start or end, scene cuts without sound), train and held-out seeds: no family worse than the old rules under the acceptance written in the bench. That acceptance is: climax hits at least theirs; intro hits at most 2 % fewer; false climaxes per clip at most theirs + 0.02, or + 0.10 where theirs exceed 0.25 (fast random walks, where neither is reliable).
  - The held-out seeds were looked at more than once in the third review (the doc says when and why), so they are no longer untouched data.
  - With strict 5 s grading three family rows fail (shown in the doc).
  - The thresholds are fitted on synthetic data only; the doc has a sensitivity grid. `kFlankGain` and `kLastStepGain` are knife edges.
  - CPU about 4x the old rules (110 us vs 28 us per call at -O2; 4.1-4.4 ms vs 0.9-1.3 ms on a 2 h clip, on a loaded machine).
- **Tests:** `tests/core/intensity_arc_test.cpp`. The cases that failed before each fix are listed in the PR history:
  - edge climax, plateau, short clips, close bursts, black cuts, merges;
  - calm intro + even action, sound-only climaxes, fade cool-down, staircase top step, boxed-in climax;
  - random walks against absolute bounds; confidence as properties (0.7x when sound and motion top out apart);
  - long empty stretches (middle, tail, lead-in, black or still), a peak beside a black stretch, scene cuts without sound (also on the first and last steps), a loud opening under +-35 % jitter.

  `phase_rules_test.cpp` unchanged. Bench: `tests/bench/phase_bench.cpp`, target `phase_bench`, not in ctest, exit 1 when the acceptance fails.
- **Limits:**
  - Synthetic only; real clips are the gate.
  - Worse than the old rules on the review's probes: staircases with jitter sd above about 0.2; random walks faster than the bench's (1.1-1.7 false climaxes per clip vs 0.8-0.9); quantised random walks (0.44 vs 0.27, made worse by the running median; three fixes tried and reverted); calm tails (0.010 vs 0); a lit, still, quiet but not silent lead-in over 60 % (0.53 vs 0).
  - The bench's own walks: 0.17 false climaxes per clip at step 0.02 (old rules 0.32-0.34).
  - A sound-only climax needs about 2x the rest of the action.
  - Slow build-ups of 100-500 s: 15 of 25.
  - Bursts 20 s apart fuse into one climax.
  - A last staircase step under 1.15x is no climax.
- **Left alone on purpose:** `LabelController::finishSuggest` divides the 500 ms loudness by the loudest step after `NormalizeLoudness` already scaled it. Not behaviour-neutral for the arc (rescaling the loudness changes the 0.6 / 0.4 blend; on cases A-E the peak times moved by up to 69 ms and the confidence by up to 0.07), so it stays until a slice measures it.
- **D-number:** D-165, because the open playhead PR #163 takes D-164. If #163 lands with another number, or another PR takes D-165 first, renumber this one.
- **Not verified:** real clips (synthetic only), Windows / MSVC, the Training stage with several confirmed climaxes on a real film.
- **Next slices (roadmap, one per PR):**
  1. one shared 11 kHz mono streaming decode plus an `AudioAnalyzer` (spectral-flux onsets);
  2. tempo from the onset curve (autocorrelation, a log-Gaussian prior at 72 BPM, octave folding, a confidence);
  3. the heart-rhythm default tempo, `base_rhythm.h` (`generator.base_bpm` 72, `generator.base_bpm_variation` 3 %, range 60-90), blended with the measured tempo; rhythmic fill stays opt-in until it beats linear fill on synthetic gaps;
  4. beat tracking (Ellis dynamic programming) as the AU-04 fallback for rounds without a visible beat bar;
  5. a speech / music / silence gate.
- **Cleanup list (found by the read-only analysis, not done here):** `AutoFillGapThreshold` always gives 800 (min of at least 2000 and `kDefaultFillGapMs` 800: a dead rule); `ImproveOptions::audio_hz` has no caller; three percentile helpers in core (`audio_envelope.cpp`, `script_quality.cpp`, and the one that moved from `phase_rules.cpp` to `intensity_arc.cpp`); three owners of an `AudioLevelDecoder` (label controller, editor controller, each `AudioLevelItem`), each decoding the same file again.

## Handover: `claude/samn2-kernel-device-service` (9 Nov, kernel / modules, slice 2: DeviceService)

- **Done:** `app/src/device_service.h` (pure virtual `DeviceService`, QObject, owns `statusChanged`). `DeviceController` derives from it (methods `override`; behaviour unchanged). `PlayerController`, `TrainingController`, `PlaybackOutput` hold `QPointer<DeviceService>`; `PlaybackOutput::setDevice` takes a `DeviceService*`; Player and Training have `setDeviceService` for C++ and keep `setDevice(DeviceController*)` and the `Q_PROPERTY(DeviceController* device)` for QML (their `device()` getter is `nullptr` when the service is no controller).
- **Tests:** `tests/app/fake_device_service.h` (the fake) and `tests/app/device_service_test.cpp` (new): levels in order, the hold follows playing, emergency stop, stroker positions, a device deleted mid-play, a lost connection, Training with an existing connection, with a mock it connects and closes again, device lost / swapped, Training emergency stop. The existing `playback_output_test` and `training_controller_test` are unchanged and keep the real controller on purpose: they assert the mock device's log lines and the real worker's behaviour.
- **Next slice:** modes registry loaders on a `QAbstractListModel` (loaders of other extra modes are recreated when one is added); then Scene Map as a registered module.

## Handover: `claude/samn2-camera-2d` (8 Nov, detection plan PR-2: 2-D camera compensation)

Numbers: `docs/benchmarks/DETECTION_CAMERA2D_2026-11-08.md` (+ `_before.csv` / `_after.csv`, all 19 scenes, the old engine run on this branch first).

- **Engine:** `EstimateCameraMotion` (`camera_motion.cpp`, `track_parts.h`) returns the full similarity and the inlier share; `TrackBox` takes its motion at the tip's previous centre (`m * p - p`), accumulates x and y per scene (reset at cuts, Savitzky-Golay 9/2 as before) and subtracts both; `frame_xs` (new) / `frame_ys` keep frame pixels for the tip path (`BuildTrajectory` uses them). Gate: inlier share under 0.5 adds nothing (`camera_frames_lost` counts those frames too). RANSAC distance 1 px instead of 3 (a moving foreground otherwise joins the background's fit and keeps the share high); `CameraMotionY` (tip finder) keeps 3 px. Axis choice on the compensated x and y. No reset at a re-anchor without a cut (same scene, same camera). No rescaling under zoom (the stroke keeps the frame's scale). Track cache version 7. Defaults unchanged (`camera_compensation` on where it was).
- **Measured:** track r `pan_x` 0.797 -> 0.999, `zoom` 0.948 -> 0.996, new `pan_x_fast` 0.663 -> 0.999 (script r 0.936 -> 0.988), new `pan_zoom` 0.800 -> 0.984; median r 0.9775 -> 0.9865. **Rule 1 fails on one row:** `light_cut_jump` track r 0.705 -> 0.689; the old value was lifted by the disc's own stroke leaking into the 3 px camera estimate (correction correlated 0.71 with it, now -0.04); the new track equals compensation off (0.689 vs 0.687); script r 0.661 -> 0.666. No-camera scenes: identical to 4 decimals except `distract_*`, `noise_jpeg` r min and `occl_4s` track r (at most 0.0005; a still camera's estimate is not exactly the identity). Cost: no measurable change (fair paired runs -0.5 % and -3.6 % ms/frame; RSS within 0.6 %).
- **Tests:** `tests/engine/camera_motion_test.cpp` (13 cases; 8 of 12 failed on the old behaviour). `WriteCameraScene` in `synthetic_scenes.h` (sub-pixel camera over a large world, cut, noise background, moving foreground); `samn_bench` scenes `pan_x_fast`, `pan_zoom`.
- **Found, not fixed:** the fixed scene-cut test fires from about 3 px per frame of camera speed on this texture (+-60 px at 0.5 Hz: 89 cuts in 150 frames), which resets the compensation and skips the tracker: PR-3. A foreground with more features than the background wins the estimate with a high share; no gate sees it.
- **Not verified:** real clips (Owner gate through `samn_compare`; the Owner clip uses X, and both axes are now compensated, so its axis choice may change), Windows / MSVC build, the self-check's c6 thresholds with the 1 px inlier share (engine tests pass).
- **PR-2 done. Next:** PR-3 forward-backward LK loss flag + confirmed scene cuts (drop30: 4 false cuts at 30 % dropped frames; fast pans; `light_cut_jump` stays lost for the whole second half, r 0.66), then PR-4 periodic bridge (occlusion 2 s / 4 s r 0.77 / 0.72), PR-5 sub-sample peaks, PR-6 640 px downscale.

## Handover: `claude/samn2-audio-onsets` (9 Nov, audio onsets, tempo, beats, heart rhythm; D-168; revised after review)

Owner: "Herzrhythmus als Standard" (a heartbeat-like default tempo for the fill), better audio analysis for the Generator, measure before and after, lean code.

- **Core, Qt-free, streaming:** `audio_onsets.h` (`OnsetCurve`: 512/256 frames at 11025 Hz, log flux over 24 log bands, fixed state under 16 KiB, digital silence skips the FFT; per-second gate blocks: silence / steady noise / speech-like / music from level, spectral flatness and quiet-frame share; `PickOnsets`, minimum gap rounded up), `audio_tempo.h` (`TempoAccumulator` / `EstimateTempo`: streaming autocorrelation, pulse = shortest period a comb over its multiples explains, tempo = duple (or handicapped triple) multiple of it, octave folding, log-Gaussian prior on 72 BPM, confidence; `Estimate` is const and thread-safe; `TrackBeats`: Ellis DP; `AssessTempo`: silence absolute at -80 dBFS or 35 dB under the clip's loud blocks).
- **Review fix (9 Nov):** off-beat hats made the first version lock to 2/5 of the tempo from ~106 BPM, 16th hats to 4/5 or 1/3 (not octaves). With the pulse comb: 228 harder synthetic clips (off-beat hats 0.1-0.6, bass-heavy, swing, 8th/16th hats, syncopation) 73.7 % -> 100 % octave-corrected (trivial autocorrelation 91.2 %, old Player estimate 40.4 %); basic 98 clips 98.0 -> 100 %, beat F 0.833 -> 0.837. Regression tests fail on the first version. The reviewer's bass-heavy 1/3 errors did not reproduce with my generator (tested anyway). Quiet masters (-40/-50 dB) now trusted (78.9/0 % -> 100 %).
- **Heart rhythm:** `base_rhythm.h`: `generator.base_bpm` (72, clamp 40-140), `generator.base_bpm_variation` (percent, 3, clamp 0-10), seeded grid with slow drift, knots every **40 beats**, so adjacent beat intervals differ by under 1 % over the whole clamped range (bound 3v / (40 (1-v)^2), 0.93 % at 10 %; tested at 40/72/140 BPM x 10 % over 60 seeds; with the earlier 10 s knots it reached 4.5 %). It is the prior centre of `EstimateTempo` (fast songs come out at half tempo, by design) and the last tempo source of the rhythmic fill. Other "base tempo" places (the Player's BPM grid, the signal chain's 3 s detrend default) were left alone: changing them would change default behaviour.
- **Rhythmic fill (off):** `generator.rhythmic_fill` (default false) makes `ImproveScript` use `FillGapsRhythmic(actions, max_gap, step_ms, audio_hz, base)`: strokes at the script's own tempo within 8 s of the hole, else the audio tempo, else the heart rhythm; a holding script gets the straight line at `step_ms` exactly as `FillGaps`. Read by `PlayerController::optimizeFor` only (checked with `g++ -fsyntax-only`, the app was not built). Fill bench: ties on the Script Doctor score, gap r 0.278 -> 0.889 where strokes go on, 0.488 -> 0.425 where the script holds: acceptance not met, so it stays off.
- **Measured** in `docs/benchmarks/AUDIO_ONSETS_2026-11-08.md` (synthetic only; tables before / first version / after). 10 h streamed: 23.5 KiB peak heap (ctest `audio_onset_memory`, bound 32 KiB). About 0.4-0.7 ms CPU per second of sound. Limits stated there: whole-clip tempo across a tempo change is the dominant tempo (1 of 8 clips gives a quarter; use windows), music with speech over it is trusted with my generator but not with the reviewer's.
- **Tests:** `tests/core/audio_onsets_test.cpp`, `base_rhythm_test.cpp` (empty / short / silent input, NaN and infinity, chunk-size invariance, determinism, 10 h fixed state, ratio-error regressions, minimum onset gap at 4-96 kHz, settings clamps, drift bound at the extremes, huge times, fill phase / hold / step). `core_tests` 520/520, ctest 3/3; the new tests and a 3000-case fuzz of the new code clean under ASan + UBSan.
- **Next slices:** (1) check `EstimateTempo` / `AssessTempo` on real music and speech before wiring (the thresholds and the pulse comb are tuned on synthetic sound only); (2) decode once into `OnsetCurve` in the app next to `AudioLevelDecoder` and let "BPM from audio" use `EstimateTempo` + `AssessTempo`; (3) per-hole decision for the rhythmic fill from the sound in the hole or the picture, then re-run the fill bench and decide the default; (4) `EditorController::optimizeFor` reads the fill settings; (5) a Settings UI for base BPM and variation; (6) MSVC build.
- **Stay off:** `core/src/intensity_arc.cpp`, `phase_rules.cpp` (#168).

## Handover: `claude/samn2-kernel-modes` (8 Nov, kernel / modules, slice 1: ModeRegistry)

Owner direction: the core is the kernel, the pages are modules, compiled in plus registries.

- **Done:** `app/src/mode_registry.*` (`ModeRegistry`, D-166): descriptors {id, title, icon, hue, tooltip, shortcut, page, visible, enabled}. `TopBar.modes`, the Ctrl+N shortcuts and the extra page `Repeater`/`Loader` in `Main.qml` read it; Player, Generator, Training stay hand-wired and built in; AI Train is registered hidden and `Plugins.setAiTrainEnabled` calls `setVisible`. `Plugins.modes` is gone. Order, titles, icons, hues, keys unchanged.
- **Tests:** `tests/app/mode_registry_test.cpp` (new, 7 cases); `shell_qml_test`: `registeredModeGetsTabKeyAndPage` (new). Existing tests unchanged. Local Release build, Qt 6.4.2 offscreen: mode_registry_test 9/9 incl. init/cleanup, shell_qml_test 25/25. Other app tests were not rebuilt here.
- **Review fixes (9 Nov):** D-165 belongs to #168, so this is D-166. `Main.qml` follows the current mode by id (`modeId`); `registerMode` refuses `builtin`; builtins cannot be disabled; extra pages come from `ModeRegistry.pages`, which notifies only when the visible extra set changes (enable / disable no longer recreates the page Loaders); the registry test uses a temp `SAMN_CONFIG_DIR` and removes every mode it registered; the builtin hues, titles and icons are pinned against `Theme`.
- **Still open (next slice):** registering or hiding one extra mode still recreates the Loaders of the others (JS-array Repeater model); a `QAbstractListModel` fixes that.
- **There is no ModeDeck:** D-150 replaced the M deck with the tab row, so slice 1 covers TopBar, keys and pages only.
- **Next slice (not started): `DeviceService`.** A pure-virtual interface (send level / position / axis, connection state, battery, generation, `setSessionActiveFor`, `stop`, `emergencyStop`) that `PlayerController`, `TrainingController` and `PlaybackOutput` use instead of `DeviceController*`. It touches `Q_PROPERTY(DeviceController* device)` in the Player and Training controllers (QML binds them), `QPointer<DeviceController>`, and `connect(..., &DeviceController::statusChanged)`; the interface needs the signals too (QObject base). Add a `FakeDeviceService` so the `playback_output_test` / `training_controller_test` need no real controller. Judged larger than the rest of this PR, so left out.
- **Stay off:** `app/qml` Player/Editor/Generator pages beyond what this PR touched (Main.qml, TopBar.qml).

## Handover: `cursor/player-drop-scenemap-635a` (9 Nov, drop Player Scene Map tab)

Owner: shrink the unfinished Player surface. Scenes already live in the Generator Editor (D-145). The Player bottom tab was a redirect stub.

- **Tabs:** Timeline · Chapters · Bookmarks · Audio Analysis. Audio Analysis is index 3. `showAudioAnalysis()` writes `player.bottomTab` 3.
- **Old prefs:** stored 4 (old Audio Analysis) opens 3; stored 3 (old Scene Map) opens Timeline (0).
- **Kept:** chapter bands on the Timeline lane (`chapterStrip`); Generator Scenes tab and `editRequested(scenes)`.
- **Test:** `playerBottomTabsDropSceneMap` in `shell_qml_test`.
- **Stay off:** Auto sync. Do not build a Scene Map view.

## Handover: `claude/samn2-parity-refresh` (8 Nov, PARITY audit and the list of what is left)

Owner asked for an honest picture of how far SamNPlayer 2 is against 0.7.0 and 0.8. **Documentation only; no code or test changed.**

- **Table:** `python3 ci/parity_status.py` (existing script, unchanged) now prints 374 done, 139 partly, 52 dropped, 113 open of 678 = 73 %. The old table in `PARITY.md` said 292 / 133 / 44 / 209 = 59 % but the markers on `main` already counted 334 / 111 / 44 / 189 = 64 %, so the table was stale before this audit.
- **Method:** every cited file and test name was checked with grep; open and partly lines were checked against `app/`, `core/`, `engine/`. 46 lines moved to done (38 from open, 8 from partly), 8 open lines became dropped or replaced with a decision, 30 open lines became partly, 6 ticked lines were downgraded to partly because no test covers them (top-bar Open and Ctrl+O, Player keyboard shortcuts, keyframe dots, time ruler, global keys, Optimize-after-Create remembering).
- **Not re-checked one by one:** 207 open and partly lines kept their 26 Oct text (counts per section in "Audit notes" at the end of `PARITY.md`). Nothing was built or run.
- **New file:** `docs/PARITY_REMAINING.md`: quick wins, medium and large blocks for 0.7, the 0.8 gaps, and what was dropped on purpose. Start with its (a) 1 to 6.
- **Stay off:** Cursor edits `PARITY.md` in its PRs; merge `main` and keep its newer ticks where they are correct, then run the script and paste the table.

**Last update (health check):** 8 Nov 2026, Claude, branch `claude/samn2-health-check`: the System tab (D-162) is extended, not rebuilt (D-167): Qt-free `samn::health` in core and five new rows. Earlier lines below still hold.

## Handover: `claude/samn2-health-check` (8 Nov, D-167)

Owner task: hardware and runtime health, iGPU and dedicated GPU for AI and video, plus a missing-parts report. Most of it already existed as Settings → System (D-162), so this extends it.

- **Core:** `core/include/samn/health.h`, `core/src/health.cpp`, `tests/core/health_test.cpp` (9 cases, synthetic fixtures only). Check model (ok / warn / bad / off), GPU kind, `RecommendAi` (provider names as strings), `RecommendVideoDecode`, parsers (lspci -mm -nn -D, PowerShell Win32_VideoController list, /proc/meminfo), `FindOnPath`, five checks.
- **App:** `system_check.*` keeps the controller; platform layer gained driver names, lspci names (Linux), a PowerShell fallback when DXGI lists nothing (Windows), RAM, ffmpeg / Python lookup. 13 rows now; a new state `off` (grey dot) for optional parts that are missing. `SettingsPage.qml`: one line (dot colour for `off`).
- **Tests:** `system_check_test` (row list and counts updated 8 to 13, plus `theMissingPartsRows`), `shell_qml_test` has a fake PC pushed through `setInputs` (rows, summary, dot colours). Core 508/508, system_check 22/22, shell_qml 24/24 on Linux (Qt 6.4.2, offscreen).
- **Timing:** a refresh takes about 50 ms wall on the test box (no GPU, no lspci), off the GUI thread; the work itself was under 2 ms. Every external program has one deadline (lspci 1.5 s, PowerShell 3 s, killed after it); the lookups run in the worker thread, not on the GUI thread.
- **Not verified:** real GPUs, Windows (DXGI driver version, the PowerShell path), lspci on a real box.
- **Next slice:** let AI Train / the detector read `RecommendAi` and pick the ONNX Runtime provider (needs a build that ships it); real-hardware check on the Owner's Windows box.

## Handover: `claude/samn2-json-fallback` (8 Nov, CI: nlohmann_json from the system first)

On 8 Nov GitHub returned HTTP 504 for the release download of `json.tar.xz` for hours, so every job died in the CMake configure step before any code was built (#168 failed four re-runs for this reason alone).

- **CMake:** `find_package(nlohmann_json 3.11 QUIET)` first; only if nothing is found, the shallow git clone of the pinned tag v3.11.3 that main already used instead of the Releases archive. No change for a developer machine without the package.
- **CI:** the three Linux jobs install `nlohmann-json3-dev` (3.11.3 on Ubuntu 24.04) together with OpenCV. Verified: configure and build `core_tests` against the system package, 491/491 pass; without the package the fallback branch is taken. First CI run of this PR failed because `include(FetchContent)` had moved into the fallback branch (vision/ needs it for ONNX Runtime): fixed, it stays unconditional.
- **Not changed:** the Windows job still fetches nlohmann_json (git clone) from github.com. Adding `nlohmann-json` to `ci/vcpkg/vcpkg.json` would change the hash that keys the vcpkg and the FFmpeg-dav1d caches and force an expensive rebuild; do it as its own PR if the outage returns.
## Handover: `cursor/player-code-tidy-635a` (8 Nov, Player code tidy)

Owner: clean Player code — same class, split translation units.

- `player_script_io.cpp` / `player_audio_jobs.cpp` / `player_marks.cpp` + `player_internal.h`
- No Q_PROPERTY / objectName / behaviour change. Stacked on probe-link.
- **Stay off:** Claude #168. No QML radius / Inspector tab split in this PR.

## Handover: `cursor/player-probe-link-635a` (8 Nov, Probe on link)

Owner: finish Player/Editor GUI except Claude #168.

- **Player:** soft `videoLinkWarning` on attach (mkv/avi / HEVC·AV1 name tags); amber bar + Make playable + Dismiss; cleared on first picture.
- **Tests:** `videoLinkWarningOnProbeContainers`.
- **Stay off:** Claude #168. Full ffprobe codec probe still optional later.

## Handover: `cursor/player-audio-vib-635a` (8 Nov, Audio vibration) — merged #189

Owner: finish Player/Editor GUI except Claude #168.

- **Player:** `audioVibration` / `playback.audio_vibration` (default off); at Play start `ApplyAudioVibration` on mapped frames; warm decode on toggle; pause restores script lanes.
- **UI:** Inspector Audio + View (`pb-audio-vibration`); Settings → Play (`st-audio-vibration`).
- **Tests:** `audioVibrationDefaultsOffAndSwapsOnPlay`.
- **Stay off:** Claude #168.

## Handover: `cursor/player-mock-635a` (8 Nov, Mock checkbox) — merged #190

Owner: finish Player/Editor GUI except Claude #168.

- **Player:** `playbackMock` / `playback.mock` (default on); Play auto-connects Mock when disconnected.
- **UI:** Inspector → View → "Mock (no device)" (`pb-mock`).
- **Tests:** `playbackMockDefaultsOnAndConnectsOnPlay`.
- **Stay off:** Claude #168.

## Handover: `cursor/player-make-playable-635a` (8 Nov, Make playable) — merged #187

Owner: finish Player/Editor GUI except Claude #168.

- **Player:** error-bar + no-picture **Make playable** → `ClipPrepController::convertForPlayback` (`<stem>-h264.mp4`).
- **Messages:** `reportMediaError` / `reportNoPicture`; `mediaError` + `videoDurationMs` for QML.
- **Stay off:** Claude #168. Full ffprobe/proxy naming still open.

## Handover: `cursor/player-video-autostart-635a` (8 Nov, Video play autostart) — merged #188

Owner: finish Player/Editor GUI except Claude #168.

- **Player:** `videoPlayAutostart` / `playback.video_play_autostart` (default on); gates `setMediaPlaying` → device.
- **UI:** Inspector → View → "Video play starts device".
- **Tests:** `videoPlayAutostartDefaultsOnAndGatesDevice`.
- **Stay off:** Claude #168.

## Handover: `cursor/player-auto-sync-635a` (8 Nov, Auto sync) — merged #186

Owner: finish Player/Editor GUI except Claude #168.

- **Core:** `EstimateAudioSyncOffset` (loudness vs stroke impulses, ±3 s, high/low/none).
- **Player:** `suggestAudioSync` / `applyAudioSyncSuggestion`; Script Tools Auto sync + Apply.
- **Tests:** `TestEstimateSyncLag*`, `autoSyncNeedsScriptAndVideo`.
- **Stay off:** Claude #168.

## Handover: `cursor/player-sound-curve-635a` (8 Nov, Sound Curve) — merged #185

Owner: finish Player/Editor GUI except Claude #168.

- **Core:** `VibrationCurveFromSound` (250 ms avg, Δ≥4 / 2 s, always last).
- **Player:** `proposeSoundCurve` / `confirmSoundCurve` / `undoSoundCurve`; Audio Analysis + Inspector.
- **Tests:** core decimate + `soundCurveWritesVibrationAndUndoes` (skips without decoder).
- **Stay off:** Claude #168.

## Shipped this stream (8 Nov, #178–#185)

- Typed offset, Drive/Recipe, Save Emotion Script (#178)
- CreateScriptAxis / + Axis… + Curve select (#179)
- FPS-Snap seeks (#180)
- Stage keys hint (#181)
- Scene markers toggle (#182)
- BPM From audio (#183)
- Parity settings docs refresh (#184)
- Sound Curve (#185)
- CI: Linux `--parallel` capped to nproc/2 (OOM kills)

## Still open (non-Claude, Player/Editor)

- Speech-Hold / Feel segment strip + heatmap bands (needs `audio_check` segments)
- Scene strip thumbnails in Player (markers done; thumbs Generator-side)

## Handover: `cursor/player-bpm-audio-635a` (8 Nov, BPM From audio) — merged #183

Owner: finish Player/Editor GUI except Claude #168.

- **Core:** `EstimateBpmFromLoudness` / `FoldBpmIntoRange` (first 10 min, 20–240).
- **Player:** `guessBpmFromAudio` via `AudioLevelDecoder`; Inspector Audio → From audio.
- **Tests:** envelope BPM + `guessBpmFromAudioNeedsVideo`.
- **Stay off:** Claude #168.

## Handover: `cursor/player-scenes-toggle-635a` (8 Nov, Scene markers toggle) — merged #182

Owner: finish Player/Editor GUI except Claude #168.

- **View → Scene markers:** `showScenes` / `ui.player.scenes` (default off); hides chapter strip.
- **Tests:** `showScenesDefaultsOffAndRemembers`.
- **Stay off:** Claude #168.

## Handover: `cursor/player-keys-635a` (8 Nov, Player keys hint) — merged #181

Owner: finish Player/Editor GUI except Claude #168.

- **Stage:** `keysHint` line (Space / seek / offset / E / T theatre / Esc).
- **Inspector:** collapse tooltip points at theatre (T) for bigger video.
- **Stay off:** Claude #168.

## Handover: `cursor/player-fps-snap-635a` (8 Nov, Player FPS-Snap) — merged #180

Owner: finish Player/Editor GUI except Claude #168.

- **Player:** `fpsSnap` (playback.fps_snap); `setVideoPositionMs` snaps script time via `SnapMs`.
- **Inspector → View → Curve edit:** FPS-Snap slider 0–60 (off at 0).
- **Tests:** `fpsSnapSeeksToFrameBoundaries`.
- **Stay off:** Claude #168.

## Handover: `cursor/plus-axis-635a` (8 Nov, CreateScriptAxis / "+ Axis…")

Owner: finish Player/Editor GUI except Claude #168. Companion axis create + Editor edit.

- **Core:** `create_companion_axis` / `is_companion_axis` / `companion_axis_path` (centred at 50, never overwrite, D-086).
- **Player:** `createCompanionAxis`, `missingAxisChoices`; Inspector Script Tools Curve select + "+ Axis…".
- **Editor:** load/save companion files; Tools menu "+ Surge/…" via `createCompanionAxis`; lanes include companions.
- **Tests:** core create; `createCompanionAxisAddsLane`; `createCompanionAxisIsEditableAndSaved`.
- **Stay off:** Claude #168. Wait #178 CI before merge; rebase this on main after.

## Handover: `cursor/typed-offset-635a` (8 Nov, Player Script Tools polish)

Owner: finish Player/Editor GUI (Claude owns #168). Closes typed offset + feel-row Drive / Save Emotion Script.

- **OffsetField:** typed ±10 000 ms, step 10; ±50 buttons kept.
- **Drive:** Recipe | Axes (`setPlaybackSource`); contact block hidden in Axes mode.
- **Save Emotion Script:** `saveAsEmotionScript` → companion `.samn`.
- **Tests:** `driveSourceAndEmotionScript`; Adapt tab asserts driveSource / offsetInput.
- **Stay off:** Claude #168.

## Handover: `cursor/player-contact-save-635a` (8 Nov, Player contact feel probe + Save) — merged #177

Owner: Player/Editor GUI — Neo 2 contact feel after Adapt. Completes PARITY §3.1 feel probe / Save to script.

- **Inspector → Playback → Contact:** Feel probe canvas (PreviewContactVibration) + Save to script (`saveContactToScript` → `ApplyContactRecipe` + reload).
- **C++:** `contactFeelProbe` property; strength follows live intensity knob.
- **Tests:** `contactControlsFollowTheScript` save+probe; `playbackTabMappingControls` UI.
- **Stay off:** Claude #168.

## Handover: `cursor/player-bake-share-635a` (8 Nov, Player Bake & share) — merged #176

Owner: Player/Editor GUI — Adapt / convert finished scripts. Unbury 0.7 Bake & share.

- **Inspector → Playback → Bake & share:** Bake feel channels (`axesFromStroke`) + Share for other apps (`exportFunscript` → community `.funscript`).
- **Adapt** card slimmed to Optimize + Editor link (bake moved here).
- **Tests:** `axesFromStrokeWritesNeoLanes` covers share; `playbackTabAdaptForDevice` asserts bakeShareCard.
- **Stay off:** Claude #168 intensity arc.

## Handover: `cursor/player-curve-invert-635a` (8 Nov, Player Invert on Curve edit) — merged #175

Owner: Player/Editor curves · optimize · align. Completes Curve edit after #173.

- **Inspector → View → Curve edit:** Invert range (with mark) / Invert all (asks first, no undo in Player).
- **C++:** `invertSelection` / `invertAll` via `applySelectionEdit` (`needMarker` flag).
- **Tests:** `invertOnHeatmapSelectionAndAll`; `viewTabCurveEdit` covers Invert button.
- **Stay off:** Claude #168 intensity arc.

## Handover: `cursor/player-curve-edit-635a` (8 Nov, Player curve edit on mark) — merged #173

Owner: Player/Editor curves · optimize · align. Cap / Softer / Stronger / Delete in the Player (PARITY §3.1).

- **Inspector → View → Curve edit:** Cap slider (150–800, `playback.cap_intensity`), Soft edges, Speed-cap / Softer / Stronger / Delete on the heatmap mark; active script lane (stroke, or baked vib/suction).
- **C++:** `PlayerController::{cap,scale,delete}Selection` via `range_edit.h`; rewrite+reload; Delete clears the mark.
- **CI:** FetchContent for nlohmann/json uses shallow `GIT_TAG v3.11.3` (Releases `.tar.xz` was 504ing configure).
- **Tests:** `curveEditOnHeatmapSelection`; `viewTabCurveEdit`.
- **Stay off:** Claude #168 intensity arc. Vision-bench (#172) is on main — leave it alone.

## Handover: `claude/samn2-vision-bench` (7 Nov, detection benchmark, measurement only) — merged #172

Owner: the Generator's video detection must become cinema-grade accurate and lean, and every change is accepted or rejected by numbers. The harness comes first. **No engine behaviour was changed.**

- **Tool:** `engine/tools/samn_bench.cpp`, target `samn_bench` (built with the project, not in ctest except `samn_bench_smoke`, about 10 s: `--quick --no-track --scenes clean --require-r 0.9`). It runs the real `Generate` (Recommended profile, no tip hint) on 17 fixed-seed synthetic scenes (`--list`): clean, pan Y, pan X, zoom, occlusion 0.5 / 2 / 4 s, blur, noise + JPEG, 30 % dropped frames, anti-phase and quarter-phase distractor, hard cut, lighting cut with a jump, fast 256x144, still disc, 300 s drift. Flags `--scenes`, `--out`, `--runs`, `--warmups`, `--quick`, `--no-track`, `--timing-only`, `--keep-clips`, `--require-r`. Output: `samn_bench.csv` (scene, metric, value), `samn_bench.md`, `clips/<scene>_truth.csv`. One process per scene on Linux, so the peak RSS (`wait4` rusage) is the scene's own; 0 elsewhere.
- **Scenes:** `tests/engine/synthetic_scenes.h` (new, on the untouched `synthetic_clips.h`; the tool gets the include path). Synthetic only: moving discs and test patterns.
- **Metrics:** 10 s windowed r (median, min, spread over runs) on the script and on the raw track (`track_r_*`), peak timing error (median, p95), extrema-count error, amplitude ratio, scene cuts found and false, lost share and loss precision / recall against the occlusion mask, jumps over 60 px, ms/frame (median of 5 after 3 warm-ups), RSS. Cuts, lost flags and jumps come from one extra untimed `TrackBox` run with the options `Generate` uses (`Generate` itself exposes none of them).
- **Acceptance rule** (also in `--help` and the baseline doc): no scene's r drops by more than max(0.01, its 5-run spread); median r rises or median peak error falls by 2 ms or more; ms/frame up by at most 10 %, RSS by at most 10 %; before a default changes, the Owner gate through `samn_compare` on his private clips (numbers only).
- **Baseline:** `docs/benchmarks/DETECTION_BASELINE_2026-11-06.md` + `.csv`; load averages and three timing repeats in it. Full run about 33 min. Findings: **pan X raw track r 0.797** (pan Y and clean 0.999): the 0.81 reproduces on the track, but the script r is 0.989 because the default detrend removes this slow pan; occlusion 2 s / 4 s r 0.77 / 0.72 with the lost flag exact; 30 % dropped frames give 4 false cuts; the lighting cut with a jump leaves the tracker lost for the whole second half (r 0.66); fast_small has half its frames flagged lost. ms/frame is about 22 to 37 here (unoptimised build) with up to 25 % spread between runs: time on a quiet machine, median of 3 or more.
- **Next slices (each judged by this bench):** PR-2 full 2-D camera compensation (done: `claude/samn2-camera-2d`) (`camera_motion.cpp` returns only `t(1,2)`, `track_box.cpp` about line 456 shifts only ys; measured: pan clip Y stroke 0.888 -> 1.000 with compensation, X stroke 0.812 -> 0.812; the Owner's clip uses X; judge on `pan_x` track r); PR-3 forward-backward LK loss flag + confirmed scene cuts (drop30, light_cut_jump); PR-4 phase-locked periodic bridge (occlusion scenes); PR-5 sub-sample peaks (peak error vs the 40 ms frame); PR-6 FR-8 640 px downscale 90 -> 35 ms/frame. CPU / RAM: chunked label-tracking decode, `grab()` for skipped frames.
- **Not done:** an automatic baseline-vs-candidate comparison of two CSVs (the rule is applied by hand); Windows (RSS 0 there, not built); real clips (Owner gate).


## Handover: `claude/samn2-playhead` (5 Nov, video playhead extrapolation) — merged #163

With a video, `QMediaPlayer` reports its position every 100 ms (Qt 6.4 FFmpeg; 50 ms in 6.7), and the output was sampled only at those reports: stroke starts 42 ms late at p50, 90 ms at p99, moves squeezed (`TIMING.md` § 4-5). Now the output follows an estimate between the reports.

- **Core:** `samn::PlayheadExtrapolator` (`playhead.h`, no Qt). Estimate = last report + elapsed x rate, at most `CapMs()` of wall time after the last report (median interval x 1.2, 20-150 ms; 120 ms default). A report behind it by up to the hold window, max(40 ms, cap x rate), holds it (no step back); further behind, ahead, or the first report after `Seek()` is taken as it is. Paused it is the last report, and after a start it moves only from the first report on. `Hold()` freezes it until the next report, `Clear()` forgets the interval.
- **App:** `MediaPlayhead` (`media_playhead.*`): monotonic clock, a 16 ms `PreciseTimer` only while the video plays, emits only while the estimate advances. `PlayerController::setMediaPlaying` / `reportMediaPosition` (new; the `MediaPlayer` of `PlayerPage.qml` calls them, `playheadMoved(pos, fromVideo)`). The ticks feed only `PlaybackOutput`; the curves, the heatmap and auto Extended-O follow the reports. The rate comes from `output.speed`. Hold on an offset change, on the report at a loop end and on the emergency stop; clear on a new video; the estimate never passes the video's duration.
- **Safety:** `PlaybackOutput::update(t, fromReport = true)`. A tick (`false`) neither restarts the stale watchdog nor wakes the output after a stop, so the watchdog still fires 1.5 s after the last *report*. A pause stops the ticks and then the device at once. The estimate is never more than the hold window ahead of the latest report.
- **Unchanged:** the script clock path (`setPlaying` / `setVideoPositionMs`), Editor, Training, the Generator page (its own player, no device output), soft seek, soft start, speed limit, offset (PS-01).
- **Tests:** `tests/core/playhead_test.cpp` (22 cases), `playback_output_test` (13 new `video*` / `scriptClock*` cases), `player_page_qml_test` (seeks announce themselves); each new one failed against a stub or the old rule first. No assertion of `main` changed.
- **Review fixes (PR #163, 6 Nov):**
  - *A step back sent a move twice with a reversal* (major): a report 41-250 ms behind the estimate set it back, so the output picked the passed point again and sent the stroker back (`max(20, …)` ms), then the next move again. Now the floor holds for any report up to the window behind; the fix is in the extrapolator, not in `PlaybackOutput`, so the script clock's behaviour (`seekEasesIn`: a small step back sends the earlier point) is unchanged. Two assertions of this branch's own core tests encoded the old rule and now expect the hold (60 ms behind at 1x, 140 ms behind at 2x); the seek case uses 150 ms behind. Test `videoReportBehindTheEstimateSendsNoMoveTwice`. The bench counts "extra sends within a move".
  - *Seeks of the page*: `PlayerController::holdMediaPlayhead()` (invokable, `MediaPlayhead::seek()`, core `Seek()`), called by `seekTo` / `seekBy` before they set the position; the seek's report is taken as it is. The loop jump uses the same.
  - *Loop end*: with the loop on, the estimate stops 1 ms before the loop end (`MediaPlayhead::setLimitMs`), so only a report reaches it.
  - *Offset change* while the video plays is no report (`fromReport` false): it does not feed the watchdog. The script clock path keeps `true`.
  - `scriptClockHasNoVideoPlayhead` now checks the exact moves of the script clock path and that ticks add nothing.
- **Second review (PR #163, 6 Nov):**
  - *A rate decrease stepped back* (major): `SetRate` held at the old rate, but the next report was judged with the new rate's smaller window (4x -> 1x: 480 -> 120 ms), so a lagging report 210 ms behind set the estimate back. `Hold()` now keeps the window from before it for the next report.
  - *The window shrank inside a report*: the new interval was counted before the window was taken (default 120 ms -> 58 ms at the first 50 ms intervals). The window of a report is now the larger of before and after its interval, and while the last report held the estimate, that report's window too. `HoldWindowMs()` is a lower bound of the window the next report gets (a new interval can only enlarge it).
  - The reviewer's fuzz (20000 runs with rate changes, holds, seeks, jitter, stalls and rewinds): 160 step backs that were not allowed before, 0 now. The test has 3000 runs: 22 before, 0 now.
  - *The loop limit under the playhead stepped back*: `MediaPlayhead` never clamps below what it already sent unless the estimate itself went back (a seek). Test `videoLoopEndBelowThePlayheadDoesNotStepBack`.
  - *A seek without a report*: `Seek()` expires after the cap; `seekTo` / `seekBy` skip the announcement when the target is the current position (`player_page_qml_test`).
  - *Wording*: D-164, `TIMING.md` § 7.1 and `playhead.h` say the same thing: no step back unless a report is more than the window behind the estimate (apart from a rate change, the media clock itself went backwards) or follows a seek; pausing returns to the last report.
  - *Left as it is (safe direction)*: a timer report queued before the seek's report, coming after `holdMediaPlayhead()`, counts as the seek's report; the seek's own report then follows the normal window rule (it can be held up to the window instead of being taken as it is). A timer report within the cap after a seek that gave no report (an unseekable stream) is taken as it is (one small step back at most).
- **Measured** (`TIMING.md` § 7, `benchmarks/TIMING_PLAYHEAD_2026-11-06.md`, after the review fixes): stroke start 42 -> 4.5 ms p50, 90 -> 13-14 ms p99, max 17.2; at most 4.2 % shorter than scripted (reports only: 20.4 %); the sent duration equals the formula in every move; no extra send within a move. The bench got the scenarios `media raw` (reports only) beside `media`, and a "move shorter than scripted" row.
- **Timing audit:** `tests/app/timing_bench.cpp` and `docs/TIMING.md` came from `claude/samn2-timing-audit` (#159, now in `main`); this branch adds the `media raw` scenarios and the squeeze row to the bench, § 7 to `TIMING.md` and changes its § 6 heading.
- **Not verified:** Windows with Qt 6.7.3 (50 ms reports, cap 60 ms), a real device, the GStreamer backend, a GUI thread that also renders.

## Handover: `cursor/editor-tools-surface-635a` (7 Nov, Editor Align bar)

Owner: Player/Editor GUI — curves, optimize, align. Unbury Tools… for everyday edit.

- **Align bar** on `editorCard` (`alignBar`): Any device / Sam Neo 2 + Optimize, Axes from stroke, From sound (+ keep contact), Beat bar — visible whenever a script is open.
- **Range row:** Soft edges toggle + Cap intensity (150–800, default 400) beside the icon tools; `rangeTool` uses them.
- **Tools…** slimmed to + lane and FPS snap.
- **Tests:** `generator_qml_test` asserts alignBar / capIntensity; axes without opening the menu.
- **Stay off:** Claude #168 intensity arc (if still open).

## Handover: `cursor/player-rhythm-timeline-635a` (6 Nov, Player rhythm + timeline) — merged #170

Owner: GUI / Player / Editor curves, BPM, optimize & align.

- **BPM:** blank field uses stroke-tempo estimate (`effectiveBpm`); *Guess from stroke* fills BPM and turns the grid on; `playback.bpm_grid` remembered. Honest notes (audio detect still later).
- **Heatmap:** height 18→28; wheel zoom like the curve lanes (`heatBar`).
- **Tests:** `bpmFromStrokeWhenFieldBlank` (QCOMPARE samples bpm after guess).
- **Stay off:** Claude playhead / intensity-arc.

## Handover: `claude/samn2-device-worker` (6 Nov, D-163: DeviceWorker into its own file, pure move)

- **What moved:** the class `DeviceWorker` (declaration and inline members into `device_worker.h`, the out-of-line members into `device_worker.cpp`, same order, bodies byte-identical) with the constants and helpers only it uses (`kPrefConnectTest`, `kStopBudgetMs`, `kStopGraceMs`, `kNoDevice`, `HintFor`, `Err`). `device_controller.cpp` keeps only `DeviceController`.
- **Shared in the header:** `kPrefTransport` and `Q()` (now `inline`), both used by the controller too; `namespace dev` alias. No `Q_OBJECT` in the worker, so no moc change. `friend class DeviceWorker` in `device_controller.h` is unchanged.
- **Checked:** sorted-line comparison old region vs new files (only includes, `#pragma once`, namespace lines and the `inline` on `Q` differ), full build, ctest, `device_controller_test` / `training_controller_test` 3x each. No test edited.

## Handover: `cursor/player-adapt-device-635a` (6 Nov, Adapt finished scripts for toys) — merged #169

Owner ask: Player/Editor GUI — convert a finished script (after Create or an existing `.funscript`) for Sam Neo 2 and other toys.

- **Player:** Inspector → Playback → *Adapt for device* (`optimizeFor` / `axesFromStroke` on `PlayerController`, rewrite+reload like Bake offset). Targets: Any device · Sam Neo 2 (defaults to Neo 2 when that device is connected). Confirm dialogs before rewrite.
- **Editor:** Post-Create *Adapt for device* row above *Play in the Player* (`postCreateAdapt`) so the convert path is not buried in Tools….
- **Tests:** `player_controller_test` optimize/axes; `inspector_qml_test` adapt card.
- **Stay off:** Claude #163 playhead / #168 intensity arc.

## Handover: `cursor/hybrid-reacquire-635a` (6 Nov, hybrid tip re-acquire) — merged #167

Legal FunGen-beat CPU lever (D-139 / D-158): ONNX only when the classical path already failed.

- **Engine:** `TrackOptions::hybrid_reacquire` + `ai_propose`; after CSRT and appearance miss, one AI box, fail-closed with `MatchesOriginal` when memory is on. Cache key v6 covers the flag.
- **App:** Advanced → Tracking → *AI re-acquire on tip loss* (`generator.hybrid_reacquire`, default off). Loads `models/student/student.onnx` or `models/nudenet.onnx`; filters by tip class.
- **Tests:** `track_box_test` hybrid hook unused when off; fires when on with a propose lambda.
- **Stay off:** Claude #163 playhead.

## Handover: `cursor/training-channel-caps-635a` (6 Nov, Training output caps) — merged #165

V1 Training lever: soft ceilings on what the device receives.

- **Core:** `Session::set_output_caps(max_vib, max_suc)` clamps in `write()` (default 1 = unchanged).
- **UI:** Session Settings *Max vibration* / *Max suction* (`training.max_vibration` / `training.max_suction`, default 1 = “off”). Visible when the channel is available; can change mid-session.
- **Tests:** `training_test` clamps peak 1.0 → 0.5; controller remembers both caps.
- **Still open Training:** skip phase; Auto Cool Down; Pause on Limit. Categories already ship.
- **Stay off:** Claude #163 playhead.

## Handover: `cursor/gen-curve-layout-635a` (6 Nov, Generator stage + CPU) — merged #164

Million-dollar Generator layout chunk + a legal FunGen-beat CPU lever (no ONNX copy).

- **GUI:** `GeneratorPage` centre column is a vertical stage (`stageSplit` ColumnLayout + drag handle — Controls `SplitView` collapsed the curves pane under the async Loader): video and curves get explicit heights from a remembered share (`generator.editor_share`, 0.18–0.65, default 0.40). Empty state keeps curves ≤20% so the tip box still has room.
- **CPU / recognition:** Expert *Tracking analysis rate* wires `GenerateRequest.max_analysis_fps` / queue (`generator.max_analysis_fps`, 0 = every frame, else 5–60 step 5). Engine already had FR-7; compare manifest kept. Help `gen-analysis-fps`.
- **Docs:** VERSIONS track-cache row marked covered; new plan row for confidence-gated hybrid track (light every frame, AI only on loss / low confidence). GENERATOR_SPEC note updated.
- **Tests:** `generator_controller_test` remembers analysis fps + editor share; `generator_qml_test` finds `stageSplit`.
- **Owner asks (Training algorithms — not in this PR):** which to add first among (a) multi-phase custom builder beyond Stop-Start/Plateau, (b) Endurance/Control/Edge categories + skip phase, (c) per-channel caps / Auto Cool Down / Pause on Limit, (d) something else? Classic Stop-Start + Plateau + wave/massage presets already ship.
- **Next recognition beat (not this PR):** confidence-gated hybrid re-acquire — CSRT/light path every frame, wake student ONNX only when CSRT confidence drops / appearance fails / periodic semantic check; keep *Reuse the tracking* for post-feel re-runs.
- **Stay off:** Claude #163 playhead files.

## Handover: `cursor/idle-flow-stage-635a` (5 Nov, Feel without film Flow stage) — merged #162

Owner GUI leftover: idle ribbon / Flow idle graphic (PARITY §11, D-138). Empty home stays Play + Open only.

- **`FlowStage.qml` restored** (removed in #93 as unused on empty home): halo breathe, cyan/amber ribbon travel, floating play mark; idle 7 / 6 / 11+13 s, playing 4 / 5.5 / 6.5 s; still when paused, reduced motion or inactive.
- **Assets:** `flow-ribbons.svg`, `flow-mark.svg`. Unused `empty-video.svg` dropped (replaced by the stage).
- **`PlayerPage`:** `scriptOnly` panel `feelWithoutFilm` — FlowStage + "Playing without video" + copy + Play/Pause + "Link video…".
- **Tests:** `scriptWithoutVideoPlaysOnItsClock` asserts the panel, idle→playing→paused `stageState`.
- **Still open Owner GUI:** real donate URLs (D-022). Broader look polish welcome. Not touched: PlaybackOutput / device timing (Claude #159).

## Handover: `claude/samn2-timing-audit` (5 Nov, timing audit, measurement only)

Owner question: is the Player as exact and fast as the Go version? Nothing had been measured. **No behaviour was changed.**

- **`docs/TIMING.md`:** the timing path with file/line references (script clock, media player reports, `PlaybackOutput`, the worker queue, every transport, worst case per stage), the Go 0.7 comparison (read from the 0.7 repository, not in this repo), the results, caveats, findings and a ranked recommendation. All rows: `docs/benchmarks/TIMING_2026-11-05.md`.
- **Bench:** `tests/app/timing_bench.cpp`, target `timing_bench`, built with the tests and **not** registered with ctest (as `clip_prep_real_check`). The real `ScriptClock` / `QMediaPlayer`, `PlaybackOutput`, `DeviceController` + worker and the real TCode UDP/TCP and Intiface transports into stand-ins on their own threads that stamp each write with the steady clock; a Sam Neo 2 stand-in transport (optional ack model). About 15 min for all scenarios; `--seconds`, `--only`, `--load`.
- **Key numbers** (Linux, Qt 6.4.2, offscreen, 4 cores, 3 repetitions at loadavg below 1.4): report -> wire 0.3-0.7 ms p50, 1.6-5 ms p99 on every transport, also with 3 busy threads; levels and stroke starts late by the sampling lag, 8-9 ms p50 / 17 ms p99 with the 16 ms clock and **42 ms p50 / 90 ms p95 with a video** (Qt 6.4.2 FFmpeg reports every 100 ms; Qt 6.7.3, the release, every 50 ms per its source); stroke ends 1 ms p50 / 4 ms p99 after due on every path; sent durations equal the formula in every move (also 0.5x / 2x and the speed limit); offset +/-200 shifts the wire by exactly +/-200.
- **Findings worth knowing:** `Qt::PreciseTimer` would change nothing (Qt already treats coarse timers of 20 ms or less as precise, on Windows with the multimedia timer); 0.7 sampled the video every ~250 ms, so SamN2 is at least as exact in every mode. Rare single gaps (one report interval up to 112 ms, one `update` call of 105 ms) were seen about once per 60 s run, cause not identified.
- **Recommended next (not done):** 1. extrapolate the video playhead between media reports (gain: video lag 0-100 ms to 0-16 ms); 2. send the next stroke at its start time with a precise timer; 3. levels at their due time (low value); 4. `LowDelayOption` on the TCode TCP socket (cheap, no evidence of a problem yet); 5. measure the Bluetooth round trip on the device before any BLE change. Details, risks and protecting tests: `TIMING.md` § 6.
- **Not measured:** a real device, the Bluetooth radio, Windows, the GStreamer backend, the GUI thread with QML rendering and video frames, the QML hop.


## Handover: `cursor/mt-debug-trajectory-635a` (5 Nov)

Player MT-Debug trajectory overlay (PARITY tip/partner path lines).

- **Controller:** `hasTrajectory`, `trajectoryOverlay` (`player.trajectoryOverlay`, default off), `tipPathPoints()` / `partnerPathPoints()`.
- **UI:** Inspector → View → *Show tip path* (+ hint when the script has no path); stage `trajectoryLayer` Canvas (tip orange, partner teal) + moving tip dot.
- **Still open:** live postprocess probe (G4b leftover); real donate URLs (Owner); Quality Doctor. Idle ribbon ✅ (this stack / #162).


## Handover: `claude/samn2-driver-registry` (5 Nov, D-163 first slice: device drivers)

Owner: the core is the kernel, Player / Generator / Training are modules, device connections are kernel-internal and extensible. Chosen after an architecture review: drivers are compiled in and registered by explicit calls (no DLL plugins, no plugin ABI, no self-registering static objects, which the MSVC linker drops from a static library).

- **Core** (`core/include/samn/device/driver.h`, `core/src/device_driver.cpp`, no Qt): `TransportKind` (none, BLE, serial line, socket line, WebSocket), `DriverInfo` (id = the kind string, name, help, transport, address field and its example, the settings key of the last target, simulated, connect timeout), `TransportRequest`, `SessionOptions` (log sink, clock), `DeviceSession` (`GetDevice`, `Info(read_battery)`, `AdoptedId`, `Alive`, `Tick`, `Pump` with a `PumpOutcome`, `HasRawValues`), `Driver` (`Info`, `Resolve`, `Create`, `Hint`), `DriverRegistry` (`Add` refuses a null driver, an empty id and a listed id; `Find`; `List` in the order added) and `RegisterBuiltinDrivers` (ble, tcode, tcode-serial, intiface, mock, the order of the device choice).
- **What moved out of `DeviceWorker`:** the kind switch of `Connect`, the typed pointers of `Session` (neo / tcode / intiface / mock) and their branches in `SessionInfo`, `IdentityOf`, `MakeSnapshot`, `Alive`, `Pump`, `Tick` and `HintFor` (the TCode texts and `IntifaceHint`), the 20 s / 30 s connect timeouts and the target settings keys (`savedTarget` and the save after a connect now read `DriverInfo::target_setting`; keys unchanged). Intiface's pump / one-shot reconnect / ping is its session's `Tick`; its `Pump` reports a reconnect (`kReconnected`: the worker sends a snapshot at once) or that it watched the link itself (`kDone`: no `CheckAlive`, as before). The reconnect refusal, battery cache and identity semantics stay in `IntifaceDevice` unchanged; the identity's adopted index is `AdoptedId()`.
- **Stays with the worker (app):** `MakeTransport` (BLE through `ble_factory_` when set), the transport's cancel (`SetCancel` only on `QtTransport`), the connect deadline, `BeginStopWindow` / `kStopBudgetMs` / `kStopGraceMs`, the per-owner holds, the generation counter, the emergency stop. `Session` declares the transport first (destroyed last) and owns a `DeviceSession`.
- **QML:** `DeviceController::drivers` (`CONSTANT`, from `List()`); `InspectorPanel.qml` builds the choice, the address field, the serial port row and the help note from it.
- **Tests:** new `tests/core/device_driver_test.cpp` (12 cases: registry order, unknown id, duplicate id, TCode Resolve for plain / udp / tcp / bad addresses and serial, the other Resolves, hints, mock / Sam Neo 2 / TCode sessions over `MockTransport`, Intiface over the fake Buttplug server incl. the reconnect-to-another-toy refusal and the pump / tick reconnect). No existing test was changed.
- **Behaves differently (slightly):** the device choice's names and help texts come from C++ and are no longer wrapped in `qsTr` (there are no translations yet; when they come, `DriverInfo` needs translatable keys). `pickSavedPort` uses the saved target of the driver that picks a serial port instead of the literal "tcode-serial". A driver whose transport kind the app cannot build fails the connect with "no transport for the device kind" (unreachable with the five built-ins).
- **Not verified:** real hardware (Sam Neo 2, OSR / SR6, Intiface Central) and Windows / MSVC (built and run on Linux, GCC, Qt 6.4 only).

## Handover: `claude/samn2-tidy` (5 Nov, behaviour-neutral tidy-up)

Four commits, one per group. Nothing the program does changed; every existing test is unchanged except where a test moved with the code it tests (`playback_test.cpp`, one comment in `shell_qml_test.cpp`) and the six engine tests whose temp folder names got a per-process suffix (`TempPath`).

- **Docs:** `MODULES.md` lists every header of core, vision and engine and every app file. The stop budget is 6.5 s (`kStopBudgetMs`), D-160 (not D-159) is cited for the Recommended recipe, stale "port of the Python player" header comments are gone, `script_finder.h` says it folds with `FoldCase`, `tracker.h` says nothing calls `VisionSelfCheck()` at start-up (tests do; not wired), `GENERATOR_SPEC.md` § 10 lists E4 as not implemented and the 2 % travel check next to FR-15.
- **Shared helpers:** `AsciiLower` (`text.h`), `ReadFileToString` (`fileio.h`; nullopt on open failure or a read error), `TimeToTm` (`time_util.h`), app `qt_paths.h` (`LocalPath`, `FsPath`, `FromFs`, `PathUtf8`, `U8`, `FromU8`) and `ai_paths.h` (`AiDir()`, so `system_check.cpp` no longer includes `train_controller.h`), `kNoMotionError` (`post_track.h`), `samn::kCanceled` (`vision/canceled.h`: vision cannot see core, so it lives there). `generator_controller.cpp` passes `PathUtf8(f)` where it passed `f.u8string()`, which does not build in C++20.
- **Dead code:** unused includes; `samn::Session`, `LoopRange` and `position_at` (tests retargeted to `PositionAt`, `RangeMarker::Valid` and `offset_key` / `clamp_offset` with `Settings`; the `LoopRange::jump` assertions have no surviving function and went with it); Find tip takes `candidates.front()` (the stable sort puts the first highest score there); fps fallbacks and `searched`; unused QML and `Q_PROPERTY` members; four never-loaded SVGs.
- **Tests:** engine tests that made fixed temp folders get a per-process random suffix (`testclips::TempPath`).
- **Left as it was, on purpose:** the `<optional>` of `funscript.h` and `<stdexcept>` of `mapper.h` (headers: other files may rely on them on MSVC), `device_controller.h` in `mapping_settings.cpp` (`playback_output.h` needs the full type), `bg-top.svg` / `bg-bottom.svg` (the sources of the bundled PNGs), the Generator recent-scripts write (skipping it would not restore a settings file deleted while the app runs, and `changed()` would not fire), `training_controller.cpp` and `device_controller.cpp` (out of scope), `marks.cpp` `ReadDoc` (its errno message and directory check).
- **Not run:** the Windows / MSVC paths (`_WIN32` branches of `time_util.cpp`, `settings.cpp`) were only read, not built.

## Handover: `claude/samn2-device-leftovers` (5 Nov, device bug hunt leftovers)

Two device bugs, each with tests that failed before the fix.

- **Training on a device change:** `TrainingController::onDeviceStatus` only looked at the device while *connecting*. A running (or paused) session lived on after a lost link, kept its session hold (so connecting again was refused as "training is in progress") and kept sending levels. After an Intiface one-shot reconnect that adopted another toy, it drove that toy. `setDevice` with another controller left Training's hold on the old one for good, and that device kept its last level. Now a running session ends when the device is gone ("Device connection lost — training ended."; nothing to disconnect) or another one (`DeviceController::deviceIdentity()`, kind + name + address + Intiface device index, differs from the start: "The device changed — training ended."). The device is stopped if it is still there, the hold is released, and a connection Training made is closed. `setDevice` ends the session on the old controller first (a connect in progress fails). The next Start uses whatever is connected then. Tests: `training_controller_test` `aLostDeviceEndsTheSessionAndALaterStartWorks`, `anotherDeviceAfterAReconnectEndsTheSession`, `aNewDeviceControllerEndsTheSessionOnTheOldOne`.
- **Intiface battery semantics** (`IntifaceDevice`, `battery.h`):
  - A reading belongs to one device session. `Connect` (before the dial), `Disconnect`, a dead socket (`MarkDead`) and the server's `DeviceRemoved` for the adopted device drop it.
  - A failed read (no reading, `Error`, socket gone) drops the old value at once. It is still cached for `kBatteryCacheTtl` (the Stop-budget rule from `samn2-stop-safety` is kept), so a failed refresh never leaves the old value on show.
  - `Info()` shows a reading for `kBatteryMaxAge` (60 s) at most.
  - A `BatteryLevelReading` counts only with the Id of the request that is waiting, so a late reply to a timed-out request is not taken as fresh. `pending_battery_` is gone.
  - `DeviceController` publishes a battery value only while the device reports it is connected.
  - New test knob: `DeviceController::setDeviceClock`. The fake server got `DropClients`, `device_name` and `battery_level`.
  - Tests: `device/intiface: a failed battery refresh drops the old reading`, `... a battery reading ages out of Info`, `... the battery reading ends with the device session`, `... a late battery reply is not a fresh reading`, `device_controller_test::intifaceBatteryIsNeverAnOldReading`.
- **Review follow-ups (PR #156):**
  - *A reconnect inside a level command drove another toy* (major). A level that hit a dead socket made `IntifaceDevice::Command` reconnect, adopt whatever toy the server listed and send it the level. The worker's `CheckAlive` sent no snapshot while the link was alive, so Training only saw the new toy at the next 2 s tick. Fixes:
    - `Command` remembers the adopted device (index and name) and refuses the command when the reconnect found another one (`SameDevice`, "the reconnect found another device").
    - `CheckAlive` sends a snapshot as soon as the identity differs from the last one shown.
    - `ApplyLevels` stops a batch when the identity changed during it.
    - Every snapshot carries a `generation`, counted up at each identity change. A level carries the generation the GUI saw, and the worker drops levels meant for an earlier device. This also covers a reconnect in the tick or pump, where levels were queued before the GUI saw the new toy.
    - Stops are never dropped.
    - Tests: `device/intiface: a reconnect to another device does not get the old command` (another name, another index); `device_controller_test::intifaceReconnectToAnotherToyGetsNoOldLevel` (no tick during the test; the old level never reaches the new toy, and the identity changes within 1.5 s).
  - *Same name at another index* (nit). The identity includes the Intiface `AdoptedIndex()`. Test: `training_controller_test::aSameNamedToyAtAnotherIndexEndsTheSession`, with no tick; the new toy gets no level above 0.
  - *Merged batches* (second review). Waiting levels merge newest-wins, and a level for a newer generation used to keep the batch's older generation, so the whole batch was dropped (the Player then stayed silent on a constant hold). Now a level for another generation first clears the waiting batch's values (vibration, suction, position, axes) and takes the batch over. Keeping only the newest generation would have let an old suction through. Tests: `device_controller_test::levelsOfAnEarlierGenerationDoNotMergeIntoNewerOnes` (a diagnostics run holds the worker; stops unaffected) and `intifaceOldGenerationLevelNeverReachesTheNewToy` (deterministic, through the new hook `setVibrationForGeneration`).
  - The fakes got `device_index` (core and app) and `device_name` (core).
- **Not changed:** the stop budget (`kStopBudgetMs` / `kStopGraceMs`) and the Sam Neo 2 battery code (a failed GATT read already drops the value and is read at every tick). The Player keeps its hold while playing through a lost link (0.7 rule: pause first), and `PlaybackOutput::setDevice` moves the hold without stopping the old device. That is noted here, not changed.
- **Not verified:** on real hardware or a real Intiface Central (no hardware here). That the server echoes the request Id in `BatteryLevelReading` follows the Buttplug v3 spec and the fakes. A server that leaves the Id out would show no battery.

## Handover: `claude/samn2-review-fixes` (5 Nov)

Three code reviews found real bugs; each fix has a regression test that failed before it (E8 is a refactor: the old O(n x peaks) prominence is the reference in the test).

- **Engine:** E1 peak spacing from the guarded `sample_hz` (a zero median step made `1000 / 0` an int cast); E2 `TrackBetweenKeys` refuses `step_ms <= 0` (was an endless loop); E3 `GenerateRunner::closing_` (set under the mutex first in the destructor, `Start()` gives 0) plus the timing test now measures the destructor and a restart-from-handler test; E4 the `.samn` is written first, the `.funscript` second, O-markers last (they live in the `.funscript`), and both are removed again if a later write throws; E5 a throwing callback never ends the worker and adds no second terminal event; E6 `SignalOptions::profile` is normalised once (`ProfileAutotune` next to `ProfileWeich`); E7 `OnnxDetector::Detect` builds the blob inside the try and catches `std::exception`; E8 `FindPeaks` prominence by a monotonic stack.
- **Core:** C1 `number.h` (`ParseDoublePrefix`, `StrToDouble`, `FormatFixed`, from_chars/to_chars) replaces strtod/stod/`%.3f` in `.samn`, marks, funscript and clip prep; nlohmann's lexer writes the locale's point into the token text, so `samn_file.cpp` maps ',' back to '.'; `main.cpp` resets `LC_NUMERIC` after Qt; display-only `%.2f` in `device_mock`/`self_check` left alone. The locale tests (`tests/core/comma_locale.h`) look for a comma locale and else build `de_DE` with `localedef`; without one they skip with a message outside CI and fail in CI, so the CI Linux job installs `locales`. C2 a history line with a wrong-typed field is skipped; C3 `Settings::set_many` writes a copy and assigns after the write, invalid UTF-8 is replaced, the error names the reason; C4 `delete_custom_script` checks the script's name, not only the slug; C5 `funscript.cpp` uses `FlexTimeMs` (marks.h), a duration that is no time or beyond 48 h counts as not set; C6 no offset-change pauses after a cancel.
- **App:** A1 one `LibraryController` in `Main.qml`, passed as `library:` to both pages (standalone each keeps its own); A2 the three `QSKIP`s in `player_page_qml_test.cpp` fail in CI (`CI` set).
- **Follow-up (review of #157):** `number.cpp` `IsHuge` uses the sign of an exponent too long for a `long` ("1e-99999999999999999999" is 0, not inf); `delete_custom_script` may delete an unreadable script file whose slug matches the typed name (readable scripts keep the name check).
- **Not touched:** device/training controllers, fileio umask, Editor dirty-guard, FlowSlider, Intiface URL.

## Handover: `claude/samn2-hunt-leftovers` (5 Nov)

The last two open items of the data-safety bug hunt. The wide-env `config_dir()` item was already fixed on main (`cursor/config-dir-wide-env-635a`).

- **`picture_signals`:** one entry was pushed per decoded frame that crossed a step, so a clip slower than the step (or a gap or start offset) made `luma[i]` not the picture at `i * step_ms`. Now the sample sits at index `ms / step_ms`; skipped steps hold the last picture with motion 0. Test `a clip slower than the step keeps one sample per step on the time axis`.
- **`repair.cpp` splice:** a failed window only partly verified by the retry was blanked in full, wiping the samples just spliced in, against the "verified part is kept" comment. New `BlankUnverified` blanks only from the verified reach. Test `a window the splice only partly verified keeps its verified samples`.
- **Not verified:** no end-to-end clip that ends a retry mid-window; the helper is unit-tested only. `run_starts` still has no consumer.

## Handover: `claude/review-previous-work-jnjttn` — FuseTipPartners (1 Nov)

The missing half of `track_multi` (#145 recorded the extras' paths only).

- **Engine:** `FuseTipPartners` (`tip_distance.h`, 0.7 port): the smallest `TipPartnerDistance` to any included partner. `TrackBox` with `distance_stroke` feeds it the gold partner and every extra; an extra whose CSRT misses that frame is left out (0.7 F-006), a fixed one always counts, tip loss or no partner holds the last gap as before. `kTrackCacheVersion` 5.
- **Tests:** `tip_distance: FuseTipPartners…`, `track: a followed extra contact drives the distance stroke` (fails without the change), `track: extras leave the tip stroke alone without a distance profile`, `generate: a followed extra contact moves the Tf/Tj stroke`; clip `WriteStillTipAndNearingExtra`.
- **Not changed:** the UI still hides Tf/Tj (D-053), so only scripts with a distance profile see this; extras without a gold partner are recorded (overlay) but drive nothing.

## Handover: Integration `claude/review-previous-work-jnjttn` (1 Nov)

Owner 1 Nov: everything green goes to `main`. All ten open Cursor PRs (#143–#152) were green but based on `main` before #142 and each conflicted in this file, so Claude merged them in number order onto this branch (one merge commit each; the Cursor branches were not touched) and lands them with #153.

- **Code conflict, #145 against #143/#144:** `PathAt` (renamed from `TipPathAt` in #143) plus `SortedExtraPath` (#145); `kTrackCacheVersion` is **4** (#144 and #145 had both taken 3 for different changes; old cache entries are dropped once).
- **This file:** section-wise merge; the Next list and the Last update line rewritten by hand.
- **Review notes:** #145 adds an optional `trajectory.extras` to `.samn`, written only when there are extra paths (files without extras stay byte-identical; 0.7 ignores the key). No workflow, binary or skipped-test changes in the ten PRs.
- **Verified locally:** Linux, Qt 6.4 + OpenCV 4.6 (Ubuntu packages), full build and ctest (result in the PR #153 description).

## Handover: `cursor/beatbar-noscript-635a` (1 Nov)

Player no-script card → **From the beat bar** (0.7 CreateBeatBarScript). Editor writes `<video> (Beat bar).funscript` / `(Beat bar N)` (D-075 never overwrite); status "Reading the beat bar…". Generator `beatBarFor` opens the video and runs `strokeFromBeatBar`.

## Handover: `cursor/g4-expert-sliders-635a` (1 Nov)

G4b Expert tuning under Create → Advanced (smooth / spacing / prominence / RDP / max speed + Impulse mirror). Live postprocess probe still open. Stacks on G4a Advanced shell.

## Handover: `cursor/user-handbook-635a` (1 Nov)

In-app User handbook (0.7 `handbook.js` / PARITY §12).

- **Copy:** `app/qml/HandbookTexts.js` — 11 sections verbatim from 0.7.
- **UI:** `Handbook.qml` overlay (lilac accent via `Theme.laneSuction`): left section nav, scroll-synced active highlight, body, footer, Close / backdrop / Esc.
- **Wire:** Settings → General → *Open user handbook*; Create header *User handbook* + Project nav entry; `Main.qml` hosts one instance; Esc closes handbook before Settings; `overlayOpen` covers handbook + Settings.
- **Test:** `shell_qml_test::userHandbookOpensFromSettingsAndCreate`.
- **Still open Owner GUI leftovers:** idle ribbon (§11), donation pages (Manifest §2). Parallel open: G4a Advanced (#147), Training coach (#148), GAP-20 (#149), G3 feel probe / extras / polish.

## Handover: `cursor/settings-broken-stamp-635a` (1 Nov) — GAP-20

Corrupt `settings.json` is moved aside as `settings.broken.<UTC YYYYMMDD-HHMMSS>.json` (`.2`… if that second collides). A second corruption no longer overwrites the first aside (LESSONS FF-07 / GAP-20).

- **Code:** `core/src/settings.cpp` `broken_aside_path`; header comment; tests keep-aside + second-corrupt keeps first.
- **Next:** Owner GUI leftovers (handbook §12, idle Flow graphic §11, donate); Generator G4b after `#147` greens; more GAP rows as small bug-fix PRs.

## Handover: `cursor/training-coach-neo2-635a` (1 Nov)

Owner training overview → only what Training already can use (device sessions). **Ignored:** gym/hip mobility flows, medical study dump, physical Squeeze of the glans, weekly strength calendar.

- **Built-in copy** (`core/src/training.cpp`): Stop-Start / Plateau / Breathing pace / Pelvic floor descriptions teach feedback 5–7, Interrupt at 8, out-breath, fully release (never press down), practice path.
- **UI** (`TrainingPage.qml`): Feedback card arousal scale rules; Safety stop-on-pain line; About path (Breathing → Stop-Start → Plateau) + 2–3×/week / 15–25 min / start low / lube; program blurb shows up to 5 lines.
- **Recap hints** (`training_controller.cpp`): many interrupts → Gentle/longer rest; avg feedback ≥8 → start lower; clean/low → raise one variable, keep 2–3/week.
- **Not changed:** engine timings, new presets, Player Extended-O (Play feature), diary schema (history/CSV already exist).
- **Next:** back to Generator G4a/G4b or Owner GUI leftovers — this tip is coaching only.

## Handover: `cursor/g4-advanced-shell-635a` (1 Nov) — G4a

Advanced settings shell on Create (closed by default). Engine hooks already existed; this wires the GUI + settings + queue.

- **UI:** Create → Advanced collapsible (`advancedPanel` / `advancedToggle`). Tracking: Invert, Camera compensation, Scene-cut (moved out of Feel). Signal: Sliding dynamics, Adaptive Keyframes. Motion axis combo Auto / Force horizontal / Force vertical.
- **Controller:** `slidingDynamics` → `dynamic_range_ms` 3000 / **-1** (explicitly off, profiles do not refill); `adaptiveKeyframes` → `adaptive_error` 6 / 0; `motionAxis` auto|x|y → `TrackAxis`. Remembered: `generator.sliding_dynamics`, `generator.adaptive_keyframe`, `generator.axis`. Wired into `create` and `runQueue` (`QueueSettings::axis`).
- **Tests:** `advancedShellChoicesAreRemembered`, signal_chain keeps `dynamic_range_ms=-1` under weich/autotune/recommended, QML opens Advanced before Invert.
- **Not in this PR (G4b+):** Expert numeric sliders + `PreviewPostprocess`, Auto-Retry / P3.6, rhythm grid, scene map, MT-Debug trajectory overlay.
- **Next for Generator:** G4b Expert sliders + live postprocess probe; then FuseTipPartners / MT-Debug; Owner GUI leftovers (handbook, idle ribbon, donate). Owner merges open green PRs (#142–#146 as listed).

## Handover: `cursor/g3-feel-probe-635a` (1 Nov)

Generator G3 Feel probe — live Sensitivity / Curve preview before Create.

- **Core:** `samn::PreviewContactVibration` / `SyntheticContactBounce` (`contact_preview.h`): fixed ~3 s bounce through `ToIntensityCurve` with contact vibration; returns active%, peak, stroke/vib polylines, peak dots (≥15).
- **UI:** Feel card under Curve — badges, 72 px Canvas (orange stroke, cyan vibe), hint. `GeneratorController::contactFeelProbe`.
- **Tests:** `preview: span earlier…`, `preview: impulse is quieter…`, `contactFeelProbeFollowsSpanAndCurve`.
- **Still open:** Player Inspector feel probe / Save to script; G4 advanced; FuseTipPartners; MT-Debug trajectory line; handbook / idle ribbon / donate URLs.

## Handover: `cursor/track-multi-extras-635a` (31 Oct)

Generator extras multi-path (`track_multi`) — magenta contact areas get their own centre paths.

- **Schema:** `TrajectoryData.extras` = `vector<vector<TrajectoryPoint>>` (index matches `contactMarks.extras`); encode/decode/Same/ShiftTrajectory.
- **Engine:** `TrackOptions::extra_boxes` → CSRT or fixed centres → `TrackResult::extra_xs/ys` → `BuildTrajectory` writes `.samn` `trajectory.extras`. Track cache v3.
- **Player:** `extraAt(i, ms)`, `hasExtras` hint; overlay follows each extra's own path (falls back to partner for old scripts). Generator status "follows own path" when Stay fixed is off.
- **Tests:** `track: fixed extra…`, `generate: non-fixed extra path…`, player `hasExtras`/`extraAt`, SortedExtraPath.
- **Still open:** FuseTipPartners (multi-partner distance stroke); MT-Debug trajectory line overlay; donation pages / handbook / idle ribbon; G4 advanced.

## Handover: `cursor/samn2-gen-fixes-635a` (31 Oct; was `claude/samn2-gen-fixes` / #140)

Takeover of Claude's Generator bug-hunt PR (CI was green; conflicted with #141 on `AGENT_STATE.md` only). Fixes, each with a test that failed before:

- A hand-drawn box started one frame late: `FrameReader::SeekMs` tolerates the half millisecond a rounded frame time carries; the controller floors the preview time. Test `frame reader: a frame's rounded millisecond seeks to that frame` (24/30/60 fps).
- Auto scene follows the primary area's class (not the default for new marks); `setAreaClass` emits `feelChanged`. Test `theAutoSceneFollowsThePrimaryAreaClass`.
- `GenerateRunner`: running ends before the terminal event, so its handler can `Start` the next run (a worker restarting itself is parked and joined later). Test `generate: the terminal event's handler may start the next run`.
- `Coast::Reanchor`: a reacquire jump is no longer a coast velocity. Test `parts: a reacquire jump is not a coast velocity`.
- Queue: two videos with one script name (`x.mp4`, `x.mkv`) fail the later one with "same script name as …" (`ScriptNameCollision`). Tests in `generate_queue_test.cpp` and `twoVideosWithOneScriptNameDoNotOverwriteEachOther`.
- Track cache key covers `adaptive_cuts` and the seed; `record_checks` gives no key; `kTrackCacheVersion` 3. Test `spec FR-11: the cache key covers ...`.
- `PositionsToActions` with `scene_ranges`: samples outside every range stay within 0..100. Test `post: samples outside every scene range stay within 0..100`.

**Supersedes open PR #140** once this merges.

## Handover: `cursor/code-gui-polish-635a` (31 Oct)

Code clarity + Owner GUI polish (26 Oct: dark oval behind empty Play; Support/donate leftovers).

- **Empty home:** removed the wide stage oval; soft **round** bloom only behind the Play; Play face a lighter navy (less black disc).
- **Code:** `TipPathAt` → `PathAt` (tip and partner); `SAMN_UPDATE_URL` uses `fromUtf8` (not `fromLocal8Bit`).
- **Support (Settings → General):** appeal text, GitHub button, "Donation page coming soon." (`supportSection` / `donateComingSoon`); shell test extended.
- **Still open:** real donate URLs (D-022, Owner), handbook, idle ribbon, `#140` merge, `track_multi`.

## Handover: `claude/review-previous-work-jnjttn` (1 Nov)

GAP-12 leftovers from the quality list of #142.

- **Tests only** (`tests/core/script_quality_test.cpp`): four cases assert `DeviceCompatMetrics` values: slow full strokes (50+ in more than 900 ms; exactly 900 ms and a 49 move do not count), the warning needs more than 25 % of the large moves, the 5-95 count (5 and 95 are usable), average and 95th-percentile intensity (a zero-length step adds no weight), and "too close" stays strictly under 100 ms. Every expected number was checked against 0.7 `funscript.EvaluateDeviceCompat` with a throw-away Go probe; the C++ code needed no change.
- **Verified:** core build without Qt/OpenCV (Linux, GCC), `core_tests` 434/434 green.
- **Still open for quality:** Quality Doctor (P3.6), limits enforced on output (not only reported); MT-Debug trajectory line overlay; `track_multi`.

## Handover: `cursor/config-dir-wide-env-635a` (31 Oct)

Code quality from the data-safety bug hunt's open item: `config_dir()` used narrow `_dupenv_s` for `%APPDATA%` / `%USERPROFILE%` / `SAMN_CONFIG_DIR`, so a Windows user name outside the ANSI code page pointed settings at the wrong folder.

- **Fix:** `_wdupenv_s` → `fs::path` for those vars (`core/src/settings.cpp`).
- **Tests:** Linux non-ASCII `SAMN_CONFIG_DIR`; Windows wide `SAMN_CONFIG_DIR` and `APPDATA` (`settings_test.cpp`).
- **Still open for quality:** ~~Script Doctor metrics assertions (GAP-12 leftovers)~~ (done 1 Nov); Quality Doctor (P3.6); MT-Debug trajectory line overlay; `track_multi`.

## Handover: `cursor/player-partner-overlay-635a` (31 Oct, merged #141)

Player partner overlay — the missing half of G2b's contact marks on Play (AGENT_STATE "Next after Tf/Tj").

- **Core:** `SortedPartnerPath` next to `SortedTipPath`; `TipPathAt` works on either sorted path. Test `partner path sorts like the tip path…`.
- **Player:** loads `trajectory.partner` into `partnerPath_`; `partnerAt(ms)` for QML; `contactMarks.hasPartner` and the hint adds "partner path" when samples exist.
- **Overlay:** non-fixed gold / magenta boxes centre on the partner sample at the playhead; fixed boxes and scripts without a partner path stay put (still not following the tip — that looked wrong). Tip stays on the tip path.
- **Still open from that tip:** extras multi-path (`track_multi`); MT-Debug trajectory line overlay (tip accent + partner teal); donation pages / handbook / idle ribbon (Owner GUI leftovers).


## Handover: `claude/samn2-data-safety` (30 Oct)

Data and file fixes from a read-only bug hunt. Each fix has a test that failed before it, with three exceptions: the Windows path tests (they can only fail on Windows), `main.cpp` and the atomic custom-script write (no test).

- **Editor (`editor_controller`):**
  - The .samn's `general` (and the input of `BakeNeoAxes` and the sound tool) is the stroke as played: an OFS `"inverted": true` funscript's flag is baked in (`playedStroke()`); the .funscript keeps its raw actions and its flag. Test `anInvertedFunscriptPlaysTheSameFromTheSamn`.
  - `open()` and `close()` cancel a running tool, and a result for an earlier script is dropped (`script_gen_`). Test `aLateToolResultNeverLandsInTheNextScript`.
  - *Optimize* drops the healed tracking gaps from the funscript metadata and the .samn with the same save. Undo brings the stroke back, not the gaps. Test `optimizeClearsTheHealedTrackingGaps`.
  - A .samn that loads gives the scenes, also when it has none. Test `theSamnScenesCountEvenWhenEmpty`.
- **Player *Bake offset*:** moves O-markers, the loop marker, the companion axis files, tracking gaps, scene and phase ranges, `samn_axes` and the tip path, in the .funscript's metadata and in the .samn. New core API: `ShiftOMarkers`, `ShiftDocumentMarks`, `ShiftedFunscriptMetadata`. Everything is read and moved first and written last, and a failed write puts back the files written before it. `sceneMap` is not moved: the app never reads it. Tests: `bakeOffsetWritesItIntoTheScript` (extended), `aFailedBakeChangesNoFile`.
- **Paths on Windows:** `ProjectPathFor`, `LoadScriptMarks` / `SaveScript*` and `PreferSamnCompanion` use UTF-8 (`u8string`, `u8path`); error messages no longer call `path::string()`. Tests `ProjectPathFor keeps a non-ASCII path` and `script marks: a .samn under a non-ASCII path`. `main.cpp` takes the start paths from `QCoreApplication::arguments()`.
- **Edit session:** a point between neighbours under 2 ms apart stays between them, so the axis stays sorted. Test `edit: a point between neighbours on one time keeps the curve sorted`.
- **Label editor:**
  - An unreadable `<clip>.labels.json` is moved to `<clip>.labels.broken.json` (`.broken.2.json` … when that name is taken) before anything is saved; the status names it. Test `aBrokenLabelsFileIsKeptAside`.
  - Training sets are `<stem>-<8 hex of the path's SHA-256>` (`LabelController::trainingSetName`). A clip without a name is refused. Test `sameNamedClipsKeepTheirOwnTrainingSets`.
  - Sets added before this change keep their plain names; re-adding such a clip makes a second set (remove the old one with × on the AI Train page).
- **Smaller fixes:**
  - `ChatContent` handles an error message that is not text (test `teacher: an error message that is not text`).
  - `TrainController::train()` ends a run whose Python cannot start (test `aPythonThatCannotStartEndsTheRun`).
  - `save_custom_script` writes atomically.
  - Notices compare the shortened text, so a long repeat is one toast (test `aLongRepeatIsOne`).
- **Open (not fixed here):** ~~`samn::config_dir()` narrow `_dupenv_s`~~ → fixed on `cursor/config-dir-wide-env-635a` (`_wdupenv_s`).


## Handover: `claude/samn2-stop-safety` (30 Oct, device and playback safety, merged #138)

- **Stop before close (Sam Neo 2):** a disconnect, a quit and Training's end set `abort_` before the worker's stop ran, and the BLE "with response" write gave up at once, so the device stayed on. `DeviceWorker::Stop` and `DestroySession` now switch the transport to a cancel of their own (`BeginStopWindow`, bounded by `kStopBudgetMs` = 6.5 s); `abort_` still ends connects and reads. TCode (serial / TCP / UDP) and Intiface sends never waited on the cancel. Tests: `device_controller_test` `samNeo2*` on a stand-in link (`setBleTransportFactory`).
- **`SamNeo2Device::StopNow`** tries both off writes, remembers both off packets either way (the keepalive repeats "off", never the old "on") and returns the first error; `Disconnect()` uses it. Tests: `device/samneo2: a failed off write ...`, `... disconnect tries both off writes ...`.
- **Stroke speed limit:** with a limit, a move takes at least the way from the last sent position at that speed (real ms, after the play-speed division); the home move counts the whole range. Test: `playback_output_test::speedLimitHoldsForLateReports` (1x, 2x).
- **Intiface battery:** a failed read (no reading, or the server's `Error` for the request, which now ends the wait) is cached `kBatteryCacheTtl` like a reading; before, every 2 s tick blocked the worker 3 s and a Stop queued behind it. Tests: `device/intiface: a failed battery read is cached too`, `device_controller_test::intifaceMuteBatteryDoesNotBlockTheStop`, `intifaceBatteryErrorEndsTheWait`.
- **Player:** leaving a playing script-only session (opening a video) calls `ctrl.setPlaying(false)` (`PlayerPage.qml::onScriptOnlyChanged`). Test: `player_page_qml_test::leavingAPlayingScriptOnlySessionStopsTheOutput`.
- **Session per owner:** `DeviceController::setSessionActiveFor(owner, on)`; playback and training each hold their own, `sessionActive` stays true while any holds it (the QML property's writer is a hold of its own). Tests: `device_controller_test::sessionHoldsArePerOwner`, `playback_output_test::sessionStaysActiveWhileAnyUserDrivesTheDevice`.
- **Extra axes** (twist / roll / pitch ...) ease their first move after a start or a seek in like the stroke, per axis from its last sent position. Test: `playback_output_test::extraAxesEaseInAfterAStartAndASeek`.
- **Not verified:** on a real Sam Neo 2 (no hardware here); the stop budget is a guess from the write timeout. The speed limit counts from the last *sent* position: a stroker still on its way to that position can move further than that.


## Handover: `claude/samn2-health` (28 Oct, D-162)

- **Done:** `app/src/system_check.{h,cpp}` (adapter list through DXGI / `/sys/class/drm`, `ClassifyGpus`, `PickTrainingGpu`, eight checks in `RunChecks`, `Report`, `SystemCheckController`); Settings tab *System* (between Device and Log, so Log is tab 3 and About 4); `Main.qml::startSystemCheck()` called once by `main.cpp` after the window is shown; `TrainController` names the NVIDIA card in its *Ready* message; `tests/app/system_check_test.cpp` and a System-tab step in `shell_qml_test`.
- **Verified so far:** Windows CI builds the DXGI path with MSVC and runs the tests (#137 green); it has not run on a PC with two adapters yet (the Owner's). On Linux only AMD reports its memory (sysfs); NVIDIA and Intel show 0 MB there, so the NVIDIA pick on Linux is by vendor, and an Intel Arc is shown as integrated.
- **Env state:** "failed" means a `train-env` folder without `samn-ready.json` (no failure marker is stored); the check has no other source for it.

## Handover: `claude/samn2-generator-spec` (26–27 Oct, merged as #132; open points below)

- **Done on the branch:** `docs/GENERATOR_SPEC.md`; opt-in gap handling, MAD outliers, end time, analysis-rate cap, raw-track cache, aliasing note (D-158); Generator toggles *Bridge short tracking gaps*, *Ignore single-frame jumps*, *Reuse the tracking*; `tests/engine/spec_generator_test.cpp` (one case per spec test id); MSVC 17.14 fix in `tests/core/device_protocol_test.cpp` (raw string moved out of `CHECK`).
- **Local Windows run (MSVC 17.14, Qt 6.7.3, vcpkg OpenCV 4.14):** build green; core, vision, identifiers and every app test up to `label_controller_test` green. Red:
  - `train_controller_test` (not touched here; check whether it fails on `main` too).
  - New spec cases with real findings:
    - **T4:** with 30 % of a 60 fps track dropped, some extrema are 50–66 ms off; the tolerance is 40.
    - **T5:** a still disc gives a script instead of the "no discernible motion" error; CSRT wobble is read as motion.
    - **T6:** CSRT does not report the vanished disc as lost (LESSONS GD-11), so the gap gets actions; gap handling needs a loss signal beyond the CSRT result (appearance-memory score).
  - Two test bugs fixed after the run (T10 overwrite guard, FR-7 progress bound); not re-run yet.
- **Owner's clip (26 Oct, private, never in the repo), findings and fixes on this branch:**
  - The Generator tracked from 0 with a box drawn at the shown frame; Find tip also searched from 0. Now both start at the frame the box belongs to (`box_ms_`, 0.7 startTimeSec).
  - 0.7 used the horizontal axis on that clip (its tip x vs its script r up to -0.86, y about 0). We picked y because a few coasted / compensated frames threw the centre out of the picture; the axis is now chosen from the 2nd..98th percentile spread.
  - 0.7's scene-cut test fires on about one frame in eight of a 256x144 clip (2004 cuts). A spike-relative test removed them but made tracking worse: the false cuts re-anchor CSRT through the appearance memory, as in 0.7. Kept as 0.7 for now; a better re-anchor comes with the D-159 tip work.
  - `samn_compare` manifests take `start_ms`.
- **Qt 6.4 crash (27 Oct, local only):** after main came in, `shell_qml_test` crashed (SIGSEGV in the qmlcachegen-compiled `Icon.qml` binding `"?" + color`) while the AI Train page loaded next to the three new Generator switches; it passed with `QML_DISABLE_DISK_CACHE=1`. `Icon.qml` now builds `#rrggbb` by hand.
- **The "gaps" on the Owner's clip are mostly right (27 Oct):** of the 23 stretches over 3 s where *Recommended* writes nothing, 20 hold only filler in the 0.7 script (a point every ~100 ms, mean amplitude 0–5 of 100, from 0.7's Fill). Real strokes there are only at 335–353 s (amplitude 33–65), 415–419 s and 829–837 s. Window r against that script over flat windows says nothing; compare on stroke windows, or against a checked reference (D-135). On the 11 ten-second windows where the 0.7 script strokes (mean amplitude ≥ 20), every one has strokes from us; median |r| 0.666 (Autotune) and 0.631 (*Recommended*, its 500/s limit differs from 0.7's 400).
- **Owner rule for the curves (26 Oct):** stroke and suction curves must swing smoothly (smoothed, no corners or jumps); vibration is different (pulses and steps are allowed). Applies to the D-159 defaults and Auto.
- **Next, in the Owner's order (D-159):**
  1. **Defaults done (D-160, 27 Oct):** the profile *Recommended* (`ProfileRecommended`, first in the style list and the default) is Autotune plus outliers 5 × MAD, speed 500/s and 60 ms spacing (100 ms on a stroker); the spec's 120 ms smoothing, 4 s detrend and gap handling measured worse and are not in it. Measuring harness: a raw track CSV through `RunSignal` variants against 9 synthetic tracks with known strokes and the Owner's clip (scratch, not in the repo). **Auto postponed (D-161):** a tempo-adaptive prototype gained 0.676 → 0.696 on the clip and nothing on the synthetic tracks; the Owner chose tracking first. Gap handling is back and on by default, from the tracker's own loss only (`kLostTracker`; the look check `kLostUnlike` counts only with `gaps_from_look`).
  2. *Tip automatically* — **first step done (28 Oct, `claude/samn2-auto-tip`):** opening a video starts Find tip by itself (switch *Find the tip by itself*, `generator.auto_tip`, on); opening a script for editing from the Player does not. A clear find becomes the box; an unsure one (top score < 0.05) only shows all its candidates (up to 6, not D-159's 2–3) to click; a box the user draws meanwhile stays. While the automatic search runs (`autoFinding`) the button still reads *Find tip*: a click takes the search over when it looks from the shown frame (its result then counts as the user's, a weak one also becomes the box), else restarts it from there; only the user's own search shows *Stop finding*. Create stays enabled: with a box it ends the search, without one it waits for it. *Create queue* ends it too (refused with a status line during the user's own search). A video opened while the previous video's search still ends gets its own search once that one has stopped (also the Player's *Create script*, which then creates). A search ended by Cancel, Create or the queue sets neither box nor status. Measured: on the ten synthetic bench scenes (TestMedia branch `claude/next-project-prep-o1ckxh`) the first proposal tracks best or equal-best in 9 of 10 (r 0.84–0.99; the scene-cut clip 0.33); on the Owner's 256×144 clip it lands within 30 px of 0.7's tip in 5 of 11 stroke windows, a candidate in 8 of 11. **Still open, needs the Owner's real clips:** better ranking on real material and the detector's confirmation. Original plan:  the rhythm grid over the whole frame (GD-15) finds where the stroke is, the detector confirms it, CSRT follows. A box is drawn only to override.
  3. Extended camera stabilisation: all four similarity parameters, a RANSAC inlier gate, keyframe and two-pass smoothing after OpenCV videostab, and a phase-correlation fallback.
  4. AV1: the Player side is on `main` (#131); the Generator's vcpkg FFmpeg has `dav1d` since 28 Oct (`ci/vcpkg/vcpkg.json`). Still open: DirectML for ONNX Runtime.
  5. Black-box comparison with the other generator on synthetic probe clips and the Owner's clips through `samn_compare`.

## Rules

- **Owner:** talk in German; the code, the interface and the docs are in English.
- **Branches:**
  - Claude works on `claude/*`, Grok on `grok/*`, Cursor on `cursor/*`. Never push to another agent's branch.
  - Nothing reaches `main` without a PR.
  - Merge only on green CI.
  - Never force-push.
  - **Routing / handoff:** one theme per agent; push often so another agent can take over (Manifest §6, D-122).
- **Model routing:** agents may route work across model tiers with the claude-router skill (`.claude/skills/claude-router`, policy `CLAUDE-ROUTING.md`); a lane's result is verified by the orchestrator before it is accepted.
- **Before every push:** run `git diff --stat` and check it. Never write a file from a shell substitution that may be empty, and never regenerate a workflow from scratch (see `ROADMAP.md` § Lessons).
- **Decisions:** look in `DECISIONS.md` first. Never restart a decided question; record new decisions there.
- **Never:**
  - adult media in the repo or in screenshots; test patterns only;
  - model identifiers in code, docs or commit messages;
  - GPL-2.0-only, BSL, EULA or proprietary code (GPL-3-compatible code is fine with its header kept and a `THIRD_PARTY.md` line, D-141);
  - placeholders that pretend to work;
  - tests that "SKIP" with exit 0 in CI.
- **Releases:** the Owner pushes tags.

## Done

- **P0** (#6): the foundation and CI.
- **P1 Python player** (#7): the reference port, now removed. Its tests live on as the C++ tests.
- **C++ pivot** (`claude/samn2-cpp`, Owner 16 Oct: C++ first, QML, every 0.7 function, the 0.7 look, a better program):
  - **Core:** all P1 logic is in `samn_core` (funscript, `.samn`, mapper, marks, script finder, settings, playback, stroke filter, beat bar), with 50 doctest cases. `.samn` and the marks files are byte-identical with Go.
  - **QML shell:** the Flow look with the logo, light ribbons, mode pill and animated mode deck (keys 1–5, Esc, M), and the Player page with video, gradient progress, the animated Play ring, the curve (glow, level of detail, playhead) and the offset.
  - **CI:** Qt 6.7 on Linux and Windows, ctest, parity tests, and an app smoke test (start, load, render, quit).
  - **Docs:** `DECISIONS.md` (126 decisions, with the conflicts resolved), `ROADMAP.md` (0.7 feature inventory → phases, lessons) and `PLAN.md`.

- **P1 Player UI** (`claude/samn2-p1-ui`, Owner: an exact functional copy of 0.7, better where we can):
  - `PlayerController` on the core: video first with script folders and variants, `.samn` preferred, lanes, heatmap, chapters and bookmarks, offset per script, A/B loop, zoom window (wheel and buttons, 0.7 steps).
  - Curves as 0.7 draws them (`curve_geometry.h`): Catmull-Rom line, glow, fill, dots, live dot, tempo wave when dense. Vibration and suction lanes ("device preview" when the script has no such channel). Audio Level lane decoded by Qt (no ffmpeg).
  - Styled controls, the no-script card, error bar. A broken QML file now fails the smoke test (exit 1).
  - Tests: 60+ core cases, 13 app Qt tests (controller, zoom, decoder with a generated WAV, curve modes and layer cache).

- **P1 Player part 2** (`claude/samn2-p1-player2`): time ruler; heatmap in the timeline (0.7 colours, 300 buckets, zoom; click = seek, drag = mark); marked range saved beside the script (0.7 `.marker.json`), loop over it, A / B, zoom selection; hover tooltips; live Vibration / Suction values; speed, sleep timer, mute, ±10 s; 0.7 keys (`,` `.` ±200 ms, +/- offset 10 ms, L, 1–9). Core: one atomic file writer (`fileio.h`) instead of three copies.

- **P1 marks** (`claude/samn2-p1-marks`): bottom tabs (Timeline · Chapters · Bookmarks · Scene Map · Audio Analysis); add / rename / remove bookmarks, chapters (from the marked range) and O-markers, saved like 0.7 in `.funscript` and `.samn` (`SaveOMarkers` same bytes as Go); O-marker bands and beat grid on curve and heatmap; video bar (link video, bookmark, full-resolution snapshot); Audio Analysis with the whole video's sound. `PARITY.md` ticks started.

- **Library and Inspector** (`claude/samn2-library`): the 0.7 v1 three-column Player. Library: Local Files, Recent, Favorites (★), Scripts imported / generated / edited, Analysis shortcuts, drop zone; kept in the settings. Inspector: Devices (live channels), Audio (BPM), Playback (offset), View (audio lane, O-markers). Transport and zoom row in 0.7 order. Settings are now safe with several objects on one file.

- **Device core** (`claude/samn2-p2-device-core`): the transport-independent device logic of 0.7 in `samn::device` (`core/include/samn/device/`, `core/src/device_*.cpp`): Sam Neo 2 and legacy packets, channel bookkeeping and keepalive, TCode, Buttplug/Intiface messages and state machine, capabilities, battery, diagnostics, mock device and mock transport. `Transport` is the seam for Qt Bluetooth / serial / UDP-TCP / WebSocket (not written yet). Go tests ported as `tests/core/device_*_test.cpp`; strings, packets and JSON were diffed against the Go code and are identical. Devices are not thread-safe: the app calls one device from one thread and drives `TickKeepalive()` (every 2 s), `Ping()` (every 2 s) and `PumpIncoming()` itself.
- **Device I/O** (`claude/samn2-p2-device-io`): the real connections for the device core. `app/src/device_io/`: Qt Bluetooth LE (Sam Neo 2: scan by name prefix, ffe0 / ffe1, write with / without response, battery read, peer info), TCode over UDP / TCP and USB serial (115200 8N1, `SerialPorts()`), Intiface over Qt WebSockets. `DeviceController` (`app/src/device_controller.*`, QML element, no QML UI yet) runs all devices on one worker thread: connect / disconnect by kind, status, last error + hint (Intiface / TCode), battery, capabilities (channels and axes), levels (coalesced), Stop / emergency stop, diagnostics (streamed entries, cancel), serial port list, the 2 s keepalive / ping timer and incoming pumping, lost-link handling, 0.7's `sessionActive` rules and remembered choice (`device.*` settings). Tests: `device_transports_test` (17) and `device_controller_test` (27), headless, against local TCP / UDP / WebSocket fakes (Buttplug shapes of `tests/core/device_fake_buttplug.h`); BLE only as "no adapter fails cleanly". CI installs qtserialport, qtwebsockets, qtconnectivity. Real BLE hardware is untested. Transport contract worth knowing: `IsConnected()` stays true after the peer closes until `Receive()` has reported it (like `MockTransport`), because `IntifaceDevice::PumpIncoming` finds a dead socket through `Receive`.
- **Project and range tools** (`claude/samn2-project-tools`): core ports of 0.7 `project.go` and `heatmap_png.go`: `project.h` (sidecar, byte-identical with Go), `range_edit.h` (snap, delete, cap speed, scale, soft-edge scale), `heatmap_image.h` (RGBA buffer, pixel-identical with Go; the app encodes the PNG). Base for the Editor's range tools and the Player's project file; not yet used by the app.

- **Device UI and playback output** (`claude/samn2-device-ui`): the Player drives a device as 0.7 did.
  - `Main.qml` holds one `DeviceController` and passes it to `PlayerPage`, `PlayerController` (property `device`) and the Inspector.
  - `PlaybackOutput` (`app/src/playback_output.{h,cpp}`, owned by `PlayerController`, no QML timers): at every video position report it sends vibration and suction (mapper lanes, times the channel level), the next stroke action for strokers (stroke filter, safety range, soft seek) and the extra TCode axes the script carries. Pause, stop and a 1.5 s silence stop the device and, with "Move home on pause", move the stroker to the bottom in 1.2 s. `sessionActive` is true while playing. The Emergency stop drops queued levels, stops at once, pauses the video and keeps the output silent until play starts again.
  - Inspector → Devices: transport choice with the saved choice, target / serial port, Connect / Disconnect, state, name, battery, error with hint, Emergency stop, chips, live channels, Output (on / level), Stroker (safety range, motion, speed limit, fill pauses, home on pause; only for linear devices), Function test (only when nothing plays).
  - D-132: `showStrokeLane` / `showVibrationLane` / `showSuctionLane` on `PlayerController`; with a connected device the lanes and their names follow what it can use, without one they show what the script carries.
  - Tests: `playback_output_test` (20, Mock device and a UDP sink for TCode), `inspector_qml_test` (loads the Inspector without QML warnings; screenshots with `SAMN_TEST_SHOT`).
  - Soft start of the levels (`playback.soft_start_ms`), Reduce peaks and Extended-O (manual, key E, and automatic in the marked range) are built: `core/include/samn/output_levels.h` holds the maths, `PlaybackOutput` applies it; Inspector Devices → Output and Playback → Extended-O.
  - Open: companion axis files into the player, the "Short test pulse", raw value test and diagnostics UI, the device shell graphic, the top-bar chip, connecting by itself at Play, the Tools / Device page.

- **Player additions** (#24, #25, #28–#31): chapter bands and "+" scene markers, Save / Load project and Heatmap PNG, play list (Add, Next, Auto-advance, Shuffle, Repeat), Script Doctor, silk waves and the breathing Play halo.
- **Old-repo knowledge** (#32, #34): CONTRIBUTING with the old and the Owner's rules; `LESSONS.md` (141 lessons, 32 rejected ideas, 27 gaps by risk), `TEST_PARITY.md` (all 1141 old tests mapped), `VERSIONS_AND_PLANS.md` (55 versions; removed on purpose vs. lost).
- **Old tests ported** (#33, #36, #39): stroke filter, beat strokes, beat bar, fileio, settings, 35 recipe / feel / contact tests, shutdown, identifier scan (ctest `identifiers`).
- **Gap fixes** (#35, #38): seek before load, names as plain text, English core errors, media errors in the error bar, multi-drop never silent.
- **Training** (#37): the 0.7 Training plus the level programs, Go parity of engine and write sequences, rings follow the device.

- **Player round, 17 Oct** (#40–#52): script-only playback with `ScriptClock` (#49), Previous / Stop / volume, the mode deck with keyboard focus, top-bar device chip and Open, window-wide drop overlay and Ctrl+O, media errors via `errorChanged`, output levels (#46), sanitizer job (ASan + UBSan), and the playback mapping settings with Effects presets, equal to 0.7's `StartPlayback` on 48 cases (#52).
- **Licence** (#53, D-141): everything GPL-3.0-or-later; the detection engine is a built-in plugin, not a separate program and not closed. `LICENSING.md`, `THIRD_PARTY.md`; the Owner's manifest is `docs/MANIFEST.md` (master document).
- **Detection engine, P3.0–P3.2 and P3.8** (#54): `vision/` (FrameReader with PTS, one CSRT factory, self-check); `engine/` (`samn_engine`): `TrackBox` bit-identical to 0.7 on 32 600 frames, the signal chain (post-processing, profiles, default detrend) giving the same actions as 0.7's `GenerateNativeCSRT` on 13 runs, and the comparison controller `samn_compare` (Motion Fidelity in core, SHA-256 manifest, windows and flags). Numbers in `docs/benchmarks/`. Core also got RDP (`simplify.h`) and O-zone suggestions (`ozone.h`).

- **P3.3 Generator = Editor, 18 Oct** (#55–#58):
  - **Generation:** `samn::engine::Generate` / `GenerateRunner` writes `.funscript` and `.samn` on its own thread. Anything thrown ends the run as an error, not the app. TSan job in CI.
  - **Generator page, laid out like the Player (D-144):** video with tip box and frame seek on top, curve lanes below, frame strip at the bottom, settings on the right. The result opens in the Editor; "Play in the Player" hands the `.samn` over.
  - **Editor:**
    - `EditSession` in core: undo as patches, transactions, hard history limit.
    - `EditorController`: drag / add / delete points, ruler range, range tools (delete, cap speed, softer, stronger, simplify, invert), undo/redo, zoom, FPS snap. Saves at once and keeps metadata (`PatchActions`).
  - **Frame strip:** `FrameCache` (open video plus a size-limited LRU, D-142) and the async `FrameProvider`.
  - **Create tools** (one undo step each):
    - vibration and suction from the stroke (`BakeNeoAxes`);
    - vibration from the sound (`sound_vibration.h`, 0.7 port);
    - stroke from the video's beat bar (`DetectBeatBarBeats`, streamed `BeatTimer`).
  - **Decisions:** D-143 (best component, licence follows) and D-144 (the Player plays, the Generator edits).
- **P3.3 continued, 19 Oct** (#59–#63 and this branch): the Player's theatre mode with the curve strip switchable; Find tip (0.7 `auto_roi`, bit-exact on 3 clips) with candidates; the queue (0.7 "Create queue"); the audit sync (`docs/AUDIT_2026-10-19.md`); **contact marks**: tip and contact classes, contact areas on the frame, contact vibration on by default; Generate writes the 0.7 recipe, `contactMarks` and the tip path (`.samn`), and the Player turns the path into spatial contact vibration (`core/include/samn/contact_marks.h`, 0.7 `feel_spatial` tests ported).
- **Scene Map in the Editor** (D-145): the mode card is gone (#65); the Generator's right panel has a "Scenes" tab (cards with frame, name, range, feel per scene, suggestion from `<clip>.scene.json` or the contact mark, Apply suggestions) and scene bands under the curve lanes (`core/include/samn/scenes.h`, `EditorController` scenes).

- **Look and shell, 19 Oct** (#76–#79): the painted wordmark (D-151); the Training stage Rings · Flow figure · Video (D-152); the support link and start card to the GitHub page (D-153); the README as a product page with screenshots (`docs/media/`).
- **Generator centre = Player** (#80, #83, D-154): the shared `VideoTransport.qml`, the Generator plays its video, the collapsible left column (`GeneratorProjectPanel.qml`: queue, recent videos, generated and edited scripts).
- **Optimize for** (#81, D-156): `core/include/samn/improve.h` (trim, fill gaps, heal tracking gaps), the Editor's *Optimize for* any device / Sam Neo 2, *Optimize after Create*.
- **Teacher on a model server** (D-155 step 4, first part, #91; Owner 20 Oct: servers first): `core/teacher.h`, `LabelController` teacher settings, *Ask the teacher* (`A`).
- **ONNX Runtime in the build** (D-155 step 4, 20 Oct, `claude/samn2-onnx-nudenet`): ONNX Runtime 1.22 from its NuGet package (FetchContent, pinned hash, `SAMN_WITH_ONNXRUNTIME`), beside every executable on Windows (`samn_copy_runtime_dlls`) and in the package with its licence; `vision/onnx_detector.h` (YOLOv8 decoding, NudeNet classes and their labels), tested on `tests/vision/data/tiny_yolo.onnx`.
- **Player/Editor P4** (26 Oct, `claude/samn2-player-editor-p4`): 0.7 lane / legend selection in the Player: a click on a lane or its name picks it for the main curve (its colour, its name on the curve), Up / Down on a focused name step through the shown lanes; the picked lane is highlighted; falls back to the stroke when the lane goes away or a new script opens (`PlayerController::activeLane` / `laneChoices`). Fixes a P1 regression: the Extended-O text button pushed Stop under the volume at 1400 px; it is a 36 px icon button now.
- **Player/Editor P3** (26 Oct, `claude/samn2-player-editor-p3`): 0.7 companion axis files beside the script (`<stem>.surge/sway/twist/roll/pitch.funscript`, `samn::companion_axis_files`) load with it, show as read lanes in the Player (0.7 colours, at least 34 px, only the axes a connected device has) and drive those axes (TCode L1/L2/R0/R1/R2). Lane selection / legend chips done; Curve select + "+ Axis…" on `cursor/plus-axis-635a`.
- **Player/Editor P2** (26 Oct, `claude/samn2-player-editor-p2`, Owner: "Werkzeuge kompakt"): the range tools are one row of icon buttons with tooltips (Delete, Cap, Softer, Stronger, Simplify, Invert) and the marked range; Optimize for, the whole-lane tools, "+ … lane", Vibration from the sound, Stroke from the beat bar, Soft edges and Snap moved into a "Tools…" menu (a tool closes it; the Cancel button shows the running tool's progress). Feel lanes keep at least 34 px (0.7), the stroke 60 px: at 1400×820 they were ~10 px. `IconButton` now shows its tip as a tooltip and names itself for screen readers.
- **CI: sanitizer apt cache** (26 Oct, `claude/ci-sanitizer-apt`): the ASan and TSan jobs ran out of their 30 min while apt was still downloading OpenCV from a slow mirror (#113, #119, #123). The `.deb` files are now cached (`actions/cache`, `~/apt-cache`), installed without recommends with 3 retries, and the jobs have 45 min.
- **Player/Editor P1** (25 Oct, `claude/samn2-player-editor-p1`): *Invert all* asks first (0.7 confirm); "+ Vibration lane" / "+ Suction lane" seed a missing feel lane with the stroke's times at 0 (`EditorController::addLane`); feel lanes draw the stroke ghost (alpha 0.3, 1 px); the Player transport gets the Extended-O trigger; PARITY §3 rechecked for these items (Previous / Stop were already built). The lane-height problem found here is fixed in P2.
- **Look vs 0.8 #550** (23 Oct Cursor `cursor/look-empty-open-polish-2c42` / #117; 25 Oct Claude takeover `claude/takeover-look-polish`): sharp Play/Editor curves (CurveItem line width, glow, fill; a 16 px hit radius for painting), Training rings, the empty Play + Open stand, Flow tabs, transport and Play-ring motion (`docs/LOOK_FROM_08_ASSESSMENT.md`). Every new loop honours reduced motion and the app being inactive; `Theme.animSlow` / `animAmbient` are 0 under reduced motion. The branch's encoded-upload workflow and `docs/.merge/` chunks were dropped (the chunks were corrupt and had truncated `generator_qml_test.cpp`).
- **Reduced motion** (22 Oct, `cursor/reduced-motion-60e6`, D-092 / Owner GUI order): Settings → General toggle; `Theme.reducedMotion` (`ui.reduced_motion`, `SAMN_NO_ANIM` for tests); silk, Play ring, mode-tab breathe, Training drift and DualRing decorative loops stop; `animFast`/`animNormal` become 0. Test `reducedMotionStopsDecorativeAnimations`. Finished by Claude (takeover) after Cursor paused.
- **Generator G2a** (20 Oct Claude `claude/samn2-gen-g2` / #111; 22 Oct Cursor takeover `cursor/samn2-gen-g2a-60e6` / #114): "+ Another contact area" (magenta) with "Extra type", Shift-drag and Shift-click on a candidate = contact, the mark just drawn is selected, "Selected: …" with its class, "Delete selected", Esc "Mark deselected.", "Clear extras", 0.7's lines after each mark. Windows CI: Shift via `keyPress` + `Qt.keyboardModifiers`; `QTest::mouseMove` 3-arg.
- **Generator G2b UI** (22 Oct, `cursor/samn2-gen-g2b-60e6` / #115): 0.7 "Partner fixed (pixels)" and "Stay fixed (extras)" toggles; `ContactMarkBox.fixed` in `contactAreas` and remembered (`generator.partner_fixed`, `generator.extras_stay_fixed`); status "stay fixed" / "follows partner path". **Still open:** engine `track_multi` / partner path recording (P3.4) so fixed=false actually tracks.
- **Generator Ignore region** (22 Oct, `cursor/samn2-gen-ignore-60e6` / #116): "+ Ignore region" (`markIgnore`); black dashed soft masks (`maskRois`); Create → `mask_rois` camera excludes. Tests `ignoreRegionsAreSoftMasks`, `track: mask rois…`.
- **Generator G2b engine (partner path)** (23 Oct, `cursor/samn2-gen-g2b-engine-60e6` / #116): gold contact → `TrackOptions.partner_box` / `partner_fixed`; second CSRT or fixed centres → `trajectory.partner` in `.samn` (tip stroke unchanged). Tests `track: fixed partner…`, `generate: non-fixed partner path…`.
- **Generator Tf/Tj distance stroke** (23 Oct, `cursor/samn2-gen-tf-tj-distance-60e6`): `TipPartnerDistance` (GD-08 2D hypot, near edge, overlap → 0); `TrackOptions.distance_stroke` when profile is Tf/Tj + gold contact → positions from tip↔partner gap; recipe / `drive_stroke` unchanged. UI still does not offer Tf/Tj (D-053). Tests `tip_distance:*`, `track: distance stroke…`, `generate: distance profile stroke…`. **Still open:** extras multi-path (`track_multi`), Player overlay follow.
- **Generator G1b** (20 Oct, `claude/samn2-gen-g1b`): the seek row while marking (Time (s), +1s, +5s), the "0–100 on video" chip and gauge on the preview; PARITY §3.8 (the Editor part of the Generator) checked against the code and the tests: drag, add, delete, clamp, save, undo / redo and the range tools are built. The Generator's help chips: Find tip, Time (s), Style, the four toggles, contact vibration, sensitivity, curve, mark contact, optimize after Create, the queue.
- **Generator G1** (20 Oct, `claude/samn2-gen-g1`, the everyday Create path as in 0.7): the lede and step prompts verbatim, the preview coach, Create without a tip finds it first, the done line, the quality box with the post-generate review (`core/polarity.h`), the Create card scrolls. Next slices: G1b seek row and help chips, G2 marking, G3 feel, G4 advanced, G5 review panel, G6 queue/drop, G7 AI assist, G8 `.sam`.
- **Help chips** (20 Oct, `claude/samn2-help-chips`): `HelpChip.qml` and 0.7's 189 `data-help` texts verbatim (`app/qml/HelpTexts.js`, `ci/help_texts.py`); placed in the Inspector, Training and Settings; the Generator's texts are ready for its slices (`help: "<0.7 id>"` on a row or card, `HelpChip { key: … }` elsewhere; `shell_qml_test::helpKeysInTheQmlSourcesExist` checks every key).
- **Error toasts** (20 Oct, `claude/samn2-toasts`): `Notices` + `AppLog::reportError` (0.7 notify.js uiError / showToast): failures toast for 8 s with *Open Log*, hints stay in the status lines (`PlayerController::setFailure` vs `setError`).
- **Training complete** (20 Oct, `claude/samn2-training-dual-ring`): 0.7's dual intensity ring (`DualRing.qml`) on the Figure stage; the script plan preview was hidden by 0.7.0 itself (#538) in favour of the Target curves, the timeline and the stepper, which SN2 has. PARITY §5: 100 %.
- **Cut a clip** (D-155 step 6, first part, 20 Oct, `claude/samn2-clip-prep`; Owner: in the app, ffmpeg with one click): `core/clip_prep.h` (times, 0.7's ffmpeg arguments and name, `marks.json`, the script cut to the window), `ClipPrepController` (ffmpeg from the imageio-ffmpeg wheel with pinned hashes or chosen, cut with progress and Stop, batch, *Copy command*), the card on the Benchmark tab; tested with a stand-in ffmpeg, checked by hand with the real one (`clip_prep_real_check`).
- **The student as a teacher** (D-155 step 5, 20 Oct, `claude/samn2-student-teacher`): *Our student* for A, a third teacher in the votes; local models in one worker.
- **Student, the app part** (D-155 step 5, 20 Oct, `claude/samn2-student-train`): `TrainController` (one-click setup with uv in `config_dir()/ai`, the training set, the run with progress and Stop, `models/student/`), *Add to training set* in the label editor, the Train card on the AI Train page; tested with a fake uv (a real venv with tiny torch and rfdetr) and a fake training script.
- **Faster start, loading screen** (26 Oct, `claude/samn2-fast-start`, Owner report "slow, builds piece by piece"): `Splash.qml` shows at once; the main window stays hidden until it is whole (`win.ready`), Generator and Training are built in background Loaders, and the corner ribbons are PNGs rendered once from their SVGs (rasterizing them took ~0.55 s at every start and they popped in late). Linux: shown after ~0.4 s instead of ~1.05 s. The log says "Startup: interface built in … ms, shown after … ms (… ms since the program started)"; the Windows package smoke test prints it as a notice.
- **Generator G3, "How it feels" follows the device** (26 Oct, `claude/samn2-gen-g3`, 0.7 gen_feel.js): *For device* (auto = the connected one, Sam Neo 2 without one) with *Show all*; the Sam Neo 2 scene, mix and *Vibration follows suction* go into the new script right after Create (`GeneratorController::applyFeel`); a vibrator can take its vibration from the sound after Create; a stroker hides contact vibration. Parity §4 40 %.
- **Video without picture** (26 Oct, `claude/samn2-player-no-picture`, Owner report): Qt's Windows FFmpeg has no AV1 software decoder (no `--enable-libdav1d` in Qt's FFmpeg options), so AV1 plays only on a GPU that decodes AV1, with no error. The Player now notices a video that plays without a frame for 1.5 s, says so in the error bar and on the stage, and offers *Convert to H.264* (`ClipPrepController::convertForPlayback`, `<stem>-h264.mp4` at its own size, scripts copied along, the copy opens). The empty Play button grows with the stage (96–140 px instead of 168). Next: ship an FFmpeg with dav1d (GPU first, CPU fallback), then the loading screen.
- **AV1 without a GPU decoder** (26 Oct, `claude/samn2-av1-dav1d`): 0.7 played AV1 everywhere (WebView2 brings dav1d), SamN2 did not (Qt's FFmpeg has no dav1d, on any Qt version). The Windows package now carries FFmpeg n7.0.2 (Qt 6.7.3's version and options) built with dav1d in CI (cached), in place of Qt's DLLs. AV1 decodes on the CPU with dav1d (FFmpeg prefers it over its hardware-only AV1 decoder); H.264 / HEVC keep the GPU. The package smoke test plays a synthetic AV1 clip (`tests/app/data/testpattern_av1_4s.mp4`, 27 KB) on the GPU-less runner and needs "Video: first picture (AV1)" in the log. Next: the health check (Owner 26 Oct: find every GPU, iGPU + card, for AI and video; report what is missing).
- **Student, first part** (D-155 step 5, 20 Oct, `claude/samn2-student-env`): `ai/train_student.py` (RF-DETR training with JSON progress and STOP, ONNX export; tested by `ai/tests`, run in CI), DETR heads in `vision/onnx_detector.h` (same results as RF-DETR's `predict`); the script ships in the Windows package under `ai/`.
- **Tracking again after a drag** (D-155 step 3, 20 Oct, `claude/samn2-retrack-after-drag`): the stretches on both sides of a drawn or dragged box are tracked again at once (switch *Track again after a drag*); runs are numbered, the last one counts.
- **Votes between teachers** (D-155 step 4, 20 Oct, `claude/samn2-teacher-votes`): `core/teacher_votes.h`; *Ask all teachers* (Shift+A) and *Teachers through the clip* in the label editor; a box only some teachers saw is an uncertain key.
- **NudeNet as a local teacher** (D-155 step 4, 20 Oct, `claude/samn2-nudenet-teacher`): one click fetches the PyPI wheel (both hashes pinned, `app/src/model_download.h`, zip reader on zlib), or *Choose a file…*; *Ask the teacher (A)* asks NudeNet here or the model server (`aitrain.teacher.kind`); the AI Train page has the NudeNet card and the choice.
- **Bug hunt, 20 Oct** (#93, #94 and `claude/samn2-bughunt2`): dead GUI paths found by matching every `Q_INVOKABLE` / `Q_PROPERTY` against the QML (Bake offset, `O` key, queue ×, *Clear all*, click a box + Delete in the Generator); the Player's keys only act while the Player shows; notes that promised features which already exist ("comes with P2/P3/P4") are now buttons that open the script in the Editor (*Edit this script*, *Open in the Editor*, *Open the scenes in the Editor*).
- **Issue #4** (#90): the codec as the decoder reports it, "codec unknown" otherwise (`samn::CodecName`, `FrameReader::fourcc`).
- **Tracking between keyframes** (D-155 step 3, #89): `engine/label_tracking.h`, `BlendTracks` / `FillBetweenKeys` / `NextUncertain`, editor `T` / `N`.
- **Phases** (D-157, #85, #86 and `claude/samn2-phase-feel`): playback shaping per phase (`.samn` `phase_ranges`, `ApplyPhaseShape` in `PlaybackFrames`, Editor *Phases shape playback*); the Training's video stage follows the film's phases (skip intro / empty, climax: pause by default); the phase lane in the label editor; rule-based suggestions (`core/include/samn/phase_rules.h`, `vision/include/samn/vision/picture_signals.h`, *Suggest phases*).
- **AI Train as a plugin mode** (#82 plan, #84, D-155, `docs/P4_AI_TRAIN.md`): the `Plugins` singleton and Settings switch, the fourth tab, the label editor (`label_track.h`, `LabelController`, `LabelEditor.qml`): classes with several objects, the contact point, *not in the picture* (X), *there but covered* (H), exclusion zones ("not this", `ExcludeZonesAt`), lanes, undo, export as images / YOLO labels / classes.txt.

**Parity (1 Nov):** recount with `python3 ci/parity_status.py` after handbook merges. Owner GUI order: toasts ✅, help chips ✅, reduced motion ✅, handbook ✅ (#150); idle ribbon ✅ (`cursor/idle-flow-stage-635a`); donate still open. Generator: G2/G3/partner on `main`; G4a Advanced landed via #153.

## Next (in order; the work list is `LESSONS.md` § Gaps, `TEST_PARITY.md` and `P3_GENERATOR.md`)

**Audio and phases (D-165), one slice per PR:** shared 11 kHz mono decode + `AudioAnalyzer` (spectral-flux onsets) → tempo from the onsets (autocorrelation, prior at 72 BPM, octave folding, confidence) → heart-rhythm default `base_rhythm.h` (72 BPM, 3 %) → Ellis beat tracking as the AU-04 fallback → speech / music / silence gate; cleanup list in the `claude/samn2-intensity-arc` handover.

**Kernel (D-163), one theme per PR, after the driver registry:** a ModeRegistry with a `Repeater` in `Main.qml`; a narrow `DeviceService` interface instead of `DeviceController*` in Player, Training and `PlaybackOutput`; `DeviceWorker` into its own file; registries for detectors and teachers.

0. **Generator:** G2 stack ✅ (#114–#116); Tf/Tj distance stroke ✅ (#119); G3 feel ✅; Player partner overlay ✅ (#141); extras multi (`track_multi`) ✅ (#145); G3 feel probe ✅ (#146); G4a Advanced shell ✅ (#147); G4b Expert knobs ✅ (#151, live postprocess probe still open); MT-Debug trajectory overlay ✅ (this PR); next G8 `.sam` / live probe / Quality Doctor.
1. **Look polish:** ✅ (#122, Claude takeover of Cursor #117; reduced-motion gates and a window-wide loop test added).
2. **Owner GUI leftovers:** handbook ✅ (#150); idle ribbon / Flow stage ✅ (`cursor/idle-flow-stage-635a`, Feel without film); real donation URLs (Manifest §2 / D-022) still Owner. Empty-Play dark oval ✅ (#143); Support card shows "Donation page coming soon." Reduced motion ✅ (#120). Broader look polish after the Generator still welcome.
3. **AI Train D-155:** script compare with labels, then golden-clip run (Owner clips).
4. **Windows CI flakiness** under ctest; diagnostics already run functions alone.
5. **Self-checking engine** (`docs/P3_SELF_CHECK_ENGINE.md`, merged with #133; **start with the handoff `docs/HANDOFF_2026-10-27_SELF_CHECK.md`**, tool `samn_selfcheck`): SC-1 sample gate, SC-2 checker signals recorded by `TrackBox` (`record_checks`) and SC-3 window verifier are built (`self_check.h`, `track_checks_test.cpp`; 83 engine tests). Measured in design §3.3: synthetic 13/13 bad windows fail, 0 false alarms; on the Owner clip the scene-cut test fires 1992 times and blocks the verifier. `AdaptiveSceneCut` (`adaptive_cuts`, off) takes the Owner clip from 1992 cuts to 2 but tracking gets worse (the false cuts were re-anchoring), design §3.4; on that clip the verdicts do not yet separate good from bad windows, so thresholds wait for the golden clips. SC-5 verified repair is built (`repair.h`, `repair_test.cpp`); on the Owner clip it repairs nothing at the strict setting because the look against the start crop cannot tell right from wrong there (design §3.5). Next in order: an identity check that works on real clips (continuity with the last good window, rhythm cell, detector) → speed (cached flow) → robust axis choice (principal axis) → wire `GateSamples` in front of the P3.2 chain behind a switch → SC-4 fast lane → SC-5 verified repair with YOLO-family re-anchoring → SC-6 golden gate. AI blocks the Owner asked for (YOLO, MediaPipe, OpenCV DL, Whisper, CLIP): roles and licences in §5a of the design.
6. **P3.6** Quality Doctor; Player finish / Svakom / Scene Map view; bug hunt; release.

### Handoff: self-checking engine (26 Oct, for the session that merges it all)
- **PR #133** (`claude/next-self-check-engine`): design + SC-1 + SC-3 + c9. CI was green on Linux, ASan/UBSan and TSan at the first push; check Windows and the push with c9.
- **Evidence:** synthetic scenes and bench scripts in the private test media, `synthetic/motion-bench-2026-10/` (branch `claude/next-project-prep-o1ckxh` there, not yet in its main). Owner clip `clips/owner-av1-256x144/` (main of the test media). AV1 does not decode in OpenCV's FFmpeg on Linux here; transcode to H.264 with `ffmpeg -c:v libdav1d` first.
- **Open questions for the Owner:** the claude-router skill (vimoxshah/claude-router) as `.claude/` files in this repo; installing it in `~/.claude` was blocked in the cloud session.
- **Old branch** `claude/next-project-prep-o1ckxh` here holds the closed PR #2 (old import); leave it, do not force-push.

### Handoff note (for the next agent)
- **Routing:** one theme per agent. Generator Tf/Tj distance on this tip; Look #117 parallel (do not steal Look files for feature work).
- Next after the 1 Nov integration: G4b live postprocess probe, MT-Debug trajectory line overlay (#160), Quality Doctor (P3.6); idle ribbon done (`cursor/idle-flow-stage-635a`).
- Do not push to `claude/*`. Push often; update this file every PR.

Owner reminders (16–17 Oct): the Generator is the Editor; detection better than 0.7 but measured against it; curves stay real curves (audio too); every function of 0.7 and all earlier versions and plans comes along, what was removed on purpose stays out (and why is in `VERSIONS_AND_PLANS.md`).

## Waiting on the Owner

- Nothing open from `DECISIONS.md` § Resolutions: one version, no Suite (D-127); the one free SamNPlayer (D-128); AI drafts opt-in and verified (D-129); no C# for now (D-131); everything open under GPL-3.0-or-later (D-141, which replaces D-128's "source private"). Repo visibility follows D-141 with the `LICENSING.md` checklist below.
- Golden clips for the generator (P3): run `samn_compare` locally with a manifest (video, box, reference, 0.7 output). They stay out of the repo.
- Before the repo goes public: the checklist in `LICENSING.md`.
- The v0.7.0 tag in the old repo.
