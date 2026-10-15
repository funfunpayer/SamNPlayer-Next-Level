# Plan: SamNPlayer 2 (Python + C++, Qt interface)

**Owner decisions, 15 Oct 2026:**
- Go is replaced completely.
- The new system is written in C/C++ and Python; C# is also named (its
  role is still open, see below).
- It lives in this repo, `SamNPlayer-Next-Level`.
- The interface is new, in Qt (PySide6).
- Everything SamNPlayer 0.7 can do is built again.

The old app (`funfunpayer/SamNPlayer`, Go/Wails) gets fixes only until this
one ships.

## Rules

- **Parity:** the Go code and its tests (about 23 000 lines) are the
  specification. A part is done when its ported tests pass and the
  behaviour matches; the C++ ports carry parity tests against the Go cases.
- **Measure before C++:** a path moves to C++ only when a measurement
  shows that Python is too slow there (3 warm-up and 5 measured runs,
  median and p95, no quality loss). The first profile of the Go generator
  is in the old repo, `docs/owner/generator-profile-2026-10.md`: 99 % of
  the time is in OpenCV/FFmpeg, with CSRT 46 % and the camera estimate 41 %.
- **File formats stay the same:** `.funscript`, `.samn`, settings. Users can
  switch back and forth.
- **No copied code** from GPL, BSL or EULA projects. MIT only with a notice.
- **Only real functions.** A mode that is not built says so; nothing
  pretends to work.

## Languages

| Part | Language | Why |
|---|---|---|
| Interface | Python + Qt (PySide6, LGPL) | Native widgets, QtMultimedia for video (FFmpeg backend), pytest-qt |
| App logic, settings, library, player and sync, devices (BLE via `bleak`), generator control, AI (onnxruntime, PyTorch) | Python | Fast to write; the AI ecosystem lives here |
| Tracking, frame pipeline, camera estimate, detection, funscript hot paths | C++ (OpenCV, pybind11) | Speed; OpenCV and ONNX Runtime are C++ themselves; `trackcv/cv.cpp` from the old repo is already C++ |
| **C#** | open | The Owner named it on 15 Oct. Possible uses: a Windows-only device bridge, or reuse of ideas from C# players (MultiFunPlayer, MIT). It is a third runtime (.NET), so it is used only where it brings something Python/C++ cannot. **Needs an Owner decision before any C# code.** |

## Phases

| Phase | Content | Done when |
|---|---|---|
| **P0** ✅ (this PR) | Repo layout; `native/` moved over (beat bar, stroke filter, beat strokes, funscript parse/write, each with parity tests); Qt main window with Play / Edit / Generate; Play opens a funscript and draws its curve (level of detail); CI for Linux and Windows | CI green |
| **P1 Player** | Video first (open a video and find its script: same name, script folders, tolerant names, variants); QtMultimedia video synced with the curve; playhead, heatmap, markers, chapters; offset per script; loop; soft seek; keyboard shortcuts; recent files; `.samn` | Play a script with its video like 0.7; ported funscript/samn tests pass |
| **P2 Devices** | Sam Neo 2 over BLE (`bleak`), mock device, sync loop, stroker filter (smooth, speed limit, idle fill, home on pause), suction/vibration (audio vibration), training mode, device profiles | Mock tests pass; **Owner hardware test** |
| **P3 Generator** | CSRT tracking and camera estimate in C++ (from `cv.cpp`; camera estimate on a second thread), post-processing (posttrack, Quality Doctor) in Python, Everyday auto-tip, rhythm grid, contact vibration, beat bar (Cock Hero), audio tempo, batch queue | Synthetic and Owner clips: r equal or better than Go, time equal or better |
| **P4 Editor and the rest** | Full editor (E1–E10 from the old roadmap), library, settings, AI helpers (ROI, teachers), Scene Map, OFS import, plugins, updater | Every behaviour on the checklist (from the old 148 JS suites and the handbook) has a passing test |
| **P5 Release** | PyInstaller/Nuitka builds for Windows and Linux with ffmpeg and OpenCV; updater switch; release workflow | First release of SamNPlayer 2 |

## Two agents

- **Claude:** branches `claude/*`. Owns the Python app and the Qt
  interface, and reviews every PR (build, tests, workflow YAML).
- **Grok:** branches `grok/*` only, never a `claude/*` branch. Owns C++
  ports in `native/`, each as a PR with parity tests.
- **main:** changes only through a PR with green CI.
