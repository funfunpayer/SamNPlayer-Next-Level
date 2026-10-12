# SamNPlayer Studio — Multi-Axis Editor, Engineering Blueprint & Competitive Research

**12 October 2026 · Owner working concept · Private repository document · Not implemented.**
Companion to [Free → Studio Product Plan](FREE_TO_STUDIO_PRODUCT_PLAN_2026-10-12.md).
This plan is for the **new Next Level software generation** derived from existing SamNPlayer Go/Wails sources. Free release first; Studio architecture and benchmarks can be prepared orthogonally, but no premature release feature claims.

## 0. Product bar: compete with the strongest, verify rather than boast

**Aspiration:** deliver a more reliable, faster and more useful experience than FunGen 2, particularly for *video-to-device fit*. Do **not** claim “best”, “100% accurate”, faster than FunGen, broader device coverage or fully automated success before a controlled, consent-respecting benchmark proves it.

Current publicly documented competitor reference points (checked 2026-10-12; these are not our implementations):
- FunGen [official website](https://fungen.app/) and [public source/README](https://github.com/ack00gar/FunGen): multi-axis frame-accurate editor; adjacent/ghost axes, rich selection, ripple and parametric transforms, action history, chapters/heatmap, script doctor, AI generation, batching, device features. [Pricing](https://fungen.app/pricing/): Free; 1-year Pro 75 EUR; Perpetual 250 EUR, per publicly displayed terms; price can change.
- [OpenFunscripter](https://github.com/OpenFunscripter/OFS): established SDL/ImGui/C++ editing reference; repository says **GPL-3.0**. Observe ideas/UX, DO NOT copy implementation into proprietary SamNPlayer without specific GPL-compliance decision.
- [MultiFunPlayer](https://github.com/Yoooi0/MultiFunPlayer): **MIT**; multiple simultaneous scripts/devices, TCode v0.2/v0.3, device-specific limits, PCHIP/MAKIMA interpolation, smart limits, auto-home, heatmap, chapters, external-player integrations, multiple outputs. Prefer interface/protocol compatibility and original implementation. Attribution + other transitive licences before importing code.
- [FunPlayer's funscript format guide](https://docs.funplayer.app/de/knowledge-base/funscript/): action time milliseconds and position 0–100; companion per-axis filename conventions; verify real interoperability.

**Licensing policy:** Learn features, interoperability and standard formats from competitors. For GPL software use public documented behavior, own design/tests, never paste GPL source into closed repo; for MIT software source reuse requires license notices, provenance, dependency audit and explicit choice to import. Do not import model weights or datasets without distribution/commercial-use review. Maintain `legal/THIRD_PARTY_NOTICES.md`.

## 1. What “full-featured editor” actually means

A **production Editor workspace**, part of SamNPlayer Studio development; do not market it before built. Keep existing Free edit tools remaining supported in Community. Studio may get genuinely new professional workflow features, not an artificial paywall over current Undo/Redo/Save/Curve editor.

### 1.1 Data model: one project, independent lanes, universal script compatibility

Canonical `Project` references a source video and canonical PTS-based timebase; `Tracks` store `axisId` (semantic identity, not visual position), `actions[{atMs,pos}]`, smoothing/interpolation metadata, confidence/provenance, lock/mute/visibility. Standard export `video.funscript` remains the master stroke. Additional physical motion axes use established companion naming:
- L0 `general/stroke` -> `video.funscript`
- L1 surge -> `video.surge.funscript`; L2 sway -> `video.sway.funscript`
- R0 twist -> `video.twist.funscript`; R1 roll -> `video.roll.funscript`; R2 pitch -> `video.pitch.funscript`
- vibrate/suction may need protocol-/application-specific naming; expose export profile and compatibility test rather than claiming one universal suffix.
- `.samn` remains native project (general, vibration, suction, chapters, bookmarks, oMarkers, recipe, metadata); project schema must be **extended backwards-compatibly** through additive axis maps / version migration; unknown extension keys preserved when possible. See `docs/SAMN_FORMAT.md` and `docs/SAM_ARCHITECTURE.md`.

**Important:** one master *stroke* trajectory does **not** imply meaningful roll/pitch/sway trajectories. Extra tracks must come from measured motion, deliberate user scripting, or an explicitly marked derived effect, and MUST NOT be fabricated by model or implied device capability. Cross-device projection is a playback/export transform over the original master + actual tracks, not destructive rewrite.

### 1.2 Studio Editor GUI — concrete layout

Top: video + source identity + precise timecode/frame controls, viewport zoom, frame step and playback speed preview (if correctly sync-safe). Left: track inspector/library and scene navigator. Center: high-density timeline with editable lanes aligned to video audio and scene cuts. Right: contextual inspector (Action, Range, Transform, Axis, Device Preview, Quality). Bottom: history/diff/Quality Doctor. Responsive: 1920×1080 exposes inspector; 1366×768 preserves min video/timeline sizes with collapsible docks; keyboard interaction remains possible.

**Interaction model:**
- **Viewport:** frame-accurate seek with correct VFR presentation timestamps; zoom anchored on pointer/playhead; horizontal scroll; snap to actual decoded PTS, FPS grid, beat, action and markers individually configurable.
- **Lanes:** dynamic per-project axis lanes, reorder/pin/solo/mute/lock, adjustable height, lane labels and units, axis visibility independently of connected toy. Never limit to three; preserve unknown axes when loading.
- **Actions:** click to insert, drag point, lasso/range select, multi-select, numerical point edit, box transform, copy/paste with relative time, delete with gap policy, keyboard nudge by frame/ms or position units, snapping, frozen selected points.
- **History:** deterministic undo/redo as commands, transactions for drag/range, history list + before/after diff, persist across project reopen via journal or snapshots; cross-axis edits atomic. No silent irreversible AI writes.
- **Transform library:** scale offset, invert, translate/time shift, stretch/compress timing, mirror, equalize, smooth (with algorithm preview), peak reduce, interpolate gaps, retime by scene or BPM, speed/acceleration guard, repeat pattern, ripple insert/delete, normalize to chosen range, blend/dissolve, trim/combine, parametric keyframe generator.
- **Selections:** all / range / scene / chapter / interval / peaks / valleys / threshold region / active axis / chosen axes, with explicit selection scope before destructive action.
- **Reference modes:** master ghost overlay; comparison candidate/previous revision; split A/B and synchronized playhead; video overlay target marker to relate to real motion.
- **Timeline context:** audio waveform and spectral/BPM optional hints; track lane line + validity gaps/confidence region, Chapters, Bookmarks, Scene Map, tracking lost masks, per-device effective output lane.
- **Quality:** warnings next to invalid timecode, duplicate/out-of-order points, severe spikes, reversals, tracking gaps, speed limit risks, dubious scene joins; side-by-side fix preview and accept/revert.
- **Safety:** editor preview never starts connected device without a deliberate Arm/Enable action; separate mock preview path. Emergency Stop and device cap enforcement live below GUI and apply identically in Free and Studio.
- **Export:** one file, selected axis, whole project/companion pack, destination collision policy, versioned project backup, compatibility profile for supported ecosystem, manifest of exported axes, round-trip validation.

### 1.3 Editor internal module contracts (proposal, do not implement blindly)

```text
UI editor interactions     (Claude HTML/Wails, ChatGPT own GUI styles/visuals)
    |
    V
Go EditorService
    |- ProjectStore (atomic save, version migration, checkpoint/replay)
    |- TimelineIndex (PTS, scene/chapter/action indexing)
    |- EditTransaction {id, expectedRevision, commands[], selection}
    |- CurveOps (pure and testable)
    |- HistoryStore (undo/redo revision graph)
    |- QualityVerifier (shared with generator)
    |- PreviewCompiler (real device profile + mock)
    |- AxisExportService (funscript / .samn + companion pack)
```

**Contracts:**
- `EditCommand {axis, operation, range, parameters, expectedRevision}` -> `EditResult {revision, changedRanges, warnings, undoToken}`; all-or-nothing transaction for multi-axis.
- `TimelinePosition` stores source PTS in integer microseconds, UI exposes milliseconds and optional frame index; never infer time by `frameIndex * (1000/fps)` for VFR.
- `AxisDescriptor {id, semantics, units, min,max,source, deviceCapabilities?}`; script axes and device axes are distinct sets.
- `CurveOps` algorithms execute on internal dense/sparse curves with deterministic input ordering. No hidden interpolation applied to on-disk `.funscript` export.
- `QualityReport` includes hard validity errors, soft warnings, confidence with calibration provenance, device-specific violations per actual selected profile; no invented quality percentages.

### 1.4 Editor acceptance tests (non-negotiable)

**ED-01** read/write legacy `.funscript` and version-1 `.samn`; preserve metadata + bookmark/chapters/O-markers, no action/time drift, no destructive migration.
**ED-02** import/export all L0/L1/L2/R0/R1/R2 companion lanes; unknown lanes round-trip or explicitly warn, never silently discard.
**ED-03** transform selection/range/specific axis only; untouched axes binary-equivalent after export where possible; anti-collision test for duplicate timestamps.
**ED-04** deterministic undo/redo of 1000 mixed edits; journal recovery after process crash; no cross-axis partial transaction.
**ED-05** frame-step and seek on VFR clips, report actual PTS; audio/video/curve align within an **owner-approved measured threshold**, not guessed perfect sync.
**ED-06** 1-hour video and 100k+ actions across tracks UI benchmark (FPS, memory, long edit); exact target after baseline.
**ED-07** handles 1366×768 and 1920×1080 keyboard/mouse, selection visualization, dark theme/contrast, reduced motion.
**ED-08** connected-device preview requires deliberate enable and enforces all caps; mock mode used for CI, physical device tests by owner.
**ED-09** transformation and AI-generated alternatives always show diff and undo/revert.
**ED-10** filesystem crash-safe atomic save, Unicode paths, missing media relinking, readonly output errors and disk full.

## 2. Studio adaptive generation — integration with the editor, not a parallel product

Single shared master project and transaction system for Generate/Create and Edit. Pipeline:
`FastScan → scene masks/ROI candidates → existing CSRT baseline → optional other registered CV backends → per-scene QualityReport → candidate selection → optional local AI candidate (based on measured tracks) → independent verification → stitched master timeline → editor review/repair → .samn + Funscript export`.

Adaptive backend choice starts with **existing** `generator/backends.py`, `generate_funscript.py`, `ai_roi.py`, `quality_doctor.py`; measure parity before relocating pieces to Go/Rust. First implement native timestamp/curve transforms only where quality and benchmarks justify. No rewrite for its own sake.

Three modes (actual user needs):
1. **Auto**: user selects target/video, engine makes measurable decisions and auto-retries weak scenes, asks about only genuinely ambiguous segments.
2. **Guided**: same engine with per-scene editable target/axis/profile and candidate choice before applying.
3. **Expert**: full Studio Editor with lane-specific transforms, comparison, locks, batch presets and explicit AI draft/repair overrides.

Batch is an orchestration over the exact same single-clip pipeline and EditorProject; not a second generator. Resume checkpoints store algorithm version, input fingerprint, source PTS, chosen method/ROI, quality gate and model/version for reproducible output.

## 3. Deep device support as first-class quality criterion

Device capability descriptors from actual connection determine channel availability. **Motion model → device mapping** respects stroke as position (where linear position supported) or recipe-derived intensity for Neo2, separates intensity/vibration/suction/rotation, and records units/responsivity/speed/accel limits.

Capabilities and protocols:
- Neo 2 first-class recipe, contact vibration, suction; maintain reliability.
- Handy via supported Intiface LinearCmd / optional validated native API; beware API network/privacy expectations.
- LELO F1S V3 / Lovense Max 2 by actual capability report, not hardcoded guessed motors.
- OSR2/SR6 TCode motion axes; carefully test max command rates, safe neutral state, disconnect / emergency stop.
- Multi-device simultaneous output is **separate future gate** (connection lifecycle, command arbitration, latency, race, safety), never imply shipped until true.
- Studio Device Preview: compare master track against *effective actuator signal after clamps and latency compensation* on real or simulated device; plot both without claiming unmeasured values.

QA matrix: mock devices for 1-axis, 2-axis, 6-axis and vibration/suction, no-device, sudden disconnect, stop, negative/out-of-range commands, lag and command-rate overload. Safety code not licensed/disabled.

## 4. Build-versus-borrow: actionable decisions

| Capability / candidate | Action | Constraint / acceptance |
|---|---|---|
| Existing Go `sam/`, `funscript/`, native .samn | **Extend in-house** | retain import/export and known project metadata |
| Existing `trackcv` C++/OpenCV CSRT | **Keep/benchmark** | prior bake-off found flow alternatives worse; new implementation must beat measured baseline |
| Go stdlib, Wails current UI | **Keep** | shared architecture, no toolkit migration without proof |
| FFmpeg existing video decode | **Keep** | timestamp accuracy, licensing and distribution notices |
| ONNX runtime / local VLM adapter | **Integrate optionally** | no unknown commercial-use model weights; opt-in install/download |
| Interpolation PCHIP / MAKIMA seen in MultiFunPlayer | **Implement mathematical approach ourselves**, benchmark | compare artifacts, overshoot, numerical parity; no C# code copy required |
| MultiFunPlayer MIT protocols/designs | **Study; potential controlled import** | MIT notices + dependencies audit; prefer original Go implementation |
| OpenFunscripter GPL-3 source | **Do not copy into proprietary core** | could study public UX and file format; legal review before any dependency |
| External C++/Rust motion packages | **Only if tests show real gain** | stable ABI, binary size, cross-platform native CI, crash behavior; provenance and licence |
| Ready-made vision models | **Optional plugin** | inference performance/ROI accuracy, licence for commercial redistribution, no user-content training without consent |
| Computer vision benchmark clips | **Build our own consented golden set** | rights, local privacy, no public screenshots of explicit real media |

**Unique IP:** per-scene adaptive decision data model, backend quality scorer with reason codes, repair engine, golden benchmark corpus/evaluation, device-aware compiler, reversible editor transaction/history and opt-in local feedback. Avoid claiming uniqueness simply because wrappers have a new name.

## 5. Competitive acceptance matrix (product parity → differentiation)

| Domain | Minimum competitive outcome | Our extra differentiator | Proof |
|---|---|---|---|
| Editing | frame-accurate multi-axis + rich selection/transforms | true per-scene evidence and reversible AI/classical compare | ED-01..10 |
| Auto generation | usable script with clear manual override | classical + optional local AI, selective retries, candidate scoring | golden baseline, quality reports |
| Batch | stable multi-video processing | crash-safe scene-level resume and per-job quality reports | 20+ clip crash/retry test |
| Devices | interoperable companion scripts + key protocols | verified effective output preview + capability-aware projection | mock and physical QA |
| Libraries | projects/recent scripts/video pairing | robust index/duplicates/relink/portable project | migration and stress tests |
| Speed | acceptable hardware resource profile | measured native Go/Rust optimizations | paired hardware benchmark |
| Privacy | user-controlled local media | no upload and opt-in learning by default | network/privacy audit |
| Business | international Free + Studio | verified >€40 supporter entitlement once legal checkout ready | end-to-end payment/refund/offline licence test |

## 6. Incremental, dependency-aware backlog

**Do not start this at the expense of the current Free release.** UI Free integration #509 and CSS #489 first; then release QA/website. Editor architecture work can begin as docs/tests separately.

| Work package | Owner | Requires | Concrete artifact |
|---|---|---|---|
| EDIT-00 Inventory current Edit/Play controls, `.samn` and timeline state | Claude | latest main/GUI integration | diff matrix against §1, feature gaps and safe source ownership |
| EDIT-01 Native axis/project schema + companion round-trip test | Claude | EDIT-00 | Additive schema, importer/exporter, no loss |
| EDIT-02 Edit transaction + commands + persisted Undo/Redo design | Claude | EDIT-01 | deterministic core with 1k-operation property tests |
| EDIT-03 Timeline VFR PTS, selection and renderer benchmark | Claude core / ChatGPT visuals | EDIT-02 | functional timeline + responsive 1366/1920 visual QA |
| EDIT-04 Curve transform library and range scope | Claude | EDIT-02 | numeric tests with known reference actions |
| EDIT-05 Contextual inspector, keyboard shortcuts, comparisons | Claude wiring / ChatGPT styling | EDIT-03/04 | real clickable UI, accessibility smoke, video sync |
| EDIT-06 Editor quality overlay / repair diff and revert | Claude | EDIT-04 + ENG-03 | independently verified candidates + undo |
| EDIT-07 Device preview + constrained multi-axis output | Claude | D2–D4 + EDIT-01 | mock tests and safety gate |
| EDIT-08 Large-project stress + crash/recovery / release QA | Claude | EDIT-03/06 | 60min/100k+ actions, restart test reports |
| AUTO-01 Scorer/selector and selective per-scene retry | Claude | Free baseline + EDIT-01 | reproducible quality choice and fallback |
| AUTO-02 Optional local AI writer using classical data and verifier | Claude | AUTO-01 | candidate API, transparent diff, no silent replacement |
| BATCH-01 Unified persisted queue using Auto + Editor project | Claude | AUTO-01 | resume/cancel/failure report |
| SDK-01 Studio plugin/extension SDK research | Claude / ChatGPT docs | stabilized public APIs | permission/trust/signature model; **no arbitrary Python plugin execution** |

## 7. Commercial and launch continuity

Owner-approved **Free kept**, international **Studio premium** later. Guaranteed Studio access for supporters giving **more than €40** is a business requirement: a benefit in exchange for payment must be reflected as a purchase/consideration in tax/consumer terms, despite colloquial “Spende”. Do not promise immediate Studio before build/launch, do not conflict with current `legal/EULA.md` §6 without formal amendment; do not weaken customer privacy/offline model. See companion plan §11 for payment event model and signed offline entitlement. No supporter portal or live website claims until actual service exists and terms approved.

## 8. Hard go/no-go for “best of the best”

- Build passes and quality stays at least as good as baseline on the agreed golden set.
- Editor round-trips axis tracks without losing data and presents frame-accurate VFR timeline.
- No silently fabricated confidence/extra axes, no dangerous toy output or skipped emergency stops.
- Speed advantage only claimed if measured on identical clips/hardware and properly documented.
- User can preview, compare, undo, recover, stop; advanced AI never bypasses classical verifier.
- International copy clear, English-first; Free vs Studio truthfully represents **what ships** rather than aspirational work.
