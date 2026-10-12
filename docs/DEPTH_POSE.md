# Depth / pose supporting signals (experimental)

These helpers **do not** generate or write a `.funscript`. They only offer
optional hints to rank ROI proposals, in line with `docs/AI_ADAPTER.md`.

## What it is

- **Classical depth proxy** (`support_signals.relative_depth_map`): fast
  image cues (gradient + local variance) mapped to `[0, 1]`. This is **not**
  metric monocular depth — only a soft contrast signal for ranking.
- **Optional ONNX** (`--depth-onnx`, `--pose-onnx`): hooks only; no model is
  bundled or downloaded. `propose_pose_boxes` is a contract stub until a
  pose model is validated on golden clips.

## How to enable ROI soft-ranking

1. Set `SAMNPLAYER_DEPTH_RANK=1` in the environment when using AI ROI
   (`--roi-finder ai` or GUI AI detection), **or** pass `use_depth_rank=True`
   when calling `ai_roi.select_best_box` / `select_two_best_boxes` from Python.
2. Default is **off** — classical CSRT/flow behavior is unchanged when the
   flag is unset.

Probe locally:

```bash
python3 generator/support_signals.py --check
python3 generator/support_signals.py --video your_clip.mp4 --frame 0
```

## Golden-clip bake-off (required before default)

Compare generated scripts with and without `SAMNPLAYER_DEPTH_RANK=1` on the
clips in `docs/GOLDEN_CLIPS.md` (same metrics as other adapter steps: Quality
Doctor, optional FunGen correlation where references exist). Promote to default
only if depth ranking improves measured Funscript quality — not just ROI overlap.

## What this is deliberately NOT

- Not a replacement for CSRT/flow tracking or the Quality Doctor.
- Not automatic profile or quality decisions.
- Not cloud inference, telemetry, or a recommended pretrained depth/pose model
  in the repo or installer.
- Not proof that monocular depth helps this largely 2D problem — that case must
  be made with measurements, not assumptions.
