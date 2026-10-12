# Roadmap — gates, not dates

A gate is passed when its checks are green and recorded in the PR. Each work
package is its own PR with tests. Plan and rules: [`MASTER_PLAN.md`](MASTER_PLAN.md).

## P — Preparation (now, while Free is finished)

Docs and specs only. No code in this repo yet, no changes to Free.

| ID | Deliverable | Status |
|---|---|---|
| P1 | Master plan, roadmap, open decisions, archived #510 docs | ✅ this PR |
| P2 | Bootstrap script + history audit of Free `main` | ✅ script; audit 12 Oct: 626 commits, 23 MB pack, no key files, no `PRIVATE KEY`, no token patterns, no video/model blobs |
| P3 | Owner answers `OPEN_DECISIONS.md` D1–D8 | open |
| P4 | Spec: golden-clip manifest schema + benchmark report format (input to ENG-01) | open |
| P5 | Spec: `.samn` v2 axis map + companion-file round trip (input to EDIT-01) | open |
| P6 | Spec: batch job state machine + on-disk checkpoint format (input to STU-01) | open |
| P7 | Spec: entitlement threat model (offline pass, refund, device change, lost pass) | open |

## N0 — Free released

Trigger to start code. Free tag published (FREE-07 in SamNPlayer), Free CI green.

## N1 — Import

- Run `scripts/import-free-history.sh` (full history, `main` only).
- CI from Free runs green here; edition `next` added next to `free`/`suite`
  (build tag, updater asset prefix `SamNPlayer-next-…`, never published to the public site).
- `.samn` / `.funscript` round-trip tests pass.
- GitHub secret scanning on.

## N2 — Measure the baseline

| ID | Deliverable |
|---|---|
| ENG-01 | Python runtime inventory, benchmark harness, golden report on the Owner's local clips |
| ENG-02 | Go backend/track contract, fast scan — **output unchanged** vs baseline |
| EDIT-00 | Inventory of current Edit/Play controls and `.samn` state vs blueprint |

Free and Next diverge from here; switch to cherry-picks.

## N3 — First sellable slice: Smart Batch

| ID | Deliverable |
|---|---|
| STU-01 | Batch UI + persistent queue, pause/resume/cancel, error report |
| STUDIO-A6 | Crash resume, atomic results |
| STU-02 | 20-clip acceptance (VFR, no audio, broken file, Unicode name, read-only target) |

## N4 — AutoDirector

ENG-03 per-scene candidates + independent verifier · ENG-04 selective retry,
no quality regression · STUDIO-A3 compare/accept/reject.

## N5 — Editor

EDIT-01 schema · EDIT-02 transactions + undo journal (1k-op property tests) ·
EDIT-03 PTS timeline · EDIT-04 transforms · EDIT-05 inspector · ED-01..10 green.

## N6 — Devices + AI

D2–D5 device adapters with mocks · EDIT-07 device preview · ENG-05 / AUTO-02
optional local AI with classical fallback · Windows 11 hardware smoke.

## N7 — Commercial go/no-go

STU-03 price/terms/VAT/provider · STU-04 offline entitlement gates (backend, not
CSS) · STU-05 landing page with honest feature list · STU-06 release. Fair
FunGen comparison on identical clips before any comparative claim.
