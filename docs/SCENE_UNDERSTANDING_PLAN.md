# Scene understanding — what is what, what moves against what (plan)

Status: **stage 2 tools done, measured; stage 2b (VLM clip mode) built,
waiting for an Owner PC run** (Claude, 28 Sep 2026; results in §3
"Stage 2 — results"). The hybrid gap-filler and the stable runtime (§4) are
next. The Owner approved:
*"Yes, definitely — plan it, put it on the board with Cursor, start Stufe 2.
Video recognition would be good too. The stable system without big AI comes
next."*

Builds on:
- `docs/VLM_TEACHER_PLAN.md` (teachers, contact points, own RF-DETR model)
- `docs/SCENE_MAP_PLAN.md` (marks, learning export)
- `docs/TFTJ_PROFILE_DIRECTION.md` (profiles, primary stroke, contact partner)

---

## 1. Why contact points are not enough

A contact point says only **where** the stroke is. It fixed the
multi-person clip (r 0.304 → 0.406), but the engine still does not know:
- **what** each region is: body parts, and which person they belong to;
- **what moves against what**, the moving part and its partner — for
  example mouth ↔ penis, hand ↔ penis, breasts ↔ penis, vagina ↔ penis;
- **what kind of scene** it is, and **how** it moves: axis, depth range,
  tempo, pauses.

That is the information that decides **what gets tracked**:
- the primary stroke target,
- the contact partner for contact vibration,
- what to ignore,
- which profile to use.

Today a person sets all of this by hand.

## 2. Principle — hybrid, the Owner's rules kept

- **The AI proposes, the classical engine measures, the user applies.**
  The rules in `TFTJ_PROFILE_DIRECTION.md` stay:
  - proposals for the primary stroke and the contact partner;
  - silently committing ROI2 remains forbidden;
  - the AI never invents the 0–100 curve.
- Proposals use the types that already exist: `ROICandidate` with `Class`
  for "pick primary" (TFTJ 4b), and `RegionClass` / `RegionClass2` on
  ROI / ROI2.
- The profile proposal is the "auto profile from the scene pattern" the
  Owner asked for.

## 3. Stages

| Stage | Teachers deliver | The system does with it | Status |
|---|---|---|---|
| **1** | contact point | the rhythm grid searches there | done (#312) |
| **2** | body-part boxes plus **roles**: moving part, partner, ignore | proposals for the primary stroke ROI, the contact partner ROI2 and excludes | **tools done** (opt-in; GUI = Cursor) |
| **3** | scene type plus axis, depth and tempo per shot | profile proposal per shot; depth range for the mapper | next |
| **4** | the course over time | draft script from our own model (hybrid: engine curve plus model structure), through QD and Keep | later |

### Stage 2 — mechanism

1. **Parts (what is what)**, per keyframe, from any teacher:
   - NudeNet: all classes, mapped to the canonical ids in
     `bodyparts.py` / `BODY_REGIONS.md`.
   - A VLM probe: boxes by label.
   - Our RF-DETR, which is multi-class; the dataset builder already keeps
     every class.
2. **Motion (what moves)** comes from the engine itself, not from a big
   model: the rhythm-grid **scan** (`ScanSceneMap`, a per-window
   cell-score heatmap at the stroke tempo).
   - New CLI `scan-scene-map VIDEO --windows N` writes it as JSON.
3. **Roles by rule** (`scene_roles.py`):
   - Per window, a part's motion is the mean of the strongest third of
     the cells under its box (a plain mean dilutes large boxes such as
     breasts).
   - Pairs: `penis`/`glans` against `mouth`, `hand_*`, `breasts`,
     `vagina`.
   - Within the most-moving pair, the part that moves more is the
     **primary stroke target** (tracked) and the other is the **partner**
     (contact vibration target).
   - If the penis is hidden (in the mouth or between breasts), the partner
     part alone plus its motion gives the scene type.
   - Other people's parts and thighs become **exclude** proposals.
4. **Scene type from the pair:**

   | Pair | Scene type |
   |---|---|
   | mouth + penis | blowjob |
   | hand + penis | handjob |
   | breasts + penis | titjob |
   | vagina + penis | penetration |
   | hidden penis | from the partner part and its motion |

5. **Output: `<clip>.scene.json`.** Per window: parts, roles, scene type
   and confidence. Plus `proposals`: `ROICandidate`-shaped primary and
   partner boxes for the first shot and per shot, and a profile
   suggestion.
6. **Into the app:**
   - Go `LoadSceneProposals(path)` returns the proposals (primary and
     partner as `ROICandidate` with `Class`, scene type, confidence,
     window); `.At(ms)` picks the one for a video time.
   - The CLI `generate --scene-proposals FILE [--scene-at-ms MS]` uses the
     primary as ROI (and its class as region class when canonical) only
     when `--roi` is not given. It is explicit opt-in and logged. The
     partner is only logged, unless `--scene-apply` ("Apply AI setup
     automatically", Owner decision 28 Sep, below).
   - Go `ApplySceneProposal(&roi, &opts, p, withPartner)` is the one rule
     the CLI and the GUI share; it returns one line per value set or
     skipped.
   - The GUI shows them in the existing pick-primary flow and the user
     applies. That GUI part is Cursor's.

### Stage 2 — results (28 Sep, NudeNet as the only teacher)

Pipeline: `SamNPlayer-cli scan-scene-map clip.mp4 --windows N --out scan.json`
→ `python3 generator/scene_roles.py --video clip.mp4 --scan scan.json
--nudenet --out clip.scene.json --contact-out clip.contact.json`.

| Clip | Anchor | r (FunGen ref) |
|---|---|---|
| multi-person 642 s | none (today) | 0.304 |
| multi-person | NudeNet contact points (stage 1) | 0.401 |
| multi-person | hand-labelled contact points | 0.425 |
| multi-person | **stage-2 moving part, confidence ≥ 0.5** | **0.440** |
| multi-person | stage-2 moving part, every typed window | 0.449 (but voll drops 0.752 → 0.702) |
| multi-person | stage-2 pairs only | 0.341 |
| clip_voll / clip_aus | stage-2, confidence ≥ 0.5 | unchanged (0.456/0.752, 0.482/0.888) |

- The moving part beats the teachers' contact point: it tracks **what
  moves**, not just what is visible.
- Scene type is right in 15 of 21 typed windows (21 of 27 windows typed).
  The typical miss is titjob read as blowjob when the face bobs with the
  stroke. Stage 3 (axis, tempo) and the VLM clip mode should separate
  these.
- The confidence floor 0.5 is what keeps the goldens unchanged: low-
  confidence windows would move a box that is already right. The hybrid
  check below does that job better and replaces the floor when it is on.

## 3b. Modes — how much the AI does (Owner, 28 Sep)

The Owner wants several modes. All three use the same parts:

| Mode | What the AI does | What the engine does | Status |
|---|---|---|---|
| **Classic** | proposes ROI, partner, profile; the user applies | tracks and writes the curve | stage 2 (now) |
| **Hybrid gap-filler** | steps in only where the engine is weak: tracking lost, low confidence, QD flag, or nothing found; there it moves the search (contact points / moving part) or fills a draft segment | verifies every AI point against its own rhythm measurement (`--contact-verify`) | **step 1 done** (#329, measured below); next: fill tracker-loss gaps / QD-flagged segments, stage-4 draft later |
| **AI script** | writes a draft script (structure: strokes, pauses, depth) from our own model | supplies the precise timing; QD checks; the user keeps or rejects per segment (Keep) | stage 4 |

### Hybrid step 1 — the engine verifies the teacher (28 Sep)

Which weakness signal to use was measured first, against Claude's hand
labels (is the engine's chosen cell on the labelled contact?):

- The chosen cell's score relative to the window's strongest cell flags
  almost every wrong window, but also 65 of 118 right windows on
  `clip_voll` (the engine is right to stay off the thighs). Too blunt on
  its own.
- `|r|` against the tracker flags too few wrong windows.

What works is to let the AI say where and the engine confirm it:
`ContactVerifyK` keeps a contact point only where the strongest cell
around it scores at least K times the cell the engine chose on its own
(first grid pass without points). K 1.2–2 is a plateau; 1.5 is used.

Real engine (`TrackROI`, OpenCV build), windowed r against the FunGen refs:

| Clip | Points | without check | `--contact-verify 1.5` |
|---|---|---|---|
| multi-person | none | 0.304 | – |
| multi-person | NudeNet (stage 1) | 0.406 | **0.414** (423 of 596 kept) |
| multi-person | scene-roles moving part, every window | 0.449 | **0.450** (547 of 705 kept) |
| clip_voll | scene-roles moving part, every window | 0.418 / **0.703** (regression) | **0.455 / 0.752** (= baseline) |
| clip_voll | NudeNet | – | 0.455 / 0.752 (= baseline) |
| clip_ausschnitt | NudeNet / scene-roles | – | 0.486 / 0.887 (= baseline) |

The offline harness gave the same numbers and kept the same points.

### Second multi-person clip (re-measured 10 Oct, main @ `a7c6e1d`)

Owner upload: 1573 s couple clip (man + woman, POV, many shots and camera
moves, 256×144, AV1 → transcoded to H.264 because OpenCV's Python build
cannot decode AV1 and NudeNet then sees 0 frames). The first frame is
black, so the run starts at 120 s (ROI on the mouth, `68,53,64,64`); the
FunGen 2.6.3 reference is shifted by −120 s. Real engine (`TrackROI`,
OpenCV build), `--rhythm-grid`, scored with
`fungen_compare_windowed.py` (30 s windows, mean |r| of confident
windows). The 642 s clip was re-run on the same tip with the same scorer
as a cross-check.

| Clip | Points | r |
|---|---|---|
| couple 1573 s (41 windows) | none | 0.328 |
| couple | NudeNet (stage 1), 486 points | 0.320 |
| couple | NudeNet + `--contact-verify 1.5` | 0.323 |
| couple | **scene-roles moving part + verify 1.5**, 390 points | **0.349** |
| multi-person 642 s (22 windows) | none | 0.293 |
| multi-person | NudeNet (stage 1) | 0.396 |
| multi-person | NudeNet + verify 1.5 | 0.404 |
| multi-person | **scene-roles moving part + verify 1.5** | **0.445** |

- The 642 s numbers reproduce the 28 Sep table on today's engine (same
  ranking, within ±0.01 of the older scorer).
- On the couple clip NudeNet's stage-1 points do **not** help (−0.008;
  the verify check limits the loss to −0.005). There is only one partner
  and the ROI is already near the contact much of the time; 418 of the
  486 teacher points come from the `cleavage` rule (68 from `penis`), so
  outside titjob shots they likely pull the search to the wrong body part
  (not verified per window).
- The scene-roles moving part with the engine check is the only variant
  that helps on **both** clips (+0.021 and +0.152). It is the
  recommended opt-in chain; NudeNet stage-1 points alone stay a
  multi-person tool.
- Still two clips with multi-person content; the ≥ 4–5 clip gate for any
  default stands.

**Recommended opt-in chain** (Owner PC, until more clips are measured):

```
SamNPlayer-cli scan-scene-map clip.mp4 --windows 64 --out clip.scan.json
python3 generator/scene_roles.py --video clip.mp4 --scan clip.scan.json --nudenet \
    --min-confidence 0 --contact-out clip.scene.contact.json
SamNPlayer-cli generate --video clip.mp4 --roi x,y,w,h --rhythm-grid \
    --contact-points clip.scene.contact.json --contact-verify 1.5
```

No default changes. Defaults are discussed only after ≥ 4–5 more clips
(Owner). GUI: **"Verify with the engine (hybrid, K=1.5)"** next to Use contact
points — shipped (opt-in, default off; same as CLI `--contact-verify 1.5`).

### Hybrid step 2 — fill where the tracker lost the target (proposal, 29 Sep)

Finding on main: `tracking_gaps` are built from `LostFlags`, and only the
two-point / multi-point trackers set them. On the Everyday single-ROI path
nothing records a gap, so *Heal tracking gaps* (#262) never fires there.
Where it does fire it bridges the gap with a straight line, so the strokes
inside the gap are lost.

**Measured before asking for code (29 Sep):**

1. *Does CSRT report loss on the single-ROI path?* No. `TrackerLostFrames`
   is 0 on all three clips — 6723 frames `clip_voll`, 1199
   `clip_ausschnitt`, 19 263 on the multi-person clip, where the box
   demonstrably sits on the wrong person. The single-ROI failure is
   *wrong place*, not *lost*, and that is what the contact check
   (`--contact-verify`) addresses. Recording CSRT-loss gaps there would
   never fire, so that part of the proposal is dropped.
2. *Rhythm bridge vs straight line* (synthetic ground truth: 4–10 s spans
   cut out of the FunGen reference scripts every 30 s, healed, compared
   with the uncut original inside the span; turning points found first,
   so dense and sparse scripts read the same):

   | Script | Span | r line | r rhythm | MAE line | MAE rhythm |
   |---|---|---|---|---|---|
   | clip_voll ref (extrema) | 4 s | −0.11 | **0.43** | 20.6 | **8.6** |
   | clip_voll ref (dense) | 4 s | 0.08 | **0.35** | 30.3 | **21.4** |
   | clip_ausschnitt ref (extrema) | 6 s | 0.01 | **0.93** | 32.2 | **5.1** |
   | clip_voll ref (extrema) | 10 s | −0.02 | **0.31** | 21.3 | **10.8** |
   | multi-person ref (erratic) | 6 s | 0.04 | 0.08 | 21.2 | 20.9 |

   The rhythm bridge wins wherever the stroke is regular. On long spans
   the phase drifts, but it still beats the line. On an erratic
   reference both are about equally poor.

**Revised proposal (lane GapFill, still ask-first):**

- **Rhythm bridge as an opt-in heal mode** for the gaps that exist:
  two-point / multi-point `tracking_gaps`, plus a **user-selected span**
  in Improve ("repair this span"). The user marks the bad part, and the
  bridge continues the rhythm from both sides. This is the realistic
  gap-filler on the single-ROI path, since the engine does not know it
  lost the stroke.
- Tempo and amplitude come from the turning points within 8 s on both
  sides. The count is fitted so that the alternation lands on the
  far-side point.
- No default change. The GUI entry (an Improve option) is Cursor's.
- **Built (29 Sep, library):** `funscript.RhythmBridgeSpans`,
  `ImproveOpts.HealRhythm` (rhythm bridge for *Heal tracking gaps*) and
  `ImproveOpts.RepairSpans` (user spans), with a line fallback for spans
  without rhythm around them. The library gives the same numbers as the
  prototype.

**ROI2 — Owner decision 28 Sep: opt-in setting "Apply AI setup
automatically".** The old locked rule "no silent ROI2" is now: no ROI2
unless the user switched this setting on. The reason for the old rule still
holds — a wrong contact partner makes the contact vibration wrong — so:

- **Default off.** Off = today: the partner is a proposal the user applies.
- **Everything applied is shown** (log line per value; the GUI shows what
  was set and lets the user undo it).
- **User values always win:** only an empty ROI / ROI2 / class is filled.
- **Everyday profiles only.** There ROI2 is stored as the contact mark
  (contact vibration target, tracked) and the tip CSRT still writes the
  stroke. With a Tf/Tj distance profile ROI2 would switch the curve source
  to two-point tracking, so there the partner stays a proposal ("the AI
  never writes the curve").
- Implemented as Go `ApplySceneProposal` + CLI `--scene-apply`; the GUI
  setting is Cursor's.

### Video, not just images

- **The engine is already video.** Optical flow and the rhythm grid see
  motion over time. Stage 2 uses exactly that for "what moves", so no big
  model is needed for it.
- **VLM clip mode (stage 2b, built):** `vlm_probe.py --clip N
  --clip-span-s S` sends N frames spanning S seconds (ending at the
  keyframe, oldest first) as one multi-image request. Qwen2.5-VL and
  Qwen3-VL are trained on video input.
  - It asks for the scene type, the moving part, its partner, the motion
    axis and the number of people, plus boxes for the last frame.
  - `scene_roles.py --vlm clip.vlm.json --truth
    generator/testdata/vlm_labels/multi_person_642s.scene_types.json`
    records the model's reading per window (`vlm`), counts agreement with
    the rules (`summary.vlm`) and scores both against Claude's hand
    labels (`summary.truth`; the rules alone score 15/21).
  - It does **not** change roles yet. Once a run on the Owner PC shows
    the VLM beats the rules on scene type, it becomes the tie-breaker
    (for example titjob vs blowjob when the face bobs).
  - Owner PC run: `python3 vlm_probe.py --video db.mp4 --model
    qwen2.5vl:7b --clip 6 --every-s 20`, then send `db.vlm.json` back.
- **Real action-recognition models** (a VideoMAE-style video classifier)
  come later. They need our own labelled shots first, which stages 2–3
  collect (confirmed scene types per shot).

## 4. The stable system without big AI (next, as the Owner asked)

At run time, only two small, fast parts that run everywhere:
1. **Our own RF-DETR multi-class detector** (ONNX via onnxruntime on CPU,
   CUDA or DirectML): what is what, per frame.
2. **The classical engine** (CSRT + rhythm grid + two-point): what moves,
   and how.

Stage 2's rules join the two: roles, scene type and proposals. Nothing at
run time needs Qwen, Colibri or NudeNet. Those remain optional
**teachers**; they produce training labels for (1) through the loop in
`docs/VLM_MODELS.md`.

Order of work:
1. Stage 2 with any teacher. NudeNet first, because it runs here and is
   measurable now.
2. Train (1) with the classes `penis`, `glans`, `mouth`, `hand`,
   `breasts`, `vagina`, `face`, `thigh`, `contact`.
3. Switch the part source to (1).
4. Measure on ≥ 4–5 clips.
5. Only then talk about defaults.

## 5. Measurement (every stage)

- **Roles:** on the labelled clips, is the proposed primary box the
  labelled contact region? This is the contact hit from `vlm_score.py`,
  per role.
- **Scene type:** accuracy against a hand label per shot. Claude labels
  the three clips we have.
- **End to end:**
  - Generate with the proposals applied, compared with today's default
    ROI.
  - On the multi-person clip, today's centre ROI sits on the heads, so
    the gain should show there.
  - The goldens must not regress.

## 6. Ownership

| Who | Does |
|---|---|
| Claude | `scan-scene-map` CLI, `scene_roles.py`, `LoadSceneProposals`, CLI opt-in, VLM clip mode, all measurements, stage 3 rules |
| Cursor | GUI: show the proposals in pick-primary / ROI2 (user applies), profile suggestion chip, candidate accept/reject (from V3), Settings AI setup buttons |
| Owner | GPU runs (VLM clip mode, RF-DETR training), 4–5 clips, confirming candidates |
