# Agent state

**Read this first** (new session, new agent, after a context loss), then [`MODULES.md`](MODULES.md), [`PLAN.md`](PLAN.md) and [`DECISIONS.md`](DECISIONS.md). Update this file in **every PR**, finished or not. **Push often:** the repo is always the current state.

**Last update:** 18 Oct 2026 (late), Claude, branch `claude/samn2-test-diagnostics`.

## Rules

- **Owner:** talk in German; the code, the interface and the docs are in English.
- **Branches:**
  - Claude works on `claude/*`, Grok on `grok/*`. Never push to another agent's branch.
  - Nothing reaches `main` without a PR.
  - Merge only on green CI.
  - Never force-push.
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

## Next (in order; the work list is `LESSONS.md` § Gaps, `TEST_PARITY.md` and `P3_GENERATOR.md`)

1. **Windows CI:** some app tests failed now and then with no output (stdout fully buffered when piped). Every app test now writes unbuffered (`tests/app/unbuffered_stdout.cpp`); find the cause on the next red run.
2. **P3.3 rest:** queue, auto tip-find, contact marks, Settings persistence; the Player's theatre mode with the waveform switchable (D-144).
3. **P3.6** Quality Doctor on the dense curve, the signal auto-retry and the stroke preview (feeds the adaptive detrend); then the controller reports Quality as its second number (GD-04).
4. **P3.4** two-point / multi-point, rhythm grid, follow marks; **P3.9** improvements, each measured with `samn_compare` (first lever: memory templates of the subject, not the box, GD-10).
5. Player finish (`PARITY.md` §1–3), donation pages (manifest), devices for Svakom and other toys, Scene Map, Settings / Tools; bug hunt; release.

Owner reminders (16–17 Oct): the Generator is the Editor; detection better than 0.7 but measured against it; curves stay real curves (audio too); every function of 0.7 and all earlier versions and plans comes along, what was removed on purpose stays out (and why is in `VERSIONS_AND_PLANS.md`).

## Waiting on the Owner

- Nothing open from `DECISIONS.md` § Resolutions: one version, no Suite (D-127); the one free SamNPlayer (D-128); AI drafts opt-in and verified (D-129); no C# for now (D-131); everything open under GPL-3.0-or-later (D-141, which replaces D-128's "source private"). Repo visibility follows D-141 with the `LICENSING.md` checklist below.
- Golden clips for the generator (P3): run `samn_compare` locally with a manifest (video, box, reference, 0.7 output). They stay out of the repo.
- Before the repo goes public: the checklist in `LICENSING.md`.
- The v0.7.0 tag in the old repo.
