# Roadmap — gates, not dates

A gate is passed when its checks are green and recorded in the PR. Each work
package is its own PR with tests. Plan and rules: [`MASTER_PLAN.md`](MASTER_PLAN.md).
One program, free, donations (Owner, 12 Oct) — there is no commercial gate.

## P — Preparation ✅

| ID | Deliverable | Status |
|---|---|---|
| P1 | Master plan, roadmap, open decisions, archived #510 docs | ✅ |
| P2 | Import script + history audit | ✅ 638 commits, no tokens, no video/model blobs. **One key file:** the DEV licence issuer key `license/testdata/issuer.ed25519` from the removed licence system (D10) |
| P3 | Owner answers [`OPEN_DECISIONS.md`](OPEN_DECISIONS.md) | partly answered 12 Oct |
| P4 | [Spec: golden-clip manifest + benchmark report](specs/P4_GOLDEN_BENCHMARK.md) | ✅ draft |
| P5 | [Spec: `.samn` v2 axis map + companion files](specs/P5_SAMN_V2_AXES.md) | ✅ draft |
| P6 | [Spec: batch job states + checkpoints](specs/P6_BATCH_JOBS.md) | ✅ draft |
| P7 | [Licence pass](specs/P7_ENTITLEMENT_THREAT_MODEL.md) | ⛔ not needed — no paid version |
| P8 | [Spec: releases and updater from this repo](specs/N1_RELEASES.md) | ✅ draft |
| P9 | [Inventory EDIT-00: editor today vs target](inventory/EDIT-00_EDITOR.md) | ✅ |
| P10 | [Inventory ENG-01 part 1: where Python still runs](inventory/ENG-01_PYTHON_RUNTIME.md) | ✅ static part; timings at N2 |

## N0 — Import ✅

Free release no longer a precondition (Owner, 12 Oct: one program).
SamNPlayer `main` `5d2f625` imported with full history; inherited `release.yml`
disabled here.

## N1 — Green and releasable from here

- Inherited CI green in this repo.
- [N1 spec](specs/N1_RELEASES.md): updater and release source point at the
  release repo the Owner picks; Suite files never in a public release.
- GitHub secret scanning on (Owner, repo settings).

## N2 — Measure the baseline

| ID | Deliverable |
|---|---|
| ENG-01 part 2 | Benchmark harness (P4), golden report on the Owner's local clips |
| ENG-02 | Go backend/track contract, fast scan — **output unchanged** vs baseline |
| — | Split the Python embed: training scripts only in Suite (ENG-01 inventory §4) |

## N3 — Smart Batch

| ID | Deliverable |
|---|---|
| STU-01 | Go job queue + batch UI, pause/resume/cancel, error report (P6) |
| STUDIO-A6 | Crash resume, atomic results |
| STU-02 | 20-clip acceptance (VFR, no audio, broken file, Unicode name, read-only target) |

## N4 — AutoDirector

ENG-03 per-scene candidates + independent verifier · ENG-04 selective retry,
no quality regression · STUDIO-A3 compare/accept/reject.

## N5 — Editor

EDIT-02 history in Go first (see EDIT-00) · EDIT-01 schema (P5) · EDIT-03 PTS
timeline · EDIT-04 transforms · EDIT-05 inspector · ED-01..10 green.

## N6 — Devices + AI

D2–D5 device adapters with mocks · EDIT-07 device preview · ENG-05 / AUTO-02
optional local AI with classical fallback · Windows 11 hardware smoke.

## N7 — Release

Version bump, changelog, handbook, screenshots from the real build, QA at
1366×768 and 1920×1080, site update. Fair FunGen comparison on identical clips
before any comparative claim.
