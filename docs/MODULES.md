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
| `marks.h` | Bookmarks and chapters, OFS times | Go `funscript/bookmarks.go` | ✅ files byte-identical |
| `script_finder.h` | Script for a video: beside it, folders, tolerant names, variants | Go `app_open_media.go` | ✅ |
| `settings.h` | Settings JSON with 0.7 keys, atomic writes | — | ✅ |
| `playback.h` | Offset, script time, loop, video beside a script, session | Go `app_playback.go` | ✅ |
| `stroke_filter.h` | Live stroker filter (PCHIP/MAKIMA, speed limit, idle fill) | Go `funscript/stroke_filter.go` | ✅ parity |
| `beatbar.h`, `beat_strokes.h` | Cock Hero beat bar → beats → strokes | Go `generator/beatbar.go` | ✅ parity |

**Open cleanups:**
- `script_finder`/`settings`/`playback` use snake_case function names; rename them to PascalCase.
- Case folding is ASCII only: "Übung" does not match "übung". Go and Python fold Unicode. Fix this before P1 ends.

## app/ (Qt 6): `samn_app` + `samnplayer`

| File | What |
|---|---|
| `src/theme.h` | All colours and sizes of the Flow look (QML singleton `Theme`). |
| `src/player_controller.*` | Player logic for QML: open, offset, playhead, errors. |
| `src/curve_item.*` | The script curve as a painted item, with level of detail. |
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
