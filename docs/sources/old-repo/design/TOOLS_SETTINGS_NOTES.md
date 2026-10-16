# Flow theme notes for Tools / Settings / Device / Log

Lightweight notes only. Reuses locked Flow tokens from `docs/design/flow-theme-tokens.css`
and airy principles from `docs/BRAND_FLOW_UI_DIRECTION_2026-10.md`.

**Status:** design notes. Claude maps tokens app-wide in the implementation PR
(`claude/keen-davinci-t2ysrs`). No ID/JS changes. No fictional controls.

## Tools tab (`#tab-tools` or via `#tools-nav`)
- Device, Log, Settings share the same surface, card, button and spacing tokens.
- Device status cards: cyan for connected/online, orange for warnings/searching, keep existing LED semantics.
- Settings sections: About, Play options, Generate options, etc. reuse card style.
- Log viewer: mono font preserved; subtle border only.

## Coordination
- Assets (PNG/ICO) still pending Owner paste to #477 or chat for Claude.
- Once Claude's token + Play layout v2 PR is open, visual QA against the checklist in `docs/design/FLOW_THEME_QA_CHECKLIST.md`.
- Scene Map remains inside Generator until Owner decision.
- Public site alignment only after source of truth assets are committed and reviewed.

**Next after Claude implementation lands:** screenshot QA and any token fine-tuning notes.