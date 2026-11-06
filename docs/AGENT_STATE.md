# Agent state

**Read this first** (new session, new agent, after a context loss), then [`MODULES.md`](MODULES.md), [`PLAN.md`](PLAN.md) and [`DECISIONS.md`](DECISIONS.md). Update this file in **every PR**, finished or not. **Push often:** the repo is always the current state.

**Last update:** 6 Nov 2026, Cursor, branch `cursor/training-channel-caps-635a`: Training Max Vibration / Max Suction caps (handover below). Gen-curve #164 and idle Flow #162 merged. Claude is on `claude/samn2-playhead` (#163) — stay off playhead/playback_output.

## Handover: `cursor/training-channel-caps-635a` (6 Nov, Training output caps)

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
- **Player/Editor P3** (26 Oct, `claude/samn2-player-editor-p3`): 0.7 companion axis files beside the script (`<stem>.surge/sway/twist/roll/pitch.funscript`, `samn::companion_axis_files`) load with it, show as read lanes in the Player (0.7 colours, at least 34 px, only the axes a connected device has) and drive those axes (TCode L1/L2/R0/R1/R2). Next: lane selection / legend chips (P4), Curve select and "+ Axis…" for these files.
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
