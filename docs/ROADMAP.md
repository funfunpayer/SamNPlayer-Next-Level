# Roadmap: SamNPlayer 2

Draft of 16 Oct 2026. Phases P1–P5 are the ones in [`PLAN.md`](PLAN.md) (P0 is done).
Decisions behind this are in [`DECISIONS.md`](DECISIONS.md) (IDs `D-xxx`). Sources are
the old repo's docs (`/home/user/SamNPlayer`); "old" means that repo.
Nothing here is built unless it says ✅.

Phases: **P1** Player · **P2** Devices · **P3** Generator · **P4** Editor and the rest ·
**P5** Release · **Later** = not in P1–P5, needs a plan. The language of each part follows
D-102 (C++ first, Qt 6/QML, Python only where clearly better); `PLAN.md` still says
PySide6 and has to be updated.

---

## (a) Feature inventory of SamNPlayer 0.7

The overview below is by mode. The complete, checkable list (every function with file:line) is [`PARITY.md`](PARITY.md).

Sources: `CHANGELOG.md` (0.5.10 to 0.7.0 and "Unreleased"), `docs/USER_HANDBOOK.md`,
`docs/DEVICE_SUPPORT.md`. Marks: ✅ already in this repo (per `AGENT_STATE.md`/`MODULES.md`),
`Suite` = Suite-only in 0.7 (edition question open, D-007).

### Player

| Feature | Phase |
|---|---|
| Video first: Open a video or script (Ctrl+O, drop); a video finds its script | P1 (✅ part) |
| Script search: script folders, tolerant names, variants (Soft/Hard), `.samn` before `.funscript` | P1 (✅ `script_finder.py`) |
| "No script" card: Create script, From the beat bar, Choose script, Open other, "always create" | P1 (Create disabled until P3) |
| Video playback synced with the curve; "Make playable" proxy for HEVC/mkv | P1 (✅ video; proxy open) |
| Curve with playhead, level of detail, tempo wave for dense strokes, glow/fill drawing | P1 (✅ curve) |
| Heatmap, heatmap PNG export | P1 (✅ heatmap); export P4 |
| Bookmarks, chapters, O-markers, Extended-O, chapter bands | P1 (✅ marks) |
| Offset per script; "write offset into script" | P1 (✅ offset); write-into-script P4 |
| Loop (AB), soft seek, skip ±10 s, sleep timer, Play/Pause and Stop | P1 (✅ loop, shortcuts) |
| Keyboard shortcuts (Space, arrows, `,` `.`, 1–9, +/−, L, E, O), recent files | P1 (✅ shortcuts) |
| `.samn` and `.funscript` open/save, `.snp.json` playback project | P1 (✅ `.samn`); `.snp.json` P1 |
| Multi-axis companion lanes (surge, sway, twist, roll, pitch) shown read-only | P1; output to device P2 |
| Contact-marks overlay and Feel Stage A (tip grazing a mark buzzes) | P3 |
| Audio-check strip (Speech-Hold / Feel segments) on timeline and heatmap | P3 |
| Playlist (queue of scripts), favourites | P4 |
| Auto sync (offset suggested from video sound), BPM from audio, BPM grid | P3 |
| Dark theme | P1 (✅) |

### Editor

| Feature | Phase |
|---|---|
| Drag, add, delete points on every lane (stroke, vibration, suction, companion axes) | P4 |
| Undo/Redo per channel | P4 |
| Range tools on heatmap selection: speed cap, scale (soft edges), ramp scale, delete range, invert | P4 |
| Invert Curve (channel, range, whole script) | P4 |
| Chapters, bookmarks, O-markers, BPM grid editing | P4 |
| Optimize for: Any device (fill/heal gaps) or Sam Neo 2 (contact vibration, baked vibration/suction) | P4 (gap logic P3) |
| Companion axis files editable, "+ Axis…" | P4 |
| Sam Neo 2 feel per scene/chapter (Classic/Oral/Intercourse/Breast/Hand, Mix, follow) | P2 (mapping) and P4 (UI) |
| Editing inside the Generator ("Generator with editor") | P4 |
| Load/save project, Scale selection | P4 |

### Generator

| Feature | Phase |
|---|---|
| Everyday Create: auto tip region → CSRT → Stroke + Contact vibration → `.samn` + `.funscript` | P3 |
| Camera shake estimate, scene-cut check, CSRT jump/drift guards, appearance memory | P3 (C++) |
| Post-processing: detrend (default), peaks, smoothing, RDP, speed cap, invert | P3 |
| Quality Doctor (Signal Quality), audio tempo check (warn only) | P3 |
| Review: trim, Fill gaps, Heal tracking gaps (auto after Generate) | P3 |
| Motion candidates overlay (pick primary stroke), contact marks, Record tip path | P3 |
| Advanced: invert direction, camera compensation, sliding dynamics, Auto-Retry, Suggest O-markers, Rhythm-robust signal (rhythm grid), fixed contact area | P3 |
| Beat-bar reader (Cock Hero / Fap Hero): one stroke per beat | P3 |
| Create queue (several videos dropped), cancel mid-run | P3 |
| Generator "For device" settings; vibration from video sound written into script | P3 |
| Smarter tip find (ONNX ROI), Suggest profile, "Apply AI setup automatically" (default off) | P4 |
| Use contact points (teachers JSON), Verify with the engine (hybrid K=1.5), Generate contact points | P4 (`Suite`-like, D-007) |
| AI draft script, Export classical run (imitation library) | P4, opt-in (D-058); edition open |
| CLI `generate`, `stroke-preview`, `benchmark` | Later (CLI not in `PLAN.md`) |

### Training

| Feature | Phase |
|---|---|
| Device training sessions: programs, custom cycles, ramp/hold/rest, Stop-Start and Plateau, History | P2 |
| Stage Clip/Rings/Waves/Video, live curves, session timeline, pause/resume, safety stop (Esc) | P2 |
| Training with your own video; training follows the connected device's channels | P2 |
| AI Train: labelling, dataset, YOLO/ONNX training, profile model, review editor | P4 (`Suite`, D-007) |

### Scene Map

| Feature | Phase |
|---|---|
| Chapters as scene grid (thumbnail, range, Prev/Next, Go to scene, video info) | P4 |
| Suggested feel per scene from scene detection or contact mark (Apply never automatic) | P4 |
| Rhythm heatmap + marks (exclude / source / region), "Show scene map" on button only | P4 (needs P3 rhythm grid) |
| Export for learning, Import candidates, Accept/Reject (reviewed YOLO export) | P4 (`Suite`, opt-in) |

### Tools and Device

| Feature | Phase |
|---|---|
| Sam Neo 2 over own BLE: vibration, suction, battery, diagnostics, connect-test pulse | P2 |
| Intiface Central / Buttplug: Linear, Scalar, Rotate; per-motor levels; one auto-reconnect | P2 |
| TCode over Wi-Fi (UDP/TCP) and USB serial; extra axes L1/L2/R0–R2 | P2 |
| Mock device; device capability chips; rows hidden for missing channels | P2 |
| Stroke safety range; Send stroke on/off; stroker filter (Smooth/Rounder, speed limit, fill pauses, home on pause) | P2 |
| Effects: Intensity, Response, Reduce peaks, tick, soft start, presets; vibration from video sound only | P2 |
| Emergency stop / Disconnect / Pause / Stop | P2 |
| Tools home (device, ffmpeg and dependency status, folders, last diagnostics, version) | P4 |
| Audio Analysis (BPM, waveform), Sound Curve (loudness → vibration curve) | P3 |
| Log viewer, log folder, Check folders / install video tools | P4 |
| Plugins (drop folder, Install pack…) | P4 |
| Local AI setup check / install profiles | P4 |
| Update check | P5 |

### Settings

| Feature | Phase |
|---|---|
| Settings JSON with the same keys as 0.7, atomic writes | P1 (✅) |
| Script folders, Play prefs, stroke filter prefs | P1 / P2 |
| AI ROI model path, AI server URL (Colibri), Collect learning data | P4 |
| EULA (first start), Licence view, Third-party notices, Support/Donate links, About | P5 |
| In-app user handbook | P4 |

### Library

| Feature | Phase |
|---|---|
| Local files, recent, favourites, playlists; Imported/Generated/Edited badges | P4 |
| Analysis section (Audio Analysis, BPM Detection) | P4 |
| OFS import (OpenFunscripter project) | P4 |
| Project column in Generator (source clips, recent videos, generated scripts) | P3 |

---

## (b) Open items of the old roadmap

### v0.8 items (`docs/ROADMAP_2026-10.md` §3, §3b)

| Item | State in old repo | Phase |
|---|---|---|
| 1 Find scripts (folders, variants, tolerant names) | ✅ old, ✅ new | P1 |
| 2 Smooth stroker output as live filter | ✅ old | P2 |
| 3 Audio files with a script (MP3, FLAC) | open | P4 |
| 4 Library as tiles (thumbnails, hover preview, sort, rating) | open | P4 |
| 5 Skip long pauses; shortcut sheet, customisable shortcuts | open | P4 |
| 6 Device wizard | open | P4 |
| Native Handy connection (upload script, server-time sync, HSSP) | open, large | Later |
| Follow an external player (VLC, MPV, DeoVR, HereSphere) | open, large | Later |
| Session recap (time, max/avg speed, distance, activity heatmap) | idea | Later |
| Vorze CSV import; folder-wide multi-axis heatmap with statistics | idea | Later |
| Beat bar: audio onset fallback for rounds without a visible bar; beat-bar overlay in the Editor | open | P3 / P4 |

### Editor E1–E10 (`docs/ROADMAP_2026-10.md` §4)

| Step and content | Phase |
|---|---|
| E1 Selection (lasso/box, select all/range/chapter/peaks/valleys, numeric edit) | P4 |
| E2 Clipboard and keys (copy/paste at playhead, nudge, delete with gap policy, frame-exact step) | P4 |
| E3 Transform library (shift, stretch, mirror, smooth with preview, equalize, normalize, repeat, ripple, retime to BPM) | P4 |
| E4 Patterns (wave, tease, ramp …; same engine fills gaps in the player) | P4 |
| E5 History (command undo with transactions, journal that survives reopening) | P4 |
| E6 Compare (ghost lane, A/B with one playhead, AI/Optimize result as diff with accept/revert) | P4 |
| E7 Quality on the timeline (duplicates, spikes, too fast for device, tracking gaps; Speed Guard) | P4 |
| E8 Device preview (only moves the device after "Arm") | P4 with P2 |
| E9 Export pack (one file, one axis, companion pack, round-trip check) | P4 |
| E10 Scale (1-hour video, 100 000+ points smooth at 1366x768 and 1920x1080) | P4 |

### Next Level capabilities (`docs/next/PRIORITIZED_CAPABILITY_REGISTER.md`, `MASTER_PLAN.md`, `ROADMAP.md`)

All 12 are specified only; none was built.

| ID | Capability | Priority | Phase |
|---|---|---|---|
| gate | Golden benchmark runner: manifest v2, report v2, no-regression gate (needed before any C++ claim) | first | P3 (gate for P3) |
| CAP-12 | Project Recovery (checkpoints, restart, no duplicate artifacts) | P0 prerequisite | P3 |
| CAP-02 | Smart Batch (persisted queue, pause/resume, retries, reports; needs CAP-12) | P0 | P3 (basic queue) / Later |
| CAP-03 | Quality Compare (classical vs classical first, then AI) | P0 | P4 (with E6) |
| CAP-01 | AutoDirector (scene split, per-scene backend choice, selective retry, verifier) | P0 | Later |
| CAP-05 | Intelligent Repair (local gap/jump repair with undo) | P0 | Later |
| CAP-04 | Device Optimizer (capability-aware mapping, mock 0/1/2/6 axis devices) | P0 | P2 (mapping, mocks) / P4 |
| CAP-06 | Scene Studio (lock, regenerate, merge scenes) | P1 | Later |
| CAP-07 | Multi-Target Tracking | P1 | Later |
| CAP-08 | Personal Motion Profiles (opt-in learning, rollback) | P1 | Later |
| CAP-10 | Professional Export (device profiles, multi-axis companions, reports) | P1 | P4 (with E9) / Later |
| CAP-11 | Hardware Benchmark (backend choice by measurement) | P1 | Later |
| CAP-09 | VR / 180° / 360° | P2 | Later (research track) |
| ENG-05 | Optional local AI assist (LM Studio / ONNX), always behind the verifier | — | P4 |

### Open tasks from the old handoff and agent state

| Item | Source | Phase |
|---|---|---|
| Keep Suite files out of public releases; guard step in release workflow (D-010) | `HANDOFF` task 1; done in old repo | P5 |
| Unknown MP4 codec must be reported as unknown, not h264 (MEDIA-01, done in old repo) | `HANDOFF` task 2 | P1 (port the behaviour and its test) |
| Frame type without per-frame PTS (`GrayFrame`); VFR needs a PTS timeline | `HANDOFF` "Findings" | P3 |
| Training scripts only in the Suite build | `HANDOFF` task 3 | P4 (depends on D-007) |
| Profile of the generator on the Owner's real clips (`clip_ausschnitt`, `clip_voll`) | `generator-profile-2026-10.md` "Still needed" | P3 (Owner supplies clips) |
| Camera estimate on a second thread; scene-cut check on a small picture; CSRT on a cropped region | same, "Next candidates" | P3 |
| Owner gates: speed-cap default, rhythm-grid default (≥4–5 clips), learned stages need a golden win | `ENGINEERING_STANCE` "Owner gates" | P3 / P4 |
| Hardware test of stroker filter, home on pause, beat-bar scripts with the Sam Neo 2 | `AGENT_STATE` "Waiting on the Owner" | P2 (Owner) |
| Sentry for development builds only (needs DSN) | `AGENT_STATE` "Open" 7 | P5 |
| Neo 2 raw-value profile from real diagnostics (G2.2–G2.5) | `PRODUCTION_ROADMAP` G2 | Later (Owner opens) |
| PoseObserver, MT-ID, depth/pose defaults | `PRODUCTION_ROADMAP` G3 | Later (Owner clip gates) |
| Updater source and release host (old D13), edition split (D-004, D-007), C# role (D-104) | `OPEN_DECISIONS`, `PLAN.md` | Decide before P5 |
| Windows code signing (D-149 builds are unsigned). Options checked on 19 Oct 2026: SignPath Foundation (free for OSI-licensed public projects; publisher shown as "SignPath Foundation"; asks for verifiable reputation; each signing approved by hand), Certum Open Source certificate (from about 49 €; shows "Open Source Developer, <real name>"), Microsoft Artifact Signing (individuals only in the US/Canada). No certificate removes SmartScreen's first warnings, EV included since 2024: reputation grows with downloads. Suggested: try SignPath first, Certum as fallback if the Owner accepts his name in the certificate. Both need the source public. | Owner question, 19 Oct | Decide before P5 |
| Tag v0.7.0 and delete old branches (old repo) | `AGENT_STATE` | Owner |
| macOS build, phone player (no generator) | `PLATFORMS.md` | Later |
| Cleanup of `samn_file.py` (own chapter/bookmark loaders, methods attached to `Document` after the class) | `MODULES.md` | P1 |
| Open `.samn` in the Play page, soft seek, recent files | `AGENT_STATE` (new) "Next" | P1 |
| Port C++ tracking and camera estimate from `trackcv/cv.cpp` | `AGENT_STATE` (new) "Next" | P3 |

---

## (c) Lessons learned

| What went wrong | Rule that follows | Source |
|---|---|---|
| A foreign agent's first pushes replaced `tests.yml` and two docs with the literal text `$(cat /tmp/...)`, one file ended in "[truncated ...]", and four CI jobs were dropped. | Run `git diff --stat` before every push; never write a file from a shell substitution that may be empty; never regenerate a workflow from scratch; each agent pushes only to its own branch. | old `AGENT_STATE` "Who works how"; new `AGENT_STATE` "Rules" (D-117, D-114) |
| The CMake generator was hardcoded to Visual Studio in CI, which broke the build on the runner. | Never hardcode a CMake generator; let the platform default decide, and handle multi-config output (`build/Release`) explicitly, as `ci.yml` does now. | Lead's brief; no written source found. `.github/workflows/ci.yml` shows the current pattern |
| pytest-qt's Qt log capture crashed when Qt's FFmpeg thread logged. | Run with `--no-qt-log` (set in `pyproject.toml`). | new `AGENT_STATE` "Test gotcha" |
| `os.fchmod` does not exist on Windows. | Guard POSIX-only calls with `hasattr` and run CI on Windows from the start. | `samn/formats/marks.py` lines 258–259 (comment in code) |
| QtMultimedia failed to import on Linux CI without PulseAudio. | Install `libpulse0` (and the other Qt system libraries) in the Linux job. | `.github/workflows/ci.yml` comment |
| A half-size camera estimate was 39 % faster but cut r from 0.94 to 0.71 (lost quality). | A speed change needs a measured gate: equal r first, then time; 3 warm-up + 5 measured runs, median and p95 (D-068, D-069). | `docs/owner/generator-profile-2026-10.md` |
| Moving from Go to C++ would not have made Generate faster: 99 % of the time was OpenCV/FFmpeg and Go code was about 0 %. | Profile before porting; speed comes from what runs, at which size and on how many threads, not from the language. | same |
| A skipped native test returned exit 0 and hid a missing module. | `SAMN_NATIVE_REQUIRED=1` in CI: a missing module fails (D-113). | old `AGENT_STATE` "In progress" M1; `ci.yml` |
| `std::stod` stops at the colon, so a duration like "10:31" was read as 10 ms in the C++ funscript port. | Port with parity tests against the old behaviour and add the odd cases (OFS durations, float rounding, corrupt actions). | old `AGENT_STATE` M1 |
| A script in the wild starts at −922 337 203 565 470 ms and ran the app out of memory. | Drop actions before 0 or after 48 h on load; keep the 758-script corpus as a standing test (D-076). | `CHANGELOG.md` 0.7.0 "Fixed" |
| The updater picked the wrong edition because assets were matched by name suffix. | Match the exact file name, never a suffix (D-017). | `CHANGELOG.md` 0.6.0 "Fixed" |
| Suite files would have been attached to the public release. | CI compiles the internal build only; a guard step fails if a published file name contains `suite` (D-010). | `OPEN_DECISIONS` D14; `HANDOFF` task 1 |
| An unknown MP4 codec was reported as h264/yuv420p, so the "Make playable" proxy was not offered. | Report unknown as unknown; a missing value never defaults to "fine". | `HANDOFF` task 2 (MEDIA-01) |
| A settings save in a loop could leave a half-updated file. | Save related settings in one atomic write; write files atomically (funscript and settings). | `CHANGELOG.md` Unreleased "Fixed"; `PRODUCTION_ROADMAP` v0.5.42 (#428) |
| Whole-clip correlation with FunGen looked bad because of swinging lag and periodicity aliasing, not because tracking was bad. | Use windowed best-lag r; report Signal Quality and Motion Fidelity separately (D-066). | `TFTJ_PROFILE_DIRECTION.md` "Diagnosis"; `AGENT_COORD.md` F-003 |
| Fusing four zones into the curve gave flatlines (#205); auto-committing a second ROI was measured bad (issue #8). | One tip, one CSRT box writes the stroke; proposals never commit silently (D-050, D-055). | `AGENT_COORD.md` "4-Zone → 1-Zone"; `TFTJ_PROFILE_DIRECTION.md` |
| Proactive re-anchoring against CSRT drift made the clip worse (10 large jumps vs 2–3) and was reverted. | Measure a fix on the real clip before keeping it; revert with `git revert`, never force-push. | `AGENT_COORD.md` Decision log 23 Sep |
| The OpenCL checkbox forced the weaker Python path on old builds, and Windows ran Python CSRT until the native path was linked. | No silent fallback to a weaker path; hard error with a clear message; keep Windows and Linux at parity. | `TFTJ_PROFILE_DIRECTION.md` "Diagnosis"; `PRODUCTION_ROADMAP` G0.1/G0.3 |
| The stroke preview read 2–3 times the real stroke rate and its CI test was flaky because encoder thread count changed the noise. | Tests use deterministic fixtures; noise floors are explicit. | `CHANGELOG.md` 0.5.46 "Fixed" |
| Three to four agents on one board collided in rebases and lanes (22 Sep: "stop the chaos"); later two agents still collided on 15 Oct. | One theme per agent, one branch owner, `main` only via PR with green CI (D-114, D-115, D-122). | `AGENT_COORD.md` "Rebase hygiene"; `AGENT_STATE` |
| Too many themes moved in parallel and a paid-Studio plan was written and then closed (PR #510). | One theme at a time behind a gate; treat proposals as proposals until the Owner decides and the decision is logged (D-003, D-067). | `PRODUCTION_ROADMAP` "Rule: one theme"; `EDITIONS.md` |
| Plan documents disagreed with each other (Go core vs full replacement, Next Level repo vs same repo, PySide6 vs C++ first). | Log every decision with a date in `DECISIONS.md`, mark conflicts, and update `PLAN.md` in the same PR as the decision. | `DECISIONS.md` CONFLICT rows |
