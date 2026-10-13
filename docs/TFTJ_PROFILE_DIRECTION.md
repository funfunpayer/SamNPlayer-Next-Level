# Tf/Tj as profile — direction (not yet implementation)

Owner direction, 21 Sep 2026 (answers amended same day). Think first;
implement only after this shape is agreed. Related: `docs/ENGINE.md`,
`docs/GENERATE_HEURISTICS.md`, `docs/NEXT.md`, `docs/FINDINGS_TIMING_TF.md`,
`docs/PRODUCTION_ROADMAP.md` (G1 / G3).

---

## What the owner wants (plain)

1. **Scene profiles are feel recipes**, not “recognition products.”
   Names will be renamed for clarity; later the **right profile should
   be suggested automatically from the motion/scene pattern** (likely AI).
2. **Everyday Generate does not force marking everything.** FunGen 2
   does not either (including without YOLO). Classical path: 4-zone
   sampling (+ audio as check/hint).
3. **Marking is optional and situational:** only for profiles that need
   a **contact partner** (Tf / blow / similar) **and only when the user
   wants contact vibration**. Then they mark the contact target. When
   that mark moves with the scene, we must **compensate** (track the
   partner, not assume a static box).
4. **Contact vibration on Normal and Auto** — default available / on,
   **user can turn it off**.
5. **Learning / AI** is for (a) later **auto profile pick** from pattern
   and (b) better partner proposals — not for inventing the 0–100 curve.

---

## Owner answers (21 Sep 2026) — locked

| # | Question | Answer |
|---|----------|--------|
| 1 | Auto / marking | **Mark only when needed:** Tf + blow (and similar contact scenes). Mark **only if contact vibration is wanted**. Not “mark everything.” When the marked partner **moves**, compensate (track it) — a dead static box will drift. |
| 2 | Contact vib on Normal / Auto | **Yes — on by default; user can disable.** |
| 3 | GUI names / profiles | **Rename** for clarity. Profiles stay **profiles**. Later: **auto-detect / suggest** profile from scene pattern (mix of current set + blow + more). That suggestion layer is **AI/pattern** work — not classical G1. |
| 4 | Where Tf/Tj “sits” | **Feel recipe on top of the best classical stroke path** — Normal/Auto recognition stays the curve writer; Tf/Tj (+ contact vib) is how Neo 2 *feels*, not a second tracker product. Owner (21 Sep pm): Normal recognition already works better; Tf/Tj should ride that, with Contact. |
| 5 | Milestone UX | **Show every usable motion candidate** the system can find; user mainly marks the **primary stroke**. Quality must not equal “how well you painted boxes.” Silent auto-commit of ROI2 remains forbidden. **Changed by the Owner 28 Sep:** ROI2 may be filled from the AI's contact-partner proposal only when the user switched on the setting **“Apply AI setup automatically”** (default off); every applied value is shown and undoable; user values always win; never with a Tf/Tj distance profile (ROI2 would change the curve source). See `docs/SCENE_UNDERSTANDING_PLAN.md` §3b. |
| 6 | YOLO while available | Optional class helpers (**penis / glans / nipple** and similar) as *proposals* for main + contact targets — never write the 0–100 curve. |
| 7 | Tf/Tj vs Contact (21 Sep eve) | **Tf/Tj profile not required in the product GUI.** Everyday = stroke track (CSRT tip or 4-zone) + **Contact vibration**. Zone 2 optional. Distance Tf/Tj remains CLI/legacy only. |

---

## Milestone (owner, 21 Sep pm) — “mark the main stroke”

Product bar we are aiming at:

```text
  Classical path finds / shows motion candidates (4-zone + overlays)
       ↓
  User confirms or paints ONE primary stroke region (optimal mark)
       ↓
  Optional: YOLO/class proposes penis / glans / nipple / partner
       ↓
  Profile = feel (Normal stroke vs Tf/Tj suction + Contact vib)
       ↓
  Funscript + device_recipe — curve from classical track, not from AI
```

**Why this is the right split**

- Measuring already showed: two-point distance alone does not close the
  FunGen gap; hub/single-ROI timing issues are shared. So “more Tf/Tj
  markers” is the wrong lever.
- Contact vib on Normal/Auto (#149) already decouples vib from “must be
  Tf profile.” Next is to make Tf/Tj *feel* the default when contact
  scenes are suggested — without forcing dual-ROI everyday Generate.
- Showing candidates (overlay / list) is honest UX; auto-committing
  `find_two_rois` was measured bad (issue #8) and stays out.

**Not this milestone:** AI invents positions; silent second ROI; park
classical until YOLO is “perfect.”

## Diagnosis (what we already measured)

| Fact | Source | Consequence |
|------|--------|-------------|
| Whole-clip FunGen r≈0.06 on Tf/Tj goldens is mostly **lag drift**, not “Tf/Tj is noise” | windowed phase / #143, `clip_voll` | Do not redesign tracking from whole-clip r alone |
| Hub (single-ROI) on same clip also drifts; Signal Quality ≠ Motion Fidelity | goldens + NEXT | Timing drift is not Tf/Tj-specific |
| Two-point distance alone does not close FunGen gap on titjob POV | NEXT Sep 16 | Multi-mark is not the FunGen parity path |
| `find_two_rois` was measured insufficient for default | NEXT §3, issue #8 | Never silent-auto-commit ROI2 (28 Sep: only via the opt-in setting “Apply AI setup automatically”, default off, shown + undoable) |
| `region_fusion` / `region_fusion_auto` exist; auto is synthetic-only | NEXT Sep 16 | No-mark classical candidate; measure before default |
| Contact vib works when distance signal is good | NEXT §5 | Decouple vib recipe from “must be two-ROI”; when partner is marked, **track** it |
| OpenCL checkbox forced Python → MIL on old builds | #141 / #145 | v0.5.16 removes trap; re-smoke before blaming “auto” |

**Bottom line:** three layers were conflated:

```text
  A. Tracking mode   → how we get a 0–100 curve from video
  B. Device profile  → how that curve maps to Neo 2 (suction / vib / recipe)
  C. Marking UX      → none | one ROI | contact partner (only if vib on)
  D. Profile suggest → later: pattern/AI picks B from the clip
```

---

## Target product shape

### Profiles (B) — feel recipes (rename in GUI)

Working set (GUI rename ship in **v0.5.21** prep; keep CLI aliases):

| Profile (concept) | GUI label (values) | Feel | Marking (C) |
|-------------------|--------------------|------|-------------|
| **Normal / hub** | Stroke (Normal) → `standard` | Stroke / usual suction | None by default |
| **Auto** | Autotune → `autotune` | “Just generate” classical path | None by default |
| **Weich** | Soft (rings) → `weich` | Soft / filtered Normal | Same as Normal |
| **Tf / Tj** | CLI only (`tf`/`tj`) | Suction-centric | Partner mark **only if contact vib enabled** |
| **Blow** (and similar oral/contact) | later | Contact-centric recipe | Same rule as Tf |
| **Mix / others** | later | Combinations we already have + more later | Per-recipe rules |

Tf and Tj remain **aliases** for one suction-family profile until rename
ships. **Blow** is a sibling contact-family profile, not a second tracker.

### Contact vibration

| Context | Behavior |
|---------|----------|
| Normal / Auto | **On by default; user can turn off** |
| Tf / Blow (contact family) | On by default when that profile is selected; off disables partner-mark requirement |
| Signal without partner mark | Envelope from **deep / high-`pos` slice** of the single curve (honest: stroke depth, not tip↔body) |
| Signal with partner mark | Distance (or relative) envelope; **partner ROI is tracked** (motion compensation) — not a frozen box |

### Marking UX (C) — situational

```text
  Default Generate     → no marks (4-zone / single-curve classical)
  Contact vib OFF      → Normal/Auto: no marks; Tf/Tj: still two markers (tip+partner)
  Contact vib ON
    + Normal/Auto      → optional partner mark; without it use depth envelope
    + Tf / Blow / …    → two markers required; track partner over time (not Fix default)
```

**Compensation rule (owner):** if the user marks a contact target and the
scene moves (camera, body, hands), the marked region must be **followed**.
Static “paint once and forget” will desync vibration from real contact.
Reuse existing multi-point / CSRT partner track; do not invent a new
tracker family for this.

### Tracking modes (A)

1. **Auto tip → CSRT Stroke + Contact** — everyday first choice (measured best vs FunGen; see `EVERYDAY_GENERATE.md`)
2. No-mark classical (4-zone) — advanced / weaker opt-in
3. Primary + **tracked contact partner (Zone 2)** — only for where Contact should feel
4. AI class proposals — region box only; never write the curve

### Profile auto-suggest (D) — later, AI/pattern

Not G1. After classical Generate + vib recipes are solid:

- Input: motion signature, optional audio tempo, optional weak class scores
- Output: **suggested profile** (Normal / Blow / Tf-family / Mix / …)
- User can override; suggestion never silently locks a bad recipe
- Needs labeled examples over time (same training loop as #119 family)

---

## Bugs / debt before new UX

1. **#145** — smoke on portable **v0.5.16**; close or fresh bug with Go log.
2. Auto path must not fall back to Python MIL.
3. Do not default-wire `find_two_rois` (issue #8).
4. Land goldens (`clip_voll` / #146 content now on main via #140) for
   windowed Motion Fidelity bake-offs.

---

## Proposed work order

| Step | Work | Exit gate |
|------|------|-----------|
| **0** | This doc + owner answers (done) | Accepted |
| **1** | Smoke v0.5.16; resolve #145 | Go CSRT / Path: Go in log — tip-only Autotune **DONE** #164; smoke **0.5.18** |
| **2** | Contact vib on **Normal + Auto**, default **on**, user toggle **off** | **DONE** #149 |
| **3** | Contact-family: Zone 2 always two markers for Tf/Tj; vib on → **tracked** partner (not Fix default) | **DONE** #160 |
| **4** | Measure no-mark 4-zone vs FunGen no-YOLO (windowed) | **DONE** on Claude `clip_ausschnitt` — 4-zone **below** tip CSRT (see below); keep opt-in, do **not** default |
| **4b** | **Motion candidates UI** — show all usable motion/ROI proposals before Generate; user picks primary | **DONE** #167 — in **v0.5.19** |
| **4c** | GUI opt-in **Track whole-frame motion (4 zones)** (`region_fusion_auto`) | **DONE** #169 — no mark; Normal + Contact vib; Contact-first (no Tf/Tj gate) |
| **5** | GUI rename of profiles; keep aliases | **IN PROGRESS** — Stroke/Soft/Autotune labels (values unchanged) |
| **6** | Decouple profile pick from forced dual-ROI when vib off; Tf/Tj feel available on Normal stroke path | **PARTIAL** — Play Contact save/preview + SuggestPipeline → Stroke; Zone2 distance still CLI / FEEL_DECOUPLE scaffold |
| **6b** | YOLO class proposals (penis / glans / nipple) as opt-in helpers for primary + partner | Suggest ≠ commit; needs working AI Train (#119) |
| **7** | AI/pattern **profile suggest** (Blow vs Normal vs Tf-family vs Mix) | After G1 goldens; G3; override always |

### Next release train (after 0.5.18 smoke)

| Release | Ship | Why |
|---------|------|-----|
| **v0.5.19** | Step **4b** candidate overlay (read-only) | Show motion without forcing marks |
| **v0.5.20** | Step **4c** GUI 4-zone opt-in + Contact-first (#169) | Everyday = stroke + Contact vib; 4-zone stays opt-in after measure |
| **v0.5.21** | Step **5** rename + step **6** Play feel-decouple + Flow soft warns | Owner Flow smoke on 0.5.20; do not default 4-zone or Zone2-distance |
| **later** | Step **6** finish (stroke+tracked partner) + **6b** YOLO + **7** profile suggest | Only after classical candidates feel trustworthy |

### Step 4 measure — Claude `clip_ausschnitt` (21 Sep 2026)

Video: `clip_ausschnitt_b76a.mp4` (~50s, 1280×720 CFR 24fps). FunGen refs from
`generator/testdata/golden_clips/clip_ausschnitt_native/`. `clip_voll.mp4` not
available in the agent environment (skipped). Windowed = 10s, max-lag 1000ms.

| Variant | whole-clip r vs ohne_yolo | windowed mean r vs ohne_yolo |
|---------|---------------------------|------------------------------|
| CSRT hub (committed native Go) | 0.440 | **0.590** |
| CSRT tip-tight (`426,450,320,180`, `--axis y`) | 0.447 | **0.468** |
| **4-zone `region_fusion_auto` (no mark)** | 0.189 inv | **0.363** |
| CSRT auto_roi huge box (bad tip) | 0.035 | 0.309 |
| Python OLD bonus | 0.165 inv | 0.446 |

Contact vib: stroke actions **identical** with/without `--contact-vibration`;
recipe gets `contact_vibration=true` + `curve=soft` (device path only).

**Gate:** promote 4-zone only if ≥ single-ROI CSRT → **not met** (0.363 < 0.468
tip CSRT, and far below committed hub 0.590). Keep GUI button as **opt-in**.

### Method matrix (same clip, eve 21 Sep) — compare trackers

Same tip ROI `426,450,320,180` `--axis y` where a mark is required. Windowed
10s vs FunGen ohne_yolo (primary ranking):

| Rank | Method | windowed mean r | whole-clip r | Notes |
|------|--------|----------------:|-------------:|-------|
| 1 | CSRT hub (native committed) | **0.590** | 0.440 | Best baseline |
| 2 | `region_fusion` tip | **0.562** | **0.501** | Marked; near hub — revisit bake-off caveat (auto_roi ROI was weaker) |
| 3 | `grid_lk` tip | 0.494 | 0.416 | Faster; below hub |
| 4 | CSRT tip-tight | 0.468 | 0.447 | Same tip as rows 2–3 |
| 5 | 4-zone `region_fusion_auto` | 0.363 | 0.189 inv | No mark — keep opt-in |
| 6 | `flow --flow-downscale 0.5` | 0.231 | 0.077 | **Completes** ~50s 720p; QD 0.55 noisy |
| — | `flow` full-res | — | — | Soft `FLOW_WALL_WARN` at 30s; hard kill ~180s at ~920/1199 |

**Implications for next prep (Windows smoke can wait):**

1. Do **not** default 4-zone or Flow in GUI.
2. Optional later: Advanced `region_fusion` (marked) — only after a second clip
   confirms tip-matched ≥ CSRT; values above beat Claude #154 auto_roi bake-off.
3. CLI Flow: recommend `--flow-downscale 0.5` (full-res still too slow).
4. Soft wall/stall warns (#172) are enough for owner Flow smoke when ready.
5. Re-run matrix on `clip_voll` when media lands.

---

## Explicit non-goals (for now)

- Silent auto-commit of two ROIs
- AI writing the funscript curve
- Mandatory marking for Normal/Auto when contact vib is off
- Treating whole-clip FunGen r as the only quality number
- Shipping profile auto-detect before vib-on-Normal/Auto + partner track

---

## Next concrete implementation (after #169 / v0.5.20)

Steps **2–4c** done. Step **4** + method matrix: keep 4-zone/Flow opt-in /
CLI; tip-matched `region_fusion` is interesting but needs `clip_voll`.

**v0.5.21 prep (#172):** profile rename, Play Contact on stroke, Flow soft
warns. **Windows owner smoke deferred** — continue prep from clip matrix.

Next prep after #172 merges:
1. Optional Advanced `region_fusion` (marked) — only after second-clip confirm
2. CLI doc: prefer `--flow-downscale 0.5`
3. AliasingRisk period-detector floor (~100–140ms)
4. Finish step 6 Zone2→tracked partner on stroke (behind vib, not default)
5. Re-run matrix when `clip_voll.mp4` available

Do **not** leapfrog to YOLO auto-commit or profile AI suggest.
