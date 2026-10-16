# Agent state

**Read this first** (new session, new agent, after a context loss), then [`MODULES.md`](MODULES.md), [`PLAN.md`](PLAN.md) and [`DECISIONS.md`](DECISIONS.md). Update this file in **every PR**, finished or not. **Push often:** the repo is always the current state.

**Last update:** 16 Oct 2026, Claude, branch `claude/samn2-library`.

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
  - copied GPL, BSL or EULA code;
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

## Next (P1, in order)

1. `docs/PARITY.md`: the full 0.7 inventory (every function, file:line), the checklist for "exact copy". A first attempt stopped at a usage limit; redo it.
3. BPM grid, O-marker bands, feel segment bands, speed highlights, scene strip, chapter bands; O-markers, bookmarks and chapters panels (add from selection).
4. Audio Analysis card and audio segments, auto sync (audio), playback speed, `.samn` export, recent files, soft seek.
5. Core cleanups from `MODULES.md`: names, Unicode case folding, one Python module.

Owner reminders (16 Oct): the Generator has the Editor built in; detection should become better than 0.7 (C++ libraries); curves stay real curves with the audio curve; everything planned in the old roadmap comes along.

## Waiting on the Owner

- The four open questions in `DECISIONS.md` § Resolutions: Suite-only features; SamNPlayer 2 as the one free app plus repo visibility; AI drafts of the curve; the role of C#.
- Golden clips for the generator (P3). They stay out of the repo.
- The v0.7.0 tag in the old repo.
