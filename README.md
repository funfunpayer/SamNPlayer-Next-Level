# SamNPlayer 2

SamNPlayer rebuilt in **Python and C++** with a **Qt (PySide6)** interface:
a funscript player, editor and generator for the SVAKOM Sam Neo 2 and
other devices. It replaces the Go/Wails app in
[`funfunpayer/SamNPlayer`](https://github.com/funfunpayer/SamNPlayer), which
gets fixes only until this one ships.

**Status:** phase P0, the foundation. A window with Play / Edit / Generate
opens a funscript and draws its curve; nothing else works yet. The plan is
in [`docs/PLAN.md`](docs/PLAN.md) and the current state in
[`docs/AGENT_STATE.md`](docs/AGENT_STATE.md).

## Layout

| Folder | Language | What |
|---|---|---|
| `samn/` | Python | app logic and the Qt interface (`samn/ui/`) |
| `native/` | C++ (CMake, pybind11) | fast paths: funscript parsing, stroke filter, beat bar, beat strokes; later tracking and the frame pipeline |
| `tests/` | Python | pytest and pytest-qt |

## Build and run

```bash
python -m pip install -e ".[test]" pybind11
cmake -S native -B native/build -Dpybind11_DIR=$(python -m pybind11 --cmakedir)
cmake --build native/build --config Release
python -m samn path/to/clip.funscript
pytest
```
