# Body regions — multi-part detect + fix + mask

Product taxonomy for **AI training**, **Generate**, and **Tf/Tj**.
UI and class IDs stay **English**. See `docs/LANGUAGE.md`.

## Why

Tf/Tj needs more than two anonymous boxes: several body parts should be
**detected**, then **locked** (fixed) or **tracked**, and extras should be
**masked** so motion does not leak into the wrong tissue. The same classes
feed normal Generate (single stroke box) and the local YOLO trainer.

## Canonical classes (English IDs)

| ID | Label | Typical generate role |
|----|-------|------------------------|
| `face` | Face | mask |
| `mouth` | Mouth | fixed (contact target) |
| `breasts` | Breasts | fixed |
| `nipples` | Nipples | fixed (common Tf/Tj target) |
| `hand_1` | Hand 1 | tracked |
| `hand_2` | Hand 2 | tracked |
| `penis` | Penis | tracked |
| `glans` | Glans | tracked (common Tf/Tj tip) |
| `vagina` | Vagina | fixed |

Legacy German labels (`brust`, `eichel`, `mund`, …) **normalize** to these
IDs in Go (`generator/bodyparts`), Python (`bodyparts.py`), and the GUI
(`bodyparts.js`).

## Roles

| Role | Meaning |
|------|---------|
| `tracked` | CSRT / multi-point tracker follows the box |
| `fixed` | Box stays where marked (static anchor) |
| `mask` | Soft-exclude punch-out for camera / grid_lk features; not a distance driver |

## Tf/Tj distance partners

- **Tip (Zone 1 / ROI1):** always tracked. Prefer class `glans`; `penis`
  (whole shaft) also works — distance uses the tip-box point **nearest the
  partner**, so contact registers at the glans end, not the shaft center.
- **Primary target (Zone 2 / ROI2):** required for Tf/Tj; optional `--roi2-fixed`.
  Typical class: `nipples` / `mouth` / `vagina`.
- **Extra targets (Zone 3+, `--target`, repeatable):** additional contact
  anchors (GUI: **+ Zone 3+**). Default **fixed**. Optional class via
  `x,y,w,h,class` or GUI **Zone 3+ class** — **label/metadata only today**
  (taxonomy / future weighting); fusion is still unweighted `min()` over
  **usable** partners (lost tracked boxes excluded). Contact vibration
  uses the same proximity signal. Go CSRT runs N partners natively
  (`TrackMultiPoints`). Soft masks: with **RhythmGrid** on they stay
  Go-eligible (converted to exclude marks; P3 #251); without RhythmGrid
  they still force Python.
- **Soft masks (`--mask`, repeatable):** GUI **+ Mask**. Punched out of
  camera-motion and grid_lk reseed feature masks only — they do **not**
  drive the stroke. Not pixel-perfect SAM segmentation; box soft-exclude.

Native Go pipeline: **MaskROIs** without RhythmGrid still force Python;
with RhythmGrid on, masks are honoured on Go (`SCENE_MAP_PLAN` M3 / #251).
ExtraTargets use Go `TrackMultiPoints` when OpenCV (or simpletrack) is available.

## Surfaces

| Surface | Capacity |
|---------|----------|
| AI training marks | up to **9** boxes/classes per frame |
| AI Train body map | Clickable human silhouette + legend (same 9 IDs) |
| Generate / Tf/Tj stroke | ROI1 + ROI2 + N extra targets (min distance) + soft masks |
| AI suggest | prefers canonical class list (`preferred_classes`) |

## Owner test

1. AI training: mark Face + Mouth + Breasts + Nipples + Hand 1 + Hand 2 +
   Penis + Glans + Vagina on one still → “Use for training”.
2. Generate Tf/Tj: tip = Glans or whole Penis (tracked), target = Nipples
   (fixed) → distance moves when the tip end approaches; contact vibration
   follows proximity.
3. Extra target: add Zone 3+ (+ Zone 3+, e.g. mouth) → stroke follows the
   **nearest** partner.
4. Soft mask: + Mask over a busy background patch → tracking/camera
   features ignore that box.
5. Settings preferred classes defaults to the CSV of all nine IDs.

Code: `generator/bodyparts`, `docs/TF_TJ.md`, `docs/KI_TRAINING.md`.
