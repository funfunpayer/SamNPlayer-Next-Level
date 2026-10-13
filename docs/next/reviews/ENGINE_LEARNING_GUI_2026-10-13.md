# Next engineering review: media, learning and GUI

**13 October 2026 · ChatGPT review for Claude · proposals, not implemented or benchmarked.**

Owner request: close demonstrable FunGen 2 gaps, improve local learning and the GUI, and reduce runtime dependencies using Go/Rust where this improves the actual product. One free, donation-supported program; no paid unlock, entitlement or timed preview. Proprietary/EULA status is unchanged.

## 1. Baseline and authority

- Next main currently contains only the initial README. [Next PR #2](https://github.com/funfunpayer/SamNPlayer-Next-Level/pull/2) is open, not merged. This review read its head `69399eebd6e5f241c5e1820b33325e21b291210d`.
- PR #2 already imports code/history and supplies the authoritative proposed [MASTER_PLAN](https://github.com/funfunpayer/SamNPlayer-Next-Level/blob/69399eebd6e5f241c5e1820b33325e21b291210d/docs/next/MASTER_PLAN.md), ROADMAP, OPEN_DECISIONS, P4 benchmark spec, P6 batch spec, ENG-01 and EDIT-00 inventories. Extend those; do not create a second product plan.
- That master plan supersedes paid Studio, expiring previews AND the old plan to remove AI/batch from a future Free edition. Suite training exposure remains an open Owner decision. Do not activate internal training for customers as part of this review.
- Original SamNPlayer PR #510 is now closed without merge, with explicit supersession. Technical concepts remain historical, including the copies in PR #2's sources directory.
- The import baseline is not the latest original-repository main. Recent companion playback/editing, TCode and motor-level work must be reconciled before implementing old gap lists. [Original PR #529](https://github.com/funfunpayer/SamNPlayer/pull/529) for creating companion axes was open at this review; do not call it shipped.
- Source inspection is static. No Windows, hardware, real-clip, runtime performance, competitor installation or model-training test was performed here.

## 2. Findings to act on first

| ID | Evidence at reviewed head | Consequence | Small next change and acceptance |
|---|---|---|---|
| M1 | `videox/frames.go`: GrayFrame contains Index, dimensions and pixels; NewGrayReader pipes rawvideo, optionally resampling FPS | Original per-frame presentation timestamps are unavailable through this interface. This does not prove a particular existing clip drift bug | Add a timestamp-capable media contract alongside the old reader; tests distinguish source PTS from resampled output time and preserve nonzero seek origins. Do not claim VFR correctness from frame index / average FPS |
| M2 | `videox/mp4probe.go`: absent codec becomes h264 and absent pixel format becomes yuv420p; `playable.go` uses codec for playability decisions | Unknown metadata can become an unsupported positive playability hint | Keep unknown codec/pixel format explicit; audit callers. Synthetic MP4 with unknown sample entry must not report H.264 or guaranteed playability. Supported AVC metadata must still match ffprobe. Truncated boxes must produce a clear incomplete/unsupported result |
| M3 | `generator/audio_vibration.go`: FFmpeg supplies mono 4 kHz f32le, while RMS and loudness normalization already run in Go | A language rewrite of the envelope is unnecessary; the replaceable boundary is decoding/downmix/resampling | Extract a PCM-source seam while leaving normalization unchanged. Verify sample origin, downmix, rate, trailing windows, silence/no-audio, cancellation and fallback |
| B1 | P6 events contain only seq/at/clip/from/to/reason, while clips JSON stores fingerprints, versions, output paths and quality | The stated ability to rebuild the entire cache from the journal is not specified sufficiently | Define recoverable event payloads or make durable snapshots authoritative; crash tests must reconstruct output identity and versions, not just a state label |
| B2 | P6 says completed clips are never repeated; crash recovery requeues running/verifying; it does not define the output-rename / done-event crash window | A crash after final rename but before done can yield a duplicate run or an incorrect skipped result | Add a durable commit-intent / commit-result protocol and hash validation; see section 5 |
| Q1 | P4 permits mean r loss 0.01 / per-clip 0.05; SELF_BUILD rejects quality regression | A port could pass P4 while violating the dependency-reduction promise | Separate exploratory ranking tolerances from shipping gates. Any allowed numerical tolerance must be justified by baseline repeatability, not borrowed from these draft thresholds |
| L1 | `generator/profilemodel/model.go`: centroid model over eight measured fields, minimum two samples, confidence derived from distance/margin | Trainable is not validated. This score is not a calibrated probability or evidence that a generated curve matches video | Preserve suggestion-only behavior. Report model availability, validation status and similarity separately; add grouped held-out evaluation before promoting a learned selector |

These are bounded static findings/specification gaps, not claims that every code path is broken. In particular, do not revive the refuted September F-003 explanation: that recorded timing example was CFR with a constant offset; periodic correlation aliasing was demonstrated separately.

## 3. FunGen 2 comparison: documented capability versus our evidence

Primary vendor pages accessed 13 October 2026; vendor descriptions are not independent quality measurements. No competitor source, model or asset is copied.

| Capability documented by FunGen | SamNPlayer evidence | Next acceptance target |
|---|---|---|
| Frame-accurate multi-axis editing, transformations and persistent undo | EDIT-00 records fixed-axis/snapshot limitations at its older baseline; original repo companion-axis editing has since landed | Refresh EDIT-00 after upstream reconciliation. Persist edit transactions, preserve unknown metadata/axes and inversion, reopen after crash, undo multi-axis changes atomically |
| Local models for 2D and VR, batch generation | Go classical path and optional AI exist; P6 persistent batch and AutoDirector remain plans in PR #2 | Complete P4/P6, then selective scene retries and independent verification. VR requires separate projection/data/evaluation work; it is not achieved by adding an LLM |
| Device-aware Doctor / correction workflow | Quality Doctor, device limits and richer device output already exist | Separate signal regularity from motion fidelity; preview proposed changes and require accept/undo. Keep the device safety governor independent of model output |
| Integrated video/editor/library workflow | Existing Player Play/Create/Edit workspace, shared clips, lane and scene work | Preserve familiar layout; prioritize correct state, recoverability, timeline readability and low interaction latency over a new skin |

Sources:
- https://fungen.app/
- https://fungen.app/de/funscript-editor/
- https://fungen.app/ai/
- https://fungen.app/guides/

Do not use FunGen output as ground truth. It is a comparator. Reference motion comes from independent annotations or known synthetic motion; manual review and error analysis remain necessary. No “best system” or parity claim until the same clips/hardware and documented versions support it.

## 4. Dependency reduction: replace responsibilities, not language labels

The existing default eligible generator path already uses Go orchestration with OpenCV CSRT through C++; Python remains for special paths/optional AI/training. A Go entry point does not mean pure Go or no native dependencies.

| Responsibility | Current inspected implementation | Candidate experiment | What remains to prove |
|---|---|---|---|
| Container metadata | ffprobe preferred; narrow Go ISO-BMFF fallback | Harden the existing Go parser first | Correct track selection, unknown values, rotation, duration, edits, malformed boxes; no guessed codec |
| Analysis frames | FFmpeg rawvideo reader; OpenCV VideoCapture in trackcv | Windows Media Foundation Source Reader behind the same contract; retain current fallback | Codec/profile/container coverage on supported Windows systems, actual PTS, color/rotation, seek/cancel, resource lifetime |
| Audio analysis input | FFmpeg decode/downmix/resample; Go analysis | Evaluate pinned Rust Symphonia for supported audio/container combinations | Codec profile coverage, resampling/downmix implementation, start offsets/gapless handling, packaging cost and output parity |
| Proxy/transcode and clip extraction | FFmpeg in videox | Keep until separately demonstrated alternatives exist | Decoding support alone does not replace encode/remux/filter capability |
| Classical tracking | OpenCV CSRT + Go posttrack | Profile first; Rust only for a measured hotspot | Pixel conversions/copy overhead, build complexity, quality and cancellation; existing losing flow bake-offs are not a reason to port |
| Optional inference/training | Python/ONNX/LM Studio and Suite training | Keep training isolated; evaluate a native inference adapter separately | Runtime/model size, model compatibility, preprocessing parity and actual inference providers |
| Product orchestration/history | Go/Wails | Go | Avoid another service/runtime for queue or history |

**FFmpeg removal must be counted honestly.** A Rust FFmpeg binding still depends on FFmpeg. OpenCV VideoCapture may use native media backends depending on the build: inventory the linked backend before claiming removal. An OS backend reduces bundled dependencies but introduces platform/codec availability dependencies. Symphonia is an audio/demux candidate, not a demonstrated replacement for our complete video/proxy pipeline.

Primary technical references:
- Microsoft Source Reader: https://learn.microsoft.com/en-us/windows/win32/medfound/source-reader
- Symphonia pinned documentation: https://docs.rs/crate/symphonia/0.6.1
- FFmpeg wrapper example: https://github.com/zmwangx/rust-ffmpeg

Microsoft documents that Source Reader can decode media but does not provide presentation-clock management or audio/video synchronization; the application still owns those semantics.

### Proposed media boundary (ENG-02 adjunct, not an implemented API)

Keep Go as owner of lifecycle, context cancellation, cache, jobs and errors. Add narrow capabilities: Probe, OpenFrames, OpenPCM, CreateProxy. Backends explicitly advertise supported operations and return unsupported separately from corrupt input or cancellation.

For every frame, record:
- selected stream identity, original integer PTS and rational timebase;
- normalized project time in integer microseconds plus normalization origin;
- presentation duration when known and an explicit missing-timestamp state;
- source versus resampled timeline identity;
- dimensions, stride, pixel format, rotation/color treatment and buffer ownership.

For PCM, record sample rate, channel layout, frame count, first-sample timeline origin, discontinuities, decoder delay/padding policy and end-of-stream. Do not infer synchronization from sample count alone if the stream starts later than video.

Seek specifies the target and reports the actual presentation time returned; test decode-from-keyframe/discard-to-target semantics. Do not silently switch decoders halfway through a job. Restart from a defined checkpoint with recorded backend/version if fallback is needed. Cancellation must terminate work and never publish partial output as complete.

For Rust, start with one optional prototype, not a second application core. Decide between a narrow C ABI and a worker only after measuring copy/startup/crash costs; validate buffer lengths, ownership, errors and cancellation. Do not introduce both paths upfront.

### Measurement gate

Record cold start, per-stage wall time, peak process-tree memory, decoder count, subprocess count, distributed binary/runtime size and setup steps. Compare identical content hashes, settings, hardware, decoder versions and output policy. Separate decoding, preprocessing, tracking, inference, postprocessing and file I/O.

Fixtures: CFR, VFR, B-frames, nonzero stream starts, rotation, no audio, late audio, multiple tracks, Unicode paths, long clips, corrupt/truncated input and seek near EOF. Record codec/profile/container rather than filename alone.

Require no loss of supported workflows, fidelity, timing, cancellation, file preservation or device safety. Use exact comparisons for unchanged deterministic Go transforms; justify decoder numeric tolerances from measurements. P4's draft correlation-loss allowances do not authorize a degraded shipping port. Equal measured behavior can justify fewer dependencies; speed alone cannot.

## 5. Make P6 crash recovery precise before coding the queue

Extend P6, do not create a second queue:

1. Journal every state with schema version, batch/clip/attempt IDs, pipeline/config digest and sufficient payload to reconstruct durable clip state; alternatively document a single authoritative snapshot+journal design.
2. Each output has a commit intent containing input identity, config/pipeline digest, temp/final paths and expected content hash. Persist it before publishing the output. Write a durable commit result afterwards.
3. Recovery reconciles intent, output hash and final event. A matching completed output is recognized even if the process died before the done event; an unrelated existing output is never assumed to be ours.
4. Keep each attempt distinct. Changed input invalidates reuse; size/mtime/first-MiB hashing alone is not full content identity. Use full content identity for reusable finalized results or explicitly document the weaker guarantee.
5. Define all legal transitions: paused/cancelled during decode, verifying and committing; terminal batches may include review/failed/skipped results. Cancellation cannot race a stale callback into done.
6. Test kill points before/after temp sync, intent sync, rename, result sync and report rebuild. Verify final contents and report reasons, not just matching counts.
7. Keep results atomic, batch recovery paused, and device output disabled during generation. Resume the same single-clip pipeline.

## 6. Learning: build on existing Go model and confirmed corrections

Existing `profilemodel` already trains and suggests locally in Go. Extend it; do not replace it with an unmeasured large model. Separate four responsibilities:
- perception proposes regions/scene evidence;
- selection ranks proven processing candidates;
- repair proposes bounded curve edits;
- device projection enforces capabilities and safety.

**Data contract proposal:** consent scope, local sample ID, source-content group, scene span, feature-schema version, pipeline/model version, original candidate, confirmed correction, accepted/rejected/uncertain label and reason, validation evidence. Default exports exclude media, identifying paths and raw free-text labels; explicit media export is separate.

**Avoid self-reinforcing errors:** auto-generated outputs are not verified training labels; “accepted” can mean stylistic preference rather than video fidelity. Keep those labels distinct. Teacher/VLM proposals need human confirmation or independent evidence before training use.

**Evaluation:** split by source video/content family BEFORE scene extraction so adjacent frames/re-encodes cannot leak between train and validation. Freeze a held-out set and compare the existing Go model, a rule baseline and the candidate on it. Record class coverage, abstention, incorrect confident suggestions and downstream fidelity; two samples mean executable training, not a reliable model.

**Promotion:** collect with opt-in → offline train → held-out evaluation → shadow suggestions → explicit enable after review. Preserve manual settings and the last accepted model. Model/version mismatch, missing runtime, timeout and unfamiliar inputs abstain or fall back to classical processing. Use a model registry with dataset digest, preprocessing/schema versions, checksum, evaluation report and rollback target. Deleting source samples must also explain/invalidate derived models trained on them; deleting a JSONL file is not model unlearning.

**AI curve draft/repair:** bounded scene/axis output, schema/finite/range/timestamp checks, independent video/timebase/fidelity verification, preview and accept/reject/undo. No unsupported inferred extra device axes. No learning process may change safety limits or emit device commands. LAN LM Studio remains explicitly enabled inference; it is not automatically a training pipeline.

## 7. GUI: evolve the approved workspace

Preserve the approved dark cyan/amber visual direction and existing Play/Create/Edit workspace, large video, library, inspector and timeline. Reuse `docs/PLAY_UI_CONTRACT.md`; request missing functional hooks from Claude.

- One shared playhead/viewport for curve, scene strip, chapters, audio and companion lanes.
- Persisted history must expose saving/recovering/conflict states; “saved” only after durable success.
- Batch shows real per-clip state and failure reason, resume/retry/cancel; percentage must come from work performed.
- AI shows proposal versus applied state, source/model, validation status and an undoable diff. Do not turn centroid similarity into a “percent correct” badge.
- Effective device preview separates the authored curve from clamped output and requires explicit arming; no connect/on-load auto-motion.
- At 1366×768, collapse side panels intentionally and keep transport, stop, active axis and timeline reachable. At 1920×1080, use the extra space for video/timeline; avoid merely enlarging empty cards.
- Cover empty, loading, ready, playing, paused, busy, cancelled, fallback, failed, disconnected, focused and disabled states; keyboard navigation and reduced motion remain first-class.
- Benchmark long projects before adding visual effects. Render only visible timeline data and avoid whole-project copies per edit. Keep the full edit data separate from display decimation.
- Test actual Wails views and recordings at both target sizes. Concept images are not screenshots or proof of working controls.

This review does not install a new GUI or alter JS/CSS. Integrate original #489 and newer lane/state hooks before a new visual layer.

## 8. Small reviewable work packages

These extend PR #2's existing gates, in order; no calendar promises.

| Package | Existing gate | Deliverable | Suggested implementation owner |
|---|---|---|---|
| REVIEW-01 | N0/N1 | Reconcile upstream original main, #527 companion edit and #529 status; refresh EDIT-00 rather than rebuild delivered work | Claude |
| MEDIA-01 | N2 / ENG-02 | Correct unknown metadata assumptions + synthetic MP4/caller regression tests | Claude; bounded slice available to ChatGPT after claim |
| BENCH-01 | N2 / P4 | Stage timings, process-tree dependency inventory, strict shipping gate and periodic-correlation safeguards | Claude core, ChatGPT review |
| BATCH-RECOVERY-01 | N3 / P6 | Complete journal/output commit protocol and crash-window tests | Claude |
| MEDIA-02 | N2 adjunct | Timestamp/PCM seams retaining current implementation and behavior | Claude |
| MEDIA-03 | after MEDIA-02 and baseline | One isolated native decoder/audio prototype with measured parity/coverage report | Claude native, ChatGPT comparison |
| LEARN-01 | N6 preparation | Grouped holdout + model provenance/rollback + shadow evaluation of existing Go model | Claude core, ChatGPT test/spec review |
| GUI-01 | N5 plus existing UI work | Durable-history/AI/batch state contract and real-view visual review | Claude behavior, ChatGPT CSS/visuals |

**Immediate recommendation:** MEDIA-01 and corrections to P4/P6. They unblock reliable dependency and product work without a speculative full FFmpeg replacement. Keep the current decoder path until a measured replacement passes.

## 9. Handoff

AGENT_COORD:
- agent: ChatGPT
- lane: Next engineering review (docs only)
- branch: chatgpt/next-engine-review-20261013
- based_on: Next main; inspected PR #2 at 69399eebd6e5f241c5e1820b33325e21b291210d
- claim: this new review document only
- implementation: not started; no benchmarks or hardware tests claimed
- needs_from_other: Claude review, reconcile upstream and claim first implementation package

Do not change the old coordination board's running lanes or rewrite Claude's branch. This additive review can land independently; contracts remain proposals until implementation PRs and their tests are accepted.
