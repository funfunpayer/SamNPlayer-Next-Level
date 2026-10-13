# Issue #290 — SceneMap P5 learning-contract audit

**Verdict: closed (completed).** Fixed on tip; board hygiene landed via [#354](https://github.com/funfunpayer/SamNPlayer/pull/354).

| Field | Value |
|---|---|
| Issue | [#290](https://github.com/funfunpayer/SamNPlayer/issues/290) — *audit: finish SceneMap P5 learning contract before L2/L3* |
| Tip at close | `main` @ `2a69b9d` ([#354](https://github.com/funfunpayer/SamNPlayer/pull/354)) |
| Prior tip check | `27d7963` (v0.5.38 / [#353](https://github.com/funfunpayer/SamNPlayer/pull/353)) |
| Status | **Closed** (`state_reason=completed`, 2026-09-28T12:11:46Z) via `Closes #290` |
| Merge SHA | `2a69b9dcef80423a81d27d89fba8359807f31582` |

---

## What #290 asked for

Gap inventory after P5a/P5b:

1. **P5c** — reviewed region → YOLO frame+txt (never train on `reviewed:false`)
2. **P5d** — auto-label candidates only when agreement rule met; stay `author:auto, reviewed:false`
3. **P5e** — richer per-window engine-trace JSONL

Non-goals: no Rhythm/AI/Everyday default flips; no L3 model.

---

## Tip evidence

| Slice | Outcome | Evidence |
|---|---|---|
| Audit itself | DONE | ChatGPT E-learnAudit: true missing slice = P5c; trace/auto/negatives/user-region JSON already on tip |
| **P5c** | DONE | [#297](https://github.com/funfunpayer/SamNPlayer/pull/297) squash `c51bb67` — `reviewedYOLOMarks` / `scene_map_learning/.../reviewed_yolo/` |
| P5c multi-box | DONE | [#326](https://github.com/funfunpayer/SamNPlayer/pull/326) @ `56a0ebf` |
| Reviewed/Confidence GUI round-trip | DONE | [#304](https://github.com/funfunpayer/SamNPlayer/pull/304) @ `05af0dd` (follow-up: no duplicate needed) |
| **P5e** | Parked by design | Useful `engine_trace.jsonl` already from P5a; ChatGPT + `docs/SCENE_MAP_PLAN.md`: “P5e parked” |
| **P5d** | Covered / not duplicated | `auto_candidates.jsonl` + `author:auto, reviewed:false`; YOLO gate remains reviewed-only |

Plan on tip: **P5a–P5c done**. Next is M3 / Owner rhythm clips before P6 — out of #290 scope.

Local check: `go test ./generator/ -run 'Reviewed|YOLO|ExportLearning|SceneMapExport|reviewed'` → ok.

---

## Close path

| Action | Result |
|---|---|
| Issue comment / PATCH close (pre-merge) | **403** — agent lacked issue-write; closed via PR instead |
| Docs PR [#354](https://github.com/funfunpayer/SamNPlayer/pull/354) CI | **green** (10/10) @ `bc11290` |
| Squash-merge | **merged** → SHA `2a69b9dcef80423a81d27d89fba8359807f31582` |
| Auto-close `Closes #290` | **worked** — issue `closed` / `completed`; no API retry needed |

Evidence table for the close is in the [#354](https://github.com/funfunpayer/SamNPlayer/pull/354) description.

---

## Constraints respected

- Everyday Go CSRT base untouched
- No #264 / Virtual Person work (cancelled / out of scope)
- No force-close without evidence (evidence above + ancestors of tip)

---

## Owner one-liner

**#290 is closed.** P5 learning contract for L0/L1 collect was already on tip; [#354](https://github.com/funfunpayer/SamNPlayer/pull/354) @ `2a69b9d` closed the issue via `Closes #290`.
