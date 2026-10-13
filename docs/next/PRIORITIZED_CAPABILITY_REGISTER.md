# SamNPlayer 2.0 — Prioritized capability register (Owner, 13 October 2026)

**Scope:** all features belong to ONE SamNPlayer 2.0, freeware supported by voluntary donations. "Studio functions" is historical shorthand for advanced capabilities, **not** a second/paid Studio edition. This document augments [MASTER_PLAN](MASTER_PLAN.md), [ROADMAP](ROADMAP.md) and [FIVE_ENGINE_ARCHITECTURE_AND_GUI](FIVE_ENGINE_ARCHITECTURE_AND_GUI.md). No feature is presumed implemented merely because it is specified.

## Feature priorities and user outcomes

| ID | Priority | Feature | Concrete user value | Main modules | Acceptance bar |
|---|---|---|---|---|---|
| CAP-01 | **P0** | **AutoDirector** | Detect scene boundaries, pick an appropriate tracking backend per scene and retry only unreliable segments | Motion + Intelligence + Core | Deterministic per-scene decisions with documented reason codes; original classical fallback; no quality regression against golden clips |
| CAP-02 | **P0** | **Smart Batch** | Process multiple videos with distinct presets, pause/resume, bounded resource usage, retries and automatic exports | Core + Motion + Intelligence | Persisted job manifest survives restart; per-job status and export, idempotent re-run, no partial files masquerading as successful output |
| CAP-03 | **P0** | **Quality Compare** | View classical vs AI-assisted (or alternate classical) result side by side, inspect differences and choose a better candidate | Intelligence + Editor + AI Assist | Aligned timebase, overlay/dual curves, A/B playback and objective report; never equate model confidence with ground truth |
| CAP-04 | **P0** | **Device Optimizer** | Translate one master script to connected device capabilities without inventing unsupported motions | Device + Core | Capability-led mapping, clamp/speed caps, audit record, mock 0/1/2/6 axis devices, stop/disconnect tests |
| CAP-05 | **P0** | **Intelligent Repair** | Repair local tracking gaps, jumps and implausible motion rather than rerender the whole video | Motion + Intelligence | Changed ranges logged; provenance, preview + undo; verifier rejects worse replacements; untouched ranges preserved |
| CAP-06 | **P1** | **Scene Studio** | Regenerate, lock, merge and quality-score individual scenes | Core + Motion + Editor | Lock prevents batch/autodirector overwrite; seam continuity + no data loss; per-scene undo |
| CAP-07 | **P1** | **Multi-Target Tracking** | Follow multiple candidate motion points and perform controlled target switches | Motion + Intelligence | Track identity continuity, explicit ambiguity handling, confidence based on measured evidence, manual override |
| CAP-08 | **P1** | **Personal Motion Profiles** | Learn locally from user-confirmed edits to improve future analyses | Intelligence + AI Assist optional | Explicit opt-in, explain source, grouped held-out validation, model/version rollback and delete/export; never silent retraining |
| CAP-09 | **P2** | **VR / 180° / 360°** | Handle stereoscopic and immersive content with format-aware projection and consistent timestamps | Motion + Core + Editor | Detect and report supported layout; eye/projection-aware tracking, defined fallback; no claims of VR support without end-to-end fixtures |
| CAP-10 | **P1** | **Professional Export** | Produce device profiles, real multi-axis companion outputs, batch and quality reports | Core + Device + Editor | Roundtrip fixtures for `.funscript`/`.samn` and real axes, naming collision policy, atomic output, reproducible manifest |
| CAP-11 | **P1** | **Hardware Benchmark** | Measure CPU/GPU capabilities and choose measured appropriate processing backend | Motion + Core | Repeatable on-device microbenchmarks, quality parity, resource ceilings, explainable choice and user override |
| CAP-12 | **P0 prerequisite** | **Project Recovery** | Restore after crash, interrupted job or failed scene without losing completed work | Core + Batch + Editor | Atomic checkpoint, restart/replay, no duplicate artifacts, partial-scene recovery, corrupt/disk-full cancellation tests |

## P0 implementation order (dependency-aware)

1. **Foundation:** stable project IDs, immutable media fingerprint, PTS/VFR mapping, `JobState`, transaction journal, atomic exports, safe cancellation.
2. **CAP-12 Recovery first:** checkpoint format + restart/reconciliation, per-scene result manifests. Smart Batch MUST NOT advertise reliable resume before this passes.
3. **CAP-02 Smart Batch skeleton:** robust bounded queue over the existing classical single-video generator. Presets, retry states, durable progress, user controls, successful export.
4. **CAP-03 Quality Compare foundations:** classical-vs-classical reference and measurable curve alignment **before** any optional AI exists. When AI is added, use exactly the same independent comparison and verifier.
5. **CAP-01 AutoDirector:** scene split, track backend registry, candidate selection, reason codes; selective rerun. Current CSRT remains baseline.
6. **CAP-05 Intelligent Repair:** local gap/smooth correction with validity checks and undo; no fake continuous motion across true missing evidence.
7. **CAP-04 Device Optimizer:** capability schema & mock safety contract can develop alongside earlier items; physical hardware approval stays separate. Never synthesize unknown axes.
8. **Release P0 integrated QA:** golden clips, mock devices, Windows 11/GUI smoke and real-device supervised tests. P1 work may proceed in isolated PRs but may not displace P0 regression closure.

## P1 follow-on integration order

- Scene Studio builds upon AutoDirector scene IDs, persistent locking and transaction operations. No hidden overwrites.
- Multi-Target builds upon source PTS and target identity/provenance.
- Personal Motion Profiles only after consistent review+accept data and held-out improvement evidence.
- Professional Export builds on real axis mappings and project recovery.
- Hardware Benchmark uses a fixed gold corpus and fallback; it may collect data during N2, but automatic backend selection only after parity.

**P2 VR:** separate research track with a small real/synthetic stereo/180/360 test matrix; do not distort basic 2D processing for a speculative feature.

## Existing Flow GUI: exactly where each feature lives

| Area | Functions | UI contract |
|---|---|---|
| **Create** | AutoDirector, Intelligent Repair, Hardware Benchmark decision | Existing video-first Create flow. `Auto Generate` primary, optional scene table and understandable quality badges, re-run bad ranges; show actual measured backend, ETA only if calibrated |
| **Tools → Jobs/Batch** | Smart Batch, Recovery, Professional Export | Durable job table with queued/running/paused/failed/recoverable/exported, preset per item, retry/recover and individual report. No fake status |
| **Edit** | Quality Compare, Scene Studio, Multi-Target | Dual/ghost curves and A/B, scene lock/regenerate/merge, target tracks, full axis lanes and undo/redo; no destructive overwrite |
| **Play → Devices** | Device Optimizer | Device-specific capability inspector, preview channels and safety status, physical actuation only after explicit start |
| **Tools → Intelligence** | Personal Motion Profiles, local AI Assist | Opt-in, learning evidence, saved versions, delete/export and disable AI. Default classical generation always available |
| **Project / Export** | Professional Export, Project Recovery | Recovery history and report; atomic saves and actual success/failed indication |
| **Future Advanced Media** | VR, 180°, 360° | Dedicated contextual import/view control only after supported projections exist |

The dark Cyan/Orange **Flow GUI**, primary Play/Create/Edit navigation and existing Wails implementation are retained. Keep video and Stop visible at 1366×768; inspectors collapse, not playback. Accessibility, keyboard selection, reduced motion and Windows-native screenshots gate UI acceptance.

## Cross-feature invariants

1. Features are free: no paid studio unlock, donation threshold, timed trials or license key.
2. Video/media stay on user's machine by default; optional LAN AI explicitly configured. No model or user-correction uploads silently.
3. No automatic inference creates imaginary data: uncertain sections are flagged, not quietly treated as verified.
4. Funscript and project formats remain backwards-compatible, migrations are tested and previous scripts never expire.
5. Device limits, Disconnect, Pause, Stop and Emergency Stop take precedence over AI, batch and previews.
6. CI + deterministic fixtures + owner-supervised real Windows/device QA needed before claiming shipped.

## Work packages to issue as individual PRs

`REC-01` checkpoint schema/replay → `BATCH-01` persisted queue → `QC-01` A/B and scoring → `AUTO-01` scene contracts and selector → `REPAIR-01` localized corrections → `DEV-01` capability mappings → `SCENE-01` locks/merge → `TRACK-01` identities → `PROFILE-01` opt-in corrections → `EXPORT-01` manifests & formats → `BENCH-01` hardware selection → `VR-01` experimental proof.

**Status:** Feature registry and proposed acceptance criteria only. Actual build/test status must be tracked in implementation PRs.
