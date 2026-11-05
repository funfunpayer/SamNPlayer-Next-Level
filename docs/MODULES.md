# Modules

Agents work on **one module per PR** and only through that module's API.
New agents start with [`AGENT_STATE.md`](AGENT_STATE.md) and this file.

## core/ (C++17, no Qt): `samn_core`

New `core/src/*.cpp` and `tests/core/*_test.cpp` files are built
automatically by CMake globs, so a new module needs no CMake change.
Functions use PascalCase and struct fields use snake_case.

| Header (`core/include/samn/`) | What | Reference | Tests |
|---|---|---|---|
| `funscript.h` | Parse/Write, ShiftActions, `samn::Action` (the only definition) | Go `funscript/funscript.go` | ✅ |
| `samn_file.h` | `.samn` document, conversion and export | Go `samn/`, `convert.go` | ✅ byte-identical with Go |
| `mapper.h` | Script → suction/vibration frames (heatmap, device) | Go `funscript/mapper.go` | ✅ values identical |
| `marks.h` | Bookmarks and chapters, OFS times; the marked range (`<script>.marker.json`) | Go `funscript/bookmarks.go` | ✅ files byte-identical |
| `script_finder.h` | Script for a video: beside it, folders, tolerant names, variants | Go `app_open_media.go` | ✅ |
| `settings.h` | Settings JSON with 0.7 keys, atomic writes | — | ✅ |
| `text.h` | `FoldCase`: UTF-8 lower case without ICU (Latin, Greek, Cyrillic, full-width); `AsciiLower` (A-Z only) for protocol words, extensions and JSON keys | Go `strings.ToLower` | ✅ |
| `number.h` | `ParseDoublePrefix` / `StrToDouble` / `FormatFixed`: decimal numbers that ignore the process locale (strtod, stod and `%f` read it; a comma locale made "1.6" read as 1) | — | ✅ incl. comma-locale tests |
| `playback.h` | Offset key and clamp, script time, the video beside a script, companion axis files | Go `app_playback.go` | ✅ |
| `stroke_filter.h` | Live stroker filter (PCHIP/MAKIMA, speed limit, idle fill) | Go `funscript/stroke_filter.go` | ✅ parity |
| `output_levels.h` | Reduce peaks, the soft start ramp and the Extended-O scale schedule as functions of time | Go `player/player.go` (ReducePeak, setOutput), `player/sync.go` (softStartWithPositions), `player/extendedo.go` | ✅ parity |
| `playhead.h` | The video playhead between two media position reports: estimate, cap, no step back, seek / pause / rate rules (`docs/TIMING.md` § 7) | new (0.7 sampled the video at its reports only) | ✅ |
| `beatbar.h`, `beat_strokes.h` | Cock Hero beat bar → beats → strokes | Go `generator/beatbar.go` | ✅ parity |
| `curve_geometry.h` | Curve as 0.7 draws it: Catmull-Rom (control points clamped to 0-100), tempo wave, density switch, zoom window. Binary search instead of 0.7's full scans. | 0.7 `playback.js` (smoothCurvePath, drawSpeedWave, zoomByFactor) | ✅ tempo wave equals 0.7 (node reference) |
| `fileio.h` | `WriteFileAtomic`: temp file, flush, rename. The one writer for marks, `.samn`, settings and the range marker. `ReadFileToString`: the whole file or nullopt. | Go `writeFileAtomic` | ✅ |
| `library.h` | Library lists (recent 8, favorites 50, scripts 50), settings keys `library.*` | 0.7 `player_layout.js` | ✅ |
| `audio_envelope.h` | Loudness envelope 0..1 per 50 ms (Audio Level lane, vibration from sound). No ffmpeg: the app decodes. | Go `generator/audio_vibration.go` | ✅ Go tests ported |
| `device/device.h`, `device/transport.h` | `Device` + optional interfaces (linear, multi-axis, motor level, raw, battery), `Status`; `Transport` = the I/O seam (BLE GATT write / line writer / WebSocket text) the app implements | Go `device/device.go` | ✅ |
| `device/protocol.h`, `device/samneo2.h`, `device/battery.h` | Sam Neo 2 + legacy Sam packets (byte-exact), change detection, Stop/raw state, keepalive, battery cache | Go `protocol.go`, `samneo2.go`, `battery.go` | ✅ Go tests ported |
| `device/tcode.h` | TCode strings and address parsing (identical to Go), `TCodeDevice` over a Transport | Go `tcode.go`, `tcode_serial.go` | ✅ Go tests ported; strings diffed against Go |
| `device/intiface.h`, `device/go_json.h` | Buttplug v3 messages (bytes as Go's `encoding/json`), handshake, device adoption, one-shot reconnect (a command whose reconnect found another device is refused), battery (one reading per device session, request Id matched, shown for `kBatteryMaxAge` at most; the constant is in `battery.h`, only Intiface uses it) | Go `intiface.go` | ✅ Go tests ported; bytes diffed against Go |
| `device/capabilities.h`, `device/mock.h`, `device/diagnostics.h` | Channels / features, `MockDevice` + `MockTransport`, the diagnostics run | Go `capabilities.go`, `mock.go`, `diagnostics.go` | ✅ Go tests ported |
| `project.h` | Project sidecar `<name>.snp.json` (offset, seek, loop marker, bookmarks, chapters): `ProjectPathFor`, `SaveProject`, `LoadProject` | Go `funscript/project.go` | ✅ file byte-identical with Go |
| `range_edit.h` | Editor range tools: `SnapMs`, `DeleteRange`, `CapSpeedRange`, `ScaleRangePos`, `ScaleRangePosFade` (errors as `RangeEditError`) | Go `funscript/project.go` | ✅ Go tests ported |
| `heatmap_image.h` | Heatmap as an RGBA buffer (buckets, colours, chapter ticks); the app encodes the PNG | Go `funscript/heatmap_png.go` | ✅ pixels identical with Go |
| `playlist.h` | Play list: replace, add, remove, pick, Next, Auto-advance, Shuffle (current first), Repeat (reshuffled cycle) | 0.7 `playback.js` playlist | ✅ tests |
| `training.h` | Training engine: up/down cycles for stamina and control practice as a state machine the caller ticks with `advance(dt_ms)` (`samn::training::Session`); level programs, categories, body cues | 0.7 `player/training.go` | ✅ |
| `training_store.h` | Training on disk: custom scripts (one JSON per slug), the per-session JSONL log, history summary and CSV, the auto-adjust nudge | 0.7 `app_training_scripts.go`, `app_training_history.go` | ✅ (in `training_test.cpp`) |
| `playback_mapping.h` | From a script to the device frames at Play: mapping settings over the script's recipe, `.samn` strength preset, live runtime adjust, Effects presets | 0.7 `app_playback.go`, `sam/runtime.go`, `player_layout.js` | ✅ |
| `label_track.h` | AI Train label tracks: box or "absent" keys per label, interpolation, confirmed flag, `<clip>.labels.json`, blending two tracker runs | D-155 §2.2 | ✅ |
| `edit_session.h` | The Editor's model: curves per axis plus a patch-based undo history with a hard size limit | D-082, D-083, D-142 | ✅ |
| `clip_prep.h` | Clip prep: time parsing, the ffmpeg arguments, cut names, `marks.json` batches and the script cut to the same window | 0.7 Benchmark Clip-Prep, D-155 step 6 | ✅ |
| `contact_marks.h` | Tip and contact areas marked in the Generator; contact vibration that follows the tip into them | 0.7 `contact_marks.go`, `feel_spatial.go` | ✅ |
| `contact_preview.h` | Live Feel probe: a fixed synthetic stroke through the contact-vibration envelope | 0.7 `PreviewContactVibration` | ✅ |
| `improve.h` | Script polish after Create: trim ends, heal tracking-loss windows, fill long gaps | 0.7 `funscript/improve.go` | ✅ |
| `motion_fidelity.h` | Motion Fidelity: how well a script follows a reference (rhythm, phase, amplitude, lag window) | 0.7 `funscript/phase.go` | ✅ |
| `script_quality.h` | Script Doctor: jitter, jumps, gaps and device limits from the points alone | 0.7 `script_quality.go`, `device_compat.go` | ✅ |
| `ozone.h` | O-zone suggestions for the primary O-marker | 0.7 `funscript/ozone.go` | ✅ |
| `phase_rules.h` | Phase suggestions from picture, loudness and tempo, without a model; always unconfirmed | D-157 | ✅ |
| `polarity.h` | Which way is up: a hint after Create, never applied alone | 0.7 `funscript/polarity.go` | ✅ |
| `scenes.h` | Chapter scenes: feel names, proposals from `<clip>.scene.json`, suggested feel | 0.7 `app_feel.go`, `scene_proposals.go` | ✅ |
| `simplify.h` | Ramer-Douglas-Peucker on a (time, position) curve, iterative | 0.7 `motionx.Simplify` | ✅ |
| `sound_vibration.h` | Vibration written into a script from the video's sound (Editor tool) | 0.7 `sam/audio_vibration.go` | ✅ |
| `teacher.h` | AI Train teachers: prompt, request body and reply parsing for a vision-language server; proposals are never confirmed | D-155 step 4 | ✅ |
| `teacher_votes.h` | Votes between several teachers: plain or uncertain proposals | D-155 step 4 | ✅ |
| `log_buffer.h` | The app's log: ring buffer, level filter, one line format | 0.7 `logging/`, `log.js` | ✅ |
| `lru_cache.h` | `LruCache`: a cache with a hard size budget, least recently used dropped first; not thread-safe | D-142 | ✅ |
| `time_util.h` | `TimeToTm`: UTC or local broken-down time without the `gmtime_r` / `gmtime_s` split | — | ✅ (through the log and settings tests) |
| `sha256.h` | `Sha256`: dependency-free content hash for benchmark manifests | GD-05 | ✅ |
| `update_check.h` | Update check: release URLs, tag parsing, newer-than compare, release-notes preview; check only | 0.7 `update/update.go`, D-147 | ✅ |
| `video_info.h` | What a video's header says: codec name from a FourCC, never a guessed default | issue #4 (MEDIA-01) | ✅ |

**Open cleanups:**
- `script_finder`/`settings`/`playback` use snake_case function names; rename them to PascalCase.

## vision/ (C++17 + OpenCV, no Qt): `samn_vision`

Frames, tracking and detection for the Generator and AI Train (P3, D-133). Built when `SAMN_BUILD_VISION` is on (default); Linux uses the system OpenCV with contrib, Windows CI uses vcpkg (`ci/vcpkg/vcpkg.json`). Linked into the app.

| File | What |
|---|---|
| `include/samn/vision/frame_reader.h` | `FrameReader`: FFmpeg backend only, every frame with `index` and `pts_us` (video time), frame-exact `SeekMs`. |
| `include/samn/vision/tracker.h` | `CreateTracker(kCsrt)`, the one tracker factory (no fallback, GD-02); `VisionSelfCheck()`, `OpenCvVersion()`. |
| `include/samn/vision/onnx_detector.h` | `OnnxDetector`: an ONNX object detector run with ONNX Runtime; YOLOv8 (NudeNet) and DETR (RF-DETR student) outputs, told apart by the model's outputs (D-155 steps 4, 5). |
| `include/samn/vision/canceled.h` | `samn::kCanceled`: the error text of a stopped run, set by the frame loops here and in the engine and compared by the app. |
| `include/samn/vision/picture_signals.h` | Mean brightness and mean change per step over a clip (small grey frames), the picture input of the phase rules (D-157). |

Tests: `tests/vision/*_test.cpp` (`vision_tests`, doctest; generated test-pattern clips, no media).

## engine/ (C++17 + OpenCV, no Qt): `samn_engine`

The Generator's engine (P3, D-140, D-142): tracking, signal chain, runs, self-check. All headers are under `engine/include/samn/engine/`; long work runs on a worker thread with progress and cancel.

| Header | What |
|---|---|
| `track_box.h` | `TrackBox`: CSRT single-box tracking with scene cuts, camera compensation, appearance memory, jump guard and velocity coast; the raw box-centre track. |
| `track_cache.h` | Cache of raw tracks keyed by video content, algorithm version and tracking options (FR-11), so a post-processing change skips decode and tracking. |
| `post_track.h` | Raw track to funscript actions: Savitzky-Golay, detrend, bandpass, normalisation, peaks with prominence, keyframes, minimum interval, RDP; `kNoMotionError`. |
| `signal_chain.h` | Profile recipes, the default detrend (GD-12), post-processing, speed limit and the distance-profile clamp. |
| `tip_distance.h` | Tip to partner gap for the Tf/Tj distance profiles. |
| `tip_find.h` | Find tip: proposes the tip box from the video's rhythmic motion (optical flow, camera compensation, FFT score). |
| `generate.h` | One generation run on a worker thread: track, signal chain, write the `.funscript` and `.samn`; `canceled` is its own outcome. |
| `generate_queue.h` | One video of the Generator's queue: Find tip, then Generate with the queue's settings. |
| `self_check.h` | The self-checking engine's maths (SC-1, SC-3): sample gate, short-gap interpolation, per-window trust; no OpenCV. |
| `repair.h` | Verified repair (SC-5): failed windows are tracked again from a fresh Find tip start and kept only where they pass the checks. |
| `label_tracking.h` | Tracking between two label keyframes: CSRT forward and backward, blended with disagreement marked (D-155 step 3). |
| `beatbar_video.h` | Beat bar from a video file: find the strip, then time every beat (the Editor's beat-bar tool). |
| `compare.h` | The comparison controller: runs the engine on clips and scores it against a reference script and 0.7's output (GD-04, GD-05, GD-06). |

Tests: `tests/engine/*_test.cpp` (`engine_tests`, doctest; generated clips).

## app/ (Qt 6): `samn_app` + `samnplayer`

Controllers are `QObject`s registered for QML; their tests are `tests/app/<name>_test.cpp`.

| File | What |
|---|---|
| `src/qt_paths.h`, `src/ai_paths.h` | Qt string to path / UTF-8 conversions and the file: URL helper shared by the controllers; `AiDir()` (the AI Train folder, shared by the trainer and the health check). |
| `src/theme.h` | All colours and sizes of the Flow look (QML singleton `Theme`). |
| `src/player_controller.*` | Player logic for QML: video first (folders, variants), `.samn`, lanes, heatmap, chapters, bookmarks, offset, loop, zoom window, errors. |
| `src/curve_item.*` | One curve lane in the 0.7 look (soft line, glow, fill, dots, live dot, tempo wave). The static layer is cached; playing only moves the playhead. |
| `src/heatmap_item.*` | The heatmap bar (0.7 colours, 300 buckets) on the zoom window, with the marked range and the playhead. |
| `src/ruler_item.*` | The time ruler above the curve (0.7 steps and labels). |
| `src/audio_level.*` | `AudioLevelDecoder` (Qt Multimedia, cached per file) and the Audio Level lane. |
| `src/library_controller.*` | The Library column: recent scripts and videos, favorites, imported / generated / edited. |
| `src/script_clock.*` | The playhead for a script without a video, with the MediaPlayer's surface. |
| `src/playback_output.*` | What reaches the device while a script plays (mapping, stroke filter, axes), without QML timers. |
| `src/media_playhead.*` | The video playhead for the device output between the media player's reports (`samn/playhead.h` on a monotonic clock, a 16 ms tick while the video plays). |
| `src/mapping_settings.*` | The Player's mapping settings, live contact controls and Effects presets; saved at once. |
| `src/generator_controller.*` | The Generator mode: video, tip box, feel, Create with progress and cancel, queue, review; runs the engine on its own thread. |
| `src/editor_controller.*` | The Editor inside the Generator: curves per lane, range tools, undo, beat-bar tool, sound vibration. |
| `src/frame_cache.*`, `frame_provider.*`, `frame_item.*` | Video frames for the Editor and Generator: a bounded cache, the `image://frames` provider, the marking canvas. |
| `src/label_controller.*` | AI Train label editor: a clip, its label tracks, teachers and detectors, undo, saved beside the clip. |
| `src/train_controller.*` | AI Train step 5: sets up the app's own Python, trains the student, `aiDir()`. |
| `src/model_download.*` | Models fetched with one click, checked against pinned SHA-256 hashes. |
| `src/clip_prep_controller.*` | Clip prep: one cut or a batch through ffmpeg, which the app fetches or the user chooses. |
| `src/training_controller.*`, `training_curve_item.*` | Training mode: presets, the live session on the shared `DeviceController`, log, history, recap; the training curves. |
| `src/settings_controller.*` | The Settings page: app-wide choices, the log view, licence texts. |
| `src/system_check.*` | The health check: graphics adapters, which one trains, what the app needs (D-162). |
| `src/notices.*` | Error toasts fed by `AppLog::reportError`. |
| `src/plugins.*` | The plugin modes: extra tabs behind switches in Settings. |
| `src/update_checker.*` | "Check for updates": asks GitHub for the newest release and offers its page. |
| `src/app_log.*` | The process log: Qt messages, device log and app notes in one buffer, optional file. |
| `src/icon_provider.*`, `wordmark_item.*` | `image://icon/` Flow icons from `qml/icons/`; the painted wordmark. |
| `src/device_controller.*` | `DeviceController` for QML: connect / disconnect by kind (Sam Neo 2 BLE, TCode address or serial, Intiface URL, Mock), status, last error and hint, battery, capabilities (channels, axes), levels / position / axes, Stop, emergency stop, diagnostics, serial port list. Every device and transport lives on ONE worker `QThread` it owns; the GUI thread never blocks. 2 s timer (keepalive / ping / battery), incoming pumping, coalesced level commands, lost-link handling. |
| `src/device_io/waiter.h` | `Waiter`: blocks the worker thread until a condition holds while the Qt event loop runs (how async Qt APIs become the blocking `Transport` calls); polls cancel every 25 ms. |
| `src/device_io/qt_transport.h` | `QtTransport`: base of the Qt transports (a `QObject` + `samn::device::Transport`), `incoming()` signal, cancel hook, `LastError()`. |
| `src/device_io/ble_transport.*` | Qt Bluetooth LE for the Sam Neo 2: scan for `kSamNeo2NamePrefix`, connect, service ffe0 / characteristic ffe1, write with or without response, `ReadCharacteristic` (battery 0x180F / 0x2A19), `Peer()`. Bounded waits; every error says "Bluetooth". |
| `src/device_io/socket_line_transport.*` | TCode over UDP / TCP: one line per `Send`; "refused" / "timeout" / "no such host" in the error texts (`DescribeSocketError`). |
| `src/device_io/serial_line_transport.*` | TCode over USB serial (115200 8N1) and `SerialPorts()` (sorted, `/dev/ttyUSB0` / `COM3`). |
| `src/device_io/websocket_transport.*` | Buttplug / Intiface: Qt WebSockets text transport with a dial timeout; same error words, so `IntifaceHint` matches. |
| `qml/Main.qml`, `Splash.qml` | Window, background, top bar, pages, keys; the loading screen. |
| `qml/TopBar.qml`, `PlayButton.qml`, `GlassCard.qml`, `SilkLayer.qml` | Flow components with animations. |
| `qml/PlayerPage.qml`, `VideoTransport.qml`, `LibraryPanel.qml`, `InspectorPanel.qml`, `MarkList.qml`, `LaneName.qml`, `PositionGauge.qml` | Player: video, transport, timeline; Library (left) and Inspector with Devices · Audio · Playback · View (right). |
| `qml/GeneratorPage.qml`, `GeneratorProjectPanel.qml` | Generator mode and its project panel. |
| `qml/AiTrainPage.qml`, `LabelEditor.qml` | AI Train: setup, models, label editor, training. |
| `qml/TrainingPage.qml`, `TrainingRing.qml`, `TrainingEditor.qml` | Training mode. |
| `qml/SettingsPage.qml`, `SettingRow.qml`, `Handbook.qml`, `HandbookTexts.js`, `HelpChip.qml`, `HelpTexts.js`, `ToolTipText.qml` | Settings, handbook and help texts. |
| `qml/FlowButton.qml`, `IconButton.qml`, `FlowSlider.qml`, `OffsetField.qml`, `NumField.qml`, `FlowCombo.qml`, `FlowTabs.qml`, `FlowTextField.qml`, `FlowToggle.qml`, `FlowFigure.qml`, `DualRing.qml`, `Icon.qml` | Styled controls; no default Qt Quick Controls look. |

**Rules:**
- QML only shows things and calls controller methods; there is no logic in QML.
- Only `app/` uses Qt; `core/` never does.
- Every QML file starts with `import SamN` (Qt 6.4 needs it).
- Test hook: `SAMN_SCREENSHOT=file.png` saves the window and quits. CI uses it as a smoke test, and screenshots use test patterns only.

## core/python/: Python bindings

One module, `samn_native`, with the submodules `beatbar`, `stroke_filter`, `beat_strokes` and `funscript` (`from samn_native import funscript`). `samn::Action` is registered once, so every part works in one process. New bindings: a `python/<part>_py.cpp` with a `Bind<Part>(py::module_&)` function, added in `samn_native_py.cpp` and `core/CMakeLists.txt`. Parity tests are in `core/python/tests/`. In CI, `SAMN_NATIVE_REQUIRED=1` makes a missing module fail.
