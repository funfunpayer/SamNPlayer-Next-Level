# Everyday Create — Emotion Script (clip → done)

Product target: pick a clip; barely choose anything else. Output is one
**Emotion Script** (`.samn`). Funscript is share/import only — see `docs/EMOTION_SCRIPT.md`.

Related: `TFTJ_PROFILE_DIRECTION.md`, `GENERATE_HEURISTICS.md`,
`AUDIO_WORKFLOW.md`, `NEXT.md` § Stroke preview / clip_ausschnitt matrix.

---

## Measured first choice (`clip_ausschnitt`, windowed FunGen r)

| Rank | Method | Windowed mean r vs ohne_yolo |
|-----:|--------|-----------------------------:|
| **1** | **CSRT hub / tip (Go)** | **0.59 / 0.47** |
| 2 | region_fusion tip | 0.56 |
| 3 | grid_lk tip | 0.49 |
| … | 4-zone `region_fusion_auto` | **0.36** — opt-in only |
| … | Flow | slow + weaker — not everyday |

**Rule:** Everyday = **auto-find tip region → CSRT → Stroke + Contact vib**.
Do **not** default 4-zone or Flow.

---

## User-visible path

```text
  1. Choose video
  2. Auto: find tip region (classic motion, or AI if checkbox on)
       — user may correct the box, or **Show motion candidates**:
         click = Tip; optional Shift-click = contact area (only if Contact vib on)
  3. Contact vibration on (default) · Step 2 then shows optional class + contact marks
       — Tip class (Glans/Penis soft-default) · Contact type (Nipples) — neither required
       — Mark contact area / + Another (e.g. both nipples)
       — Marks optional; Stroke vib still follows depth until feel-decouple
  4. Generate → .samn + .funscript
  5. Review & improve: trim start/end · fill gaps · audio check on/off
       — then **Play**: soft curve + keyframe dots (FunGen-like); Edit curve to adjust
       — **0–100 on video** gauge over Generator preview / Play (toggleable)
```

**Everyday needs (CSRT):** tip box (auto or mark) + Contact vib on/off. Contact marks,
soft masks, Tf/Tj distance are optional. **4-zone is CLI-only** — not in the Generate GUI
(weaker on measured clips; backend kept for evidence experiments).
Contact marks (when vib on) are **stored** in `metadata.contact_marks` for feel /
later feel-decouple — they do **not** change the tip-CSRT stroke curve today.
**Play** shows them under the video (**Show contact marks**, default on when present).
**Feel Stage A:** when the script also has a recorded tip trajectory and
contact areas, Play vibration is `max(depth-slice, tip-near-mark)` — tip grazing a
nipple mark can buzz even on a shallow stroke. Generate soft-enables
**Record tip path** whenever Contact vib is on (Advanced; user can turn off).

Advanced (collapsed): knobs that **change CSRT output** (invert, cam-comp,
scene-cut, dynamics, retry, **Rhythm-robust signal**). Rare posttrack numbers
(axis, smooth, peak spacing, RDP, max speed, Re-find) live under
**Expert tuning** (nested, closed). Tracking method UI is CSRT-only (select
kept in DOM for payload; research backends = CLI).
Impulse contact vib: primary control is Feel → Curve → Impulse; Advanced
Expert keeps a synced mirror checkbox. **AI draft** is nested closed under
Advanced — never Everyday; Hybrid = assist/verify on the Go CSRT spine only.
- **Invert motion direction** — flips the curve (100−pos). FunGen polarity mismatch, not a tracker bug.
- **Rhythm-robust signal** — opt-in; stroke from the most rhythmic flow cell near the tip box (anti-drift on long clips). Off by default; Everyday unchanged.
- **Fix contact area (static)** (Step 2, when Contact vib on) — keep the gold contact box fixed; off = track it (better with camera motion).
Removed from Everyday Advanced: AI second opinion (no curve effect), audio
check row (moved to Review), Flow downscale (Flow is CLI-only), **4-zone stroke mode**. Scene memory
collapsed under Power-user. Imported scripts → Play **Optimize for Neo 2**
(`docs/CONTENT_SOURCES.md`).

AI checkbox = **region proposal only** (ONNX). Never writes the 0–100 curve
(`AI_ADAPTER.md`).

---

## Classical heuristics package (G1 — what Create already does)

Order on the Go path (no AI writing positions):

```text
  ROI (manual / auto-find) → CSRT tip → posttrack (smooth / peaks / RDP / speed)
    → Quality Doctor (Signal Quality) → audio tempo check (warn only) → write .samn
```

- **Audio** never invents the curve. If Signal Quality fails *and* audio Hz is
  clear, you get: *“Audio suggests ~X Hz — check ROI / axis”*.
- **Rhythm-robust signal** stays Advanced opt-in until Owner gates more clips.
- **Speed cap** on Normal/Soft stays off unless you set it (Autotune may set 400).
  Product rule for a Neo-2 default still needs an Owner decision — not silent.

Full knob map: `docs/GENERATE_HEURISTICS.md`.

---

## What stays automatic

| Piece | Behavior |
|-------|----------|
| Backend | CSRT (Go when OpenCV linked) |
| Profile | Stroke (`standard`) |
| Contact vib | On by default |
| Stroke preview | Stage A pre-pass (timing/flags; audio if weak) |
| Audio check | On when ffmpeg present (generate + Review toggle) |
| Fill gaps | **Auto after Generate** (auto + median-aware second pass) + Review step — linear bridge; optional audio-tempo spacing |
| Heal tracking gaps | **Auto after Generate** + Review — strip junk inside `metadata.tracking_gaps`, linear bridge those windows only, then clear the windows so Contact vib is not muted forever. Not a CSRT re-track |
| Trim start/end | Review step (start also via seek before Generate) |
| Flow downscale | Hidden unless Flow (CLI) |
| Tf/Tj distance | CLI / Advanced only — not everyday GUI |
| License | Gates wired (`MaxOutputMs` trial / `.samn` Play); Enforcement still **off** |

---

## Acceptance

1. New user: video → Generate without painting a box (auto-ROI ran).
2. Result path is CSRT Stroke (1-zone). 4-zone is CLI-only — not offered in Create GUI.
3. AI off by default; on only proposes ROI.
4. Clip gate: everyday path ≤ few % behind measured tip/hub CSRT on goldens.
5. After Generate: gaps filled once (Go); Play shows dots; Edit is opt-in.
6. Stale auto-ROI results from a previous video are ignored.

---

## Remaining Python (Everyday)

| Step | Still Python? | Notes |
|------|---------------|-------|
| Auto-find tip | Yes (`auto_roi.py`) | Highest-value Go port next |
| CSRT track + posttrack + QD + audio | **Go** when OpenCV linked | Windows → Python CSRT until #120 |
| Improve trim/fill | **Go** (`funscript.ImproveScript`) | |
| AI region | ONNX (`ai_roi.py`) | Opt-in only |
