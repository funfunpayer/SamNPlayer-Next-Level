# FunGen2 vs SamNPlayer — feature compare (release prep)

Snapshot for the 0.5.3 test release. FunGen2 is the common reference
generator; SamNPlayer is player + device + generator + training. Numbers
are product scope, not a claim that every FunGen algorithm is cloned.

## SamNPlayer has (FunGen2 typically does not)

| Area | What |
|---|---|
| Device runtime | Sam Neo 2 BLE + Intiface/Buttplug, mock device, diagnostics sweep |
| Playback | Funscript player with sync modes, offset, O-markers, curve editor |
| Training | Technique training tab + history |
| Quality | Script Doctor + dense Quality Doctor on native path |
| Phase | `phase` / `compare` CLI (lag correlation vs FunGen refs) |
| Packaging | Single GUI/CLI binaries, update check, log folder |

## FunGen2 is stronger / SamNPlayer still Python or weaker

| Area | Notes |
|---|---|
| Multi-backend tracking | Flow / Grid-LK / fusion still Python-only in SamN |
| Project UI / batch | FunGen’s project workflow is broader; SamN is clip→script |
| AI ROI / YOLO train | Optional in SamN; needs Python + models |
| Tracker quality | CSRT (OpenCV) only on Linux OpenCV builds; Windows uses simpler `simpletrack` |
| Two-point Tf/Tj | SamN has distance profiles; FunGen parity on real clips still open (goldens) |

## Shared / overlapping

- CSRT-style single-ROI funscript generation  
- Funscript interchange (`.funscript`)  
- Optional quality heuristics  

## What “further from Python” means next

Already Go without Python for **default single-ROI CSRT** (auto path +
simpletrack on Windows). Still Python: other backends, Tf/Tj extras,
per-scene ROI, AI/audio opinion, ROI YOLO training, some analysis scripts.

Priority order (measure before porting):

1. Your golden clips → prove Go quality  
2. Port next high-traffic Python-only feature that goldens need  
3. Optional Windows OpenCV/CSRT (CI), not required for no-Python default  

## SAM script format

Package `sam/` exists with high test coverage but is not the GUI default yet.
Next architecture step after goldens: wire SAM as Intent layer
(`docs/ENGINE.md`), not “another tracker dropdown”.
