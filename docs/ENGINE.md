# Engine direction (lean)

Short operational summary of the Engine Gesamtkonzept / Masterplan
(17 Sep 2026). Full measurement history stays in `CHANGELOG.md`,
`FINDINGS_TIMING_TF.md`, and `NEXT.md` — this file is the **direction**,
not a third copy of every finding.

## Chain (do not skip layers)

```
Video PTS → Perception (observations) → Motion state → SAM Intent → Device
```

Perception must not write device values directly. Measure each stage;
prediction only after generator phase error and device latency are
separated.

## Quality vs language

Go is the application / domain language. **One strong Generate path:**
CSRT. Prefer our own Go CSRT (`trackcv`) everywhere — including Windows —
so the product does not depend on a soft “fallback” ladder. Until Windows
OpenCV is linked into the release binary, Generate’s product path on that
OS is **Python CSRT** (required, clear error if missing). `simpletrack`
(NCC) stays lab/CLI (`PreferSimpletrack`), not the GUI product path.

Python stays intentional for **AI training / research**, not as a silent
degrade of Generate quality.

**Lean self-build:** fewer dependencies only if quality stays **equal or
better** on clip tests — never ship a weaker self-build
(`docs/SELF_BUILD.md`).

## What is already shipped (v0.5.x)

- Signal Quality ≠ Motion Fidelity (`SIGNAL_VS_FIDELITY.md`, API `kind`)
- Phase Analyzer core + CLI (`phase`, `compare`)
- Script Doctor (Go) + dense Quality Doctor on native CSRT / simpletrack
- Generate product path: **Go CSRT** when OpenCV linked; else **Python
  CSRT** (Windows today). Goal: Windows in-binary CSRT (#120) — no
  dual-quality story
- Tf/Tj suction double-floor fix; trackcv `valid`/`confidence`/`reason`
- Device diagnostics (software-measurable only)
- `GenerateWithContext` cancel tests (Python + native)
- Log tab (copyable) + CLI `generate` + pipeline visibility (v0.5.3)
- Kontakt-Vibration + SAM Densify/RuntimeAdjust live overrides (v0.5.4)

## What still needs you / measurement (do not invent)

| Priority | Item | Gate |
|---|---|---|
| P0 | Real golden-clip manifest | your clips + refs |
| P0 | FunGen real-clip correlation | same manifest |
| P0 | Full PTS→device phase trace | media + instrumentation |
| P1 | SAM as live Intent layer | wire after goldens |
| P1 | Real Sam Neo 2 feel / BLE+Intiface | hardware |
| P1 | Native CSRT as default | measured win on goldens |
| P1 | **Windows native OpenCV (CSRT)** | **Next after 0.5.11** — single Go product path on Windows; MinGW OpenCV+contrib, link `trackcv`, ship DLLs in portable zip (#120) |
| P1 | Go `auto_roi` parity | after Windows CSRT; clip gate vs Python |
| P2 | macOS release | Mac builder + notarization (`docs/PLATFORMS.md`) |
| P2 | Mobile player (no generator) | share player/device/funscript/samn only |
| P2 | 4-zone relative graph as default | bake-off vs two-ROI; product shape in `docs/TFTJ_PROFILE_DIRECTION.md` |
| P2 | Depth / pose / spatial | per-variant benchmark |
| P2 | Prediction / look-ahead | only after measured latency |

## Phases (order)

Production spine (do not skip — see `docs/PRODUCTION_ROADMAP.md`):

0. **G0 Stabilize Generate** — one CSRT path everywhere (Windows OpenCV in-binary)  
1. **G1 Classical heuristics + audio** — goldens, Tf/Tj stabilize, posttrack package (`docs/GENERATE_HEURISTICS.md`); **no AI script writer**  
2. **G2 Raw-value / Neo 2 mapping** — diagnostics → Intent→device; feel measured  
3. **G3 AI helpers** — ROI / second opinion / train only on strong classical baseline  
4. **G4 License go-live / platforms / site** — after G0–G2 hold  

Longer research ladder (still gated by goldens):

5. **4-zone v2** — relative graph, reliability, common-mode  
6. **Go motion core** — module-by-module with golden gate  
7. **Spatial/3D** — depth/pose only if scores improve  
8. **Native AI runtime** — WinML/DirectML/CUDA/… when quality matches  
9. **Prediction** — compensate measured E2E latency, never hide generator lag  

## Definition of Done (any new technique)

Same quality on a fixed dataset; timing not worse; occlusion/loss cases
tested; latency/RAM measured; platform fallback; no unmeasured hardware
assumptions; a regression test that fails without the change.

## Not in-repo as dated state dumps

Dated review archives and multi-file research packs are **not** maintained
here (review feedback: third copies drift). Decisions live in code
comments + `CHANGELOG.md`; open work in `ROADMAP.md` / `NEXT.md`.
