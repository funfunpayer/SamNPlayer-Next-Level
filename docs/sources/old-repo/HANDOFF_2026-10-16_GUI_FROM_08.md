# Handoff · 16 Oct 2026 — GUI / Flow from SamNPlayer 0.8 → SamNPlayer 2 (Next)

**Audience:** agents working in [`funfunpayer/SamNPlayer-Next-Level`](https://github.com/funfunpayer/SamNPlayer-Next-Level) (C++ / Qt 6 / QML).  
**Source of truth for the look that shipped in the Go/Wails app:** PR [#550](https://github.com/funfunpayer/SamNPlayer/pull/550) on `funfunpayer/SamNPlayer`, branch `cursor/flow-silk-glow-2c42` (merged onto that branch with current `main` as of commit `657ac02`).  
**Owner language:** German with the Owner; English in code, UI strings, and docs.

This handoff is the **gesamt** transfer of the mid-October Flow/Player/Training GUI work so Next does **not** re-port an already-superseded empty Player.

---

## 1. What changed in the Go app (must mirror in Next)

### 1.1 Player empty home (Owner: “richtiger Playbutton … ohne den Kram”)

**Final intent (locked by Owner feedback 15 Oct):**

| Do | Don’t |
|---|---|
| One large circular **`.flow-play`** control, centered in the Player stage | Decorative silk “X” / Flow stage *behind* the empty card |
| Label **Open…** under the play mark (same click → open video/script) | Wordmark “SamNPlayer”, kicker “PLAY”, “Ready when you are”, long hint paragraph |
| Soft cyan→orange silk **halo on the button** (existing `.flow-play` ring) | Thin stroke-line SVG trails as the main empty graphic |
| Header **Open…** may stay quiet (secondary) | Treating silk ribbons as the primary empty CTA |

**Wiring (Go/Wails):**

- Markup: `cmd/gui-wails/frontend/src/playback.js` → `#pb-empty` / `#pb-choose-empty.pb-empty-cta`
- Open path: same as `#pb-choose` via `open_media.js` (`pickAndOpen`) and playback fallback `chooseScript`
- Layout CSS: `style.css` (`.pb-empty-cta`, `.pb-empty-open`, empty stage flex centering under `data-pb-state="empty"`)
- Look CSS: `theme_flow.css` (`.flow-play` circular Play + conic silk halo)

**Tests:** `cmd/gui-wails/frontend/test/flow_wiring_test.py` expects `.pb-empty-cta` + “Open” text and **no** `#pb-empty .flow-fx`.

### 1.2 Where Flow stage art still belongs

`mountFlowStage` (`flow_fx.js` / `flow_fx.css`) remains for **“Feel without film”** (`#pb-flow-novideo`) only — not the empty home.

Artwork assets (still used by training / chrome / novideo):

| Asset | Role |
|---|---|
| `assets/flow/stage-ribbons.svg` | Lush cyan↔orange ribbon bridge |
| `assets/flow/bg-top.svg` / `bg-bottom.svg` | App-chrome corner silk |
| `assets/images/logo-universal.png` | Brand squircle (novideo / marks) |

Intrinsic `img` width/height **168** on the flow mark so harnesses without `flow_fx.css` do not blow layout (`pb_axis_lanes_test`).

### 1.3 Training rings (mockup alignment)

- Soft circular Play (not logo squircle as the transport Play).
- Rings stage closer to `docs/visual-target/training.png`: glow gauges, silk between rings, center name/%/wave.
- Files: `theme_flow.css` (`.flow-tr__silk`, rings-stage), `training_rings.js` / related Training CSS.

### 1.4 Visual targets

Owner mockups live in the Go repo:

- `docs/visual-target/player.png`
- `docs/visual-target/training.png`
- (+ scene-map / tools / create)

---

## 2. Critical conflict with Next PR #18

Open PR in Next-Level: [#18 Player polish: Flow stage welcome…](https://github.com/funfunpayer/SamNPlayer-Next-Level/pull/18) (`claude/samn2-player-polish`).

It ports the **old** 0.7 empty Player:

- animated Flow stage (halo / ribbons / floating mark)
- wordmark + “PLAY” + “Ready when you are” + hint + big Open…

**That empty-state composition is superseded.** Do **not** merge #18’s empty welcome as-is without replacing the center with:

1. Large circular Flow Play button (QML equivalent of `.flow-play`)
2. **Open…** text under it
3. Centered in the Player main column
4. No decorative Flow stage / wordmark / title stack on empty home

Keep from #18 if still useful: 0–100 position gauge, video fullscreen, Flow icons in the top bar, SVG Tiny fixes — those are independent of the empty CTA.

Suggested follow-up lane name: **EmptyHomePlay** — rewrite `FlowStage.qml` usage on empty to the Play+Open CTA (or retire FlowStage from empty and keep it only for no-video / training silk).

---

## 3. Also landed on Go `main` (context for Next parity)

Already on `funfunpayer/SamNPlayer` `main` (via #547–#553 and related), relevant to Next core/parity:

| Area | Notes |
|---|---|
| Stroke motion filter | Smooth PCHIP / Rounder MAKIMA, speed cap, fill pauses, home on pause — Inspector Devices |
| Soft-start pause → home | Stop + home when pausing during soft-start ramp |
| Beat bar (Cock Hero / Fap Hero) | Auto-detect beat strip → one stroke per beat; `CreateBeatBarScript` |
| D14 / MEDIA-01 | Suite not published on public release; unknown MP4 codec not fake-h264 (#553) |
| `native/` ports | C++ ports + parity tests for beat bar / stroke filter / funscript pieces |
| Docs | `docs/REWRITE_PLAN_FAST.md`, `docs/MIGRATION_CPP_PYTHON.md`, generator profile |

Next already has stroke filter + beat bar in `core/` per `AGENT_STATE.md` — treat Go as the behavior reference when parity drifts.

---

## 4. Pointers in the Go repo

| Path | Why |
|---|---|
| PR [#550](https://github.com/funfunpayer/SamNPlayer/pull/550) | GUI FlowSilk / empty Play+Open |
| `docs/AGENT_COORD.md` lane **FlowSilk** | Board claim |
| `docs/next/HANDOFF_2026-10-15.md` | Engine handoff (N1 Suite, MEDIA-01, …) — partially done on main via #553 |
| `docs/next/OPEN_DECISIONS.md` | D12/D13/D6 still open; D14 addressed in release workflow |
| `docs/PLAY_UI_CONTRACT.md` | Play regions / state hooks |
| `CHANGELOG.md` Unreleased → Changed | Empty home wording |

---

## 5. Recommended next steps in SamNPlayer-Next-Level

1. **Read this file** + update `docs/AGENT_STATE.md` “Last update” and “Next” with EmptyHomePlay.
2. **Align empty Player** with §1.1 (block or amend #18 empty welcome).
3. **Training:** when porting Training UI, use soft circular Play + silk between rings (§1.3), not the old logo-as-Play experiment.
4. **Parity checklist:** tick empty-home against current Go `#pb-empty`, not against pre-#550 screenshots.
5. Do **not** force-push; PR-only to `main` (Next house rules in `AGENT_STATE.md`).

---

## 6. Smoke checklist (Next QML)

- [ ] Empty Player: only Play circle + “Open…”; click opens picker / video-first flow
- [ ] Cluster optically centered in Player main column (not top-hugging)
- [ ] No silk stage / SamN wordmark / “Ready when you are” on empty
- [ ] Loaded + no video: optional Flow stage still OK if product wants “Feel without film”
- [ ] Transport Play remains round Flow ring (not header squircle logo)
- [ ] `ctest` + app smoke green; no adult media in screenshots

---

## 7. Status of this Cursor lane

- Go PR #550: **IN REVIEW** (Owner smoke; Frontend CI watched after mark-size fix).
- This handoff: written 16 Oct 2026 for Next; no further Go GUI work claimed until Owner asks.
- Preview stubs (`vite.preview.config.mjs`, `preview-stubs/`) are **local-only** in the Go worktree — do not port them.

---

*End of handoff.*
