# MT-Speed notes — E2

Date: 22 September 2026. Owner: ChatGPT. Documentation baseline: main
`a46ab3cd09cb103d5244e9e870feb037b2689d65`.

**Status: research/write-up complete; performance and quality are unmeasured.**
This document completes E2's planning deliverable, not the MT-ID clip gate.
No model, dependency, Generate default, or Stroke writer is changed.

## Decision and scope

Keep Go tip-CSRT + Contact as the product path. Optimize an optional,
part/class-aware proposal service first: Tip plus optional body-part regions,
ranked for the user to accept. Do not make Partner CSRT mandatory. A proposal
must never silently replace a selected region or become a second Stroke writer.

Use this order:

1. Establish a class-capable detector and a detector-only proposal baseline.
2. Measure smaller input sizes and sparse proposal refresh independently.
3. Compare ONNX Runtime against the same model's original backend.
4. Only after the owner's MT-ID gate shows identity failures, compare explicit
   ByteTrack and BoT-SORT configurations. Start with ByteTrack; add camera
   compensation or ReID only to address an observed failure category.

This is a proposed experimental order, not a measured recommendation for a
particular machine. MT-Seed remains Cursor's lane. PoseObserver Stage A is a
separate proposal bake-off after MT-Seed + E2; it is not implemented here.

## Classes before tracking

COCO detection's 80-class taxonomy contains `person`, not separate hand/mouth
or application-specific Tip classes [S6]. Filtering a pretrained detector by
class cannot create those missing classes. Before timing any candidate, record
its actual class map and whether each required region is directly detected,
derived from landmarks, or unsupported. A landmark-derived box needs its own
quality evaluation; do not label it a trained body-part detector.

For a future service, propose this response contract: source frame index and
PTS; original image dimensions; model hash; class/part label; confidence;
original-image box; optional track ID; and observed/predicted/lost state.
Distinguish a semantic label (hand) from a temporal ID (track 12) and from the
user's role assignment (Tip or Zone 2). Reject stale video/seek responses.
Reset tracking state on source change, seek, or shot cut. Do not infer contact
or device output from overlapping proposal boxes.

## Tracker comparison

| Candidate | Useful capability | Experiment to justify it | Limit |
|---|---|---|---|
| Detector only | Independent region proposals | On-demand selection or periodic refresh | No identity continuity claim |
| ByteTrack | Two-stage association also uses lower-score detections [S1,S2] | Lowest-complexity ID baseline after the gate | No appearance/ReID model; cannot invent classes |
| BoT-SORT, ReID off | Motion association plus camera-motion compensation [S1,S3] | Measured camera-motion failures | Measure added cost on the target machine |
| BoT-SORT, ReID on | Appearance-assisted association [S1,S3] | Remaining annotated identity swaps | Person-oriented embeddings may not distinguish local body parts |

Select `tracker="bytetrack.yaml"` or `tracker="botsort.yaml"` explicitly;
never depend on the installed package's default. Copy and hash the full YAML.
Record `track_high_thresh`, `track_low_thresh`, `new_track_thresh`,
`match_thresh`, `track_buffer`, plus BoT-SORT `gmc_method`, `with_reid`,
`proximity_thresh`, `appearance_thresh`, and encoder identity [S1].

Do not filter out all low-score detections before ByteTrack's second stage.
Record detector filtering and association thresholds together. Tune only after
examining false proposals and missed regions. Same array slot or unchanged ID
number alone does not establish that the same physical target was recovered.

## Speed knobs and experiments

These values are **trial settings**, not production defaults or speed claims.

| Knob | First comparison | What must remain comparable |
|---|---|---|
| Model capacity | Smallest class-capable model, then one larger variant | Class map, labeled frames, confidence policy |
| `imgsz` | 640 baseline; 480 then 320 | Original-coordinate accuracy and small-region recall |
| Proposal cadence | N=1, then 2, 4, 8 | Original PTS and proposal age |
| Precision | FP32, then supported FP16 | Same model and quality labels |
| Runtime | Original backend vs ONNX Runtime | Pre/postprocessing, shape, class map, NMS policy |
| ReID | Off, then on only for measured ID failures | Same detections and temporal sampling |

Ultralytics exposes input size, video stride, batching and streaming options
[S4]. Input reduction can remove small-region detail; class filtering alone
does not promise reduced backbone inference cost. Inspect the actual padded
tensor dimensions rather than treating the requested size as the full story.
Use batch=1 for interactive response measurements. Keep a loaded model/session
alive instead of paying startup per request. Measure decoding, resizing, copies,
postprocessing and transport as well as inference.

### Every N frames is not free tracking between frames

`vid_stride=N` skips input frames in the prediction path [S4]. It does not prove
that a tracker was updated on intervening frames. Separate two experiments:

* **Sparse proposals:** run detection at sampled original PTS, display the latest
  proposal with its age, and expire it when stale. Go CSRT retains its existing
  frame cadence. Do not generate invented intermediate observations.
* **Persistent IDs:** N=1 is the reference. A tracker called once per sampled
  image may count processed updates rather than original frames and may use a
  fixed motion-model timestep. Verify the pinned implementation before varying
  cadence. Passing empty detections is not equivalent to skipping an update.

At 30 source fps, N=4 gives a 133.3 ms sampling interval. If a lost buffer counts
30 sampled updates, it spans about four seconds, not one. This is conditional
arithmetic, not a claim about every tracker implementation. For variable-rate
media use original PTS deltas and report loss duration in milliseconds. Store
both source-frame index and processed-update index. Do not reuse MT-Go's
eight-frame coast budget as an unexamined external tracker setting.

## FP16 / half and ONNX

Use FP32 CPU as a compatibility reference. FP16 is a separate hardware/provider
experiment, not a universal CPU acceleration switch. Current export docs
describe `quantize=16` and legacy `half=True` compatibility; older installed
versions can differ [S5]. Pin the package and verify its accepted arguments.

ONNX exports the detector computation, not the complete tracking lifecycle.
Keep tracker state, preprocessing and result conversion explicit. Record input
shape, opset, output layout and NMS inclusion. First compare original-backend
and ONNX boxes/classes/scores on identical frames; document tolerances before
timing. Defer INT8 until representative calibration data and a quality baseline
exist. An export that loads is not proof of equivalent detections.

ONNX Runtime uses execution providers for device-specific execution [S7]. Log
available/selected providers and profile actual execution, including fallback
and host/device transfers. Compare CPU first; test a GPU provider only on a
machine that supports it. Avoid claiming an exported model is faster without
end-to-end measurements. Preserve a working CPU fallback.

## Budget worksheet

Define measured costs in milliseconds: `D` decode/preparation per source frame,
`M` model inference plus postprocessing per detected frame, `A` association per
tracker update, and `U` proposal transport/render work per refresh. For a serial
pipeline with detection, association and refresh all occurring every N frames:

`mean cost/source frame ≈ D + (M + A + U) / N`

If association runs every source frame, use `D + A + (M + U) / N` instead.
Do not count shared decode twice. Concurrent CPU/GPU contention means these
equations are planning estimates; measure wall time and tail latency separately.

**Illustration only:** D=4, M=40, A=2, U=2 gives 48 ms/frame at N=1 and
16 ms/frame at N=4 in the first model. At 30 fps, N=4 adds up to about 133 ms
of sampling wait before processing/queueing. None of these numbers is a
SamNPlayer benchmark or a promised speedup.

Proposed initial interactive target for owner review: p95 frame-PTS-to-proposal
age <=250 ms, including sampling and queueing; never build an unbounded queue.
For offline analysis, report wall seconds/video second rather than requiring
real time. If the target is missed, offer on-demand suggestions before silently
reducing region quality. Targets are provisional, not release acceptance yet.

## Reproducible measurement handoff

The owner has the original `clip_ausschnitt` and `clip_voll`; cloud agents do not.
E2 therefore supplies a protocol, not fabricated benchmark rows.

1. Pin repo SHA, OS, CPU/GPU/driver, Python, detector/tracker/runtime versions,
   weights SHA256 and YAML hash. Check the code and model redistribution terms
   before adding a dependency; an export format does not change those terms.
2. Record each clip's hash, resolution, frame count and PTS, selected intervals,
   and initial Tip/body-part ROIs. Include visible motion, occlusion, camera
   movement and similar-looking regions. Annotate reference boxes/identities
   independently of model output. Existing funscript curves are not box labels.
3. Run detector-only FP32/640/N=1 as the quality baseline. Record cold load
   separately, warm up 20 processed frames, then run three timed repetitions
   of each identical interval. Synchronize asynchronous GPU work for timing.
4. Change one factor at a time: size, then cadence, then runtime/precision.
   Do not run a full Cartesian sweep. Recheck the combined winning settings.
   Only enter the tracker comparison if the owner opens MT-ID.
5. Log per-run p50/p95 preprocessing, inference, association, transport and
   end-to-end proposal age; wall time, peak RAM/VRAM, dropped/skipped counts,
   provider fallback, original PTS and predicted-box age.
6. Score per-class precision/recall at a declared IoU threshold (start with
   0.5), center error normalized to frame size, longest unavailable interval,
   wrong-class assignments, ID switches and recovery delay where IDs apply.
   Report each clip separately and inspect the failing frames; aggregate FPS
   alone cannot select a winner.

Suggested result columns:

```text
clip_hash,interval,repo_sha,model_hash,class_map,backend,provider,precision,
imgsz,actual_tensor_shape,N,tracker_yaml_hash,reid,run,cold_load_ms,
infer_p50_ms,infer_p95_ms,proposal_age_p95_ms,wall_s,video_s,peak_ram_mb,
peak_vram_mb,precision_iou50,recall_iou50,center_error,wrong_class_count,
id_switches,recovery_ms,max_missing_ms,notes
```

Candidate selection: propose faster settings only when the owner accepts the
per-class error tradeoff. Initial conservative gate: no new critical wrong-part
selection in annotated intervals and no observed ID-switch regression; report
all precision/recall changes, rather than silently choosing a tolerance. Small
goldens cannot establish general robustness. MT-ID stays blocked until owner
clip evidence exists. The off path must preserve existing generated actions
and tracking metadata, apart from explicitly requested debug fields.

## Handoff and completion

E2 is complete when this note and the board update are available for review.
Remaining work is implementation/measurement: Cursor can use the experiment
order for MT-Speed after MT-Seed; the owner supplies local clip measurements;
PoseObserver keeps its own Stage A scope. No spike script was added because
there is no class-capable pinned model or local golden media to validate one.

## Primary sources (checked 22 September 2026)

Rolling documentation is not a dependency lock. Archive versions/configuration
with actual benchmark results; do not copy upstream speed figures as our data.

- [S1: Ultralytics tracking arguments and configurations](https://docs.ultralytics.com/modes/track/)
- [S2: ByteTrack authors' implementation](https://github.com/FoundationVision/ByteTrack)
- [S3: BoT-SORT authors' implementation](https://github.com/NirAharon/BoT-SORT)
- [S4: Ultralytics prediction arguments](https://docs.ultralytics.com/modes/predict/)
- [S5: Ultralytics export arguments and precision](https://docs.ultralytics.com/modes/export/)
- [S6: COCO detection taxonomy](https://docs.ultralytics.com/datasets/detect/coco/)
- [S7: ONNX Runtime execution providers](https://onnxruntime.ai/docs/execution-providers/)
