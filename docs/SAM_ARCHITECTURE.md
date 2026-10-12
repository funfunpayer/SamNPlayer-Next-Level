# SAM: a richer internal motion model, funscript kept as compatibility layer

This file records a long-term architecture direction proposed by the user
(September 14, 2026, pasted as a 10-phase vision document) and scoped down
to what this repo will actually commit to next, after discussion the same
day. It follows the same pattern as `docs/AI_ADAPTER.md`: keep what already
works, add new capability alongside it, never on top of a rewrite.

## Naming (settled September 17, 2026)

- **`.funscript`** = the on-disk / interchange **position format**. There is
  **no** “new funscript format called SAM.” Files users generate and share
  stay `.funscript`.
- **SAM** = this project’s **internal motion model** (`sam/` package): richer
  fields (Intensity, Confidence, Velocity, …) describing *what kind of
  motion* is happening. Optional `.sam` is a serialization of that model
  for tooling (`SamNPlayer sam …`), not a Funscript replacement.
- **Not** Meta’s Segment Anything (sometimes also abbreviated “SAM” in
  Perception 2.0 notes). If a segmentation layer is ever added, it needs
  a different name — see `docs/ROADMAP.md` open decision #3.

## Leitprinzip (the user's own wording, kept as-is)

> `.funscript` beschreibt, wo ein Gerät zu einem Zeitpunkt stehen soll.
> SAM soll beschreiben, was für eine Bewegung stattfinden soll.

`.funscript` is a position curve. SAM is meant to be a richer description of
*what kind of motion is happening* (rhythmic, linear, oscillating; how
intense, how smooth, how confident the measurement is) - a superset, not a
replacement. The runtime turns that description into device output, the
same way `funscript.MapOptions`/`ToIntensityCurve` already turn a position
curve into `Frame{Vibration, Suction}` today.

## What this is not: webcam/live is explicitly out of scope here

The user's own direction, the same day the vision doc was discussed: **real-
time webcam input and live correction (the vision doc's Phase 3, "Webcam /
Live Vision") is not part of this product's near-term plan** - "vielleicht
Letzte Sache oder für ein neues eigenes Produkt" (maybe the last thing, or
for a separate future product). Two independent reasons back this up, not
just preference:

1. **Not requested for this product's actual use case.** SamNPlayer
   generates from pre-recorded video; nothing in the current feature set
   needs a live camera feed.
2. **Measured evidence from the same day (`docs/NEXT.md` priority 8) makes
   30-60Hz real-time perception look infeasible with today's stack, not
   just unbuilt.** A single CSRT tracker call alone already costs ~17ms on
   a 256x144 clip and internally uses ~2.2 of 4 available cores; this
   OpenCV build has no CUDA and OpenCV has no GPU-accelerated CSRT at all
   even if it did. Reaching a 100-500ms look-ahead at 30-60Hz update (the
   vision doc's own target numbers) needs either a fundamentally different,
   much cheaper perception algorithm than classical CSRT/optical-flow
   tracking, or a real feasibility spike to find out what's actually
   achievable - not something to architect around before that spike
   exists. Building runtime/prediction infrastructure for a perception
   layer that can't yet hit real-time on this hardware would be building on
   an unproven foundation.

Everything below is scoped assuming **offline analysis of pre-recorded
video** (today's actual model) unless stated otherwise. Live input, if it
happens, is a later, separately-evaluated effort - possibly its own
product, per the user's own framing.

## What stays exactly as it is

Per the user's explicit instruction: improve, never dilute or regress what
already works. This is not a rewrite. The classical, measurable pipeline
(CSRT tracking, camera-motion compensation, two-point distance, Quality
Doctor, the cache, the whole `funscript` Go package) keeps working exactly
as today throughout every step below - the acceptance bar for every
milestone includes "existing `.funscript` generation and playback are
unaffected."

The vision doc's own Phase 2 (P2.1) already states this directly: *"Vorhandene
Funktionen nicht neu entwickeln, sondern in SAM Perception überführen... Keine
große Umbenennung durchführen, bevor Schnittstellen und Tests stehen."*
Existing components, mapped to what a future `perception` layer would wrap
rather than replace:

| Vision doc's term | Already exists as |
|---|---|
| Optical Flow | `generator/flow_backend.py` |
| CSRT | `generator/generate_funscript.py` (`create_tracker`, `track_roi`, `track_two_points`) |
| automatische Regionssuche | `generator/auto_roi.py` (classical), `generator/ai_roi.py` (ONNX, opt-in) |
| Szenenerkennung | scene-cut detection in `generate_funscript.py` |
| Kamerakompensation | `estimate_camera_motion` in `generate_funscript.py` |
| Zwei-Punkt-Messung | `track_two_points` (see `docs/NEXT.md` priority 2/5 for its real, measured limits) |
| Bewegungsprofile | `generator/tf_tj_meta.py`, `funscript/recipe.go` |
| Qualitätsanalyse | `generator/quality_doctor.py`, `generator/quality_model.py` |
| Cache | `generator/track_cache.py` |
| Batch-Verarbeitung | batch mode in `generate_funscript.py` |
| Device Abstraction (Phase 7) | already substantial: `device/` (BLE, Intiface, `SamNeo2Protocol`) - extend, don't rebuild; hardware research and the Device Response Model this phase needs are in `docs/SAM_NEO_2_RESEARCH.md` |

Renaming `generator/` to `perception/` is explicitly **not** part of the
first milestone, matching the vision doc's own instruction above.

## First milestone - concrete, scoped, matches the vision doc's own steps 1-4

The vision doc's "Reihenfolge der nächsten Implementierung" lists ten
steps; steps 1-4 are well-scoped, low-risk, and useful independent of how
far the rest of the vision goes, so they're the actual next block:

1. **SAM Motion Model - implemented** (`sam/sam.go`): a versioned Go data
   model, richer than `funscript.Action{At, Pos}`: motion type, direction,
   velocity, acceleration, range/depth, intensity, energy, tempo,
   smoothness, variation, tension, anticipation, confidence. Not every
   field has a producer yet (only `Position` and `Type` are populated
   today, by `FromFunscript`) - the schema tolerates fields it doesn't
   know yet, verified by a test that unmarshals a file with fields this
   version has never seen and confirms the known fields still parse
   correctly (`TestParseIgnoresUnknownFutureFields`).
2. **SAM Script v0.1 - implemented** (`sam/sam.go`): the file format
   carrying that model, versioned (`"version": "0.1"`, `sam.ScriptVersion`),
   frames keyed by `"time"` per the vision doc's own example. `Parse`
   rejects a missing version or empty frame list, clamps out-of-range
   normalized fields the same way `funscript.Parse` already clamps `Pos`,
   and sorts frames by time since source order isn't guaranteed.
3. **Funscript → SAM converter - implemented** (`sam.FromFunscript`).
4. **SAM → Funscript converter - implemented** (`sam.ToFunscript`), with
   roundtrip tests (`sam/funscript_test.go`): a synthetic 10,000-action
   file roundtrips with every sampled action identical, confirming timing
   and positions survive intact (existing players, existing scripts, the
   existing ecosystem all keep working un-migrated).

Not yet done from this milestone's own DoD: ~~no producer sets any field
besides `Position`/`Type` yet~~ **first producer + contact consumer
shipped (September 17, 2026):** `sam.Enrich` / `FromFunscriptEnriched`
fills `Velocity`, `Confidence` (from `tracking_gaps`), and for Tf/Tj +
contact also `Intensity`/`Range`. Playback for contact uses
`sam.PlaybackFramesFromFunscript` (SAM Intensity → vibration). CLI:
`SamNPlayer sam FILE.funscript`. Plain `FromFunscript` stays thin. User
files remain `.funscript` — SAM is not a replacement format. Motion
classification etc. stay deferred.

### Definition of Done for this milestone

Trimmed from the vision doc's own DoD list to what steps 1-4 actually cover:

- SAM Motion Model exists as a Go type, schema versioned.
- SAM files can be saved and loaded.
- An existing `.funscript` can be imported to SAM and exported back
  (roundtrip test: timing preserved, positions preserved, large files
  tested).
- Unit tests exist for the model, the schema validation, and both
  converter directions.
- Documentation (this file, extended) matches what's actually built.
- CI is green.
- Existing `.funscript` generation and playback are provably unaffected -
  no behavior change to anything currently shipped.

## Second milestone (started September 17, 2026)

The vision doc's P1.1/P1.2 (runtime, live correction) applied to **existing,
already-generated scripts** - no webcam, no prediction yet. Shipped so far:

- **`sam.Densify`**: expand keyframe SAM onto the playback tick grid and
  recompute contact `Intensity` from interpolated Position (same Span/Curve
  as classic `ToIntensityCurve`) — closes keyframe-lerp divergence.
- **`sam.RuntimeAdjust` / `AdjustDeviceFrames`**: IntensityScale,
  IntensityBoost, SuctionScale, ExtraSmooth, MuteContact — correction on
  top of mapped frames; original `.funscript` / `.sam` unchanged.
- **CLI**: Tf/Tj+contact playback uses SAM (sidecar or Enrich+Densify);
  `--contact-intensity`, `--mute-contact`, `--contact-extra-smooth`.
- **GUI**: „Kontakt-Stärke“ / Empfindlichkeit / Kurve live + bestehende
  „Kontakt ab“; Vibrationsspur folgt den Overrides (`GetVibrationCurvePreview`).
- **CLI**: `--contact-intensity`, `--mute-contact`, `--contact-extra-smooth`,
  `--contact-span`, `--contact-curve`.

Contact vibration and O-markers remain mandatory carry-forwards (below).
Prediction and in-play scrubbing of range stay unscheduled until this layer
is used on real hardware.

## Non-negotiable: contact vibration and O-markers must carry forward

The user's direction (September 14, 2026, repeated explicitly while this
milestone was being built): contact-triggered vibration for Tf/Tj (`docs/
NEXT.md` priority 5) and O-function event markers (priority 7) are **not
optional extras SAM is allowed to drop** - "muss weiter drin sein... das
ist wichtig." Both now ship: manual O-marker placement (the curve editor),
classical auto-suggestion (`SuggestOZone`, no separate detection model,
per the user's own September 15 direction), and an opt-in auto-apply at
generation time. Whatever milestone 2's runtime ends up looking like, it
must have an equivalent or better path for both, not silently lose them in
the transition away from `funscript.MapOptions`.

Concretely, both already fit SAM's existing field set without needing new
schema design:

- **Contact vibration** maps onto `Motion.Range` (the ROI1↔ROI2 distance
  that already drives it in `mapper.go`) plus `Motion.Intensity` for the
  resulting pulse strength - the same relationship
  `ToIntensityCurve`'s `contactEnabled` branch already encodes, just
  expressed as SAM fields instead of being computed inline from raw `pos`.
- **O-markers** are events, not continuous state - exactly the "both
  continuous state and events" requirement the vision doc's own P0.2
  already calls for in SAM Script. A marker is a `Frame` (or a short
  run of frames) with elevated `Anticipation`/`Intensity` and a `Type`
  that says so, not a new top-level concept.

This is a constraint on milestone 2's design, not new work for milestone 1
(this commit) - noted here so it isn't forgotten by the time the runtime
is actually built.

## Deferred, no fixed schedule - pursue only when a concrete use case justifies it

Per the user's own steer ("alles was nützt und das System besser macht,
ohne das Gute zu verwässern... aber professionell, nicht auf Teufel komm
raus testen"): these stay as long-term direction, not near-term
commitments, and each needs its own scoping/feasibility pass before
starting, the same way the AI adapter and the CSRT/FunGen work this session
were each measured before being trusted:

- **Prediction** (P1.3) - needs the runtime (milestone 2) first, and a real
  measured error signal to calibrate against, not a guess.
- **Motion classification** (P2.2) and **confidence scoring** (P2.4) - this
  session's own real-clip work (`docs/NEXT.md` priority 2) already found
  that even the *existing*, much simpler two-point distance measurement is
  hard to get right (ROI2 anchor choice dominates match quality, no
  reliable heuristic found yet on one real clip). Classifying motion *type*
  automatically, with a trustworthy confidence number, is a harder version
  of a problem that isn't solved yet at the simpler level - treat as
  research, not an engineering task with a known solution.
- **Multi-ROI candidate correlation** (P2.3) - same caveat; `auto_roi.
  find_two_rois` already has a documented, measured "not good enough"
  verdict for the current, simpler approach (see `docs/NEXT.md` priority
  3) - a more ambitious version of the same idea inherits that risk.
- **Emotion Engine** (Phase 5) - an abstraction layer over Motion; useful
  concept, but downstream of Motion Model + real usage data, not before.
- **Adaptive Learning / Context Memory** (Phase 6) - the vision doc's own
  P5.4 already gets this right: start simple (feature vectors, similarity
  search, weighted history), only reach for a trained model once it beats
  the simple approach - exactly `quality_model.py`'s existing, proven
  pattern (`docs/AI_ADAPTER.md`). Needs real correction data to exist
  first, which needs the runtime (milestone 2) shipped and used first.
- **Audio/external events** (Phase 8) - additional input sources, sensible
  once the SAM model exists to feed; not before.
- **Additional device protocols beyond what `device/` already supports** -
  the vision doc's own "Nicht jetzt" list agrees.

## Not part of this change

Matches the vision doc's own "Nicht jetzt" list, restated for this repo:

- No webcam or other live input (see above - deferred, possibly a separate
  product).
- No complex neural network, no cloud training - anything Colibri/ONNX-
  adjacent follows `docs/AI_ADAPTER.md`'s existing "AI proposes, classical
  system measures" rule; SAM's Motion Model is a data format, not an AI
  system by itself.
- No full Emotion AI, no new device protocols, no GUI redesign forced by
  this work, no smartphone app, no plugin marketplace.
- No renaming of `generator/` to `perception/` yet (needs interfaces and
  tests first, per the vision doc's own instruction).
- `.funscript` is never removed or deprioritized as an output format -
  existing players, scripts, and the wider ecosystem keep working, full
  stop.

## Decision: staying in Go, no C#/.NET/Avalonia migration

The user shared a much larger, more formal 16-file documentation package
("SamNPlayer GitHub Leitbild", September 14, 2026) proposing this same SAM
direction at a bigger scale, including a phased migration of the whole
application from Go/Wails to C#/.NET with Avalonia UI, OpenCvSharp for
computer vision, a dedicated Training Lab and Device Lab, and full
multi-source perception fusion (CSRT + optical flow + pose + depth +
audio). Reviewed and discussed the same day; the outcome:

**No language/framework migration.** The user's own reasoning, in their
words: they want a "hochprofessionelle Sprache" and to build the system
themselves rather than just glue libraries together - not a preference for
C# specifically. Go already satisfies that: it's a production-proven,
professional language (Docker, Kubernetes, Cloudflare, Uber all ship
production systems in it), this repo already has a working cross-platform
desktop shell in it (Wails), and none of `ARCHITEKTUR_TECHSTACK.md`'s
concrete technical reasons for C# (interfaces, modular boundaries, SQLite,
ONNX inference, cross-platform packaging) are things Go can't do -
Go has SQLite drivers, an ONNX Runtime Go binding exists, and interfaces/
package boundaries are exactly as expressible in Go as in C#. The fusion
engine, perception-source abstractions, and everything else the Leitbild
package describes in C# get built the same way, in Go - "selbst
entwickeln, nicht nur nutzen" applies regardless of language.

**Also confirmed staying out: webcam/live input**, per the same reasoning
as earlier in this document (measured CSRT cost, no GPU path) - the
Leitbild package's own roadmap had quietly reintroduced it as milestone
M8, which the user did not intend; explicitly re-confirmed still out.

**What's worth carrying forward from the Leitbild package despite
rejecting its language choice** - these are language-independent and
genuinely good practice, expressed in Go instead of C#:

- **Go/No-Go gates per milestone** (`MACHBARKEIT.md`'s Gate A-F pattern) -
  a concrete, checkable bar before calling a milestone done, sharper than
  this document's current prose-only "Definition of Done" sections.
- **Golden Clip test suite** (`TESTSTRATEGIE.md`) - fixed, version-
  controlled synthetic test clips with known ground truth (linear motion,
  approach/recede, occlusion, scene cuts, low confidence) that every
  generator/perception change is checked against - this repo already does
  pieces of this ad hoc (`two_point_test.py`'s `sine()` fixtures,
  `fungen_compare_test.py`'s synthetic clips) but not as one organized,
  reusable suite. Worth formalizing later, not urgent.
- **Explicit module boundaries as Go packages with real interfaces**
  (`ARCHITEKTUR_TECHSTACK.md`'s `IPerceptionSource`/`ITracker`/
  `IFusionEngine` idea) - translates directly to Go interfaces, useful
  once there's more than one perception source to actually abstract over
  (not yet - today there's still just CSRT/flow, an interface with one
  real implementation is premature per this repo's own "no interface
  without a second implementation" instinct already visible in the
  codebase).
- **Benchmark reproducibility metadata** (commit, OS, CPU/GPU, dataset
  version, settings) - cheap to add whenever a real benchmark harness
  exists, worth keeping in mind.

**Rejected outright, not just deferred:** the C#/.NET/Avalonia stack
itself, OpenCvSharp (would still hit the exact same CSRT cost ceiling
measured in `docs/NEXT.md` priority 8 - it's a binding around the same
native OpenCV, not a different implementation), and the Leitbild
package's SQLite-backed "Knowledge Base with ADRs" as a near-term
priority (plain markdown docs, as this repo already uses, are sufficient
at the current scale).

## Privacy / architecture principles (the user's own wording, unchanged)

Local-first stays the default: no cloud required for core functionality,
no automatic media upload, learning data (once it exists, per the deferred
Adaptive Learning item above) stays local, is deletable, and learning
itself must be possible to switch off. Matches `docs/AI_ADAPTER.md`'s
existing principle for the AI engines almost exactly - same rule, restated
for SAM.
## SAM Perception v1 (17 September 2026)

Architecture decision (`docs/SIGNAL_VS_FIDELITY.md`, `docs/ENGINE.md`):

> The next milestone is **not** “another tracker”, but **SAM Perception
> v1**: measure the observers we already have (CSRT, grid_lk,
> region_fusion, region_fusion_auto, flow), score segments, produce
> confidence, fuse on agreement/disagreement, and derive a
> device-independent motion model — then map that to funscript / runtime.

Preconditions (same review’s order):

1. Real golden clips with refs/ROIs/hashes — **first filled 21 Sep 2026**
   (`clip_voll_tftj`, `clip_ausschnitt_native` with FunGen2 refs; see
   #140 / #150). Provenance must stay honest before comparing.
2. Same-segment bake-off of all backends (Motion Fidelity **and** Signal
   Quality metrics — see `docs/SIGNAL_VS_FIDELITY.md`) — **done 21 Sep
   2026, lane B (Claude)**, result below. No Go port earned yet.
3. Raw tracking vs post-processing scored separately

Only after a reproducible golden-clip win may a fusion path become the
default. Go ports stay secondary to perception correctness.

### Bake-off result (21 Sep 2026): no candidate beats CSRT yet

`flow`, `grid_lk`, `region_fusion` run against both goldens (`clip_voll`
30s-windowed, `clip_ausschnitt` whole-clip; same FunGen2 mit_yolo/ohne_yolo
refs already committed), auto-detected ROI (`auto_roi.find_roi`, not the
original hand-marked ROI — see caveat below):

| Backend | `clip_voll` windowed mean r | `clip_ausschnitt` r (mit/ohne) |
|---|---|---|
| CSRT `tj`/native (existing baseline) | 0.349 | 0.273 / 0.440 |
| CSRT `hub` (existing baseline) | 0.264 | — |
| `grid_lk` | 0.241 | 0.121 / 0.105 |
| `region_fusion` | 0.248 | 0.105 / 0.112 |
| `flow` | **no result** — hung past a 5-min timeout on `clip_voll` (280s) and a 3-min timeout on `clip_ausschnitt` (50s). Contradicts its own docstring ("~4x faster than CSRT"); looks like a real performance bug, not just a slow long-clip case since it also failed on the short clip. Not root-caused yet — flag for lane E triage, not lane B. |||

**Verdict**: neither `grid_lk` nor `region_fusion` beats CSRT on either
golden (`clip_ausschnitt` especially: both land at roughly a third of
CSRT's r). Per the standing rule ("Go only where it measurably helps"),
**no Go port for either is justified by this data**. `flow` is
unmeasurable until its hang is fixed — no verdict either way yet.

**Caveat**: this bake-off used one auto-detected ROI per clip, not the
original hand-marked ROI the committed CSRT baselines used — not a
perfectly matched comparison. A rerun with the same ROI across all
backends would be needed before treating "CSRT wins" as fully settled,
but the gap (grid_lk/region_fusion at ~30-40% of CSRT's r on
`clip_ausschnitt`) is large enough that ROI choice alone is an unlikely
full explanation.

**Update (same day, tip-matched rerun on `clip_ausschnitt`):** with a
shared tip ROI `426,450,320,180` `--axis y`, windowed (10s) vs FunGen
ohne_yolo: CSRT hub 0.590 > **region_fusion tip 0.562** > grid_lk 0.494 >
CSRT tip 0.468 > 4-zone auto 0.363 > flow×0.5 0.231. So ROI choice *was*
the main gap for region_fusion/grid_lk on this clip — tip-matched fusion
nearly matches the committed hub. Still **no Go port** until a second
golden (`clip_voll`) confirms. Flow full-res still incomplete (~180s /
~77% frames); `--flow-downscale 0.5` completes but quality is poor.
Full table: `docs/TFTJ_PROFILE_DIRECTION.md` § Method matrix.

**Consequence for fusion/cross-check**: the original idea (use a second
observer as a gap-filler or cross-check where CSRT's own `confidence`/
`lost` telemetry flags trouble, not as an always-on second tracker) is
still architecturally sound and matches this section's own design. The
tip-matched rerun puts `region_fusion` back on the candidate list for
that role — still opt-in / measure-first, not a default. Revisit a Go
port only if a second golden (`clip_voll`) and/or a fixed `flow` show a
real edge.
