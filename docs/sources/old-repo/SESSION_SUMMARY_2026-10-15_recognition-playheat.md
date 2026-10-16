# Session summary — Recognition / PlayHeatUX (to 15 Oct 2026)

Paste into a new Cursor session if needed. Repo: `funfunpayer/SamNPlayer`.  
**Status:** parked — **waiting on Owner**. No feature lane in flight.

## Product rules that stayed locked

- Everyday Create stays **Go tip-CSRT**. No Everyday / Rhythm / AI **default** flips.
- Partner mark = optional Contact vibe, not a second stroke writer.
- Silent tip “Ausfallcode” (KCF/MIL soft-fallback, scene reuse, preferred-class any-pick) was removed on purpose; intentional variants stay.

## What shipped (this lane)

| Item | Where | Notes |
|------|--------|--------|
| PlayHeatUX / G4.C | [#460](https://github.com/funfunpayer/SamNPlayer/pull/460) → tag [`v0.5.45`](https://github.com/funfunpayer/SamNPlayer/releases/tag/v0.5.45) (`75fb1ca`) | Taller Play curve/heatmap; Tip→Partner snack; progressive disclosure; C7 Find tip / Play load retry |
| Recognition honesty | same PR | Python tracker **CSRT-only**; cut/scene/preferred-class **fail-closed**; dead `_legacy_threshold_box` removed |
| Cancel / seq hygiene | same PR | `genSeq` / `roiSeq` + `CommandContext`; tip-find / candidates / AI tags / contact-points cancellable; FE drops stale ROI |
| Soft-start idle | `player/sync.go` | Pause/idle during soft-start zeros immediately (`goQuiet` no longer no-ops mid-ramp); FE `ReportVideoSyncIdle` on pause/waiting |
| Tip coast | tip track path | Brief tracker loss **velocity-coasts** (same short budget as Tf/Tj partners) before hold |
| Partner snack fix | FE Create | Highlight Mark contact (`highlightPartnerMark`) — **do not** auto-arm gold paint via `setRoi2Mode` (that broke tip body-map paint) |
| Rel45 board | [#461](https://github.com/funfunpayer/SamNPlayer/pull/461) | PlayHeatUX / Rel45 marked DONE on `AGENT_COORD` |

Owner waived portable smoke / goldens / Rel45 gate for the “im Kern weiter bauen und bugfix” pass. Later main moved on (modes, v0.7.0, v0.8 script folders / stroker filter) — **this summary does not claim those lanes**.

## Recognition block — mental model (Create Step 2)

1. **Tip** = CSRT stroke box only (Find tip / paint tip).  
2. **Partner** = optional gold contact mark for vibe (snack guides Tip → Partner → More).  
3. Cancel any in-flight ROI / tip-find / AI tag / contact-points run; unlock Create; status stays English and friendly (raw errors → log).

Key FE helpers (names may evolve — search if renamed): `cancelAnyRoiRequest`, `unlockRecognitionControls`, `highlightPartnerMark`.

## Key files (as of #460)

| Area | Paths |
|------|--------|
| Play soft-start / idle | `player/sync.go` (+ soft-start idle test) |
| Tip coast / track | `generator/trackcv/` (tip path), Python CSRT-only create_tracker |
| Cuts / scenes / preferred class | track cut, `track_by_scenes`, preferred-class fail-closed |
| Create cancel / ROI | generator cancel + `FindROI*WithContext`, `tipFindBusy`, contact-points Cancel |
| FE recognition | `frontend/.../generator.js` (snack, unlock, partner highlight) |
| Changelog | `CHANGELOG.md` § `[0.5.45]` |
| Board | `docs/AGENT_COORD.md` Active → PlayHeatUX / Rel45 **DONE** |

## Explicitly not done / not started

- No new Everyday CSRT / Rhythm / AI default changes.
- No Owner smoke on `v0.5.45` (waived for that pass; still listed Owner-gated if wanted later).
- No new feature lane after Owner: *aufräumen · dokumentieren · zusammenfassen · ins Repo · warten*.

## Board intent after this docs PR

- **In flight:** none. Wait for Owner.
- **Preserve:** ChatGPT BugE / E-steward; ClaudePath OK CONTINUE; Docs521 if still open.
- **Do not** flip Everyday / Rhythm / AI defaults without Owner + CHANGELOG.

## One-liner for the next agent

> PlayHeatUX / recognition honesty / cancel+soft-start fixes are on main via #460 (`v0.5.45`). Partner snack uses highlight-only, not `setRoi2Mode`. Cursor is **parked waiting on Owner** — read `docs/AGENT_COORD.md` tip board before claiming anything.
