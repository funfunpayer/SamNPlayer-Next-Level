# ENG-01 (part 1) — Where Free still runs Python

**Status:** static inventory from code · Free `main` at `52d0529` · 12 Oct 2026
Part 2 (timings, RAM, golden report) needs real clips and runs at N2 with P4.

## How Python is shipped

- `generator/generator.go`: `//go:embed *.py requirements*.txt` — all 35
  non-test `.py` files (including training scripts) are in **every** build,
  Free included (Free `docs/EDITIONS.md` "Not yet split").
- Subprocesses via `command()` / `commandContext()` with hidden console on
  Windows; long runs are cancellable.
- Dependencies (user installs via the app's AI setup):

| File | Packages | Needed for |
|---|---|---|
| `requirements.txt` | `opencv-contrib-python`, `scipy`, `numpy` | Python generate path |
| `requirements-ai.txt` | `onnxruntime` | AI region finder |
| `requirements-ai-train.txt` | `onnx`, `ultralytics` (pulls PyTorch, hundreds of MB) | Suite training only |
| `requirements-pose-observer.txt` | — | pose observer research |

## Generate: Go first, Python as fallback

`generator.NativePipelineEligible` (`generator/native.go`) sends Generate to the
**Go path** (`trackcv` CSRT with OpenCV linked, else `simpletrack` NCC) when:

- the ROI is set,
- backend is `csrt` (or empty),
- per-scene ROI re-search is off,
- AI quality opinion is off,
- soft masks are used only together with RhythmGrid and without Tf/Tj partners.

Single-ROI and Tf/Tj two-point both run in Go. On builds with Go CSRT,
`softenPythonOnlyOptsForGoCSRT` even drops per-scene ROI to stay in Go.

**Still Python (`generate_funscript.py`):** non-CSRT backends (`backends.py`,
`flow_backend.py`, `fusion.py` …), per-scene ROI re-search, AI quality opinion,
soft masks without RhythmGrid / with Tf/Tj, and `PreferPython`.

## Other Python call sites

| Script | Called from | Purpose | Edition |
|---|---|---|---|
| `auto_roi.py` | `generator.go` (find ROI, two ROIs, candidates) | Classical region finder | Free |
| `ai_roi.py` | `generator.go` (5 sites) | ONNX region finder, two ROIs, body parts | Free (optional, needs onnxruntime) |
| `support_signals.py` | `generator.go` | Support signals | Free |
| `ai_setup.py` | `ai_setup.go` | Check/install AI packages | Free |
| `contact_points.py` | `ai_setup.go` | Contact points | Free |
| `golden_clip_benchmark.py` | `generator.go` | Bench tab | Suite |
| `train_yolo_model.py`, `bootstrap_yolo_dataset.py` | `roi_training.go` | Model training, dataset | Suite |

## What this means for Next

1. **Everyday generate is already Python-free** on the default path. The
   "Go/Rust rewrite" question is therefore about the special paths and about
   speed, not about removing Python from the common case.
2. ENG-02 (backend/track contract) should wrap **both** paths behind one
   `TrackResult` interface first; AutoDirector can then compare the Go CSRT
   result with the Python backends per scene without porting them.
3. Port candidates, in order of how often users hit them: per-scene ROI
   re-search → soft masks with Tf/Tj → `auto_roi.py` → AI quality opinion.
   Each port needs P4 golden parity.
4. Training scripts (`train_yolo_model.py`, `bootstrap_yolo_dataset.py`,
   `export_yolo_onnx.py`) and `requirements-ai-train.txt` belong in the Suite build
   only. Splitting the embed (Suite-only `//go:embed`) is a cheap early PR and
   shrinks the normal build.
