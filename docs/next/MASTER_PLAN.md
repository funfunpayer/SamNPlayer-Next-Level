# SamNPlayer Next — master plan

**Status:** code imported · 12 Oct 2026 · single source of truth for this repository.
The five planning documents from SamNPlayer PR #510 are archived unchanged in
[`sources/`](sources/). Where they disagree with this file, this file wins.

**Owner five-engine blueprint (13 Oct 2026):** [SamN Core, SamN Motion Engine, SamN Intelligence, SamN AI Assist, SamN Device Engine — integrated GUI, dependency policy and test contracts](FIVE_ENGINE_ARCHITECTURE_AND_GUI.md). This is the architectural elaboration of this master plan; it does not revive paid Studio or create a competing roadmap.

Open Owner decisions: [`OPEN_DECISIONS.md`](OPEN_DECISIONS.md).
Order of work: [`ROADMAP.md`](ROADMAP.md).

## 1. One program, free, financed by donations (Owner, 12 Oct 2026)

**There is only one SamNPlayer.** It is free freeware with an EULA and is
financed by voluntary donations. **There is no paid version**, no licence key,
no price and no supporter tier. "SamNPlayer Next" is the name of the **next
generation** of that same program, built in this repo.

| Build | What it is | Where |
|---|---|---|
| **SamNPlayer** (default build) | The program everyone downloads. Free, donations. | this repo, no build tag |
| **SamNPlayer Suite** | Internal test build (AI training, golden-clip benchmark) | this repo, `-tags suite` |

**Superseded in the archived docs:** everything about a paid "Studio" / "Next"
product — prices, the >40 € supporter right, the licence pass, payment
providers, checkout, Studio previews with expiry, and the plan to cut AI and
batch from a "Free" edition. Those parts of `sources/` are history only.

## 2. Fixed rules

1. **Everything is free.** Every feature built here ships to everyone, unless
   the Owner keeps it in the internal Suite build. Donations give no extra
   rights (EULA §6 unchanged).
2. **Safety first.** Emergency stop and device caps work in every build, always.
3. **Classical first, AI optional.** Every AI result is checked by an
   independent verifier against video evidence, timebase and device limits.
   Without a model, the classical pipeline still works.
4. **Formats:** `.funscript` stays the universal export; `.samn` is extended
   backwards-compatibly only. No invented device axes.
5. **Go is the core.** Rust/C++ only for measured hotspots with golden-clip
   parity. No rewrite without a benchmark win.
6. **Local and private.** No telemetry, no hidden uploads, learning is opt-in
   with export and delete.
7. **No empty buttons, no unproven claims.** Nothing is shown or announced
   before it works and is measured.
8. **Users keep their data.** Updates never break existing `.samn`,
   `.funscript`, projects or settings.

## 3. What Next adds (scope)

Ranked by the order we can prove it. IDs from the archived docs.

| Rank | Feature | IDs | Why first |
|---|---|---|---|
| 1 | **Smart Batch** — persistent queue, resume after crash, per-clip/scene retry, reports | STUDIO-A2, A6, STU-01/02, BATCH-01 | Clear value, builds on the existing single-clip pipeline and the existing in-UI queue, easy to test |
| 2 | **AutoDirector** — fast scan, per-scene method choice, selective retry, verifier | STUDIO-A1, A3, A5, ENG-02..04, AUTO-01 | Core quality lever; needs the benchmark harness first |
| 3 | **Pro multi-axis editor** — PTS timeline, transactions, undo journal, transforms | EDIT-00..08, ED-01..10 | Parity with FunGen/OFS |
| 4 | **Device optimizer** — capability-aware projection, mock + real preview | STUDIO-A4, D2–D5, EDIT-07 | Builds on the device work already landing (SamNPlayer #526, #527) |
| 5 | Local AI assist (LM Studio / ONNX), learning in shadow mode | ENG-05, AUTO-02, STUDIO-B3 | Only after verifier and goldens exist |
| later | Multi-target, hardware profiler, VR, plugin SDK | B2, B5, C1, SDK-01 | Later |

## 4. Architecture (target)

```text
Wails UI -> Go Job API
  -> VideoProbe / FrameCache (FFmpeg, PTS-aware)
  -> SceneScanner (classical)
  -> BackendRegistry [legacy Python | native Go | native Rust/C++]
  -> CandidateScorer -> SelectiveRetry -> CurveComposer
  -> optional LocalAIAdapter -> IndependentVerifier
  -> .samn + .funscript -> Device Adapter -> Safety Governor
```

Contracts (`TrackResult`, `DecisionResult`, `CurveSegment`, `JobState`,
`EditCommand`, `AxisDescriptor`, `TimelinePosition` in integer µs PTS) are
specified in `sources/FREE_TO_STUDIO_PRODUCT_PLAN_2026-10-12.md` §4 and
`sources/STUDIO_EDITOR_COMPETITIVE_BLUEPRINT_2026-10-12.md` §1.3. They are
proposals; each becomes binding when its PR lands with tests.

Batch is orchestration over the same single-clip pipeline, never a second
generator.

## 5. This repo and the old repo

1. This repo holds the full history of `funfunpayer/SamNPlayer` `main` up to
   `5d2f625` (imported 12 Oct 2026, 643 commits).
2. The old repo is not touched. What happens to it is decided later (D12).
   Until then, fixes that land there are merged in here (`git merge` of its
   `main` — cheap, shared history).
3. Releases: the inherited `release.yml` is disabled here until the release
   source is decided ([N1](specs/N1_RELEASES.md)).

## 6. Test levels

L0 static (Go/Python/JS/Rust tests, lint, licence notices) · L1 deterministic
fixtures · L2 golden clips (local, manifests only in git) · L3 local AI with a
mocked OpenAI-compatible server · L4 device mocks (Neo 2, Handy, multi-axis,
no device, disconnect, e-stop) · L5 native Windows 11 Wails at 1366×768 and
1920×1080 · L6 performance bake-off (3 warm + 5 measured runs, median/p95,
peak RAM, no quality regression). Details:
`sources/NEXT_LEVEL_REPOSITORY_BOOTSTRAP_2026-10-12.md`.

## 7. Donations

Unchanged from SamNPlayer: Donate in the app and on the site
(`legal.PatreonURL` / `legal.PayPalURL`), voluntary, no rights attached.

## 8. Roles

- **Claude:** Go/native core, wiring, tests, CI, release.
- **ChatGPT:** visuals, copy, docs, website on its own branches; never pushes to `claude/*`.
- **Owner:** decisions, real hardware, Windows acceptance, tags.
