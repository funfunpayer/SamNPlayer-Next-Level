# core/

`samn_core` is the C++17 library behind SamNPlayer 2: file formats, mapping, filters, beat bar, script search, settings and playback logic. It has no Qt dependency, so the app, the tests and the Python tools share it. The module list is in [`docs/MODULES.md`](../docs/MODULES.md).

- **Headers:** `include/samn/*.h`
- **Sources:** `src/*.cpp`. They are picked up by the CMake glob, so new files need no CMake change.
- **Python bindings:** `python/*_py.cpp`, with parity tests in `python/tests/`.
- **Tests:** `tests/core/*_test.cpp` at the repo root, using doctest.
