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
| `text.h` | `FoldCase`: UTF-8 lower case without ICU (Latin, Greek, Cyrillic, full-width) | Go `strings.ToLower` | ✅ |
| `playback.h` | Offset, script time, loop, video beside a script, session | Go `app_playback.go` | ✅ |
| `stroke_filter.h` | Live stroker filter (PCHIP/MAKIMA, speed limit, idle fill) | Go `funscript/stroke_filter.go` | ✅ parity |
| `output_levels.h` | Reduce peaks, the soft start ramp and the Extended-O scale schedule as functions of time | Go `player/player.go` (ReducePeak, setOutput), `player/sync.go` (softStartWithPositions), `player/extendedo.go` | ✅ parity |
| `beatbar.h`, `beat_strokes.h` | Cock Hero beat bar → beats → strokes | Go `generator/beatbar.go` | ✅ parity |
| `curve_geometry.h` | Curve as 0.7 draws it: Catmull-Rom (control points clamped to 0-100), tempo wave, density switch, zoom window. Binary search instead of 0.7's full scans. | 0.7 `playback.js` (smoothCurvePath, drawSpeedWave, zoomByFactor) | ✅ tempo wave equals 0.7 (node reference) |
| `fileio.h` | `WriteFileAtomic`: temp file, flush, rename. The one writer for marks, `.samn`, settings and the range marker. | Go `writeFileAtomic` | ✅ |
| `library.h` | Library lists (recent 8, favorites 50, scripts 50), settings keys `library.*` | 0.7 `player_layout.js` | ✅ |
| `audio_envelope.h` | Loudness envelope 0..1 per 50 ms (Audio Level lane, vibration from sound). No ffmpeg: the app decodes. | Go `generator/audio_vibration.go` | ✅ Go tests ported |
| `device/device.h`, `device/transport.h` | `Device` + optional interfaces (linear, multi-axis, motor level, raw, battery), `Status`; `Transport` = the I/O seam (BLE GATT write / line writer / WebSocket text) the app implements | Go `device/device.go` | ✅ |
| `device/protocol.h`, `device/samneo2.h`, `device/battery.h` | Sam Neo 2 + legacy Sam packets (byte-exact), change detection, Stop/raw state, keepalive, battery cache | Go `protocol.go`, `samneo2.go`, `battery.go` | ✅ Go tests ported |
| `device/tcode.h` | TCode strings and address parsing (identical to Go), `TCodeDevice` over a Transport | Go `tcode.go`, `tcode_serial.go` | ✅ Go tests ported; strings diffed against Go |
| `device/intiface.h`, `device/go_json.h` | Buttplug v3 messages (bytes as Go's `encoding/json`), handshake, device adoption, one-shot reconnect, battery | Go `intiface.go` | ✅ Go tests ported; bytes diffed against Go |
| `device/capabilities.h`, `device/mock.h`, `device/diagnostics.h` | Channels / features, `MockDevice` + `MockTransport`, the diagnostics run | Go `capabilities.go`, `mock.go`, `diagnostics.go` | ✅ Go tests ported |
| `project.h` | Project sidecar `<name>.snp.json` (offset, seek, loop marker, bookmarks, chapters): `ProjectPathFor`, `SaveProject`, `LoadProject` | Go `funscript/project.go` | ✅ file byte-identical with Go |
| `range_edit.h` | Editor range tools: `SnapMs`, `DeleteRange`, `CapSpeedRange`, `ScaleRangePos`, `ScaleRangePosFade` (errors as `RangeEditError`) | Go `funscript/project.go` | ✅ Go tests ported |
| `heatmap_image.h` | Heatmap as an RGBA buffer (buckets, colours, chapter ticks); the app encodes the PNG | Go `funscript/heatmap_png.go` | ✅ pixels identical with Go |
| `playlist.h` | Play list: replace, add, remove, pick, Next, Auto-advance, Shuffle (current first), Repeat (reshuffled cycle) | 0.7 `playback.js` playlist | ✅ tests |

**Open cleanups:**
- `script_finder`/`settings`/`playback` use snake_case function names; rename them to PascalCase.

## vision/ (C++17 + OpenCV, no Qt): `samn_vision`

Frames and tracking for the Generator (P3, D-133). Built when `SAMN_BUILD_VISION` is on (default); Linux uses the system OpenCV with contrib, Windows CI uses vcpkg (`ci/vcpkg/vcpkg.json`). Not linked into the app yet (P3.3).

| File | What |
|---|---|
| `include/samn/vision/frame_reader.h` | `FrameReader`: FFmpeg backend only, every frame with `index` and `pts_us` (video time), frame-exact `SeekMs`. |
| `include/samn/vision/tracker.h` | `CreateTracker(kCsrt)`, the one tracker factory (no fallback, GD-02); `VisionSelfCheck()`, `OpenCvVersion()`. |

Tests: `tests/vision/*_test.cpp` (`vision_tests`, doctest; generated test-pattern clips, no media).

## app/ (Qt 6): `samn_app` + `samnplayer`

| File | What |
|---|---|
| `src/theme.h` | All colours and sizes of the Flow look (QML singleton `Theme`). |
| `src/player_controller.*` | Player logic for QML: video first (folders, variants), `.samn`, lanes, heatmap, chapters, bookmarks, offset, loop, zoom window, errors. |
| `src/curve_item.*` | One curve lane in the 0.7 look (soft line, glow, fill, dots, live dot, tempo wave). The static layer is cached; playing only moves the playhead. |
| `src/heatmap_item.*` | The heatmap bar (0.7 colours, 300 buckets) on the zoom window, with the marked range and the playhead. |
| `src/library_controller.*` | The Library column: recent scripts and videos, favorites, imported / generated / edited. |
| `qml/LibraryPanel.qml`, `qml/InspectorPanel.qml` | Player columns as 0.7 v1: Library (left), Inspector with Devices · Audio · Playback · View (right). |
| `src/device_controller.*` | `DeviceController` for QML: connect / disconnect by kind (Sam Neo 2 BLE, TCode address or serial, Intiface URL, Mock), status, last error and hint, battery, capabilities (channels, axes), levels / position / axes, Stop, emergency stop, diagnostics, serial port list. Every device and transport lives on ONE worker `QThread` it owns; the GUI thread never blocks. 2 s timer (keepalive / ping / battery), incoming pumping, coalesced level commands, lost-link handling. |
| `src/device_io/waiter.h` | `Waiter`: blocks the worker thread until a condition holds while the Qt event loop runs (how async Qt APIs become the blocking `Transport` calls); polls cancel every 25 ms. |
| `src/device_io/qt_transport.h` | `QtTransport`: base of the Qt transports (a `QObject` + `samn::device::Transport`), `incoming()` signal, cancel hook, `LastError()`. |
| `src/device_io/ble_transport.*` | Qt Bluetooth LE for the Sam Neo 2: scan for `kSamNeo2NamePrefix`, connect, service ffe0 / characteristic ffe1, write with or without response, `ReadCharacteristic` (battery 0x180F / 0x2A19), `Peer()`. Bounded waits; every error says "Bluetooth". |
| `src/device_io/socket_line_transport.*` | TCode over UDP / TCP: one line per `Send`; "refused" / "timeout" / "no such host" in the error texts (`DescribeSocketError`). |
| `src/device_io/serial_line_transport.*` | TCode over USB serial (115200 8N1) and `SerialPorts()` (sorted, `/dev/ttyUSB0` / `COM3`). |
| `src/device_io/websocket_transport.*` | Buttplug / Intiface: Qt WebSockets text transport with a dial timeout; same error words, so `IntifaceHint` matches. |
| `src/ruler_item.*` | The time ruler above the curve (0.7 steps and labels). |
| `src/audio_level.*` | `AudioLevelDecoder` (Qt Multimedia, cached per file) and the Audio Level lane. |
| `qml/FlowButton.qml`, `IconButton.qml`, `FlowSlider.qml`, `OffsetField.qml`, `FlowCombo.qml` | Styled controls; no default Qt Quick Controls look. |
| `qml/Main.qml` | Window, background ribbons, top bar, pages, mode deck, keys. |
| `qml/TopBar.qml`, `ModeDeck.qml`, `PlayButton.qml`, `GlassCard.qml` | Flow components with animations. |
| `qml/PlayerPage.qml` | Player: video, transport, timeline. |
| `qml/NotYetPage.qml` | What modes not yet built show. |

**Rules:**
- QML only shows things and calls controller methods; there is no logic in QML.
- Only `app/` uses Qt; `core/` never does.
- Every QML file starts with `import SamN` (Qt 6.4 needs it).
- Test hook: `SAMN_SCREENSHOT=file.png` saves the window and quits. CI uses it as a smoke test, and screenshots use test patterns only.

## core/python/: Python bindings

One module, `samn_native`, with the submodules `beatbar`, `stroke_filter`, `beat_strokes` and `funscript` (`from samn_native import funscript`). `samn::Action` is registered once, so every part works in one process. New bindings: a `python/<part>_py.cpp` with a `Bind<Part>(py::module_&)` function, added in `samn_native_py.cpp` and `core/CMakeLists.txt`. Parity tests are in `core/python/tests/`. In CI, `SAMN_NATIVE_REQUIRED=1` makes a missing module fail.
