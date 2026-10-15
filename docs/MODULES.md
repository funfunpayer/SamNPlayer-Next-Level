# Modules

Each module is a folder with a README covering its API, dependencies, Go
reference and status, plus its own tests. Agents work on **one module per
PR** and only through that module's API. New agents start here and in
`AGENT_STATE.md`.

| Module | Language | What | Owner | Phase / status |
|---|---|---|---|---|
| `native/` | C++ (pybind11) | Fast paths: funscript parse/write, stroke filter, beat bar, beat strokes; later tracking and the frame pipeline | Grok (ports), reviewed by Claude | P0 ✅, P3 tracking open |
| `samn/native.py` | Python | Loads the C++ modules (a missing module is an error) | Claude | P0 ✅ |
| `samn/formats/` | Python | `.funscript` (now), mapper (script → suction/vibration, heatmap), `.samn`, OFS | Claude | P0 load ✅, mapper ✅ (9 Go tests, identical values), `.samn` open |
| `samn/settings.py` | Python | Settings JSON, atomic writes, same keys as 0.7 | Claude | P1 ✅ |
| `samn/library/` | Python | Script search, folders, variants | Claude | P1 logic ✅ |
| `samn/ui/` | Python + Qt | Main window; Play page (QtMultimedia video, curve with playhead, heatmap, offset, loop, shortcuts) | Claude | P0 ✅, P1 Play page ✅ |
| `samn/player/` | Python | Offset, loop, script time, video beside a script | Claude | P1 ✅ (device sync in P2) |
| `samn/devices/` | Python (+ C# open) | Sam Neo 2 over BLE, mock device, stroker filter | open | P2 |
| `samn/generator/` | Python + C++ | Tracking, post-processing, Quality Doctor, beat bar | open | P3 |
| `samn/editor/` | Python | Editing operations (undo, select, transform) | open | P4 |

**Rules between modules:**
- `ui` may call every module; no other module imports `ui`.
- Only `samn/native.py` imports C++ modules.
- A module's tests must not need another module's internals.
