# SamNPlayer 2.0 — Five-engine architecture and complete product direction

**Owner-confirmed 13 October 2026. Status: architecture / delivery contract, not a claim of implementation.**

This is a continuation and major evolution of the existing **one SamNPlayer** application, not a paid Studio product, a licence tier or an unrelated rewrite. **Freeware financed by voluntary donations**. The existing Go/Wails, OpenCV tracking, Python generator, SAM/.samn, device capabilities and Flow GUI must be inventoried, retained where effective, and improved behind benchmarks and safe migrations.

## Five modules — responsibilities and stable boundaries

| Module | Primary language | Single responsibility | Existing building blocks to reuse | Output / contract |
|---|---|---|---|---|
| **SamN Core** | Go | Wails API, lifecycle, job orchestration, cancellation, project persistence, media/script indexing, master `.funscript` / `.samn`, undo/versioning and secure settings | `cmd/gui-wails`, `funscript/`, `sam/`, player, generation workflows | `JobRequest → JobState, ResultManifest`; atomic project writes and progress events |
| **SamN Motion Engine** | Go with selected Rust/C++ | Read actual video timestamps, scene partitioning, ROI/target tracking, optical/motion features, camera compensation, audio side evidence, posttracking and script curves | `generator/generate_funscript.py`, `generator/backends.py`, existing OpenCV/CSRT/C++ `trackcv`, FFmpeg decode | `TrackResult {PTS, raw position px, validity, scene cuts, features, provenance}` — no premature normalization |
| **SamN Intelligence** | Go | Classical-first method selector, candidate comparison, per-scene retry, independent quality verifier, threshold calibration, opt-in learning from confirmed corrections | `generator/quality_doctor.py`, Scene Map, existing profilemodel, SAM confidence metadata | `CandidateDecision {winner, reason codes, scores, rejected candidates, fallback, review ranges}` |
| **SamN AI Assist** | Go adapter + local optional ONNX/VLM inference | Optional ROI/scene hints; candidate curve generation, smoothing or repair; never final authority and never device command source | `generator/ai_roi.py`, `generator/vlm_probe.py`, `generator/aiscript/`, existing local-model adapters | `AIProposal {modelVersion, inputEvidence, proposedRanges, curveCandidate, uncertainty}` fed back into **independent** verifier |
| **SamN Device Engine** | Go, selected protocol/native bindings | Map master motion and *real* companion axes to actual connected-device capabilities; enforce timing, range, speed, disconnect and emergency-stop limits | `device/`, Neo2, Intiface, recent TCode work, `.samn` axis/companion models | `Capabilities → EffectiveOutput` with mandatory safety governor and mock hardware tests |

**Boundary rules:** AI is optional; without it the classical generator still produces scripts. Device output is derived only from validated curves via the Device Engine, never directly from AI. One primary Funscript stroke curve plus correctly sourced optional additional axes; no invented movement or unsupported device channels. All project outputs remain accessible/exportable after failures or cancellation.

## User workflow (make it automatic, not mysterious)

1. User selects a video, optional target/ROI and device; **Auto** is default, with **Guided** and **Expert** views for deeper work.
2. Motion Engine probes actual PTS/VFR, performs fast scan and scene segmentation, generates reliable classical baseline.
3. Intelligence evaluates each scene using raw motion, tracking loss, temporal discontinuities, jitter, axis bounds and verifiable evidence; retries only deficient ranges with alternative already-tested methods.
4. Optional AI Assist proposes ROI, a revised curve segment or a full draft **based on measured scene/track data**. Intelligence independently compares it to baseline; reject/fallback when unsupported or worse.
5. Core assembles a reproducible master project, preserving source, model/backend versions and explicit user corrections; Device Engine previews and maps to actual device capabilities.
6. User reviews flagged ranges only, uses full Multi-Axis Editor, undo/redo, project library, long-job queue and standard exports.

## Development sequence (map onto authoritative `docs/next/ROADMAP.md`)

- **N1 Core contract and inherited release viability:** make imported code/tests work, verify Free/Suite builds, import/release/updater privacy, establish deterministic project/timebase/format tests.
- **N2 Measurable Motion baseline:** benchmark current CSRT/classical generator; synthetic and owner-held golden clips, CPU/RAM, PTS alignment, raw-pixel continuity, posttrack parity.
- **N3 Motion Engine modularization:** Go job/PTS/FrameCache and versioned backend contracts, existing Python/OpenCV legacy backend as oracle; identify actual hot spots before porting any Rust.
- **N4 Intelligence:** stable per-scene feature extraction, candidate ranking and selective rerun; report reasons and hard quality gates. Each change must pass an independent no-regression corpus.
- **N5 Device Engine:** capability model and mock matrices (Neo2, Intiface Linear/Scalar where implemented, TCode 1/2/6 axes), emergency stop/timeout/disconnect, physical tests only with Owner.
- **N6 Editor and Batch:** transaction-safe multi-axis timeline, VFR frame precision, range operations, ghost/compare, history/undo, recoverable queues with crash/retry/cancel, quality manifests.
- **N7 Optional AI Assist and learning:** Mac LM Studio via explicitly configured trusted local LAN plus mock adapters, ONNX. User-confirmed examples only, held-out calibration/rollback; compare with classical baseline and route every proposal through verifier.
- **N8 Optimization and release:** Go/Rust/C++ hot spot bake-offs only when faster or lower memory at equal/better quality; Windows 11 native Wails UI and hardware QA, international English website, voluntary Donate, no premium paywall.

The existing [ROADMAP.md](ROADMAP.md) remains canonical for gate numbering and order if differing; this breakdown supplies the **architecture-to-gate implementation mapping**, not an alternate release train.

## Complete UI concept: evolve the existing Flow layout

**Do not design a new unrelated dashboard.** Keep familiar dark **Flow** look (cyan/orange accent), top-level Player/Training/Tools, Player subviews Play/Create/Edit, Library and navigation conventions already approved. Upgrade usability and density, not basic identity.

- **Play:** large video stage, responsive transport, per-device capability controls, master/companion track visibility, safe status/error details; never fake “connected” states.
- **Create:** one prominent **Auto Generate** path: Video → Target → Analyze → Quality → Save. Guided expandable scenes and Expert method details; accurate progress sourced from Core. AI availability is shown only if installed/configured.
- **Edit:** **full professional multi-axis editor** with movable lane list, L0/L1/L2/R0/R1/R2 + vibration/suction when present, frame-accurate zoom, snap, lasso, range editing, numerical transforms, waveform, chapters, ghost A/B, Undo/Redo, scene-quality overlays and device-preview without accidental physical actuation.
- **Training:** separate device sessions from opt-in **AI learning**; avoid mixing labels, training progress or inferred confidence with real device signal.
- **Library / Batch:** reliable local videos/projects/script indexing, relink broken media, saved queue with resume/retry/cancel, presets/quality reports. No fake previews or upload.
- **Tools:** real device diagnostics, backend/runtime health, per-model opt-in setup, benchmarks, privacy/learning deletion and donation links. Keep offline core usable.
- At **1366×768** hide optional inspectors, not video/timeline/Stop; at **1920×1080** allow larger previews + expanded editor; keyboard, contrast, focus and reduced-motion support throughout.
- Style and copy remain **English in product**, owner/agent planning in German permitted.

## Dependency and independent implementation policy

Build **Core orchestration, selector, verifier, editor command model, safe device compiler and local learning logic in-house**. Keep FFmpeg for reliable codecs/PTS unless concrete tests prove an alternative handles required formats; investigate targeted Rust/Go decode/audio/track alternatives behind adapters, not blind FFmpeg removal. Reuse existing OpenCV C++ where measured strong. Avoid a mandatory remote AI service, mandatory cloud account and heavyweight new UI framework. Third-party code import requires licence/transitive dependency audit; never paste GPL editor code into proprietary app.

## Test matrix: proof, not wishful features

| Layer | Automated acceptance |
|---|---|
| Core | deterministic Go tests for job lifecycle, cancel/resume, atomic save, migration and file roundtrip |
| Motion | synthetic moving targets and scene cuts, timestamp/VFR correctness, tracking-loss and drift scores, golden corpus parity |
| Intelligence | per-scene candidate reason codes, independent verifier, worse candidate rejected, fallback on timeout/low evidence |
| AI | mock local server, invalid/late/no responses, wrong ROI/hallucinated curves rejected, network opt-in/privacy, held-out quality |
| Device | mock 0/1/2/6 axes, scalar outputs, speed/caps, disconnection, STOP from any state, no commands when unarmed |
| Editor | 1000 mixed cross-axis undo/redo edits, `.funscript` companion roundtrip, 100k actions/long clips, precise VFR snapping, corrupt files |
| Batch | 20 diverse clips, crash/restart, duplicate names, disk-full, cancellation, safe checkpoint recovery |
| Native QA | Windows 11 Wails screenshots at 1366×768 and 1920×1080, accessibility/keyboard, active device and library states |
| Performance | same clips/hardware/config, warm-ups+repeated runs, median/p95/peak RAM; Rust merged only after measurable improvement with quality parity |

**Mac LM Studio is inference serving, not automatic training.** Actual learning requires a separate opt-in correction dataset, labeling, train/validate split, versioning, quality holdout and rollback. No intimate video/model examples stored in the public repository.

## Next PRs: small, independently testable

1. **CORE-01:** legacy capability + domain-model inventory, unit tests for motion/project/timebase contracts; no behavior change.
2. **MOTION-01:** instrument classical generator Golden JSON report, reference scripts and deterministic synthetic videos.
3. **MOTION-02:** Go orchestration wrapper and backend capability/PTS contract; preserve existing Python output.
4. **INTEL-01:** independent per-scene quality schema and baseline scorer.
5. **DEVICE-01:** mock capability profiles, safety-state-machine contract and failure tests.
6. **EDIT-01:** backward compatible axis/project conversion + atomic undo API and tests.
7. **GUI-01:** Flow-layout actual-state map, Auto/Guided/Expert screens and responsive CSS against real bindings.
8. **AI-01:** mock-verified local adapter contract without any mandatory model.
9. **BATCH-01:** persistent queue/checkpoint failure semantics and tests.
10. **OPT-01:** Go/Rust profiling and compare only after baseline reports.

**Status:** Not all of these functions exist yet. Do not claim benchmark wins, physical device QA or working 2.0 release without test evidence. Owner and Claude control core merge/release; ChatGPT contributes doc, CSS/visuals and isolated tests on its own branches.
