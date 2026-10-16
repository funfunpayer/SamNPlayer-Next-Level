# Flow theme + Play layout v2 — QA / acceptance checklist

Companion to `docs/BRAND_FLOW_UI_DIRECTION_2026-10.md`, `docs/design/flow-theme-tokens.css`, `docs/design/play-layout-v2-proposal.css`, `docs/design/generator-layout-proposal.css`, `docs/design/training-layout-proposal.css`, `docs/design/tools-layout-proposal.css` and `docs/PLAY_UI_CONTRACT.md`.

**Status:** proposal for Claude's implementation PR (branch `claude/keen-davinci-t2ysrs`). No assets or frontend changes in this file.

## Pre-merge checks (Claude PR)

- [ ] Tokens mapped app-wide onto existing `:root` / theme vars (`--bg`, `--surface`, `--accent`, `--text`, `--border`, …). Cyan primary `#00d9f4`, orange secondary `#ff8a23`, navy `#06121f`.
- [ ] Axis colours distinguishable: stroke orange `#ff8a23`, vibration cyan-blue `#00bfe9`, suction violet `#ad66f2`. Contrast checked on navy surfaces.
- [ ] Play layout uses grid areas / spacing only; no thick boxed borders. Video dominant.
- [ ] Empty state: branded abstract waves / logo, **no implied video content**. One primary button.
- [ ] Tools column collapsible; slim strip + toggle button remains visible when folded (T key still works). Persistence if already implemented.
- [ ] Responsive: core view (video + transport + timeline) fits 1366×768 without horizontal scroll or critical overlap. Usable at 1920×1080.
- [ ] `prefers-reduced-motion` respected; focus rings visible (`:focus-visible`).
- [ ] No ID / selector / JS binding changes. Existing tests pass: `pb_tools_toggle_test.py`, `pb_axis_lanes_test.py` and any others touched.
- [ ] Keyboard navigation and screen-reader labels intact.
- [ ] Screenshots at 1366×768 and 1920×1080 (empty + loaded, tools open/folded) attached to the PR.

## Asset integration (separate small PR)

- [ ] Exact owner-approved 1024 PNG + multi-res ICO used (no redraw).
- [ ] Legibility verified at 16×16, 32×32 and Windows taskbar.
- [ ] Placement: `build/appicon.png`, `build/windows/icon.ico`, `frontend/src/assets/images/logo-universal.png`, website media as coordinated.
- [ ] In-app tagline remains **Generate · Play · Train** (dots). Top nav **Play / Generator / Training / Tools**. Scene Map stays inside Generator until Owner decision.

## Post-merge visual QA (Owner / ChatGPT)

- [ ] Wave motifs low-opacity, never behind text.
- [ ] Generator / Training / Tools / Settings reuse the same spacing, card and button tokens; no fictional controls. See lightweight proposals in `docs/design/generator-layout-proposal.css`, `docs/design/training-layout-proposal.css`, `docs/design/tools-layout-proposal.css` and `docs/design/OTHER_TABS_NOTES.md`.
- [ ] Multi-axis lanes retain shared playhead, legend, zoom; colours match tokens.
- [ ] No regression in device controls, script loading or existing bindings.

**Next after this checklist is green:** further polish notes if needed; assets integration once Owner pastes binaries to #477.
