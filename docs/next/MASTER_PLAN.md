# SamNPlayer Next — master plan

**Status:** preparation · 12 Oct 2026 · single source of truth for this repository.
The five planning documents from SamNPlayer PR #510 are archived unchanged in
[`sources/`](sources/). Where they disagree with each other, this file records
which one wins. Open Owner decisions: [`OPEN_DECISIONS.md`](OPEN_DECISIONS.md).
Order of work: [`ROADMAP.md`](ROADMAP.md).

## 1. Product lines

| Name | What it is | Where |
|---|---|---|
| **SamNPlayer Free** (Community) | Public freeware, EULA, donations. "Free. Reliable. Community-supported." | `funfunpayer/SamNPlayer`, default build |
| **SamNPlayer Suite** | Internal test edition (AI training, golden-clip benchmark) | `funfunpayer/SamNPlayer`, `-tags suite` |
| **SamNPlayer Next** | Future commercial product. Suite features move into it. Licence system. | **this repo** (public for now, Owner 12 Oct — see D11) |

"Studio" in the archived documents means **Next**. It does not mean the old
`-tags studio` build (renamed to Suite in SamNPlayer #513).

**Superseded:** `FREE_TO_STUDIO_PRODUCT_PLAN` says "one program, not a second
app". The later Owner decision (`FREE_NEXT_LEVEL_SCOPE`) replaced that: Next is
a separate software generation in its own repo, derived from Free with full git
history.

## 2. Fixed rules (Owner)

1. **Free first.** No Next work may delay the Free release. Next starts building
   code after the Free release (FREE-07). Docs and specs may be prepared now.
2. **Free stays free and good.** Safety (emergency stop, device caps) is never
   gated, in any edition, by licence or trial expiry.
3. **Classical first, AI optional.** Every AI result is checked by an
   independent verifier against video evidence, timebase and device limits.
   Without a model, the classical pipeline still works.
4. **Formats:** `.funscript` stays the universal export; `.samn` is extended
   backwards-compatibly only. No invented device axes.
5. **Go is the core.** Rust/C++ only for measured hotspots with golden-clip
   parity. No rewrite without a benchmark win.
6. **Local and private.** No telemetry, no hidden uploads, learning is opt-in
   with export and delete.
7. **No empty buttons, no unproven claims.** Nothing is marketed or shown before
   it works and is measured.
8. **Licence system** belongs to Next only, after legal/payment review.

## 3. What Next adds (scope)

Ranked by the order we can prove and sell it. IDs from the archived docs.

| Rank | Feature | IDs | Why first |
|---|---|---|---|
| 1 | **Smart Batch** — persistent queue, resume after crash, per-clip/scene retry, reports | STUDIO-A2, A6, STU-01/02, BATCH-01 | Clear value, builds on the existing single-clip pipeline, easy to test |
| 2 | **AutoDirector** — fast scan, per-scene method choice, selective retry, verifier | STUDIO-A1, A3, A5, ENG-02..04, AUTO-01 | Core quality lever; needs the benchmark harness first |
| 3 | **Pro multi-axis editor** — PTS timeline, transactions, undo journal, transforms | EDIT-00..08, ED-01..10 | Market parity with FunGen/OFS |
| 4 | **Device optimizer** — capability-aware projection, mock + real preview | STUDIO-A4, D2–D5, EDIT-07 | Needs device support work from Free |
| 5 | Local AI assist (LM Studio / ONNX), learning in shadow mode | ENG-05, AUTO-02, STUDIO-B3 | Only after verifier and goldens exist |
| later | Multi-target, hardware profiler, VR, plugin SDK | B2, B5, C1, SDK-01 | After launch |

**Free keeps** single-video Create/Edit/Play/Training, classical generator,
standard import/export, device safety. The later Free scope-down (less AI, no
batch) happens only with explicit Owner go-ahead, a feature manifest, and
non-destructive migration — see `sources/FREE_NEXT_LEVEL_SCOPE_2026-10-12.md`.

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

## 5. Repository relationship with Free

1. Next is created from Free `main` **with full history** (`scripts/import-free-history.sh`).
2. Until Next diverges (gate N2), Free `main` is merged into Next regularly —
   cheap because history is shared.
3. After divergence: Free bug fixes are cherry-picked with tests and a
   `(cherry picked from …)` line. No automatic two-way merges.
4. Separate release channel and updater asset names per edition; Free never
   updates into Next and vice versa (as today for Free/Suite, SamNPlayer #506).
5. Suite development stops in the Free repo once Next is bootstrapped
   (see `OPEN_DECISIONS.md` D6).

## 6. Test levels

L0 static (Go/Python/JS/Rust tests, lint, licence notices) · L1 deterministic
fixtures · L2 golden clips (local, manifests only in git) · L3 local AI with a
mocked OpenAI-compatible server · L4 device mocks (Neo 2, Handy, multi-axis,
no device, disconnect, e-stop) · L5 native Windows 11 Wails at 1366×768 and
1920×1080 · L6 performance bake-off (3 warm + 5 measured runs, median/p95,
peak RAM, no quality regression) · L7 commercial (checkout, refund, offline
entitlement). Details: `sources/NEXT_LEVEL_REPOSITORY_BOOTSTRAP_2026-10-12.md`.

## 7. Commercial

- Donations in Free stay voluntary and give no rights (EULA §6).
- A guaranteed Next licence for supporters must be a **purchase** with its own
  terms, invoice, VAT and refund path — not a donation. No advertising or
  payment for it before those terms exist.
- Entitlement: signed offline pass (Ed25519, public key in the app, private key
  offline with the Owner), no online check at every start. The pre-10-Oct key
  design in SamNPlayer history (`533457e`, `license/`) is a starting point; its
  DEV key must **not** be reused.
- Payment provider must accept adult-adjacent software in writing before any
  integration.
- Price, threshold and upgrade policy: `OPEN_DECISIONS.md` D1–D4.

## 8. Roles

- **Claude:** Go/native core, wiring, tests, CI, release.
- **ChatGPT:** visuals, copy, docs, website on its own branches; never pushes to `claude/*`.
- **Owner:** decisions, real hardware, Windows acceptance, legal/payment, tags.
