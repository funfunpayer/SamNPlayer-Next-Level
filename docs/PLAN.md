# Plan: SamNPlayer 2

**Goal (Owner, 15–16 Oct 2026):** SamNPlayer 2 is the C++ version of SamNPlayer. It has every function of SamNPlayer 0.7 and the same animated Flow look. It should be a better program, not a copy. Go is gone at the end.

The decisions behind this, and the old ones carried over, are in [`DECISIONS.md`](DECISIONS.md). The feature inventory, open items and lessons are in [`ROADMAP.md`](ROADMAP.md); the full checklist of every 0.7 function (661 items, file:line) is [`PARITY.md`](PARITY.md). We never restart a decision from zero.

## Principles

1. **The best language for each part.** C++ first. Python only where it is clearly better: AI training and experiments, through the Python bindings of the core.
2. **Our own engine.** We build things ourselves when that gives us an advantage. We avoid dependencies we do not need.
3. **Quality and stability first.** Then performance (CPU, RAM), which is tuned but never at the cost of quality. Nothing may feel slow.
4. **An exchangeable interface.** All logic lives in C++ (`core/` and the app controllers). QML only shows it, so another interface needs nothing but the same controllers.
5. **Parity.** The Go original and its tests are the specification. Every port carries its tests. Spot checks compare outputs with Go or the earlier Python port, and file outputs are byte-identical where Go's are.
6. **Only real functions.** A mode that is not built says so.
7. **File formats stay** (`.funscript`, `.samn`, settings keys), so users can switch between 0.7 and 2 at any time.

## Architecture

| Layer | Folder | Language | Contents |
|---|---|---|---|
| Core | `core/` | C++17, no Qt | Funscript, `.samn`, mapper (script → suction/vibration), bookmarks and chapters, stroke filter, beat bar, beat strokes, script search, settings, playback logic. Later: device protocols, tracking, the generator. |
| Python bindings | `core/python/` | pybind11 | Core functions for AI tools and parity tests. |
| App | `app/src/` | C++ (Qt 6) | Controllers (`PlayerController`, …), painted items (`CurveItem`), `Theme`. |
| Interface | `app/qml/` | QML (Qt Quick) | Pages and components in the 0.7 Flow look, with animations. |
| Tests | `tests/core`, CI smoke | doctest (vendored, MIT) | Core tests, ported from Go and Python; an app smoke test in CI. |

Dependencies, each with a reason:
- **Qt 6** (LGPL, dynamic linking) for the window, QML, video and Bluetooth.
- **nlohmann/json** (MIT) for JSON.
- **doctest** (MIT, vendored) for tests.
- **pybind11** (BSD) for the bindings only.
- **OpenCV** (Apache-2.0) for the generator, from P3.

Releases are built with Qt 6.7 or later and its FFmpeg multimedia backend. Development works with Qt 6.4 or later.

## Phases

| Phase | Content | Status |
|---|---|---|
| **P0** | Repo, CI on Linux and Windows, core library | ✅ |
| **P1 Player** | Video first; script search, variants and folders; video and curve sync; heatmap; chapters and bookmarks; offset; loop; shortcuts; `.samn`; Flow look with mode deck and animated Play button | **In progress.** Core ✅ (all P1 logic ported to C++, 50 test cases). QML shell ✅: top bar, mode deck, Player page with video, transport, curve and offset. Next: library and inspector panels, heatmap and lanes, variants and chapters in the UI, styled controls, the no-script card. |
| **P2 Devices** | Sam Neo 2 over BLE (Qt Bluetooth), mock device, sync loop, stroker filter, audio vibration, training mode, device profiles | open, then the **Owner hardware test** |
| **P3 Generator** | Everything of 0.7's Generator in C++ with the Editor built in, and better detection: plan in [`P3_GENERATOR.md`](P3_GENERATOR.md) (steps P3.0–P3.9, D-133 to D-136) | open |
| **P4 Editor and the rest** | Full editor (E1–E10), Training, Scene Map, library, settings, AI helpers, OFS import, plugins, updater | open |
| **P5 Release** | Installers for Windows and Linux, the updater switch, the release workflow; then the old Go app is archived | open |
| **Last** | A bug hunt across all modes (the Owner's request), before the first release | open |

## Two agents

- **Claude** works on `claude/*` branches. It reviews every PR (build, tests, workflow YAML) and merges on green CI.
- **Grok** works on `grok/*` branches only, mainly on C++ ports for P3.
- Nobody pushes to another agent's branch. Never force-push.
