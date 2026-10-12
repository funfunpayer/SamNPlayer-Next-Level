# Session handoff — 23 Sep 2026 (Cursor Cloud → next session)

Paste this into a new Cursor session as context. Repo: `funfunpayer/SamNPlayer`. Base: `main`.

## Product

- Product name: **Emotion Script** (`.samn`); Create/Play prefer `.samn`.
- Generate stroke: **1-Zone Tip CSRT** only in GUI (4-Zone backend stays, hidden).
- Feel Stage A already shipped (v0.5.27): Contact vib + marks + tip trajectory → Play can buzz near marks.

## What Cursor shipped this session

| Item | PR / tag | Notes |
|------|----------|--------|
| Emotion GUI look | #225 | Sora + Figtree VF, warmer ink, EN Create/Play copy |
| Create→Play feel bugfix | #227 | `.samn` keeps recipe / `contactMarks` / tip `trajectory`; Create busy/overwrite/cancel; sidebar empty |
| Rel28 | #228 + **tag `v0.5.28`** | Portable live: https://github.com/funfunpayer/SamNPlayer/releases/tag/v0.5.28 |
| Board hygiene | #229 | Rel28 DONE on AGENT_COORD / ROADMAP |

Stack merge order used: **#227 → #225 → #228**, then tag. After #227 squash, #225/#228 needed rebase onto main.

## What Claude shipped (do not re-do)

| Round | PR | What |
|-------|-----|------|
| 1 | #226 | `dispGuard` + appearance-memory gating (tracker). Active correction tried + **reverted** (made worse). |
| 2 | #230 | **Adaptive detrend default** for stroke profiles (2× stroke period from pre-pass tempo, clamp 1.5–4s, fallback 3s). Applied in `GenerateWithContext` **before** Go/Python fork → both paths. **Tf/Tj excluded**. Opt-out: `DetrendWindowMs < 0`. |

### #230 measured win (`clip_voll`, real Go generator vs FunGen)

- windowed r: 0.275/0.261 → **0.386/0.552** (ohne/mit YOLO)
- stuck 10s windows (<25pt band): **62% → 3%**
- Rejected: CSRT scale lock; slower `filter_lr`

### Claude’s open proposal (Claude owns `trackcv` lane)

1. **Rhythm-grid drift check** — per-cell energy at stroke frequency; flag when box sits in weak-rhythm cell next to strong one. Better than template matching on skin.
2. Later: frames where CSRT + rhythm-grid agree → auto-label YOLO training data.

**Owner decision already given:** Claude starts rhythm-grid; Cursor does **not** steal that lane. Cursor reviews when PR opens.

## Current board intent (as of post-#229)

- **Shipped:** v0.5.28 (+ #230 on main post-tag — still under CHANGELOG Unreleased until Rel29).
- **Claude THIS:** rhythm-grid in `trackcv` only.
- **Cursor:** Rel28 DONE; engine free **except** Claude’s rhythm-grid lane.
- Sensible Cursor next: **Rel29** (bump to include #230 detrend) and/or review Claude’s rhythm-grid PR; other free engine/GUI/docs — claim in `docs/AGENT_COORD.md` first.

## Order / rules that mattered

1. Locked earlier: bugfix → Rel28 → drift. Drift wait while Claude worked.
2. Never silent Everyday/tracker default changes without CHANGELOG + board note (#230 did this correctly).
3. One theme per agent; claim Active before coding; don’t force-push another agent’s tip.
4. Stroke stays Go tip-CSRT.

## Key files

- Coord: `docs/AGENT_COORD.md`
- Roadmap: `docs/PRODUCTION_ROADMAP.md`
- Version: `VERSION` + `update.BaseVersion` (= `0.5.28` now)
- Detrend default: `generator/detrend_default.go` (+ wire in `generator/generator.go`)
- Tracker guards: `generator/trackcv/dispguard.go`, `appearance_memory.go`, `track.go`
- Feel/samn: `samn/` Document + convert/export; GUI Create/Play paths

## Open non-drift items (not Cursor-claimed this session)

- #223 site Emotion face (open)
- #222 ChatGPT copy overwrite targets (open)
- #224 draft coord face (draft)
- Untracked leftover sometimes seen: `website/media/gui-training.png` — ignore unless site work

## Do next in new session (suggested)

1. `git fetch origin main && git checkout main && git pull`
2. Read Active table in `docs/AGENT_COORD.md`
3. If Claude rhythm-grid PR exists → review only (don’t rewrite)
4. Else Cursor options: claim **Rel29** (`VERSION`/`BaseVersion` → 0.5.29, CHANGELOG move #230 Unreleased → 0.5.29) **or** other free lane after Owner ask
5. Do **not** start rhythm-grid / YOLO auto-label unless Owner reassigns

## One-liner for the new agent

> v0.5.28 is shipped (look + samn feel + CSRT guards). Claude #230 adaptive detrend is on main (post-tag). Claude starts rhythm-grid drift check in trackcv; Cursor free elsewhere — prefer Rel29 to ship #230, review Claude’s PR when open. Stick to Claude’s measured findings; don’t re-attempt active tracker correction.
