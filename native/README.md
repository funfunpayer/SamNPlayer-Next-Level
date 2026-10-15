# native/

C++ library for SamNPlayer hot paths (M1 of the migration).

Built with CMake and exposed to Python via pybind11.

## Build

```bash
cd native
mkdir build && cd build
cmake .. -Dpybind11_DIR=$(python3 -m pybind11 --cmakedir)
make
```

Then import `beatbar_native`, `stroke_filter_native`, `beat_strokes_native` and `funscript_native` from the build directory (or install).

## Modules

- **beatbar_native**: beat-bar detection (parity with `generator/beatbar.go`)
- **stroke_filter_native**: stroke filter (parity with `funscript/stroke_filter.go`) — PCHIP/Makima smooth, max speed, idle fill
- **beat_strokes_native**: BeatStrokes (parity with `funscript/beat_strokes.go`) — one stroke per beat, speed-limited, hold on long gaps
- **funscript_native**: core parse/write + ShiftActions (parity with `funscript/funscript.go` and `shift.go`)

## Tests

Parity tests live in `tests/` (see `tests/README.md`).

```bash
# after the build above
PYTHONPATH=native/build python3 native/tests/test_beatbar_parity.py
PYTHONPATH=native/build python3 native/tests/test_stroke_filter_parity.py
PYTHONPATH=native/build python3 native/tests/test_beat_strokes_parity.py
PYTHONPATH=native/build python3 native/tests/test_funscript_parity.py
```
