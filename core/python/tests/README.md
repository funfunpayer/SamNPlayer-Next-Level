# core/python/tests

Parity tests for the C++ ports against the existing Go reference.

## beatbar

`test_beatbar_parity.py` mirrors the synthetic cases from
`generator/beatbar_test.go` (`TestFindBeatBarAndBeats` and the plain-noise
negative test).

## stroke filter

`test_stroke_filter_parity.py` mirrors key cases from
`funscript/stroke_filter_test.go` (smooth PCHIP/Makima, speed limit, etc.).

## beat strokes

`test_beat_strokes_parity.py` mirrors the cases from
`funscript/beat_strokes_test.go` (down on every beat, speed limit, hold).

## funscript

`test_funscript_parity.py` mirrors core parse/write cases from
`funscript/funscript_test.go` / `parse_compat_test.go` plus ShiftActions
from `shift_test.go`.

All parts live in one module, `samn_native` (`from samn_native import beatbar`).
`test_one_module.py` checks that they load into one process with one `Action` type.

Build from the repo root (the module lands in `build/core`, or
`build/core/Release` with MSVC):

```bash
cmake -S . -B build -Dpybind11_DIR=$(python3 -m pybind11 --cmakedir)
cmake --build build -j
for t in core/python/tests/test_*.py; do PYTHONPATH=build/core python3 "$t"; done
```

A missing module prints SKIP and exits 0, except with `SAMN_NATIVE_REQUIRED=1`
(set in CI), where it fails.
