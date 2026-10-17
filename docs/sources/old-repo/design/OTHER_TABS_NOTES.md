# Flow theme proposals for Generator, Training and Tools

Lightweight, non-imported CSS proposals that reuse the locked Flow tokens
(`docs/design/flow-theme-tokens.css`) and the airy layout principles from
`docs/BRAND_FLOW_UI_DIRECTION_2026-10.md`.

**Status:** design notes only. Claude implements app-wide token mapping;
these files show how Generator (`#tab-generator`), Training (`#tab-training`)
and Tools/Settings (`#tab-tools` / `#tab-settings`) can adopt the same
surfaces, spacing, radii and accents without inventing controls or changing
IDs/JS.

- Generator: step panels, cards and primary buttons map to Flow tokens.
  Scene Map remains inside Generator (Advanced) until a separate Owner decision.
- Training: cards, progress indicators and visual areas follow the same tokens.
  No new fictional features.
- Tools / Settings: cards, sections, primary buttons and the existing
  `#tools-nav` (Device / Log / Settings) reuse the same tokens. No new
  fictional controls.

See also `docs/design/generator-layout-proposal.css`,
`docs/design/training-layout-proposal.css` and
`docs/design/tools-layout-proposal.css`.

**Coordination:** same branch ownership as the rest of the design-spec PR.
Assets still pending Owner paste to #477.
