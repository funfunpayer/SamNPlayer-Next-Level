# Implementation brief: match user-rated FunGen2 motion references

**Status:** this brief's own job is done — Work Package 1 (a trustworthy
benchmark, `generator/fungen_compare.py`) shipped, and Work Package 2's
concrete bug (`track_two_points`'s Y-only distance) was found and fixed.
The baseline table below is the brief's *original* measurement on a local,
never-published dataset; it is superseded by real-clip numbers gathered
since — see `docs/NEXT.md` priority 2 for the current, still-growing
measurement record (real clips, synthetic-dataset run, the grid_lk
two-point result, all with sources). Keep this file for the rationale and
work-package structure; treat its own baseline table as historical, not
current.

## Objective and product decision

The user reports that FunGen2 follows the source motion substantially better
than SamNPlayer. Treat the user's FunGen2 exports as the working comparison
target, not as an interchangeable generator with no preferred outcome.
Improve SamNPlayer's timing, rhythm, and relative motion fidelity for the
shared Tf/Tj use case (titfuck / titjob). These names describe the same use
case: identical results are intentional, not a defect.

This is an implementation assignment, not a claim that parity is achieved.
Prepared against `main` at `bd8fe13` on September 14, 2026.

## Measured baseline

The local test folder supplied by the user contains 30 SamNPlayer exports
(10 groups, each with Hub/Tf/Tj) and four unqualified `.funscript` exports
treated as FunGen2 references. All 34 files have finite positions in 0–100
and strictly increasing nonnegative timestamps. Four binary `.fungen`
projects start with `FGPROJ`; they are not JSON exports and were excluded.

All ten Tf/Tj pairs have identical action timestamps and positions.
One reference contains only two equal-position actions. Its correlation is
undefined; it is excluded from the aggregate motion comparison.

The following measurements use linear interpolation at 20 ms on the shared
absolute timestamp interval. MAE is in position points; r is signed Pearson
correlation. Test IDs correspond to sorted SamNPlayer group names in the
local analysis. Each of the three moving references receives equal weight.

| Test | Shared duration | Reference actions | Hub r / MAE | Tf and Tj r / MAE |
|---|---:|---:|---:|---:|
| 04 | 76.3 s | 163 | 0.005 / 27.5 | 0.066 / 35.7 |
| 06 | 696.1 s | 848 | -0.157 / 33.0 | -0.037 / 26.2 |
| 09 | 24.0 s | 56 | -0.055 / 39.4 | 0.038 / 30.3 |
| Mean | — | — | -0.069 / 33.3 | 0.023 / 30.7 |

These results support the reported mismatch, but do not isolate its cause.
Tf/Tj output spans 20–90, while moving references span 0–100. Direct MAE
therefore includes device mapping differences. Do not remove the device
range restriction merely to improve this metric. A ±2 s exploratory offset
search did not reveal a consistently strong match across the three clips.
Do not optimize absolute correlation: opposite phase is not equivalent.

## Reproduce before changing the algorithm

The user-provided dataset remains in a local `Downloads\funscript-tests`
folder on the user's machine.
The earlier local analysis and full filename/hash mapping are in
`docs/chatgpt-context/script-comparison.json` and `compare_local_scripts.py`
in the original local workspace; these are local artifacts, not repository
dependencies. The raw videos, filenames, project binaries, and chat exports
are not part of this published brief. A remote contributor needs a separately
provided dataset or neutral fixtures; do not assume these local paths exist.

First deliver a portable comparison command accepting dataset and output
paths. Store an explicit manifest with neutral case IDs, hashes, generator
versions, video time ranges, reference-export settings, ROIs, and parameters.
Verify identical source cuts and absolute timestamps before interpreting
differences. Use exact names or the documented batch filename sanitation
and 80-character truncation, rejecting ambiguous matches. Do not silently
search other Downloads folders or count copies twice.

Report overlap coverage, missing references, constant signals, invalid
actions, and exclusions explicitly. Compare the pre-device motion signal
and final mapped output separately. Export per-clip plots and machine-readable
metrics: signed r, raw MAE, a documented shape-normalized error, excursion
amplitude, and peak/trough timing and event precision/recall. Define event
prominence and matching tolerance before tuning; match events one-to-one.
Do not use a per-clip best offset or inversion as the production score.

## Work packages in order

### 1. Establish a trustworthy benchmark

- Reproduce the baseline above and explain any difference rather than
  silently replacing it with new numbers.
- Resolve the constant reference with the user: intentional inactivity or
  incomplete export. Retain inactivity as a separate test if intentional.
- Obtain references for the six unmatched groups and reserve complete clips
  for validation before tuning. Three moving references are insufficient
  to establish generalization; do not split adjacent frames across folds.
- Add neutral synthetic fixtures for known phase, irregular cycles, pauses,
  amplitude changes, timestamps with offsets, and constant signals.
- Regression tests must expose the old comparison errors: duplicate inputs,
  independent start-time shifting, invalid correlation for constants, and
  ambiguous filename matching.

### 2. Isolate the source of the mismatch

Relevant code: `generator/generate_funscript.py::track_two_points`,
`generator/auto_roi.py`, `generator/tf_tj_meta.py`, and
`generator/backends.py`. Preserve intermediate tracking and processing data.

The existing local batch runner invents ROI2 by shifting ROI1 right by
1.2 box widths. This does not establish that either box tracks the intended
reference point. Compare manually validated two-region runs with the current
automatic setup on the same clips before changing smoothing or normalization.

The current two-point tracker measures absolute vertical center separation,
holds the previous box after a tracking failure, and reports no scene cuts.
Investigate orientation sensitivity, identity swaps, occlusion, lost tracking,
and transitions independently. Its comments claim zoom invariance, but pixel
distance changes under zoom: test that claim and correct it. These are
investigation targets, not measured causes of the current mismatch.

Run controlled ablations: validated regions, raw distance, scene processing,
smoothing/keyframe reduction, then position clamping. Keep input cuts and
all unrelated settings fixed. Record which stage introduces lost cycles,
phase errors, flattened excursions, or motion during pauses.

### 3. Improve the shared tracking path

Implement the smallest change supported by the ablations. Reuse the backend
registry for a new analysis method. PR #6 already adds optional ONNX region
proposals (`generator/ai_roi.py`, `docs/AI_ADAPTER.md`); it does not demonstrate
two-region identity tracking or reference parity. Evaluate it only with a
documented compatible model and measured benefit. Preserve a working local
non-AI path and manual correction.

Add failure/confidence reporting where necessary so tracking loss does not
silently become a confident motion signal. Do not treat an internal Quality
Doctor pass as evidence of agreement with FunGen2. Update TRACK_CACHE_VERSION
when changing cached tracking behavior, as required by CONTRIBUTING.md.

### 4. Present one profile, retain compatibility

`tf_tj_meta.py` already accepts both names and writes canonical metadata
`profile: tj`. Keep `tf` and `tj` as compatible CLI/settings/import aliases.
Use one shared recipe and one combined GUI choice; inspect persisted values
before migration. Preserve `suction_position`, vibration off, and existing
device limits. Do not require differences between the aliases in tests.
Keep this UI cleanup separate from measured tracking improvements.

## Acceptance and review

- Benchmark artifacts include commands, manifest, versions, settings,
  per-clip before/after metrics, plots, exclusions, and user assessment.
- On frozen validation clips, both median shape error and median matched
  event timing error must improve over baseline. Report every clip's
  regressions and event misses; an aggregate gain cannot hide them.
- Agree absolute tolerances with the user from reviewed reference segments
  before parameter tuning. Thresholds are not yet calibrated; do not invent
  a correlation cutoff and label it parity.
- The user confirms that representative outputs are closer to FunGen2.
  Metric improvement without this check is an intermediate result.
- Existing two-point, camera, cache, backend, and Tf/Tj metadata tests pass.
  Add a failing-before/passing-after regression for each behavior fix.
  If GUI/Go interfaces change, follow the frontend/Wails checks in
  CONTRIBUTING.md. Record hardware testing separately from signal comparison.
- Independently implement improvements. Follow the repository's third-party
  policy: no FunGen source reuse or structurally equivalent port; output
  comparison is the reference method.

Deliver benchmark and diagnosis first, then measured tracking fixes, then
profile presentation cleanup in reviewable PRs. Update `docs/NEXT.md` after
each merge. This brief changes no generator or device behavior.
