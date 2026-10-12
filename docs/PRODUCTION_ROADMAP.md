# Production roadmap (rock-solid)

Living plan for what ships next, what **you** (owner) must test, and how
results turn into fixes. Product UI/docs stay **English** (simple English
where possible). We talk in German; the app does not.

Related: `docs/ROADMAP.md` (engineering checklist), `docs/ENGINE.md`
(chain + DoD), `docs/TFTJ_PROFILE_DIRECTION.md` (Tf/Tj = profile;
no-mark 4-zone; contact vib → Normal/Auto), `docs/AGENT_COORD.md`
(Cursor ↔ Claude task board), `docs/AUDIO_WORKFLOW.md`,
`docs/FUNSCRIPT_ALGOS.md`, `docs/SELF_BUILD.md`, `docs/LICENSE_SYSTEM.md`,
`docs/PLATFORMS.md`, `docs/COMPETITIVE.md`, `docs/LANGUAGE.md`.

---

## Rule: one theme at a time (dependencies first)

We spread too thin when Generate, GUI polish, AI Train, website, license
sharpness, and device feel all move “in parallel.” **From here, ship in
order.** A later phase may start only when the previous gate passes —
or when it is explicitly marked *orthogonal* (does not touch Generate
quality).

```text
  G0 Stabilize Generate path
       ↓
  G1 Classical heuristics + audio workflow  (NO AI writing scripts)
       ↓
  G2 Raw-value / Neo-2 mapping layer
       ↓
  G3 AI only as helper on top of a strong classical baseline
       ↓
  G4 License go-live / platforms / mobile / site   (when G0–G2 hold)
```

**License (10 Oct 2026):** freeware + EULA + donations, key system removed — see `docs/LICENSE_SYSTEM.md`. Older note: stay in **developer mode** (`Enforcement = false`) through
G0–G2. Import/verify Settings stay; do not flip sharpness until Generate
+ device mapping feel stable.

**AI rule (unchanged):** classical tracking writes the funscript; AI may
propose ROI / second opinion only (`docs/AI_ADAPTER.md`). We first make
classical Generate excellent — that is the training signal AI needs later.

---

## How you test → how we improve

```text
  You run a check (tables below)
       ↓
  Note: clip name, settings, what you saw, pass/fail
       ↓
  Put results in: GitHub issue OR message on this agent with
      - clip id / path (not the private video itself if licensed)
      - expected vs actual
      - screenshot of English UI if layout issue
       ↓
  We: reproduce → fix on a branch → clip/CI gate → merge → next tag
```

**Where results go:** prefer a GitHub issue titled `test: <area> — <clip>`
so history stays searchable. Attach screenshots (English UI). Do not commit
private golden videos into the repo (`docs/GOLDEN_CLIPS.md`).

**What we then build:** only changes that keep or improve **script quality**
(Quality Doctor + your usable/borderline/unusable feedback). Layout/docs
fixes are fine if they do not dilute Generate/Play.

---

## Release train (now)

| Version | Status | Contents (short) |
|---------|--------|------------------|
| **v0.5.10** | Shipped | Bugfix + GUI motion + multi body-part + soft masks |
| **v0.5.11** | Shipped | #119 AI Train opencv; progress UI; #120 one CSRT path |
| **v0.5.12** | Shipped | Sequential Generate GUI; G0–G4 spine docs |
| **v0.5.13** | Shipped | Windows OpenCV CSRT + portable DLLs (G0.1) |
| **v0.5.14** | Shipped | Tf/Tj tip-end + Zone 3+ class + Go TrackMultiPoints |
| **v0.5.15** | Shipped | Tf/Tj fusion: exclude lost partners from min() (F-006) |
| **v0.5.16** | Shipped | OpenCL auto (no GUI toggle forcing Python) + MSYS2 CI |
| **v0.5.17** | Shipped | Contact vib on Normal/Autotune; AGENT_COORD; goldens + phase --window-ms |
| **v0.5.18** | Shipped | #145 Autotune tip-only; TFTJ step 3; F-003 AliasingRisk flag |
| **v0.5.19** | Shipped | TFTJ 4b motion-candidate overlay (pick primary) |
| **v0.5.20** | Shipped | 4-zone GUI opt-in + Contact-first (#169); owner Flow smoke |
| **v0.5.21** | Shipped | Profile rename; Play feel-decouple; Flow soft warns (#172) |
| **v0.5.22** | Shipped | Training clip+ring; Everyday Generate; Playback HiDPI/seek; bugfix P0/P1 (#177/#179/#176); tag #181 |
| **v0.5.23** | Shipped | Training portable clip frames (#187); MT-Go/Seed/Debug/Infra; QC fixes |
| **v0.5.24** | Shipped | Improve sidecar (#200); Training presets + Re-find Zone2 (#199); Pose Stage A research (#201) |
| **v0.5.25** | Shipped | Contact marks UI (#208); Training improve (#206); 4-zone Everyday button hidden |
| **v0.5.26** | Shipped | Contact marks metadata + Play overlay (#211/#212) |
| **v0.5.27** | Shipped | Feel Stage A + tip-path soft-on + Training mosaic/suction (#214–#217) |
| **v0.5.28** | Shipped | Emotion look (#225); Create→Play `.samn` feel (#227); CSRT jump/memory guards (#226) |
| **v0.5.29** | Shipped | Stroke detrend on by default (2× stroke period) removes tracker-drift baseline shifts (#230) |
| **v0.5.30** | Shipped | Rhythm grid opt-in (#233) + Advanced GUI toggle (#236); orientation coin-toss fix |
| **v0.5.31** | Shipped | Profilemodel #244 + semantic target lock #248 + SceneMap P1/P2 #246/#247 + BF-3 Stage B #239 |
| **v0.5.32** | Bookkeeping | Rel32 CHANGELOG/VERSION (#271); **never tagged** — portable deferred to 0.5.33 after post-Rel32 features |
| **v0.5.33** | Shipped | Rel32 stack + Plugin `virtual_person` H0/H1 (#273/#274) + Generator Follow/Ignore marks S0/S1 (#276/#277) — tag `v0.5.33` |
| **v0.5.34** | Shipped | Post-Rel33 stack: vib impulse/spatial (#279/#283), AIWrite S2/S2b/S3prev (#280/#282/#288), Intiface live+reconnect (#281/#286), Marks S2/S3 (#284/#285), IdLock release (#287) — tag `v0.5.34` |
| **v0.5.35** | Shipped | Rel35 test cut: Scene2+GUI (#329/#334), Apply AI (#336), AutoReview (#328), impulse (#337), Go CSRT force (#339), VP product scrub (#341). Park #264. At ship: #290 P5 audit leftovers (later closed — P5c #297 on tip) — tag `v0.5.35` |
| **v0.5.36** | Shipped | Patch: AI Train review YOLO box overlays on absolute dataset paths (#342) — tag `v0.5.36` |
| **v0.5.37** | Shipped | Patch: large AI Train review editor (#345); Windows 11 AI setup scripts (#344) — tag `v0.5.37` |
| **v0.5.38** | Shipped | Sammel: Repair Heal+Fill (#348); Update `//` popup (#351); Advanced cleanup (#347); Benchmark MVP (#350); Patch channel (#349) — tag `v0.5.38` |
| **v0.5.39** | Shipped | GUI Anleitung alignment (#357) + Improve heal label (#356); Everyday Go CSRT unchanged — tag `v0.5.39` |
| **v0.5.40** | Shipped | Contact Verify GUI (#359) + Bench Suggest beside (#360); Everyday Go CSRT unchanged — tag `v0.5.40` |
| **v0.5.41** | Shipped | Sammel post-0.5.40: Tip-Find (#364), Clip-Prep cutter (#365), reacquire (#366), Bugfix (#372), E2E (#371), cleanup (#369), docs/tests (#362/#363/#367); #264 VP cancelled; Everyday Go CSRT unchanged — tag `v0.5.41` |
| **v0.5.42** | Shipped | Sammel post-0.5.41: GapFill (#403/#413); Play/Create knobs+probes (#378–#400); Plateau from floor (#426); Expert/EO/Contact 0 GUI (#425/#421/#424); seek/clock + CLI (#377/#401–#417); Intiface/training (#422); Audio cancel (#420); atomic funscript (#428); #264 cancelled; Everyday Go CSRT unchanged — tag `v0.5.42` |
| **v0.5.43** | Shipped | Sammel post-0.5.42: Train window all labels (#441); ROI empty-val heal (#442); Play/Create/AI Train prefs (#437–#439); CLI generate/stroke-preview cancel (#432/#435); #264 cancelled; Everyday Go CSRT unchanged — tag `v0.5.43` |
| **v0.5.44** | Shipped | Sammel post-0.5.43: video swap (#444); Contact-vibe tip+partner (#445); Claude OFS load/companion (#446); scene roles (#448–#450); AI Train free tags/track (#451); OFS clock/inverted (#452–#453); never #447; #264 cancelled; Everyday Go CSRT unchanged — tag `v0.5.44` |
| **v0.5.45** | Shipped | PlayHeatUX / G4.C (#460): taller Play heatmap, Tip→Partner snack, declutter; bug-hunt fixes #455–#459 — tag `v0.5.45` |
| **v0.5.46** | Shipped | Sammel post-0.5.45: audio-only vibration switch (#462), stroke-preview rate fix (#462), contact points 2nd clip (#463), Owner UX asks Epochs cap / BPM from audio / write offset into script (#464); RF-DETR licence confirmed; Everyday Go CSRT unchanged — tag `v0.5.46` |
| **v0.6.0** | Release prep | V1 workspace Player · Training · Tools (#494–#498), Play/Pause, undo/redo, knobs, Reduce peaks, Auto sync, Sound Curve, Invert Curve, skip ±10 s, sleep timer (#487–#504), device capabilities D1 (#499); **first release with the Suite edition** (`SamNPlayer-suite-…`); updater matches its own edition — tag `v0.6.0` (Owner pushes) |
| next | After v0.5.44 | Owner portable smoke on `v0.5.44` (OFS load/inverted, video swap, Contact-vibe, AI Train free tags, Generate→Play→Neo 2). Still Owner: ≥4–5 rhythm clips (rhythm default gate); speed-cap; G1.3. Cursor: PlayHeatUX + G4.C declutter + bugfix (taller heatmap, Tip→Partner snack, Invert→companion, video sync idle, Device raw/diagnostics closed) |
| later | After G1 gate | G2 raw-value Neo-2 layer → then G3 AI → G4 license/platforms |

### Owner after each tag

1. Confirm CI green on `main`
2. Spot-check Generate (CSRT path only — Go or Python)
3. Play: load script, seek, heat map
4. Hardware spot-check when device available
5. Public site sync **when you choose** (not a Generate blocker)

### Owner pre-release checklist (before tagging)

| Check | Pass? |
|-------|-------|
| Startup: no update/error popup; Log quiet if already on latest | |
| Play: load `.samn` / video, seek, heat map | |
| Generate: mark ROI → generate → script opens in Play | |
| Tf/Tj: tip + Fix ROI2 / +Target / +Mask if you use them | |
| Settings → Check folders: ffmpeg found (portable) | |
| AI Train **skipped** for G0/G1 gates (or samples only — do not block tag) | |
| Portable zip from previous tag still runs on a clean Windows folder | |

Agent prepares PR + version bump; **tagging on `main` is the release
switch** (`CONTRIBUTING.md`).

---

## Sequential phases (Generate spine)

### G0 — Stabilize Generate (current)

**Goal:** one strong, boring, reliable CSRT path on every desktop we ship.
No new trackers, no AI default changes, no license sharpness.

| ID | Item | Gate | Your test |
|----|------|------|-----------|
| G0.1 | Windows OpenCV CSRT in release binary (`trackcv` linked + DLLs in portable zip) | Go CSRT on Win without Python | Generate on 1 short clip; no Python required |
| G0.2 | Clip / cancel / Quality Doctor smoke on Go CSRT | CI + local clip | Cancel mid-run recovers; score present |
| G0.3 | Python CSRT remains only as emergency until G0.1 | Hard error if CSRT missing | Clear English error, no NCC |
| G0.4 | `PreferSimpletrack` stays lab/CLI only | Code + GUI | No “weaker” path in product UI |
| G0.5 | Release hygiene | F1–F4 every tag | clip7776 + portable zip |

**Exit G0 when:** Windows portable Generate uses Go CSRT; Linux already does;
cancel + clip7776 green; you rate a few clips usable/borderline without
chasing AI Train.

**Docs:** `docs/ENGINE.md`, issue #120.

---

### G1 — Classical heuristics + audio (no AI script writer)

**Goal:** think the generate workflow through once, then ship a **classical
heuristics package** that improves Motion Fidelity *and* Signal Quality
without any AI writing positions. AI Train stays optional / research.

**Product workflow (target — classical only):**

```text
  1. ROI (manual; VerifyROI warn-only if box looks weak)
  2. Track CSRT (Go) → observations
  3. Posttrack heuristics (smooth / peaks / RDP / speed / axis / autotune profile)
  4. Quality Doctor (Signal Quality — label it)
  5. Audio tempo check post-hoc (when ffmpeg present) — warn only
  6. Write .funscript + .samn
```

Detail: `docs/AUDIO_WORKFLOW.md`, `docs/FUNSCRIPT_ALGOS.md`,
`docs/SIGNAL_VS_FIDELITY.md`.

| ID | Item | Prerequisites | Your test |
|----|------|---------------|-----------|
| G1.1 | **Heuristics package v1** — document + implement as one coherent set: axis choice, peak distance, smooth window, RDP, speed cap, weak-ROI hint, tracking-gap messaging | G0 exit | Same ROI → better or equal usable rate vs 0.5.11 |
| G1.2 | Audio post-check always when ffmpeg available; if Quality Doctor fails *and* audio Hz clear → English hint (“check ROI / axis”), **never** invent positions from loudness | G1.1 sketch | **Shipped** (warn only; Go + Python) |
| G1.3 | Optional audio **pre-pass tempo hint** (bias peak distance only) — only if golden numbers help | G1.2 + A1 goldens | Before/after on 2–3 clips |
| G1.4 | Golden-clip manifest populated (your clips + FunGen refs) | Your material | Bench tab report pasted to issue |
| G1.5 | FunGen / phase bake-off on fixed ROI (Motion Fidelity), not Auto-ROI | G1.4 | Correlation + lag; no invented ROI2 |
| G1.6 | Tf/Tj stabilize (missing data, normalize, turning points) **classical** | G1.4 | Two-ROI manual marks; usable suction feel |
| G1.7 | Go `auto_roi` only if equal to Python on 3 clips | G0.1 + G1.4 | Same box as Python |

**Exit G1 when:** classical Generate on goldens is measurable (Signal Quality
+ Motion Fidelity separate); heuristics are one package (not scatter);
audio is a check, not a writer; you trust scripts enough to judge device
mapping next.

**Explicitly not in G1:** wiring AI two-ROI as default; new trackers in GUI
dropdown; depth/pose defaults; website redesign.

---

### G2 — Raw-value readout → good Neo 2 scripts

**Goal:** after Generate is stable, build the layer that turns a clean
motion curve into **device-correct raw values** for Sam Neo 2 (vibration +
suction/constrict), instead of treating funscript 0–100 as mechanical
position.

Chain (do not skip):  
`Video PTS → Perception → Motion state → SAM Intent → Device raw`

| ID | Item | Prerequisites | Your test |
|----|------|---------------|-----------|
| G2.1 | Device diagnostics on **real** Neo 2 (already coded; needs hardware) | Device + you | **Owner smoked OK 24 Sep** — Connect / Play feel good; further raw-profile (G2.2) parked until Owner opens |
| G2.2 | Per-channel raw-value profile (resolution, accept range, interaction) | G2.1 | Note feel vs log; issue `test: neo2-raw` |
| G2.3 | Intent → raw mapping for Tf/Tj + Hub (suction floor, contact vib, Extended-O) measured, not guessed | G2.2 + G1 exit | Same script: before/after feel notes |
| G2.4 | Keep Signal Quality ≠ Motion Fidelity in reports when mapping changes | G2.3 | Labels still correct in GUI |
| G2.5 | Optional look-ahead / latency compensation | Only after measured E2E lag | Never hide generator error |

**Exit G2 when:** one documented Neo 2 profile from real diagnostics, and
Generate→Play→device feels consistent on your reference clips.

**Docs:** `docs/SAM_NEO_2_RESEARCH.md`, `docs/ENGINE.md`, device diagnostics.

---

### G3 — AI themes (only on a strong classical baseline)

**Goal:** AI improves **ROI / labeling / second opinion**, never replaces
CSRT as the script writer. Train on clips where classical Generate already
passes your usable bar — otherwise the model learns noise.

| ID | Item | Prerequisites | Your test |
|----|------|---------------|-----------|
| G3.1 | AI Train only after G1 goldens + usable classical scripts | G1 exit | Collect samples from good runs — tooling ready; formal goldens still Owner |
| G3.2 | ROI proposal + VerifyROI; two-ROI suggest stays opt-in until bake-off win | G3.1 + G1.5 | **Helpers shipped** (VerifyROI; AI/semantic/MT-Seed suggest ≠ Apply). Two-ROI AI default still off |
| G3.3 | Quality model train from usable/borderline/unusable | Feedback JSONL | Scaffold exists; adopt only if CV beats fixed rules |
| G3.4 | Perception fuse / multi-observer | Golden win required | See `SAM_ARCHITECTURE.md` Perception v1 — **do not start** |
| G3.5 | Depth / pose / segmentation (non-track) | Own go/no-go each | PoseObserver Stage A #201; Stage B / depth default = Owner bake-off |
| G3.5a–e | **Multi-track Fahrplan** (Tip+Partner IDs / proposals) | See section below | MT-Go/Seed/Debug/Infra **DONE**; MT-ID gated on Owner clips |
| G3.6 | **AI script draft (opt-in)** — local model drafts stroke; QD + Keep | Owner opened 26 Sep; S0 plan | `docs/AI_SCRIPT_WRITER.md`; Everyday CSRT default |

### Multi-track Fahrplan (22 Sep) — fold into G1 polish + G3

Synthesizes owner research (YOLO26 + ByteTrack/BoT-SORT, speed knobs,
VSDC “movement map”, Unite.ai lib filter, MovieGo architecture notes) with
the existing spine. **Stroke writer stays Go CSRT tip.** Proposals ≠ script.

**Product stance (owner 22 Sep — aligns Everyday):**
- **Tip CSRT + Contact layer** is the product path. Do **not** treat “always
  track a Partner box” as the default multi-object goal.
- Zone 2 / Partner CSRT mainly pays off for **tip↔partner distance** (legacy
  Tf/Tj). On Stroke + Contact vib, Zone 2 is often preview-only (vib =
  stroke depth).
- Prefer **body-part zones/classes** (mouth, hand, …) as ranked **proposals**
  — not N stroke writers.
- Multi-track IDs = stable IDs for **parts/regions** when useful; generic
  Partner tracking only when distance/feel truly needs it.

```text
  Motion candidates / YOLO-ONNX / (later) PoseObserver  →  Tip + optional body-part proposals
           ↓
  Go CSRT tip (TrackMultiPoints when multi marks exist)  →  Stroke + Contact
           ↓
  (only if part IDs still break on goldens)  →  Python ByteTrack/BoT-SORT ID layer
```

| Step | ID | What | Depends on | Done when |
|------|-----|------|------------|-----------|
| **1** | **MT-Go** | Go coast + track-ID continuity + honest lost UI (`LostFlags` / `lostHeavy`) on `TrackMultiPoints` | Current `main` | **DONE** #183 (`94b2bae`) — Cursor |
| **2** | **MT-Seed** | Ranked N proposals → user picks **Tip** (+ optional Partner/body-part Zone 2+) → seed Go CSRT; suggest ≠ auto-commit | MT-Go + G3.2 | **DONE** #186 (`5715b6b`) |
| **3** | **MT-Speed** | Proposal path: small model, `imgsz`↓, detect every N; ByteTrack before BoT-SORT unless ReID needed — bias toward **part/class** proposals | MT-Seed | **DONE (write-up)** — [MT_SPEED_NOTES.md](MT_SPEED_NOTES.md); implementation/owner measurement pending |
| **4** | **MT-ID** | Optional Ultralytics track **only** if tip or named part IDs still die on `clip_ausschnitt` / `clip_voll` | MT-Seed fail gate | Gate = **Owner** clip verify |
| **5** | **MT-Debug** | Optional Review polyline of tip (+ other marks if present); pref off | MT-Go | **DONE** #188 + #189 (`b7f5d2d`, `5762629`) |
| **∥** | **MT-Infra** | ffmpeg ctx-kill + proxy single-owner (filtergraph/PTS already fine) | Orthogonal | **DONE** #190 (`7802a14`) |
| **∥ later** | **PoseObserver** | Pretrained RTMPose/MediaPipe as **proposal** layer (`POSE_OBSERVER.md` / #191/#192) | MT-Seed + E2 | **Stage A spike** (offline Python) — bake-off Owner/local; Stage B only after metrics |

**Libs we keep / adopt later vs ignore** (Unite.ai filter): keep OpenCV,
NumPy/SciPy, optional ultralytics+ONNX; Train-only PyTorch/torchvision;
ignore Diffusers/LLM/DeepSpeed/Pillow-first stacks for Generate.

**Explicitly out of this Fahrplan:** frame-diff as Tip finder; YOLO every
frame as Stroke; **default Partner CSRT for Everyday**; VSDC titles/captions/
censor product; adopting MovieGo as a dependency for tracking.

Detail table + Go upgrade mapping: `docs/AGENT_COORD.md` § Parked
multi-object. Checklist mirrors: `docs/ROADMAP.md` § Multi-track.

---

### G4 — License, platforms, GUI, site (orthogonal, after spine holds)

Do these **without** diluting Generate. License stays developer until you
flip go-live.

| ID | Item | When | Notes |
|----|------|------|-------|
| G4.E | License enforcement (E5) | After G2 feel OK + your OK | Replace pubkey; trial 1 min |
| G4.C | GUI improve (C5–C7) | Small slices anytime if Generate untouched | Play first; no card redesign |
| G4.B | macOS / mobile | Later | `PLATFORMS.md` |
| G4.S | Public site sync | Owner schedule | Not a Generate gate |

---

## Open workstreams (detail tables)

Same letters as before — **ordered by the G-phases above**, not “run all
in parallel.”

### A — Script quality (= G0 + G1 + parts of G3)

| ID | Item | Phase | Prerequisites | Your test |
|----|------|-------|---------------|-----------|
| A0 | One CSRT product path (shipped 0.5.11) | G0 done | — | Generate = CSRT |
| A0w | Windows in-binary OpenCV CSRT | **G0** | MinGW OpenCV + portable DLLs | Win Generate without Python |
| A1 | Golden-clip manifest + FunGen r | **G1** | Your clips + refs | Bench report |
| A2 | Tf/Tj + classical two-ROI bake-off | G1 | A1 | Manual ROI scores |
| A2b | Multi body-part regions | shipped / refine in G1 | Taxonomy | Mark classes; Fix ROI2 |
| A2c | Soft masks + N partners | shipped / refine in G1 | `--target`/`--mask` | Extra target / mask |
| A3 | Go `auto_roi` if = Python | after G0 + A1 | Clip gate | 3 clips same ROI |
| A4 | Contact-vibration feel | **G2** | Real Neo 2 | Subjective + log |
| A5 | Heuristics package v1 | **G1** | G0 | See G1.1 |
| A6 | Audio workflow polish | **G1** | ffmpeg | See G1.2–G1.3 |

### B — Desktop platforms & tools

| ID | Item | Phase | Your test |
|----|------|-------|-----------|
| B1 | Portable zip on real Win PC | every tag / G0 | Fresh PC, no system ffmpeg |
| B2 | Install video tools (Settings) | shipped | Missing ffmpeg → install |
| B3 | macOS GUI build | G4 later | Open `.app` |
| B4 | Linux WebKitGTK smoke | every tag | Play + Generate CSRT |

### C — Player / GUI improve

Polish that helps Play + Generate first. **Not parallel to G0/G1** if it
risks Generate regressions — otherwise small orthogonal PRs OK.

| ID | Item | Phase | Your test |
|----|------|-------|-----------|
| C1 | Language sweep (leftover DE) | ongoing | Screenshot any DE |
| C2 | Thanks / feedback English | done | usable / borderline / unusable |
| C3 | README vs public portal | G4.S | Brand + portable CTA |
| C5 | GUI improve pass | G4.C slices | Before/after screenshots — **#460:** Create Advanced nests; Play Contact tune / Project export; Settings Plugins+diagnostics; progressive disclosure throughout Create/Play |
| C6 | Settings License block polish | with C5; enforcement still off | **#460:** multi-line Import → status (state / until / enforcement / effective) |
| C4 | Curve zoom / BPM grid | after C5 | **Done** (BPM grid + curve/heatmap zoom) |
| C7 | Empty states / errors | with C5 | Each banner + CTA |

### D — Mobile player (no generator)

| ID | Item | Phase | Your test |
|----|------|-------|-----------|
| D1 | Scope freeze | done | Read `PLATFORMS.md` |
| D2 | Shared Go core API sketch | G4 later | Review package list |
| D3 | iOS/Android prototype | G4 later | Play `.samn` to device |

### E — License / distribution (developer mode through G2)

| ID | Item | Status | Notes |
|----|------|--------|-------|
| E1–E4 | Concept, tool, Settings, library | **done, not sharp** | Keep using for import tests |
| E5 | Enforcement (trial / `.samn` play) | **G4 — after G2** | Your explicit go-live |

### F — Stability & bugfix (every release)

| ID | Item | Target | Your test |
|----|------|--------|-----------|
| F1 | clip7776 playback suite | every tag | CI + `pb_clip7776_test.py` |
| F2 | Cancel generate mid-run | every tag | Cancel → UI recovers |
| F3 | Make playable (HEVC/mkv) | every tag | Banner → convert → play |
| F4 | Device connect test toggle | every tag | Mock play full script |

---

## What still needs **your** testing

**Now (G0 / G1):**

1. **Private golden clips** — Bench / FunGen numbers (A1) — unblocks almost everything  
2. **Windows portable** Generate without Python on **v0.5.13** (G0.1 gate)  
3. **Classical Generate** usable/borderline/unusable on 2–3 clips (no AI required)

**Then (G2):**

4. **Real Neo 2** — Device Diagnose + contact / Extended-O feel (A4, G2)

**Anytime orthogonal:**

5. **GUI improve notes** (C5) — screenshots of friction  
6. **License import** in Settings (dev keys) — do not expect limits yet  
7. **Any DE leftover** (C1)  
8. **Public site** vs README when you schedule sync  

---

## Dependency picture (why this order)

| If we skip… | What breaks |
|-------------|-------------|
| G0 (stable CSRT) | Heuristics/AI tune noise; Windows quality ≠ Linux |
| G1 goldens | Cannot tell FunGen gap from ROI luck (issue #8 lesson) |
| G1 classical heuristics | AI Train learns from weak scripts |
| G2 raw-value / Neo 2 | “Good funscript” ≠ good feel; mapping stays guesswork |
| Early E5 license sharp | Friction while Generate still moving |
| Early AI default ROI2 | Near-zero correlation traps (already measured) |

**Orthogonal OK anytime:** F-stream bugfix, C1 language, docs, license
*import* UX (not enforcement), site sync when you want.

---

## Split rule (quality first)

Work may be split across branches/PRs **within** the active G-phase.  
**Do not** open a new G-phase theme that changes Generate defaults until
the current phase’s exit gate is met.  
Self-build only when equal or better (`docs/SELF_BUILD.md`).

## Language lock

| Surface | Language |
|---------|----------|
| GUI, help, status, screenshots | English (simple) |
| README / docs / CHANGELOG / portal | English |
| Code comments (new) | English preferred |
| Chat with owner | German |

---

## Not in this train (parked)

- macOS notarized build  
- Mobile player binary  
- License enforcement (E5) until after G2 + your OK  
- WebGL video sharpen / soft upscale  
- Full pure-Go H.264  
- Go ports of flow/`grid_lk` without golden win  
- ~~AI as funscript writer~~ — **opened 26 Sep** as **opt-in local draft** (`docs/AI_SCRIPT_WRITER.md`); Everyday CSRT stays default; cloud LLM curve writer still parked  
- “audio invents positions” (still parked)  
- Wails-v3 / frameless / card redesign (rejected — see `ROADMAP.md`)  
- Website as a Generate blocker  
