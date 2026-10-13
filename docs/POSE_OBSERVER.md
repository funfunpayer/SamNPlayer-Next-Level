# Pretrained pose observer for Generate

Status: **Stage A spike landed** (offline Python; outside Everyday Generate). Concept from #191/#192.

## Decision to test

SamNPlayer should evaluate pretrained pose/keypoint models as an optional **observer/proposal layer**, not as a replacement for the existing measurable tracking and mapping pipeline.

This extends the rule in `AI_ADAPTER.md`: **AI proposes; the existing system measures.**

The first bake-off candidates are:

- **RTMPose / MMPose exported to ONNX** — primary candidate.
- **MediaPipe Pose Landmarker** — lightweight comparison candidate.
- **Current classical motion-candidate → CSRT/simpletrack path** — required baseline.

YOLO/MOT remains a separate multi-object proposal experiment. Pose and MOT can later be complementary, but neither should become the Stroke writer without measured evidence.

## Target architecture

```text
Video / decoded frames
        |
        +--> classical motion candidates --------+
        |                                        |
        +--> optional PoseObserver ---------------+--> Proposal Fusion
        |       landmarks + confidence            |        |
        |                                         |        v
        +--> camera / motion evidence -------------+   ranked ROI seeds
                                                         |
                                                  Tip + Partner
                                                         |
                                               Go CSRT/simpletrack
                                                         |
                                           mapper + Quality Doctor
                                                         |
                                                     funscript
```

The observer returns evidence. It does not return final funscript actions.

## Neutral contract

Keep the product-facing contract independent of a model family:

```text
PoseObservation
  timestamp_ms
  frame_width / frame_height
  people[]
    person_id?          optional, observer-local
    landmarks[]
      name
      x_norm
      y_norm
      confidence
    bbox?
    confidence
  model
    family
    version
    weights_id
```

Coordinates should be normalized at the adapter boundary. Conversion to pixel ROI boxes belongs in SamNPlayer, so model-specific input resolution never leaks into tracking logic.

A future implementation can expose a small interface conceptually equivalent to:

```text
Observe(frame, timestamp) -> PoseObservation
```

Do not freeze a Go API until the offline spike proves which fields are actually useful.

## What pose should help with

The useful question is not “can a model recognize a person?” It is whether landmarks improve SamNPlayer's downstream measurement.

Candidate uses:

1. body-relative seed proposals for Tip/Partner tracking;
2. stable anchor selection when a manually inferred ROI2 is poor;
3. re-seed proposals after an occlusion/loss event;
4. rejecting whole-frame/camera motion as a false motion candidate;
5. selecting candidate regions without requiring the user to mark everything.

A pose model is not assumed to expose every product-specific point directly. Derived anchors must be treated as proposals with confidence, then measured by the existing tracker.

## Bake-off

Use frozen clips/fixtures and record each candidate with identical decode/timestamp inputs.

### Observer metrics

- landmark/proposal availability per frame;
- confidence distribution;
- temporal jitter for nominally stable anchors;
- reacquisition latency after occlusion;
- stability under pan, zoom and body rotation;
- multi-person ambiguity / identity swaps;
- CPU wall time, effective FPS and peak memory.

### Downstream metrics

More important than raw pose accuracy:

- successful initialization of Tip/Partner CSRT;
- tracker lost-frame rate;
- re-seed success after loss;
- generated signal timing/shape metrics already used by the Golden-Clip workflow;
- per-clip regressions;
- operator assessment on representative real material.

The classical proposal path is the baseline. A pretrained model earns integration only if the downstream result improves.

## Runtime strategy

### Stage A — research (**code spike ready**)

Use the model in its easiest supported local runtime (Python). Keep it
**outside Everyday Generate**.

| Artifact | Path |
|----------|------|
| Contract + adapters | `generator/pose_observer.py` |
| Bake-off CLI | `generator/pose_observer_spike.py` |
| Fixture tests (no weights) | `generator/pose_observer_test.py` |
| Optional pip | `generator/requirements-pose-observer.txt` |

```bash
# Status (no video)
python3 generator/pose_observer_spike.py --status \
  --mediapipe-model "$HOME/models/pose_landmarker_lite.task"

# Single frame → PoseObservation JSON + --seeds
python3 generator/pose_observer_spike.py \
  --video clip.mp4 --frame 0 --backend mediapipe \
  --mediapipe-model "$HOME/models/pose_landmarker_lite.task" \
  --seeds --out /tmp/pose.json

# Metrics JSONL every 15 frames
python3 generator/pose_observer_spike.py \
  --video clip.mp4 --every 15 --max-frames 120 --backend mediapipe \
  --mediapipe-model "$HOME/models/pose_landmarker_lite.task" \
  --out /tmp/pose_metrics.jsonl

# Roll up JSONL → availability / wall-time / jitter summary
python3 generator/pose_observer_spike.py --summary /tmp/pose_metrics.jsonl
```

**No auto-download.** Owner places MediaPipe `.task` or RTMPose ONNX locally.
Missing model → soft `error` field, empty `people` (tests cover this).

CI runs `pose_observer_test.py` in the Python (Generator) fast suite (no weights).

Seed proposals map pose landmarks → product body-part ids (`mouth`,
`hand_1`/`hand_2`, `face`, weak `penis` from hip midline). Tip/glans is
**not** in COCO/MediaPipe pose — treat hip-midline Tip as low-confidence
only. `fuse_proposal_lists` ranks classical + pose proposals with provenance;
Feed into MT-Seed ranking later (Stage B); do not silent-commit.

### Stage B — opt-in adapter

If the benchmark succeeds, wire it through the existing local-AI adapter pattern. No model in the repository, no telemetry, no automatic download. Absence/failure must fall back to classical proposals.

### Stage C — packaging decision

Only after measurement choose among:

- external/local Python helper;
- ONNX Runtime-backed helper;
- native Go binding/runtime if packaging and maintenance justify it.

“Rewrite it in Go” is not a goal by itself. The stable boundary is the observation contract.

## Fusion rules to test

Start simple and observable:

1. classical and pose proposals are generated independently;
2. normalize coordinates and attach confidence/provenance;
3. rank proposals using confidence + temporal stability + motion evidence;
4. GUI may suggest Tip/Partner, but does not silently commit a low-confidence choice;
5. once accepted, existing Go tracker measures motion;
6. on tracker loss, observer may propose a re-seed; it does not silently rewrite history.

Do not train a custom network in this phase. Fine-tuning becomes a later option only if pretrained models show useful signal but systematic domain errors.

## Licensing and privacy gate

For every candidate record separately:

- framework/code license;
- model/weights license and source;
- redistribution constraints;
- model size/hash/version;
- whether inference is fully local.

Do not assume the framework license automatically covers downloaded weights.

## Test plan

Unit/fixture tests should cover:

- observation JSON/schema parsing;
- normalized ↔ pixel coordinate transforms;
- confidence filtering;
- missing landmarks;
- multiple people;
- malformed/partial observer output;
- timeout/crash fallback;
- deterministic proposal ranking.

Benchmark artifacts should include command, model/version/hash, runtime versions, machine information and per-clip metrics.

## Integration with current lanes

- **Cursor MT-Seed remains owner of motion-candidate → Tip+Partner suggestion UX.** Do not duplicate it.
- Pose observer should feed the same proposal concept later, so MT-Seed becomes reusable rather than replaced.
- Existing Go CSRT/simpletrack remains Stroke tracking/writing.
- Current ChatGPT E/E2 work is not displaced. Issue #191 is a separate research lane unless the owner explicitly reprioritizes it.

## Exit criteria

Proceed from research to implementation only when:

1. at least one pretrained candidate beats the classical baseline on relevant downstream metrics across representative clips;
2. regressions are listed per clip;
3. runtime is practical for the intended Generate workflow;
4. local/offline fallback is proven;
5. model/weights licensing is acceptable;
6. integration does not require changing Everyday Generate defaults.

If no candidate clears the gate, keep the existing classical pipeline and record the negative result.
