# Versions and plans: what SamNPlayer 2 must still carry

Draft of 16 Oct 2026. Source: the old repo `/home/user/SamNPlayer` (read-only; `origin/main` at `b502b49`, VERSION 0.7.0, 666 commits, 50 tags). SamNPlayer 2 docs checked: `ROADMAP.md`, `PARITY.md`, `DECISIONS.md`, `PLAN.md`, `P3_GENERATOR.md`, `MODULES.md`.

Method: tags, `CHANGELOG.md`, `VERSION` history, `PRODUCTION_ROADMAP.md`, and a mechanical diff between old tags and `origin/main` of (1) Wails `App` methods, (2) exported Go functions in `funscript player device samn sam generator videox update edition`, (3) Python `def` names in `generator/`, (4) element ids and visible labels in `frontend/index.html` and `frontend/src/*.js` for all 50 tags.

**Result of the diff:** between any old tag and 0.7.0 only two code areas disappeared completely: the licence system and the Virtual Person plugin host. Every other loss is a control removed from the GUI (engine code stayed). No exported Go function and no Python function of `generator/` was deleted without one of these two reasons. No function was found that is lost without a reason (see §2.3 for the watch-list).

---

## 1. Version timeline

55 versions: 50 tags, plus 0.4.1, 0.5.32, 0.5.46, 0.6.0 (never tagged; their content shipped in the next tag) and 0.7.0 (prepared; the Owner pushes the tag, not yet in `git tag`). "Unreleased" on main adds the stroker motion filter, script folders and variants, the beat-bar reader and a new empty-Player home (v0.8 items 1 and 2).

Dates: tag date; untagged versions use the CHANGELOG date. Sources: `CHANGELOG.md`, `docs/PRODUCTION_ROADMAP.md` (release train), `VERSION` history.

| Version | Date | Main features |
|---|---|---|
| 0.2.1 | 2026-09-14 | First tag. CLI + Wails GUI, Sam Neo 2 over BLE, funscript player with video sync, Extended-O, training tab; Tf/Tj profile and second region (ROI2); optional local AI region proposal (ONNX) |
| 0.2.2 | 2026-09-14 | AI profile suggestion and quality second opinion (local server), issue #8 measurement findings |
| 0.3.0 | 2026-09-16 | Golden-Clip Benchmark tab, `grid_lk` backend, contact-triggered vibration, manual curve editor, O-zone / ring-down / polarity, Script Doctor, training history, audio-tempo check, secondary O-markers, SAM model package, YOLO bootstrap tooling, dark UI |
| 0.4.0 | 2026-09-16 | Device diagnostics tool, AI-training tab, motion axis auto, Tf and Tj merged |
| 0.4.1 | 2026-09-16 (untagged) | Training dependency check, Windows CSRT fix, reuse of the device connection |
| 0.4.2 | 2026-09-16 | YOLO training batch size auto |
| 0.5.0 | 2026-09-17 | `region_fusion` backend, three CSRT API shapes; dense Quality Doctor and FunGen-compare CLI in Go, help chips, GPU device switch for AI training |
| 0.5.1 | 2026-09-17 | Doc cleanup, phase CLI flags, `ENGINE.md` |
| 0.5.2 | 2026-09-17 | Go generate without OpenCV/Python on Windows (simpletrack), cancel tests |
| 0.5.3 | 2026-09-17 | Log tab, CLI `generate`, FunGen compare docs, Go generate automatic |
| 0.5.4 | 2026-09-18 | Contact-vibration controls (sensitivity, curve), SAM enrich / sidecar / Densify / RuntimeAdjust |
| 0.5.5 | 2026-09-18 | OpenCV 5 CSRT, autotune stack, OFS bookmarks/chapters, playlist |
| 0.5.6 | 2026-09-19 | English UI, class-aware AI ROI, playback codecs, accessibility |
| 0.5.7 | 2026-09-19 | English logs, depth/pose supporting signals |
| 0.5.8 | 2026-09-19 | Tooltips, playlist shuffle/repeat |
| 0.5.9 | 2026-09-19 | Native `.samn`, portable ffmpeg, Go audio check, production roadmap |
| 0.5.10 | 2026-09-20 | Bugfix, GUI motion, multi body-part, soft masks; weak backends off the GUI; licence block |
| 0.5.11 | 2026-09-20 | AI Train OpenCV, progress UI, one CSRT path |
| 0.5.12 | 2026-09-20 | Sequential Generate GUI, G0-G4 spine |
| 0.5.13 | 2026-09-20 | Windows OpenCV CSRT in the release binary |
| 0.5.14 | 2026-09-20 | Tf/Tj tip end, Zone 3+ class, `TrackMultiPoints` |
| 0.5.15 | 2026-09-20 | Tf/Tj fusion excludes lost partners |
| 0.5.16 | 2026-09-21 | OpenCL automatic (checkbox removed) |
| 0.5.17 | 2026-09-21 | Contact vibration on Normal/Autotune, golden/phase window |
| 0.5.18 | 2026-09-21 | Autotune tip only, aliasing-risk flag |
| 0.5.19 | 2026-09-21 | Motion-candidate overlay (pick the primary stroke) |
| 0.5.20 | 2026-09-21 | 4-zone opt-in, Contact-first (Tf/Tj no longer needed) |
| 0.5.21 | 2026-09-21 | Profile rename, Play feel decouple |
| 0.5.22 | 2026-09-22 | Training clip + ring, Everyday Generate (auto tip find), HiDPI playback, stroke preview, Review/Improve, Optimize for Neo 2 |
| 0.5.23 | 2026-09-23 | Portable training clip frames, multi-track (tip + partner) |
| 0.5.24 | 2026-09-23 | Improve sidecar, training presets, pose observer research |
| 0.5.25 | 2026-09-23 | Contact marks UI, training improve |
| 0.5.26 | 2026-09-23 | Contact marks persist, Play overlay, 4-zone removed from GUI |
| 0.5.27 | 2026-09-23 | Feel Stage A, training suction fixes |
| 0.5.28 | 2026-09-23 | Emotion look, Create-to-Play feel, CSRT jump/memory guards |
| 0.5.29 | 2026-09-23 | Stroke detrend on by default |
| 0.5.30 | 2026-09-23 | Rhythm grid opt-in, Advanced toggle |
| 0.5.31 | 2026-09-24 | Profile model, semantic target lock, Scene Map P1/P2 |
| 0.5.32 | 2026-09-27 (untagged) | Scene Map P3-P5, Look, AI write, Play marks |
| 0.5.33 | 2026-09-27 | Plugin drop folder, Generator Follow/Ignore marks |
| 0.5.34 | 2026-09-27 | Vibration impulse/spatial, AI draft script, Intiface live + reconnect, Marks S2/S3, ID lock |
| 0.5.35 | 2026-09-28 | Scene2 proposals, Apply AI, auto review |
| 0.5.36 | 2026-09-28 | AI Train YOLO box overlays |
| 0.5.37 | 2026-09-28 | Large AI Train review editor, Windows AI setup scripts |
| 0.5.38 | 2026-09-28 | Heal + fill repair, update popup fix, Benchmark MVP, patch channel |
| 0.5.39 | 2026-09-28 | GUI guide alignment, Improve heal label |
| 0.5.40 | 2026-09-28 | Contact Verify, Bench Suggest |
| 0.5.41 | 2026-09-28 | Tip-Find, Clip-Prep cutter, reacquire, E2E tests |
| 0.5.42 | 2026-09-29 | GapFill, knobs/probes, Plateau from floor, zero-GUI, seek/CLI, atomic funscript |
| 0.5.43 | 2026-09-30 | Train window labels, ROI empty-val heal, CLI cancel |
| 0.5.44 | 2026-09-30 | Video swap, Contact-vibe tip + partner, OFS load/companion, scene roles, AI free tags |
| 0.5.45 | 2026-10-02 | Taller Play heatmap, Tip-to-Partner snack, declutter |
| 0.5.46 | 2026-10-10 (untagged) | Audio-only vibration switch, stroke-preview fix, contact points on a second clip, Epochs cap, BPM from audio, write offset into script, freeware (licence keys removed) |
| 0.6.0 | 2026-10-12 (untagged) | V1 workspace, Flow look, Invert Curve, Sound Curve, undo/redo, skip +/-10 s, sleep timer, speed 0.5-2x, Auto sync, device capabilities, Suite edition |
| 0.7.0 | 2026-10-14 (VERSION 10-15; tag pending) | Five modes (Mode pill), video first, Generator with editor, Sam Neo 2 feel per scene, vibration from sound, TCode (Wi-Fi/USB, extra axes), stroker safety range, soft seek, corrupt-time fix (758-script corpus) |

Note: `CHANGELOG.md` has two sections headed 0.5.0 (16 and 17 Sep); the `VERSION` history has no 0.5.0-to-0.5.9 gaps.

---

## 2. Functions from earlier versions

Legend: **(a)** removed on purpose, **(b)** replaced, **(c)** lost without a reason. SamNPlayer 2 status checked against `PARITY.md`, `ROADMAP.md`, `DECISIONS.md`, `P3_GENERATOR.md`.

Count: **(a) 13, (b) 14, (c) 0 confirmed** (3 watch-list items in §2.3).

### 2.1 Table

| # | Function | Version(s) | What happened | Source | SamNPlayer 2 |
|---|---|---|---|---|---|
| a1 | Licence-key system: import/clear/status, trial limit, `.samn` play block | 0.5.10 - 0.5.45 | (a) removed, freeware with EULA | CHANGELOG 0.5.46 / 0.6.0 ("freeware, keys removed"); commit f008132 | Decided: D-001, D-002, D-015. Stays out |
| a2 | Paid Studio, supporter pass above EUR 40, 7-day expiring preview | planned 12 Oct, never shipped | (a) dropped | PR #510, #515; `docs/EDITIONS.md` | D-003, D-006 |
| a3 | Virtual Person plugin host (enable/disable, status) | 0.5.33 - 0.5.34 | (a) cancelled, product scrubbed, drop-folder infra kept | CHANGELOG 0.5.35; `docs/owner/roadmap.md` ("cancelled"); #264, #341 | **Not named in any SamNPlayer 2 doc.** Plugin drop folder is in PARITY. Add a decision row |
| a4 | 4-zone tracking (GUI button, Advanced "Tracking method") | 0.5.20 - 0.5.25 opt-in; removed 0.5.26; dead button `gen-nomark` until 0.5.44 | (a) removed after measurement | CHANGELOG 0.5.26, 0.5.21, 0.5.46; `TFTJ_PROFILE_DIRECTION.md` | D-050, D-064 |
| a5 | Flow, `grid_lk`, `region_fusion` in the backend dropdown | 0.3.0 - 0.5.9 | (a) off the GUI, CLI only | CHANGELOG 0.5.10 ("Weak generator backends off the product GUI") | Engine planned in C++ (`P3_GENERATOR.md` P3.5); D-050 |
| a6 | Flow downscale control | 0.5.5 - 0.5.21 | (a) CLI only | CHANGELOG 0.5.22 | Follows a5 |
| a7 | GPU / OpenCL checkbox | 0.2.1 - 0.5.15 | (a) removed, OpenCL automatic | CHANGELOG 0.5.16 | `P3_GENERATOR.md` "GPU where available" |
| a8 | AI "second opinion" on quality (checkbox) | 0.2.2 - 0.5.21 | (a) dead knob removed; code and Python script stay | CHANGELOG 0.5.22 ("Dead Advanced knobs removed"); `AI_ADAPTER.md` (no field data) | Named only as a Python item in `P3_GENERATOR.md` and PARITY. No decision |
| a9 | Separate Tf and Tj profiles; Tf/Tj in the product GUI | 0.2.1 - 0.5.19 | (a) merged 0.4.0, off the product GUI 0.5.20 | CHANGELOG 0.4.0, 0.5.20 | D-053 |
| a10 | Light mode, frameless window, card layout, Wails v3 | idea 0.5.7 | (a) rejected | `ROADMAP.md` (old) "Evaluated 19 Sep" | D-094, D-090 |
| a11 | KCF/MOSSE, parallel two-tracker, upscaling before tracking, WebGL sharpen, C#/.NET rewrite | tried 0.3.0, never shipped | (a) rejected | CHANGELOG 0.3.0 "Investigated and explicitly not shipped"; `HANDOFF.md` | D-025 |
| a12 | NCC "simpletrack" and Python CSRT as a product path | 0.5.2 - 0.5.12 | (a) lab/CLI only | `PRODUCTION_ROADMAP.md` G0.3, G0.4 | Not needed: SamNPlayer 2 builds OpenCV (P3.0) |
| a13 | Cloud sync, community platform, streaming plugins, WebCodecs, self-hosted cloud | ideas 19 Sep | (a) rejected | `ROADMAP.md` (old) "Evaluated 19 Sep" | D-027, D-028, D-048 |
| b1 | Top tabs Playback / Generator / Bench / AI training / Device | 0.2.1 - 0.5.45 | (b) Player workspace, then five modes + Mode pill + Tools home | `V1_PLAN.md`; CHANGELOG 0.6.0, 0.7.0 | D-091 |
| b2 | Play / Edit toggle | 0.6.0 | (b) Editor is a mode | CHANGELOG 0.7.0 | D-091 |
| b3 | "Optimize for Neo 2" | 0.5.22 - 0.6.0 | (b) "Optimize for" (Any device / Sam Neo 2) | CHANGELOG 0.7.0 | D-085 |
| b4 | Manual motion-axis choice | 0.2.1 - 0.3.0 | (b) automatic, forcing x/y stays | CHANGELOG 0.4.0 | PARITY (invert / axis) |
| b5 | "Go pipeline (experimental)" checkbox | 0.5.0 - 0.5.2 | (b) automatic | CHANGELOG 0.5.3 | Moot |
| b6 | "Export .funscript" button | to 0.5.21 | (b) "Share for other apps" | commit 8009331 | PARITY |
| b7 | German UI | 0.2.1 - 0.5.8 | (b) English | CHANGELOG 0.5.6, 0.5.9 | D-089 |
| b8 | Browser `confirm()` update popup | to 0.5.9 | (b) in-app banner + patch channel | CHANGELOG 0.5.10, 0.5.38 | PARITY (update banner, patch) |
| b9 | Pixel-mosaic training clip | 0.5.2x | (b) Clip / Rings / Waves / Video stage | commit a155a8c; CHANGELOG 0.7.0 | ROADMAP Training |
| b10 | Region-training controls `rt-*` in Generator | 0.4.0 - 0.5.4 | (b) AI Train window | CHANGELOG 0.5.5 | ROADMAP (AI Train, Suite) |
| b11 | Python audio check, Python Script Doctor | 0.3.0 - 0.5.8 | (b) Go | CHANGELOG 0.5.0, 0.5.9 | C++ in P3.6 |
| b12 | Hand-drawn region only | 0.2.1 - 0.5.21 | (b) auto tip find (Everyday) | CHANGELOG 0.5.22 | D-050 |
| b13 | Tools sub-nav | to 0.6.0 | (b) Tools home with real status | CHANGELOG 0.7.0 | ROADMAP Tools |
| b14 | Duplicate JS `roi_cancel.js`, `settings_honor.js` | 0.5.4x | (b) merged | CHANGELOG 0.5.45 | Moot |

Not removed, only reworded (checked): script offset +/-50 ms buttons, Remember scene (under "Power-user"), log level, cache clear, update-on-start, device diagnostics, ring-down, polarity, O-zone hotkey, frame snap, playlist, quality feedback, motion signature, profile suggestion. All still in 0.7.0; all in PARITY except SAM items (see §4).

### 2.2 Removed on purpose — why (lessons)

These stay out of SamNPlayer 2 (Owner, 16 Oct). The lesson is the point. "C++ note" says whether a limit of Go, Python or the browser caused the removal, and whether the C++ Generator (`P3_GENERATOR.md`) could change it. A C++ revival is allowed only if the original reason no longer applies and the golden clips show a win (D-068, D-122).

| # | What went wrong | What was learned | Do better in SamNPlayer 2 | C++ note |
|---|---|---|---|---|
| a1, a2 | Licence infra (Ed25519 keys, trial, `.samn` play block) and a paid Studio plan were built before the product and its price were settled; the plan was written, then closed (#510, #515); a dev key sits in git history | Business model first, infra second. Gating code adds friction and risk (a key in history) | No licence code, no trial limits. If keys ever return: new key, decision first (D-015) | None |
| a3 | A character/plugin product grew next to the core and was cancelled after it was built | Side products dilute the one core job; cancel early | Keep only the generic plugin drop folder. One theme at a time (D-122) | None |
| a4 | 4-zone fusion gave flat lines (#205); on `clip_ausschnitt` windowed r was 0.363 (4-zone) vs 0.468 (tip CSRT) vs 0.590 (hub). It was already hidden at 0.5.25 and removed at 0.5.26 | More trackers do not mean a better stroke; measure on the same clip before any default | A fusion mode needs a golden win first | Not a language limit. Quality limit. P3 item 1 ("run backends together") is the same idea with confidence weights; it stays off unless it beats 0.7 |
| a5, a6 | Flow, `grid_lk`, `region_fusion` were Python-only, slower (Farneback about 36 ms per frame at 0.5 scale, 56 s for 50 s of 720p) and "fusion of two sources always lands between them, never beats the best" (`HANDOFF.md`) | A backend needs its own benefit; extra Python dependency is a cost | Port only behind the benchmark gate (P3.5) | **Partly a Python/speed limit**: slow because of the Python/cv2 loop. C++ may make flow usable; the quality finding still stands |
| a7 | The OpenCL checkbox forced the weaker Python path and skipped Go CSRT | A switch must never silently choose the weaker path (ROADMAP lesson, D-024) | Hardware choice automatic; log it | Library limit: stock OpenCV CSRT is not GPU-accelerated. C++ DNN on CUDA/OpenCL is a separate gain (P3 item 6) |
| a8 | A second AI opinion on quality had no field data showing it helps; ballast on the Advanced panel | Do not ship a knob without evidence (D-080) | Keep Quality Doctor rule-based; add AI only with held-out proof | Python/server dependency (local server). ONNX in C++ removes the server, not the missing evidence |
| a9 | Tf and Tj were the same recipe under two names; Tf/Tj on real clips scored r about 0.06-0.10 vs stroke mode 0.18-0.26 | Do not offer two identical options; a distance profile did not beat the stroke | Tf/Tj is a feel recipe on top of the best stroke path (D-053) | Quality limit |
| a10 | Light mode, frameless window, card layout would have broken the settled look | Polish must not change identity | Keep the Flow look (D-090) | Qt/QML may make a frameless window easy; still no reason to add it |
| a11 | KCF/MOSSE collapsed on small regions; upscaling was unstable; WebGL sharpening was measurably worse because the loss happens at frame extraction | Test the claimed gain on hard regions, not only the average | Keep the rejected list as a regression check | WebGL/browser limit for sharpening: gone in Qt, but upscaling is still unstable (quality) |
| a12 | Template-matching fallback was a weaker tracker; Windows ran it because OpenCV was not linked | No silent fallback to a weaker path (D-024) | One tracker path, hard error with a clear message | **Missing library limit**: OpenCV not linked on Windows. P3.0 builds OpenCV on both systems, so no fallback is needed |
| a13 | Cloud features contradict local-first | Local only, no accounts | D-027, D-028 | None |

### 2.3 (c) Lost without a reason — watch-list

Confirmed (c): **none**. Items that deserve an Owner look:

| Item | Evidence | Why suspicious |
|---|---|---|
| `find_two_rois` (AI proposes two separated regions) | Built in 0.3.0, "not wired into generation", still unwired in 0.7.0 (CHANGELOG 0.3.0; `ROADMAP.md` old "Open tasks") | Not removed, never finished. SamNPlayer 2 only has D-055/D-056 (never commit silently), no item to build it |
| `gen-tftj-hint` and `rt-docs-link` elements (0.2.1 / 0.5.6 - 0.5.44) | Gone in #460 "declutter"; no CHANGELOG line names them | Most likely moved with Tf/Tj and AI Train; unverified |
| `.sam` sidecar, SAM Densify, SAM RuntimeAdjust (live contact strength / sensitivity / curve), CLI `sam` | Added 0.5.4; code in `sam/densify.go`, `sam/runtime.go`, `app_playback.go` in 0.7.0 | **Present in 0.7.0 code but in no SamNPlayer 2 doc** (grep of PARITY, ROADMAP, DECISIONS, PLAN found nothing). A silent gap in the parity list |

---

## 3. Planned functions

Sources read in full or by section: `ROADMAP_2026-10.md`, `ROADMAP.md`, `PRODUCTION_ROADMAP.md`, `NEXT.md` (priority list and "Later"), `V1_PLAN.md`, `V1_TARGET_FEATURE_MAP.md`, `V1_VISUAL_TARGETS`, `next/**` (master plan, roadmap, capability register, five-engine, review, handoff, specs, sources), `owner/roadmap.md`, `owner/motion-recognition-next.md`, `DEVICE_SUPPORT.md`, `PLATFORMS.md`, `SCENE_MAP_PLAN.md`, `SCENE_UNDERSTANDING_PLAN.md`, `VLM_TEACHER_PLAN.md`, `AI_SCRIPT_WRITER.md`, `TEAM_STAND.md`, `AUDIO_WORKFLOW.md`, `CONTENT_SOURCES.md`, `OFS_LEARN.md`, `GUI_MAKEUP.md`, `EDITIONS.md`, `COMPETITIVE.md`, and the deleted `IDEAS_BACKLOG.md` and `perception_update_2026-09/10_backlog.md` (read from git history). Versions named in the old plans: v0.8, v0.9, "1.0" (V1_PLAN, became 0.6.0/0.7.0), "2.0 Next".

Status: **covered** (where), **missing** (in no SamNPlayer 2 doc), **against** (D-number).

| Item | Source | Target | SamNPlayer 2 |
|---|---|---|---|
| Script folders, variants, tolerant names | ROADMAP_2026-10 §3 #1 | v0.8 (done, Unreleased) | covered: ROADMAP P1 |
| Smooth stroker output (PCHIP/MAKIMA, speed limit, fill, home) | §3 #2 | v0.8 (done) | covered: D-040, ROADMAP P2 |
| Audio files with a script (MP3, FLAC) | §3 #3 | v0.8 | covered: ROADMAP P4 |
| Library as tiles (thumbnails, hover, sort, rating) | §3 #4 | v0.9 | covered: ROADMAP P4 |
| Skip long pauses; shortcut sheet, customisable | §3 #5 | v0.8 | covered: ROADMAP P4 |
| Device wizard | §3 #6 | v0.9 | covered: ROADMAP P4 |
| Native Handy (upload, HSSP) | §3 Large | later | covered: D-039, ROADMAP Later |
| Follow external player (VLC, MPV, DeoVR, HereSphere) | §3 Large | later | covered: ROADMAP Later |
| Session recap | §3b | idea | covered: D-088, ROADMAP Later |
| Vorze CSV import; folder multi-axis heatmap | §3b | idea | covered: ROADMAP Later |
| Beat bar: audio onset fallback, beat-bar overlay in Editor | §3b | open | covered: ROADMAP P3/P4 |
| UwU-style library badges (watched/NEW), tags, NFO tags | §3b | item 4 | partly: PARITY has Imported/Generated/Edited badges; tags and NFO **missing** |
| Lovense bridge, chat control, game mode | §3b, §5 | not now | against: D-047 |
| In-app EroScripts browser | §5 | not now | against: D-048 |
| BuzzyBody / Fap Hero Journey style apps | §3b | not now | **missing** (no decision; "far from core") |
| Editor E1-E10 | §4 | v0.8 (E1-E2), v0.9 (E3-E5), later | covered: D-081..D-083, ROADMAP P4 |
| Bezier / checked interpolation in the editor | NEXT_LEVEL_MASTER_ROADMAP N8 | next | **missing** |
| Golden benchmark runner (manifest v2, gate) | next/ROADMAP N2, P4 spec | before any C++ claim | covered: D-068, D-072, ROADMAP |
| Project Recovery (CAP-12), Smart Batch (CAP-02) | capability register | P0 | covered: ROADMAP, D-072, D-078 |
| AutoDirector (CAP-01), Intelligent Repair (CAP-05), Device Optimizer (CAP-04), Quality Compare (CAP-03) | register | P0 | covered: D-077, ROADMAP |
| Scene Studio (CAP-06), Multi-Target (CAP-07), Personal Motion Profiles (CAP-08), Professional Export (CAP-10), Hardware Benchmark (CAP-11) | register | P1 | covered: D-077, ROADMAP Later |
| VR / 180 / 360 (CAP-09) | register | P2 | covered: ROADMAP Later (research) |
| Auto / Guided / Expert Create views | FIVE_ENGINE | 2.0 Next | **missing** |
| Library: relink broken media, project library, duplicates | FIVE_ENGINE, ED-10 | 2.0 Next | **missing** |
| Multi-device simultaneous output | STUDIO_EDITOR_BLUEPRINT §3 | separate gate | **missing** |
| `.samn` v2 axis map (P5) | next/specs/P5 | next | covered: D-105, ROADMAP |
| Release safety / updater source (P8, N1) | next/specs/N1 | before 0.7 tag | covered: D-010, D-012, D-017 |
| Unknown MP4 codec = unknown (MEDIA-01) | HANDOFF task 2 | done | covered: ROADMAP P1 |
| PTS per frame (`GrayFrame`) | HANDOFF findings, review M1 | N2 | covered: ROADMAP P3 |
| Audio PCM seam, native decoder prototype (Media Foundation, Symphonia) | review §4 | later | not needed: C++ uses OpenCV/FFmpeg. Not recorded as a decision |
| Learning data contract, grouped hold-out, model registry, rollback | review §6 | N6 | partly: D-029, CAP-08 in ROADMAP; the data contract itself **missing** |
| Local AI assist (LM Studio / ONNX) behind verifier | ENG-05 | N5 | covered: D-031, D-059 |
| Neo 2 raw-value profile (G2.2-G2.5) | PRODUCTION_ROADMAP G2 | Owner opens | covered: D-049, ROADMAP Later |
| Look-ahead / latency compensation, prediction | G2.5; perception backlog P4 | after measured lag | **missing** |
| PoseObserver, depth, MT-ID, segmentation layer | G3.4/G3.5; POSE_OBSERVER | Owner clip gates | partly: PoseObserver, depth, ByteTrack in ROADMAP/D-064; segmentation layer ("SAM 3.1" class) **missing** |
| Perception v1 fusion, Auto-Evaluator, confidence calibration | old ROADMAP "Perception 2.0", SAM_ARCHITECTURE | each needs a go/no-go | **missing** as named items (P3 item 1 is a related idea) |
| Camera Motion Compensation 2.0 (confidence, geometric models) | old ROADMAP | go/no-go | **missing** |
| Climax ("cum") detection | NEXT §6 ("urgent", 14 Sep); old ROADMAP "deferred" | undecided | **missing** (PARITY's "Climax" is only the audio Feel band) |
| Audio pre-pass tempo hint; "no motion, fall back to audio" | AUDIO_WORKFLOW, PRODUCTION_ROADMAP G1.3 | G1 | **missing** (D-079 only says audio never invents the curve) |
| Chapters from scene cuts with regeneration of single sections | TEAM_STAND §8 | later | partly: CAP-06 Later; the scene-cut chapter source **missing** |
| Tracking-cache reuse (change parameters without re-reading the video) | TEAM_STAND "Lessons from FunGen 2" | idea | **missing** |
| Hybrid gap-filler (AI only where tracking is lost), scene-type / axis / depth / tempo per shot (stage 3) | SCENE_UNDERSTANDING_PLAN §3, §3b | next | partly: P3 item 2 (re-acquisition); stage 3 **missing** |
| Own multi-class detector trained from teacher labels (RF-DETR, classes penis ... contact) | SCENE_UNDERSTANDING_PLAN §4, VLM_TEACHER_PLAN V3 | later | partly: ONNX body-part detector in P3.7; the teacher-to-detector loop **missing** |
| Scene-map learning stages L1-L4 (priors, region detector, learned cell scorer, fusion) | SCENE_MAP_PLAN M6 | P6+ | partly: ROADMAP "AI Train", "Export for learning"; stages **missing** |
| AI draft stage S4 (seeded from tip ROI + scene marks) | AI_SCRIPT_WRITER | no default | covered: D-058 |
| Second-clip contact points, Verify with engine, Bench Suggest | 0.5.40 - 0.5.46 | done | covered: ROADMAP P4 |
| Quick Access pinned folders ("Demo Scenes", "My Projects") | V1_TARGET_FEATURE_MAP Player | 1.0 | **missing** |
| Note / comment at the playhead (video bar) | V1_TARGET_FEATURE_MAP | 1.0 | **missing** |
| Audio Analysis Spectrum tab (FFT) | V1_TARGET_FEATURE_MAP | 1.0 | **missing** |
| Scene tags (free tags), notes and marker list in scene details, Auto Scene Detect as an action, Scene Merge / Export Scene Map | V1_TARGET_FEATURE_MAP | 1.0 | partly: merge in CAP-06 Later; the rest **missing** |
| Create tabs Templates / Presets / Batch | V1_TARGET_FEATURE_MAP | 1.0 | partly: Batch queue and presets in PARITY; Templates **missing** |
| Training: skip phase, Endurance / Control / Edge Play categories, per-channel caps (Max Vibration / Max Suction), Auto Cool Down, Pause on Limit | V1_TARGET_FEATURE_MAP Training | 1.0 | **missing** |
| Config export / backup, Calibrate quick tool | V1_TARGET_FEATURE_MAP Tools | 1.0 | **missing** |
| Timeline lanes per connected device (D3 open); export of companion axes (D4 open) | DEVICE_SUPPORT | after D5 | partly: E9 export pack; "lanes per device" **missing** |
| Playlist crossfades | old ROADMAP "Evaluated 19 Sep" | later | **missing** (low) |
| Mobile player (iOS/Android, no generator); macOS GUI | PLATFORMS | G4 later | covered: D-018, ROADMAP Later |
| Content catalogs (video + script download) | CONTENT_SOURCES | future | **missing** (D-048 only covers the in-app browser) |
| Plugin / extension SDK, no arbitrary Python plugins | STUDIO_EDITOR_BLUEPRINT SDK-01 | research | **missing** |
| WebSocket bridge to external players | OFS_LEARN | optional later | covered: ROADMAP Later (follow external player) |
| Update signature (Ed25519 asset next to checksums) | owner/patch-system.md "Future" | later | **missing** |
| Sentry for development builds only | AGENT_STATE | Owner DSN | covered: D-030 |
| Forum post, public site sync, README / screenshot pack, issue template `test: <area> - <clip>` | owner/forum-post.md, PRODUCTION_ROADMAP, GUI_MAKEUP | each release | partly: PARITY has links and About; the process **missing** |
| International English website with Donate | FIVE_ENGINE N8 | 2.0 release | partly: D-022 donation links; website **missing** |
| Tag v0.7.0, delete old branches (92), hardware tests | owner/owner-aufgaben.md | Owner | covered: D-126 |

Rejected by the old plans (not to be built; for the record): Light mode, frameless window, card layout, Wails v3, cloud sync, community platform, streaming plugin, WebCodecs, self-hosted cloud, GUI "max out CPU/GPU" slogans, Frequency knob (Neo 2 has none), picture-in-picture (V1_PLAN "Not in 1.0"; D-089-class entries in PARITY only mention it).

---

## 4. Missing in SamNPlayer 2

Items from §2 and §3 that no SamNPlayer 2 document covers. Grouped by area. The first group of each area is "was in 0.7.0 code but not in PARITY"; the rest is planned.

**Player**
- Quick Access pinned folders (V1_TARGET_FEATURE_MAP, Player).
- Note / comment at the playhead, stored in script metadata (same file).
- Audio Analysis "Spectrum" tab from an FFT of the decoded audio (same file).
- Playlist crossfades (old ROADMAP, "Evaluated 19 Sep", low).

**Generator / Editor**
- SAM sidecar `.sam`, Densify, RuntimeAdjust (live contact strength, sensitivity, curve), CLI `sam`: in 0.7.0, absent from PARITY (CHANGELOG 0.5.4).
- Bezier / checked interpolation in the editor (NEXT_LEVEL_MASTER_ROADMAP N8).
- Auto / Guided / Expert Create views (FIVE_ENGINE).
- Audio pre-pass tempo hint and "no motion, fall back to audio" warning (AUDIO_WORKFLOW, G1.3).
- Tracking-cache reuse without re-reading the video (TEAM_STAND).
- Chapters from scene cuts with regeneration of single sections (TEAM_STAND §8).
- Templates tab in Create (V1_TARGET_FEATURE_MAP).
- Camera Motion Compensation 2.0 with confidence (old ROADMAP, Perception 2.0).
- Perception v1 fusion, Auto-Evaluator, confidence calibration (old ROADMAP; SAM_ARCHITECTURE).
- Climax detection (NEXT §6; undecided, needs a design decision).
- `find_two_rois` AI two-region proposal, built in 0.3.0, never wired.
- Segmentation precision layer (SAM-class), prediction / look-ahead (old ROADMAP, G2.5).

**Devices**
- Multi-device simultaneous output (STUDIO_EDITOR_BLUEPRINT §3; separate safety gate).
- Timeline lanes per connected device (DEVICE_SUPPORT D3 open).
- Latency compensation and look-ahead after a measured end-to-end lag (G2.5).

**Training**
- Skip phase; Endurance / Control / Edge Play categories as filters; per-channel caps (Max Vibration, Max Suction); Auto Cool Down; Pause on Limit (V1_TARGET_FEATURE_MAP, Training).

**Scene Map**
- Free scene tags, notes and a marker list in the details pane, Auto Scene Detect as an action, Export Scene Map (V1_TARGET_FEATURE_MAP).
- Stage 3 of scene understanding: scene type plus axis, depth, tempo per shot, with a profile proposal per shot (SCENE_UNDERSTANDING_PLAN).
- Learning stages L1-L4 as named items: priors, region detector, learned cell scorer, fusion (SCENE_MAP_PLAN M6).

**Library**
- Tags and NFO tags, watched / NEW badges (ScriptPlayer-CN, UwU Player in ROADMAP_2026-10 §3b).
- Relink broken media, project library, duplicates (FIVE_ENGINE, ED-10).

**Settings / Tools**
- Config export / backup and a Calibrate quick tool (V1_TARGET_FEATURE_MAP, Tools).
- Privacy view with deletion of learning data and derived models (review §6; D-029 states the rule, not the view).

**Formats**
- Learning-data contract (consent scope, content group, feature-schema version, reasons) (review §6).
- Content catalogs (video + script download) (CONTENT_SOURCES; D-048 only rejects the in-app browser).

**AI**
- Teacher-to-detector loop: own multi-class detector trained from teacher labels (VLM_TEACHER_PLAN V3, SCENE_UNDERSTANDING_PLAN §4).
- Hybrid gap-filler step 2: fill segments where the tracker lost the target (SCENE_UNDERSTANDING_PLAN §3b).
- Independent verifier work packages as a list (REC, QC, AUTO, REPAIR, DEV ids in the capability register are only in ROADMAP as capability names).

**Release / Editions**
- Update signature asset (Ed25519) next to checksums (owner/patch-system.md).
- Process for community test reports: issue title `test: <area> - <clip>` and attached English screenshots (PRODUCTION_ROADMAP).
- International English website with Donate button (FIVE_ENGINE N8; D-022 covers only in-app links).

**Community**
- Plugin / extension SDK research, permission model, no arbitrary Python plugin (STUDIO_EDITOR_BLUEPRINT SDK-01).
- Decision on "BuzzyBody / Fap Hero Journey" style apps (ROADMAP_2026-10 §3b: "far from our core") and on Virtual Person (cancelled; §2.1 a3).
- Forum post and public-site sync as release tasks (owner/forum-post.md).
- Add a decision row: Virtual Person is out of scope (a3).

---

## 5. Unsure

- Whether the 55-version count should include 0.7.0 (VERSION says 0.7.0, tag not yet pushed) and 0.5.46 / 0.6.0 (never tagged).
- `CHANGELOG.md` lists 0.2.2 only as a stub; 0.2.1 / 0.2.2 contents come from `git log` and the commit titles.
- Label-based comparison used tags (50 snapshots); a control that existed only between two tags would not show. The id and label diffs found only the removals listed above.
- "Not in SamNPlayer 2 docs" is a text search (e.g. "Climax", "Spectrum", "SDK", ".sam"); a synonym could hide a match. `PARITY.md` lines were checked by key term, not read in full.
- `P3_GENERATOR.md` is new; items I list as "partly" (hybrid gap-filler, own detector) may be intended by P3.7 and should be confirmed by its owner.
