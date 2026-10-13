# Historical team plan and proposed changes

Snapshot: September 13, 2026.
Written after reviewing `funfunpayer/SamNPlayer` on `main`, the existing
HANDOFF/README, and public FunGen 2 descriptions (website, changelog, README).
**No FunGen source code was read or copied.**

> Historical record: the findings, proposed names, and parameter tables
> below reflect that snapshot, not the current implementation. Some work
> has since been completed or superseded. Use [NEXT.md](NEXT.md) for current
> tasks, [TF_TJ.md](TF_TJ.md) for the implemented Tf/Tj recipe, and
> [CONTRIBUTING.md](../CONTRIBUTING.md) for current collaboration rules.

This was the shared development record. Code changes belong in branches
and pull requests, not only in chat.

---

## Goal

Outperform FunGen 2 **on the SVAKOM Sam Neo 2**, rather than pursue feature parity.

Start with measured classical methods. Add AI later as a suggestion layer
using the same intermediate data: tracks, signatures, ratings, and recipes.

No action detector in the first stage. The user supplies a name
(`titjob` / `blowjob` in the original proposal); the application transfers
parameters to similar motion signatures.

The original plan excludes FunGen 1 source-code reuse and structurally
equivalent ports because of its PolyForm Strict license. Publicly described
*behavior* may inspire an independent implementation.

---

## Existing strengths to preserve

- Layers: `device` / `funscript` / `player` / `generator` / GUI.
- BLE protocol documented against Buttplug Rust (`device/protocol.go`).
- `Sync()` follows the video clock; script offset is applied in one place.
- Generator options supported by measurements in HANDOFF.
- Two-point distance (`--roi2`), motion signatures, Quality Doctor.
- MIT license; `update.RepoOwner` / `RepoName` point to `funfunpayer/SamNPlayer`.

---

## Proposed order of changes

The original review prioritized the Go core's effect on device output
before UI work or specialized profiles.

### 1. BLE write path

Files: `device/samneo2.go` and related tests.

At the time of the review, every tick called `SetVibration` and `SetSuction`,
producing two GATT writes with response, up to approximately 40/s at 50 ms.
Keepalive stored only `lastPacket`, so the other channel was not maintained
during pauses.

Proposed changes:

- Maintain device state as `{vibration, suction}`.
- Repeat **both** channels during keepalive.
- Do not resend an unchanged level.
- Combine writes where the protocol supports it.

Without reliable output, recipes and profiles remain theoretical.

### 2. Mapper tests and recipe fields

Files: `funscript/mapper.go`, a new `funscript/mapper_test.go`, and
`funscript/funscript.go` for metadata.

At the time, `ToIntensityCurve` had no dedicated tests. `MinVibration`
affected the speed signal, and there was no `suction_floor` or peak gate.

Proposed changes: test independent mapping, stationary input, and fast
strokes, then add optional device-recipe fields (see below). Pass through
`profile` and `device_recipe` metadata and read them in the player.

### 3. Complete generator embedding

File: `generator/generator.go`, specifically `writeScriptToTemp`.

At the time, the temporary copy contained only `generate_funscript.py` and
`quality_doctor.py`. Imports such as `flow_backend` and `quality_model`
were missing. This was often invisible during source-tree development,
but broke `--backend flow` and the learned model in the **packaged executable**.

Proposed change: embed every module imported by the generator at runtime.

### 4. Original `titjob` / `blowjob` profile proposal

A parameter package, GUI cards, and a second ROI, rather than a detector.
These were proposed starting values, to be recalibrated after hardware tests:

| Parameter | standard | titjob (peak + baseline) | titjob_peak | blowjob |
|---|---|---|---|---|
| Signal | 1 ROI | ROI1–ROI2 distance required | Same as titjob | Mouth/head axis; optional distance to base |
| pos_floor / pos_ceil | 5–95 | 20–90 | 15–92 | 8–95 |
| hold_contact | Off | On | On | Off |
| norm_window_s | 6 | 4 | 4 | 5 |
| Suction source | Position | Inverted distance + floor 0.20 | Only pos > 70 | Depth; valleys may reach 0 |

Proposed `MapOptions` starting values:

| Parameter | standard | titjob | titjob_peak | blowjob |
|---|---|---|---|---|
| Sync | independent | independent | independent | independent |
| TickMs | 50 | 50 | 50 | 40 |
| MaxSpeed | 0.60 | 0.50 | 0.55 | 0.70 |
| MinVibration | 0.15 | 0.20 | 0.12 | 0.10 |
| Smoothing | 0.30 | 0.22 | 0.18 | 0.20 |
| suction_floor | 0 | 0.20 | 0 | 0 |
| suction_peak_gate | Off | Off | On | Off |

The original Titjob default was **peak plus baseline pressure**, with a
peak-only checkbox variant. The proposed internal names were `titjob` /
`blowjob`, with no silently assigned action classes. These historical names
and values are not the current Tf/Tj contract; see [TF_TJ.md](TF_TJ.md).

### 5. Allow the Doctor to repair output

Quality Doctor checks results. Public FunGen descriptions include repairs
for speed and gaps. Proposed first steps here: enforce minimum spacing and
speed caps (partly implemented), then provide an explicit repair step with
a before/after diff.

### 6. Small editor (learn from OFS, implement in Go)

After generation, show the curve, allow peaks to be dragged and sections
to be deleted or repeated, and retain the existing offset control. An
overlay of the most recent generated curve is enough for the first version.

**OpenFunscripter (OFS)** is a strong reference for *editor* quality
(max-speed highlights, chapters in metadata, project persistence,
frame/tempo aids). Adopt those ideas selectively and reimplement them in
Go under SamNPlayer — see [OFS_LEARN.md](OFS_LEARN.md). This is not an
ImGui/OFS UI clone and not a multi-axis OSR studio as the product shell.

### 7. UI afterward

Dark layout, icon sidebar, large video, curve and heatmap below it, and
persistent device information on the right. Amber for vibration, violet
for suction. The original generator-card proposal was Standard / Breast / Oral.

### 8. Later

- Chapters from existing scene cuts, with regeneration of individual sections.
- Audio rhythm as a fallback when visual tracking is unreliable: an idea
  from the FunGen changelog, independently implemented.
- Remove the Python runtime dependency, connecting `videox` only after
  measuring a benefit.
- AI as an adapter that suggests ROIs/profiles for human confirmation.
  It must outperform fixed rules in cross-validation. Concrete plan and
  first implementation (ROI proposal via ONNX): `docs/AI_ADAPTER.md`.

---

## Lessons from public FunGen 2 behavior

Sources reviewed at the time: fungen.app and the GitHub README/changelog
for ack00gar/FunGen binaries, not the archived FunGen 1 Python source.

Ideas to consider:

- Generate → inspect → repair → play on **this** device.
- Device-specific latency handling and interpolation.
- Provenance in the file: `creator`, origin, and our device recipe.
- Make tracking-cache reuse visible: change parameters without reprocessing video.
- Chapters and markers as editing boundaries.

Excluded from the proposal:

- Reusing its YOLO/body-part pipeline or VR Pro models.
- Six axes for OSR devices.
- Stash/XBVR as the first product goal.
- Structurally equivalent reimplementations of its trackers.

---

## Test clips

Synthetic clips without adult content and with known ground truth:

- Strokes with a stationary camera and with panning.
- Two-point distance for relative-motion testing.
- Rhythmic motion with pauses.
- Scene changes, occlusion, and changing amplitude.

The original plan called for the team to provide real 15–40 s H.264 clips,
starting with a tripod. Record human ratings in the measurement report and,
where available, compare against an externally generated script of the
same film.

---

## Original GitHub collaboration notes

Repository: [funfunpayer/SamNPlayer](https://github.com/funfunpayer/SamNPlayer).

- Keep `main` runnable.
- Work on `feat/…` or `fix/…` branches.
- Submit every code change as a pull request, including small changes.
- Include measurements in the PR rather than impressions alone.
- Update HANDOFF.md alongside code changes.
- Update the shared plan when priorities or recipes change.

Work produced in the original Grok chat was intended to go into a branch
and PR in this repository, not remain a chat snippet. Current workflow
rules are maintained in [CONTRIBUTING.md](../CONTRIBUTING.md), and the
active task list is [NEXT.md](NEXT.md).
