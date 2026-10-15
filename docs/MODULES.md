# Modules

Each module is a folder with a README covering its API, dependencies, Go
reference and status, plus its own tests. Agents work on **one module per
PR** and only through that module's API. New agents start here and in
`AGENT_STATE.md`.

| Module | Language | What | Owner | Phase / status |
|---|---|---|---|---|
| `native/` | C++ (pybind11) | Fast paths: funscript parse/write, stroke filter, beat bar, beat strokes; later tracking and the frame pipeline | Grok (ports), reviewed by Claude | P0 ✅, P3 tracking open |
| `samn/native.py` | Python | Loads the C++ modules (a missing module is an error) | Claude | P0 ✅ |
| `samn/formats/` | Python | `.funscript` (now), `.samn`, OFS, settings | Claude | P0 load ✅, P1 rest |
| `samn/library/` | Python | Script search, folders, variants | Claude | P1 logic ✅ |
| `samn/ui/` | Python + Qt | Windows and widgets | Claude | P0 ✅ |
| `samn/player/` | Python | Playback clock, sync, offset, loop | open | P1 |
| `samn/devices/` | Python (+ C# open) | Sam Neo 2 over BLE, mock device, stroker filter | open | P2 |
| `samn/generator/` | Python + C++ | Tracking, post-processing, Quality Doctor, beat bar | open | P3 |
| `samn/editor/` | Python | Editing operations (undo, select, transform) | open | P4 |

**Rules between modules:**
- `ui` may call every module; no other module imports `ui`.
- Only `samn/native.py` imports C++ modules.
- A module's tests must not need another module's internals.
