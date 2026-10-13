# Roadmap — consolidated task list

Single source of truth for "what's left to do", merging `docs/NEXT.md`'s
priority list with the user's "Perception & Motion System 2.0" concept
(16 September 2026) into one actionable checklist. Written in English per
this project's documentation convention (see `HANDOFF.md`); day-to-day
discussion stays German.

**Relationship to the other planning docs, so they stop drifting apart:**

- **This file** — the current checklist. Check items off here as they ship.
- `docs/PRODUCTION_ROADMAP.md` — rock-solid production plan: open
  workstreams, owner test loop, targets / prerequisites, what you test
  and where results go.
- `docs/NEXT.md` — research journal (measurements / rejected approaches).
- `docs/ENGINE.md` — lean engine direction (phases + DoD).
- `docs/FINDINGS_TIMING_TF.md` — Go-migration / timing inventory.
- `docs/TFTJ_PROFILE_DIRECTION.md` — **agreed direction (21 Sep 2026):**
  Tf/Tj = device profile/feel; no-mark 4-zone classical; contact vib on
  Normal/Auto; dual-ROI not the everyday path. Think-first; implement
  only after owner amends/accepts that file.
- `docs/AGENT_COORD.md` — **Cursor ↔ Claude ↔ ChatGPT shared board:** who
  owns which lane right now (claim before code; avoid colliding PRs).
- `HANDOFF.md` — architecture and “tested and rejected”.
- `CHANGELOG.md` — what shipped, release by release.

---

## Principles (recap — see `HANDOFF.md` for the full rationale)

1. **Don't add what isn't needed.** No dependency, module, or feature
   ships speculatively — `fusion.py` stays tested but unconnected until a
   genuine third independent signal source exists (see Open tasks).
   Prefer a **lean self-build** only when clip tests show **equal or
   better** quality — never worse (`docs/SELF_BUILD.md`). Fewer deps are
   a win only after that gate.
2. **Every quality/speed claim needs a measurement, not an estimate.**
   Reproduce before changing an algorithm; keep the raw numbers, not just
   the conclusion.
3. **Behavior changes need a regression test that fails without the
   change** (`CONTRIBUTING.md`'s central rule).
4. **AI proposes, the classical system always measures and decides.**
   No second, AI-only output path ever writes a `.funscript` on its own
   (`docs/AI_ADAPTER.md`).
5. **Nothing bundled, nothing automatic.** No model, no download, no
   telemetry, no cloud call the user didn't opt into.
6. **Never auto-commit an unvalidated guess** — classical or AI — for a
   region, ROI2, or parameter (issue #8's lesson, learned the hard way).
7. **Definition of Done for a new technique**: it has to beat the current
   production path on a fixed benchmark, stay within its
   latency/memory budget, run reproducibly on the target platforms, and
   have a documented fallback — not "it's newer" (this is also section 23
   of the Perception 2.0 concept; same rule, independently arrived at
   twice).

---

## Done (Finishing)

Everything below is shipped and merged to `main`. Grouped, not one line
per PR — see `CHANGELOG.md` for the fuller list and `git log` for exact
commits.

**Core generator**
- [x] Three interchangeable tracking backends: CSRT (default), flow
      (~4x faster, no region needed), `grid_lk` (~15x faster, more robust
      on small/hard regions) — all GUI-wired via `#gen-backend`.
- [x] Automatic region selection (classical, rhythm-based), scene-cut
      detection with per-scene region search, camera-motion compensation,
      appearance memory, adaptive keyframes, RDP reduction, speed
      limiting, axis selection, caching.
- [x] Two-point distance measurement (`--roi2`) for Tf/Tj, contact-
      triggered vibration (opt-in).
- [x] Motion profiles (`standard`/`weich`), motion-signature scene
      matching for parameter reuse.
- [x] Quality Doctor (timestamps, range, gaps, speed, rhythm, tracking
      loss, amplitude, active-time fraction, reconstruction error) and
      the learned quality model (adopted only if it beats fixed rules in
      cross-validation).
- [x] Device-compatibility checks against community-established limits.
- [x] **Golden-Clip Benchmark**, with its own GUI tab — the fixed,
      repeatable comparison basis this list itself kept asking for.

**Local AI adapter** (`docs/AI_ADAPTER.md`)
- [x] Region proposal (`ai_roi.py`, ONNX), profile suggestion, quality
      second opinion — all GUI-wired, all gated behind the classical
      pipeline.
- [x] Bootstrap/export tooling to train your own ROI model — no model
      bundled.
- [x] AI can propose a *second*, separated region too (`find_two_rois`)
      — built and tested, **not yet wired into generation** (see Open
      tasks below, this is deliberate per principle 6).
- [x] Audio-tempo plausibility check (`--audio-check`) — classical, not
      AI; GUI default **on** when ffmpeg is available. Native Go path
      runs `CheckAudioTempo` post-hoc (no longer forces Python). Workflow:
      [`docs/AUDIO_WORKFLOW.md`](AUDIO_WORKFLOW.md). Pre-pass tempo hint
      / “no motion → audio” fallback still deferred (audio is not a
      position curve).
- [x] ROI auto-detect second-pass (`VerifyROI`) — warn-only motion
      concentration check after classic/AI find-region.

**O-markers / Extended-O**
- [x] Manual placement, classical auto-suggestion (primary + secondary),
      both wired into the manual button and the generation-time checkbox.

**Player / editor**
- [x] Manual funscript curve editor (drag/add/delete, video follows the
      dragged point).
- [x] Script Doctor for imported files without tracking data.
- [x] Heatmap, chapters, bookmarks, training-session history.
- [x] OFS-style max-speed highlights on the curve (`funscript.SpeedHighlights`).
- [x] Chapters/bookmarks in `.funscript` metadata (`funscript/bookmarks.go`).
- [x] Heatmap PNG export with chapter ticks.
- [x] Lightweight project sidecar `.snp.json`.
- [x] Frame snap + range delete/speed-cap (Wiedergabe OFS-row).
- [ ] BPM/tempo grid overlay (optional polish — after GUI improve C5;
      see `docs/PRODUCTION_ROADMAP.md` C4).

**Device / playback**
- [x] BLE + Intiface/Buttplug transports, both-channel keepalive,
      unchanged-packet suppression.
- [x] Training tab (Stop-Start, Plateau), session logging + history view.
- [x] **Device-Diagnostics tool** (`device/diagnostics.go`, "Geräte-Diagnose"
      in the Device tab) — an automated test sequence against the connected
      device (raw-value acceptance sweep per channel, maximum stable
      update rate, channel-interaction cases: alone/simultaneous/offset/
      rapid-switching), logging every command with monotone time, wanted
      vs. sent value, write latency, and errors to a JSONL history. This
      is the automation of the measurement plan in
      `docs/SAM_NEO_2_RESEARCH.md` §11/§12 - but only the part that's
      actually measurable in software: write-round-trip latency and value
      acceptance, not felt intensity or true physical rise/fall time
      (the report says so explicitly, no number is faked). Still needs a
      real device to produce a real profile - see "Needs real hardware"
      below.

**SAM long-term architecture** (`docs/SAM_ARCHITECTURE.md`)
- [x] First milestone: `sam/` package, funscript roundtrip — correctness
      verified against real Tf/Tj output. **Deliberately not wired into
      any GUI/CLI flow yet** — no consumer for the richer fields exists,
      so a save/load button would add surface area without payoff.

---

## Open tasks

Merged from `docs/NEXT.md`'s priority list and the Perception 2.0
concept's phases. Status tags: 🔒 blocked on you · 📏 needs measurement
first · 🧭 needs a decision from you · 🔓 buildable now, no blocker.

### Near-term, buildable without new heavy dependencies

- [ ] 🔓 **Wire the AI two-region proposal into `--roi2`/the GUI** — the
      building block (`find_two_rois`) exists; still needs real-clip
      measurement (per principle 6/7) before it can become even an opt-in
      suggestion, let alone a default.
- [x] 🔓 **OFS-inspired editor upgrades in Go** — chapters/bookmarks metadata,
      heatmap PNG, project sidecar, frame snap, range ops (`docs/OFS_LEARN.md`).
      Optional later: BPM grid UI, WebSocket bridge.
- [ ] 🔓 **Populate the Golden-Clip manifest with real clips** and run a
      first baseline — the tool is built, nobody has fed it real material
      yet. This unblocks *every* future "did X help" question on this
      list.
- [ ] 📏 **Persist the reproduction manifest** `docs/NEXT.md` priority 2
      already called for (hashes, generator version, ROIs per clip) —
      the Golden-Clip Benchmark's manifest format is most of this already;
      close the gap between the two.
- [ ] 🧭 **Secondary/weaker O-marker count and intensity heuristic** —
      shipped with a first reasonable heuristic (median-baseline peak
      detection); revisit only if real usage shows it's off.
- [x] 🔓 **`videox`/native tracker port** — `generator/trackcv` +
      `posttrack` + opt-in `NativePipeline` (CSRT when OpenCV linked;
      `simpletrack` NCC/SAD over ffmpeg otherwise — Windows without
      Python, v0.5.2). Dense Quality Doctor + FunGen-compare CLI in Go.
      Still open: Tf/Tj two-point with goldens; optional Windows OpenCV
      for CSRT parity; native as default after real-clip win.
- [x] 🔓 **Tf/Tj suction double-floor** — fixed; see `FINDINGS_TIMING_TF.md`.
- [x] 🔓 **Phase Analyzer core** — `BestLagCorrelation` / `DiagnosePhase`
      + CLI. Full 8-point PTS pipeline still open (`docs/ENGINE.md`).
- [x] 🔓 **Signal Quality ≠ Motion Fidelity** — `SIGNAL_VS_FIDELITY.md`.

### SAM Perception v1 (next architecture milestone)

Confirmed direction from the 17 Sep 2026 script-quality review: **not
“another tracker in the dropdown”**, but measure / fuse the observers we
already have. Blocked on golden clips for any default change.

Order (do not skip ahead of measurement):

1. 🔒 Populate Golden-Clip set (real videos + refs + ROIs + hashes)
2. 📏 Same-segment backend bake-off (correlation **and** turning-point /
   amplitude / phase / lost-fraction / confidence)
3. 📏 Split raw-tracking vs posttrack scores
4. 🔓 Auto-evaluator (pick / weight observer per segment) — after 1–3
5. 📏 Calibrate confidence vs error
6. 🔓 Training queue for disagreeing / low-confidence segments
7. 🔓 Further Go ports only with measured gain (see FINDINGS inventory)
8. 🧭 SAM Motion Model as stable middle layer (see `docs/SAM_ARCHITECTURE.md`)
9. 🔓 Keep Signal Quality and Motion Fidelity separate in GUI/reports
10. 🧭 Promote a fusion path to default only after golden-clip wins

### Needs real hardware (blocked on you)

- [ ] 🔒 **Validate on a real Sam Neo 2**: BLE + Intiface separately,
      connection/playback/pause/stop/reconnect/training. The Device-
      Diagnostics tool (see "Done" above) now automates the raw-value/
      update-rate/channel-interaction part of this - connect in the
      Device tab, click "Diagnose starten", the resulting profile lands
      in the history. What it can't tell you: whether it actually *feels*
      right - that's still yours to judge and note down separately.

### Funscript algorithm workflow

See [FUNSCRIPT_ALGOS.md](FUNSCRIPT_ALGOS.md) for the multi-stage Flow→CSRT→Autotune recipe.

### FunGen parity (open-ended, ongoing measurement)

- [ ] 📏 Keep closing the real-clip correlation gap (`docs/NEXT.md`
      priority 2) — synthetic-clip results (r≈0.5) don't predict real-clip
      results (r≈0.05–0.36) yet; needs more real clips, ideally through
      the Golden-Clip Benchmark once populated.
- [ ] 📏 Settle whether ROI2 anchor choice or tracking quality is the
      dominant lever (current evidence: anchor choice, by a wide margin —
      needs more clips to generalize).

### Perception 2.0 concept — later phases, each needs its own go/no-go

Every item here is explicitly **not started**, and per principle 7 none of
them ships without first winning against the Golden-Clip Benchmark.
Ordered as the concept itself proposed:

- [ ] 🧭 Multi-ROI + relative-motion framing as the default measurement
      unit (concept's own "biggest structural win" — this is the direction
      the two-region AI work above is already a piece of).
- [ ] 🧭 Camera Motion Compensation 2.0 (explicit confidence value,
      geometric models beyond the current feature-based correction).
- [ ] 🧭 Track memory / occlusion recovery as a first-class concept
      (partially exists via appearance memory; not the full track-state
      model the concept describes).
- [ ] 🧭 **ByteTrack/BoT-SORT adapter** — ⚠️ needs its own hard-ROI/
      small-region measurement first, the same way KCF/MOSSE was tried
      and rejected for exactly that failure mode. Not a safe default
      assumption just because it's a newer tracker family.
- [ ] 🧭 Confidence + disagreement fusion across multiple signal sources
      — `fusion.py` already exists and is tested, deliberately
      unconnected until a genuine third independent source exists (own
      docstring is explicit about this).
- [ ] 🧭 Auto-Evaluator (run multiple candidate pipelines, pick or defer
      to "unsure").
- [ ] 🧭 **Segmentation precision layer** (concept calls it "SAM 3.1")
      — ⚠️ naming collision, see decisions below before this goes any
      further. Also a materially heavier dependency than anything shipped
      so far.
- [ ] 🧭 Monocular depth as a supporting signal (Depth Anything V2 class
      of model) — unproven relevance to a largely-2D problem; benchmark
      before adopting.
- [ ] 🧭 Prediction/look-ahead (kinematic first, temporal model later)
      for device-latency compensation.
- [ ] 🧭 Active learning / similarity memory for parameter reuse — the
      "our own AI" direction already scoped in `HANDOFF.md`'s "Later"
      section (extend `quality_model.py`'s existing pattern, not a new
      architecture).

### Next focus after v0.5.11 (sequential — do not parallelize themes)

Canonical order: **`docs/PRODUCTION_ROADMAP.md`** phases **G0 → G1 → G2 →
G3 → G4**. Heuristics design: **`docs/GENERATE_HEURISTICS.md`**.
License stays **developer mode** until after G2.

1. [ ] 🔓 **G0 — Stabilize Generate** — Windows OpenCV CSRT in release
       binary (#120); clip/cancel gates; no weak NCC; AI Train not a tag
       blocker.
2. [ ] 📏 **G1 — Classical heuristics + audio** — one posttrack package;
       goldens + FunGen fixed-ROI bake-off; Tf/Tj classical stabilize;
       audio warn-only. **No AI as script writer.** Product shape for
       Tf/Tj-as-profile + no-mark 4-zone + contact vib on Normal/Auto:
       follow `docs/TFTJ_PROFILE_DIRECTION.md` (steps 2–5), do not
       parallelize with unrelated themes.
3. [ ] 🔒 **G2 — Neo 2 raw-value layer** — Device Diagnose on real hardware;
       Intent→raw mapping for good feel (blocked on you + device).
4. [ ] 📏 **G3 — AI helpers only after G1** — train/ROI suggest on strong
       classical scripts; two-ROI AI stays opt-in until bake-off win.
       **Multi-track Fahrplan** (opt-in Tip+Partner IDs/proposals, Stroke
       stays Go CSRT): see `PRODUCTION_ROADMAP.md` § Multi-track Fahrplan —
       [x] MT-Go (#183) · [ ] MT-Seed (Cursor) · [ ] MT-Speed (ChatGPT after
       verify) · [ ] MT-ID (only if needed) · [ ] MT-Debug (Claude) ·
       [ ] MT-Infra (Claude optional).
5. [ ] 🎨 **G4 orthogonal** — GUI improve slices (C5–C7) if Generate
       untouched; license enforcement (E5) only after your go-live;
       site/macOS/mobile later.

Archive note — former “after v0.5.9” items: Quality-Doctor learning and
depth/pose scaffold remain shipped; Tf/Tj+AI two-ROI waits for G1 goldens
then G3; GUI improve is G4.C not parallel to G0/G1.

### Next after v0.5.6 (archive — polish train)

These were **out of scope for the polish PR only**, not rejected. Items
1/3/4 done; item 2 carried into “Next focus after v0.5.9” above.

1. [x] 🔓 **English for remaining German code comments / backend logs** —
       operator-facing Log-tab / stderr / `fmt.Errorf` / file-dialog titles
       English (`docs/LANGUAGE.md`). Historical comments may stay German
       until touched — no mass rewrite.
2. [ ] 📏 **Tf/Tj + AI two-ROI on real clips** — see Next focus after v0.5.9.
3. [x] 🔓 **Quality-Doctor learning from user judgments** — see above.
4. [x] 🔓 **3D / depth / pose supporting-signal scaffold** — see above.

### Evaluated 19 Sep 2026 — Wails-v3 / “GUI modernisation” idea dump

Selective review (same bar as the earlier polish pass). **Ship none of
these in the 0.5.7 train** unless noted.

| Idea | Verdict |
|------|---------|
| Frameless window + custom titlebar | **Skip** — brand/desktop chrome already settled; overkill vs Wails v2 app chrome |
| Card-based layout / more whitespace redesign | **Skip** — conflicts with “no cards by default” / existing dark gold-teal composition |
| Dark/Light mode (`IsDarkMode`) | **Skip** — product is dark gold/teal; light mode rejected in 0.5.6 review |
| Micro-interactions / tab transitions | **Later (small)** — `prefers-reduced-motion` already respected; only add if it helps hierarchy |
| Heatmap/curve tooltips + zoom | **Done** — tooltips + curve/heatmap zoom window (selection / wheel / reset) |
| Multi-axis funscripts “v2.0” | **Done as `.samn`** — native source of truth with general/vib/suc + recipe/axes drive + strength presets; Funscript export for community. See `docs/SAMN_FORMAT.md` |
| Playlist shuffle / section repeat / crossfades | **Partial (shuffle + playlist repeat)** — crossfades still later |
| Plugin video-sync / streaming sources | **Skip** — local files + H.264 proxy is the codec path; no streaming plugin surface |
| Cloud sync for settings | **Skip** — local-only / no telemetry principle |
| Swap BLE stack (tinygo / bleak) | **Skip until measured** — BLE+Intiface+Mock already work; swap is risk without hardware matrix |
| Central event-bus state | **Later (hygiene)** — modular JS is fine; bus only if coupling becomes painful |
| Automated Mock-BLE test runner | **Partial already** — Mock transport + device diagnostics + frontend device tests; expand if protocol regressions appear |
| Self-hosted SamNPlayer Cloud | **Reject for now** — out of product scope |
| WebCodecs custom video pipeline | **Skip** — ffprobe + “Make playable” proxy is the measured path |
| Community Funscript platform | **Reject for now** — tool, not a social network |

**Next buildable after 0.5.7 (if picking from this list):** ~~heatmap/curve
value tooltips~~, ~~playlist shuffle/repeat~~ — pick another “Later (fit)” item
(e.g. curve zoom) or hygiene; not frameless/light/cloud.

### Evaluated 19 Sep 2026 — less Python / player / detection

| Idea | Verdict |
|------|---------|
| Decouple `--audio-check` from `NativePipelineEligible` | **Done** — Go `CheckAudioTempo` post-hoc; default GUI stays on CSRT/simpletrack |
| Port `audio_check.py` to Go | **Done** — `generator/audiocheck.go` (Python kept for PreferPython path) |
| Soft CSS / WebGL canvas upscale + sharpen | **Skip** — measured worse than native `<video>` (`docs/NEXT.md` §10) |
| Soft upscale inside tracking / proxy | **Skip upscale** — Lanczos **downscale** only when proxy width > 1920; GrayReader Lanczos |
| Remux-first H.264 → MP4 | **Done** — avoid re-encode when codec already playable |
| More player containers (ts/flv/mpg/…) | **Done** — pick filters + MIME; proxy still the fallback |
| Live second-pass ROI rewrite | **Skip rewrite** — `VerifyROI` warns only (no silent box change) |
| AI quality opinion on Go path | **Still Python** — keep blocking eligibility until a Go port exists |
| Bundle / auto-ship ffmpeg | **Done** — portable release + opt-in Install video tools; no surprise download at startup |
| Pure-Go H.264 decode/encode | **Skip** — codec stack; keep ffmpeg as the only media I/O helper (`docs/FFMPEG_TOOLS.md`) |
| Lean self-build over heavy OSS | **Principle** — `docs/SELF_BUILD.md`; MP4 ISO-BMFF probe fallback without ffprobe **Done** |
| Competitive GUI + GitHub exterior | **Bar documented** — `docs/COMPETITIVE.md`; Play stays home; no phone-shaped desktop |
| macOS release binary | **Blocked on hardware/CI** — code path kept; see `docs/PLATFORMS.md` |
| iOS/Android player (no generator) | **Hinted** — share `player`/`device`/`funscript`/`samn`; desktop keeps Generate |
| “Max out CPU/GPU/RAM” slogans | **Reject as policy** — use measured budgets; CSRT already multi-threads; GPU ≠ CSRT |

## Explicitly deferred, not forgotten

- [ ] 🧭 Climax ("cum") detection — needs a design decision on signal
      shape and false-positive handling before any implementation; the
      classical, signal-only O-marker suggestion was judged sufficient
      for now.
- [ ] 🔒 Real-hardware "does contact-vibration feel right" check —
      blocked on the same hardware item above.
- [x] 🔑 **Yearly personal license (infra, not sharp)** —
      [`docs/LICENSE_SYSTEM.md`](LICENSE_SYSTEM.md). **1 year**, **1 person**
      per key; invite/internal; `cmd/license-tool` + Settings import.
      `license.Enforcement` stays **false**. Gates (E5) later at go-live.
- [ ] 📱 **Mobile player (iOS/Android)** — play + device only; no
      generator. Architecture fence: [`docs/PLATFORMS.md`](PLATFORMS.md).
- [ ]  **macOS GUI release** — needs Mac builder + notarization; Linux
      and Windows remain the shipped pair.
---

## Explicitly rejected — don't retry without new evidence

Full table with numbers: `HANDOFF.md`, "Tested and rejected". Short list:
KCF/MOSSE trackers (collapse on hard ROIs), two-tracker CSRT
parallelization (no net gain), gentle upscaling before tracking
(unstable), WebGL/Canvas video sharpening (measurably worse), fusion of
CSRT+flow with only two sources (always lands between them, never beats
the best), a C#/.NET/Avalonia rewrite.

---

## Decisions needed from you

Things this file can't resolve on its own — flagging rather than guessing:

1. **Open source or not, going forward.** **Decision leaning closed:** keep
   application source **private** (core team only); use public showcase
   [SamNPlayer-site](https://github.com/funfunpayer/SamNPlayer-site) for
   Anna/reviewers. Owner must flip `SamNPlayer` to private (agent cannot).
   Details: [`docs/CLOSED_SOURCE.md`](CLOSED_SOURCE.md). Past MIT-published
   copies are not undone by going private.
2. **"Drei-Experten-Regel"** — mentioned once, not found in any existing
   doc (`TEAM_STAND.md`, `HANDOFF.md`, `NEXT.md`, `CONTRIBUTING.md`). Is
   this a new principle to adopt (e.g. "don't ship a design call without
   three independent opinions/checks agreeing"), or a reference to
   something from earlier that didn't make it into this file? Want it
   captured correctly in the principles list above, not guessed at.
3. **Naming: "SAM 3.1" (Meta's Segment Anything, from the Perception 2.0
   concept) vs. this project's own internal "SAM"** (`sam/` package, SAM
   Motion Model, `docs/SAM_ARCHITECTURE.md`). **Settled for the motion
   model (17 Sep 2026):** keep **SAM** = internal motion model; **never**
   call a Funscript variant “SAM”; `.funscript` stays the interchange
   format. Meta Segment Anything (if ever) needs a **different** name
   before that segmentation phase starts — collision still open for that
   future phase only.
4. **Which Perception 2.0 phase to greenlight next**, if any, beyond the
   Golden-Clip Benchmark already shipped — each remaining phase pulls in
   real new dependencies (SAM-equivalent segmentation, ByteTrack/BoT-SORT,
   depth models) that need individual sign-off, per principle 7.
5. **License go-live (E5):** replace embedded pubkey, flip
   `license.Enforcement`, wire Generate/Play — settle remaining open items
   in [`docs/LICENSE_SYSTEM.md`](LICENSE_SYSTEM.md) (edit `.samn` without
   key? renew grace? shop). Infra E2–E4 already landed.
