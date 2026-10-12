# Competitive bar — GUI & GitHub exterior

Goal: look and work like a serious product next to FunGen / OFS-class
tools — **without** Electron bulk or feature theater.

## GitHub exterior (public face)

Must be true on every release page / README:

1. **Brand first** — logo + wordmark readable above the fold  
2. **One sentence** what it does (Generate · Play · Train · Sam Neo 2)  
3. **Download** with portable archives called out (ffmpeg included)  
4. **Screenshots** of Play + Generate that match the current UI  
5. **Honest limits** — local-only, optional AI, Mac/mobile status in
   [`PLATFORMS.md`](PLATFORMS.md)

Do not turn the README into a dashboard of badges and feature piles.

## In-app layout & function

| Rule | Why |
|------|-----|
| Play is the home tab | Competitors are judged as players first |
| One job per tab | Generate ≠ AI Train ≠ Bench |
| Brand in the rail | Lockup stays visible; no generic “admin” shell |
| Device always reachable | Topbar connect + Device tab |
| Errors → Log | No silent failure |
| English UI | Product language (`docs/LANGUAGE.md`) |

Visual language stays the existing dark gold/teal system (`style.css`
tokens). No purple-on-white redesign, no card grids in the hero/rail.
Makeup ideas + backlog: [`docs/GUI_MAKEUP.md`](GUI_MAKEUP.md).

## What “hervorragend” means here

- **Function:** load script/video, play to device, generate with ROI,
  cancel cleanly, recover from missing ffmpeg via portable/tools  
- **Look:** consistent spacing, readable type (Space Grotesk), clear
  active tab, no clutter on first Play viewport  
- **Trust:** Quality Doctor numbers, no silent AI Funscripts  

Polish passes that regress cancel, sync, or Tf/Tj contact are rejected.

## Explicit non-goals (for now)

- Frameless glass UI / light mode  
- Streaming / social Funscript network  
- Shipping a phone UI inside the desktop shell  

Mobile player is a **separate** surface later — see `PLATFORMS.md`.
