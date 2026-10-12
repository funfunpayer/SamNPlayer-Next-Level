# GUI makeup — professional polish ideas

Visual language: **dark ink + soft lilac accent + teal secondary** (`style.css`
tokens — Look v3, Owner 26 Sep: lila preferred over honey orange). Emotion fonts:
**Sora + Figtree**. No card-grid shell, no light mode.

## Shipped

### Look v1 (#225)
- Calmer rail / active tab; primary CTA; content vignette; quieter Play empty

### Look v2 (#259)
- Thinner chrome, organic radii, warmer honey accent (superseded by v3)

### Look v3 (this branch / #263)
- **Lilac accent** (`#b896e8`) replacing honey chrome — help chips / CTAs / washes / scrollbars / checkboxes
- Teal kept as secondary; warn stays warm amber for alerts
- **Motion:** tab in, Create head + step panel, help popover, handbook modal, rail breath, primary press, progress shimmer
- `prefers-reduced-motion` honored
- Figure heat ramp: teal → lilac → coral (Device / Training shared tokens)
- **In-app handbook** — Settings + Create open the same FAQ modal (`handbook.js`)
- Play tip trajectory overlay uses lilac (was honey)
- **Play bookmarks** — add at playhead / seek / remove (wires existing Go APIs)
- **Play chapters** — add from heatmap selection / seek / remove

## Ideas backlog (next slices)

| Idea | Why | Risk |
|------|-----|------|
| Play first viewport brand moment | Competitors judge players first | **#460:** wordmark + mark on empty Play; no stats/chips |
| Settings density | Pro tools look | **#460:** need-list + Logs behind closed details; license/runtime stay primary |
| Screenshot pack for README | Public face | Match Look v3 |## Explicit non-goals

- Frameless glass everywhere  
- Rounded-full pill clusters / emoji decoration  
- Dashboard layout on Play  
- Redesigning Generate flow for “pretty” only  
- Cream-terracotta / broadsheet AI-default looks  
- Defaulting AI draft on (Everyday CSRT stays)
