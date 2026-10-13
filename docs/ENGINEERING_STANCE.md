# Engineering stance (Bau / Planung lock — Rel31 / 24 Sep 2026)

Cursor coordinating doc. Product UI stays English; this file is the
**shared technical stance** so agents do not re-litigate stack choices or
steal each other’s lanes.

Related: `AGENT_COORD.md` (who owns what), `PRODUCTION_ROADMAP.md` (G0–G4),
`SCENE_MAP_PLAN.md` (Claude plan #243 — lands with that PR),
`AI_ADAPTER.md` / `KI_TRAINING.md` (AI suggest-only),
`GENERATE_HEURISTICS.md` (G1 knob map).

---

## Product locks (do not confuse)

| Topic | Lock |
|-------|------|
| **Everyday Generate** | Stroke tip-CSRT (1-Zone) + Contact vib. No 4-Zone in Generate GUI. |
| **Detrend** | On by default for stroke profiles (#230). **Not** the same as Speed Cap. |
| **Speed Cap** | Autotune forces 400 if unset; Normal/Soft stay uncapped (`MaxSpeed=0`) until Owner decides a Neo-2-safe default. G1.1 gate. |
| **Tf / Tj** | Profile / feel + CLI / Advanced legacy distance path — **not** Everyday Create. |
| **Rhythm grid** | Opt-in Advanced (#233/#236, v0.5.30+). **Target lock** (#248, v0.5.31) seeds the first cell from the confirmed ROI. Default-on only after Owner ≥4–5 clips. |
| **AI** | Everyday: proposes only (ROI / profile / quality). **Opt-in AI draft** (Owner 26 Sep): local model may draft stroke — `docs/AI_SCRIPT_WRITER.md`; no cloud LLM curve writer. |
| **Scene map** | Advanced *Show scene map* + marks (#246/#247, v0.5.31). Marks = current window; engine consume = P3. Never auto before Create. |
| **Profile model** | Local Go `profilemodel` (#244) — suggest-only; Apply required. |
| **F-003 tier 2** | Do not implement lag tie-breaking; tier-1 `AliasingRisk` is enough for now. |

---

## Stack stance (essay mapping — adopt ideas, not blog deps)

External essays (Go libs, Go+AI, FFmpeg+Go, RAG/FT, optimization) were
reviewed as **input**, not a shopping list.

| Keep / prefer | Why |
|---------------|-----|
| **`videox` + os/exec FFmpeg** | Already the decode/proxy/tools path; no FFmpeg-as-library binding. |
| **stdlib-first Go** | Prefer `slog`, stdlib HTTP/JSON, small pure-Go packages over framework sprawl. |
| **Py train / Go infer** | Heavy training (YOLO/ultralytics) stays Python; product inference prefers ONNX / pure-Go (`profilemodel`) when possible. |
| **Local-only AI** | No bundled weights, no auto-download, no telemetry, no cloud call without opt-in. |

| Reject / defer | Why |
|----------------|-----|
| LLM / chat model as Everyday stroke writer | Breaks per-frame spatial identity; cloud chat still rejected. Opt-in **local** draft = `AI_SCRIPT_WRITER.md` only. |
| RAG-first Generate | Wrong layer — Generate needs measured tracks, not retrieval. |
| PGO / micro-opt as a priority | Measure clip quality first; no speculative optimizer work. |
| Blog dependency stacks (extra ORMs, LLM SDKs, Diffusers, DeepSpeed, …) | Unite.ai filter already in Multi-track Fahrplan; same filter here. |
| Replacing `videox` with a third-party “Go FFmpeg toolkit” | Architecture notes OK; dependency swap needs a measured win. |

---

## Open lanes (24 Sep) — merge order

```text
  Parallel OK (different layers):
    • Claude #243  — SCENE_MAP_PLAN.md (docs only)     → merge when green
    • Manus #244   — profilemodel + GUI Suggest→Apply  → merge when green
        (CI green; no trackcv overlap; only AGENT_COORD may conflict)

  After both on main:
    Cursor builds SceneMap P1 → P5 per SCENE_MAP_PLAN.md
    (Claude reviews engine + goldens; Owner § 6 already decided on #243)
```

**#244 may land on main now.** It is already GUI-wired (Remember scene +
style, Train Go profile model, Suggest→Apply). It does **not** write
curves and does not touch Everyday defaults.

**Do not “code-merge” Claude’s plan into Manus’s PR.** They stack later:

| Layer | Who | What |
|-------|-----|------|
| Style / profile suggestion | #244 `profilemodel` | eight motion features → Normal/Soft/Autotune guess |
| Where the stroke is | SceneMap P1–P3 | rhythm heatmap + exclude/source marks |
| Who / labels for YOLO | SceneMap P4–P5 → L2 | `.samn` marks + export into `KI_TRAINING` dataset |
| How / cell scorer | SceneMap L3 | engine-trace JSONL — after enough clips |

So: merge Manus for the profile helper; keep Claude’s plan as the map /
learning spine; Cursor implements SceneMap on top of both. No rewrite of
#244 into a heatmap, and no SceneMap code inside #244.

Do **not** start SceneMap **code** until #243 is on `main`. G1.1 tune
stays blocked on Owner (speed-cap rule + rhythm clips).

### SceneMap (Cursor after #243)

Canonical plan: `docs/SCENE_MAP_PLAN.md`. Short order:

| Phase | What | Gate |
|-------|------|------|
| **P1** | Split score/choose; `SceneMap` + quick scan; Wails bind | bit-identical curve |
| **P2** | Advanced map view + marks (exclude / source / region) | Owner can paint + persist |
| **P3** | Engine honours marks; masks on Go when RhythmGrid on | goldens, announced |
| **P4** | `metadata.scene_map` in `.samn` | round-trip; export unchanged |
| **P5** | Local training export + privacy off-by-default | delete works |
| **P6+** | L1–L4 learning | per-stage suggest-only gates |

### AIScript (#244 — ChatGPT / Manus)

First measurable G3 helper slice: pure-Go `generator/profilemodel`
(centroids + spread, reject unknown/ambiguous), Create stores scene+Style,
AI Training trains JSON model, Suggest → Apply only. Falls back to
nearest-scene / Colibri. Fits `AI_ADAPTER.md` order (region → profile →
quality). Field data still needed before threshold tuning.

---

## Owner gates (block defaults)

1. **Speed-cap product rule** — Normal uncapped vs Neo-2-safe default + opt-out.
2. **Rhythm-grid default** — ≥4–5 real clips; r and orientation gates.
3. ~~**SceneMap § 6**~~ — **DECIDED 24 Sep** (on #243 tip): scan = **Show scene map** button (not auto); learning collect **off**/opt-in; auto-labels need **review**; marks default to **current scene**. Cursor must not re-open these while building.
4. **Any learned stage (L2/L3) or fusion default** — golden win required.

**SceneMap P1 approved** (Owner via Claude #243): start after #243 merges; order P1→P2→P3; curve bit-identical to `main` @ `e9e697e` baseline.

---

## Agent roles (Wagenheber)

| Who | Owns now |
|-----|----------|
| **Cursor** | Coord / stance / board; after #243 → SceneMap P1+; releases |
| **Claude** | #243 plan; reviews SceneMap engine + measures goldens |
| **ChatGPT / Manus** | #244 AIScript foundation; steward / bugfix |
| **Owner** | Gates above; merge order; local clip smoke |

One theme per agent. Base on current `main`. No silent default changes
(`AGENT_COORD` rule 4).

---

## Go / video / CV link dump (24 Sep) — filter, don’t shop

Owner pasted pkg.go.dev / Medium / GitHub topic links. Same rule as the
Unite.ai filter in the Multi-track Fahrplan: **ideas yes, dependencies no**
unless a golden-clip gate says otherwise.

### Keep (already ours)

| Topic | Ours |
|-------|------|
| OpenCV in Go | Thin `trackcv` CGO (`cv.cpp`) — **CSRT** Stroke |
| FFmpeg decode / proxy / tools | `videox` + os/exec |
| YOLO as detector | ONNX ROI **proposal** (`ai_roi`) — never writes the curve |
| MOT ID ideas | MT-Go coast + MT-Seed done; ByteTrack only if Owner MT-ID gate fails |
| MovieGo notes | Already folded into MT-Infra (filtergraph / PTS) — **not** a dep |

### Reject / out of Generate spine

| Link theme | Why |
|------------|-----|
| **GoCV** (`gocv.io`, hybridgroup examples, face-detect tutorials) | Full contrib wrapper — compile/patent pain; we deliberately don’t use it |
| **MOSSE / KCF** trackers | Measured collapse on hard tip ROIs (`NEXT.md`) |
| **Frame-diff / motion-detect** mains | Rejected as Tip finder |
| **go4vl / V4L2 webcam**, realtime-capture Medium posts | Product = file → script, not live camera |
| **gostream / Pion / Surf streaming apps** | Streaming UX ≠ Generate quality |
| **AlexEidt/Vidio** as decode replacement | Nice pure-Go demux ideas; swap only after measured parity vs `videox` |
| **mowshon/moviego** as dependency | Architecture notes only (already logged) |
| **go-rknnlite / RKNN / Rockchip** edge stacks | Wrong target (desktop portable, not NPU board) |
| **Google Video Intelligence / cloud.google.com/go/video** | Cloud + telemetry — violates local-first / no auto network |
| **Track-Anything** / SAM-equivalent segmentation | Heavy research; Perception v1 gate first |
| **go-object-detection** hobby detectors | Not body-part taxonomy / not our ONNX contract |
| **LibHunt / awesome-go video lists** | Catalogs, not product decisions |

### Already “erweitert” relative to those blogs

```text
  CSRT tip track + coast/reacquire
  + rhythm grid (opt-in)
  + SceneMap plan (#243) for marks/learning
  + profilemodel (#244) for Style suggest
  + YOLO-ONNX propose / ByteTrack only if IDs die on goldens
```

No second tracker stack. No cloud video API. No webcam path for Generate.

### Bewirtschaften — concrete borrows (no new deps)

What we *can* take from the second link dump and use on our path:

| # | Borrow | From | Where it lands | Gate |
|---|--------|------|----------------|------|
| **B1** | **Sparse random-access frames** — `ReadFrame(n)` / `ReadFrames(...)` style (seek a few timestamps, don’t decode the whole clip) | Vidio API shape | SceneMap **P1** `ScanSceneMap` (~6×8s windows) + training still export | Must beat or match current ffmpeg seek cost on `clip_voll`; still via `videox`/ffmpeg, **not** adding Vidio |
| **B2** | **Pipeline stages with bounded buffers + ctx cancel kills FFmpeg** | Edge-processing article (goroutine/channel + `context`) | Already half-done (MT-Infra #190). Reuse the same pattern when SceneMap scan runs beside Generate: one owner, cancel → kill child | Regression: cancel mid-scan leaves no orphan ffmpeg |
| **B3** | **Keep non-video streams when remuxing** (`StreamFile` idea) | Vidio `Options.StreamFile` | Player “Make playable” / proxy — if audio ever drops on remux, copy other streams explicitly in the existing ffmpeg argv | Only if Owner reports silent proxies |
| **B4** | **Detect → associate → stable ID** (YOLO box + ByteTrack-like link) | go-rknnlite / YOLOv5-tracker writeups | Already MT-Seed (propose) → MT-ID (only if tip/part IDs die on goldens). No RKNN, no Rockchip path | Owner clip gate |
| **B5** | **Interactive “mark what matters / what not” on a first pass** | Track-Anything *UX idea* (click/paint regions), not the SAM stack | SceneMap **P2** exclude/source/region marks — Advanced only; time scope default = **current scene** (§ 6) | — |
| **B6** | **Local edge = process here, ship only labels/events** | Edge-processing privacy/cost pitch | Matches AI Adapter + SceneMap M5 (local JSONL/YOLO labels, collect off by default, delete button) — reinforce, don’t invent cloud | Already stance |
| **B7** | **Heatmap as human-readable “what the engine sees”** | Retail analytics aside in edge article | SceneMap map view (rhythm scores) — we already planned this from #233 prototype | P2 GUI |

**Explicitly still not cultivated:** GoCV dependency, MOSSE default, webcam/V4L2, WebRTC player, Google Video Intelligence, Track-Anything weights, motion-diff Tip finder, awesome-go shopping.

**Next code that may use B1/B2/B5:** Cursor SceneMap P1 after #243 merges — seek-sparse scan + cancel-safe ffmpeg, then P2 marks.

---

## Funscript ecosystem links (24 Sep) — already mostly ours

| Link | What it is | Ours |
|------|------------|------|
| [funjack/launchcontrol](https://pkg.go.dev/github.com/funjack/launchcontrol/protocol/funscript) | Go Launch player + Kodi/VLC sync; 100 ms TX floor | **Metrics adopted** (`HANDOFF`: 100 ms min spacing). Playback = our `player` + BLE/Buttplug Neo 2 — **do not** add Launchcontrol as a dep. Basename script↔video pairing = already product. |
| [defucilis/funscript-utils](https://github.com/defucilis/funscript-utils) | TS intensity `500×‖Δpos‖/‖Δt‖`, heatmap palette, half-speed, Handy CSV | **Intensity + compat already in** `funscript/device_compat.go` / Quality Doctor / heatmap. Heatmap PNG + curve editor shipped. FunHalver half-speed = optional editor idea later (not Generate). No npm dep. |
| [honato/funscript-generator](https://gitlab.com/honato/funscript-generator) | POC OpenCV/OpenCL auto-generate + Gradio | **Superseded by us** (tip CSRT, detrend, rhythm grid, SceneMap plan). Their own README: camera motion + pre-roll break it — problems we already attack. No code reuse; no Gradio path. |

**Cultivate only if needed later:** half-speed / group-aware rest matching as an **editor** repair (G4.C / Script Doctor), measured against goldens — not a second generator.



