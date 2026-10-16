# SamNPlayer 2

SamNPlayer, rebuilt in **C++** with **Qt 6** and a **QML** interface in the animated Flow look of 0.7. It is a funscript player, editor and generator for the SVAKOM Sam Neo 2 and other devices. Python is used only where it is clearly better (AI training), through bindings of the C++ core.

It replaces the Go/Wails app in [`funfunpayer/SamNPlayer`](https://github.com/funfunpayer/SamNPlayer), which gets fixes only until this one ships.

**Status:** phase P1 (Player).
- The core holds all Player logic and its tests.
- The interface shows the Flow shell and the Player page with video, curve and offset.

The plan is in [`docs/PLAN.md`](docs/PLAN.md), the decisions in [`docs/DECISIONS.md`](docs/DECISIONS.md) and the roadmap in [`docs/ROADMAP.md`](docs/ROADMAP.md). The current state is in [`docs/AGENT_STATE.md`](docs/AGENT_STATE.md).

## Layout

| Folder | What |
|---|---|
| `core/` | C++17 library without Qt: formats, mapping, filters, beat bar, script search, settings, playback |
| `core/python/` | pybind11 bindings for AI tools and parity tests |
| `app/` | Qt 6 app: C++ controllers (`src/`) and the QML interface (`qml/`) |
| `tests/core/` | Core tests (doctest) |
| `third_party/` | Vendored doctest (MIT) |

## Build and run

You need CMake 3.21+, a C++17 compiler and Qt 6.4+ (Quick, Multimedia, Svg). For the Python bindings you also need pybind11.

```bash
cmake -S . -B build -Dpybind11_DIR=$(python -m pybind11 --cmakedir)   # or -DSAMN_BUILD_PYTHON=OFF
cmake --build build --parallel
ctest --test-dir build
./build/app/samnplayer path/to/clip.funscript
```

How we work: [`CONTRIBUTING.md`](CONTRIBUTING.md).
