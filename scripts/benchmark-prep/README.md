# Benchmark clip prep (Owner)

Personal tool for cutting **short, downscaled** clips from longer sources
so Benchmark / golden-clip / KI labels stay fast and repeatable. **Not**
part of Everyday Create UI — may stay owner-only.

**GUI:** Bench tab → Clip-Prep In/Out cutter (ffmpeg via Go,
`ExportBenchmarkClip`) — same encode defaults as these scripts.

Everyday recognition basis remains **Go CSRT**. These clips feed the
same sizes used in golden measurements (`clip_ausschnitt` /
`clip_voll` ≈ **1280×720**).

Full owner guide (DE): project store
`docs/benchmark-clip-prep.md` (SamNPlayer Project).

## Recommended defaults

| Setting | Default | Why |
|---------|---------|-----|
| Max width | **1280** (`--preset-res 720p`) | Matches golden clips; CSRT tip lock stays reliable |
| Alt width | **960** (`--preset-res 960w`) | Smaller files when tip is still clear |
| Avoid | ≪720p / tiny proxies | Low-res (e.g. 256×144) was too weak for tracking work |
| Codec | H.264 `libx264` CRF **20** | Same ballpark as `videox/playable.go` soft proxy |
| Scale | `scale='min(W,iw)':-2:flags=lanczos` | Aspect kept; **never upscale** (proxy rule) |
| Length | **20–60 s** typical | Long enough for stroke tempo; short enough for bench loops |

## Single clip

```bash
# From repo root (needs ffmpeg on PATH / portable tools)
./scripts/benchmark-prep/cut_clip.sh \
  -i /path/long.mp4 \
  --start 01:20 --end 02:05 \
  -o ~/clips/hub_easy.mp4

# Lighter file
./scripts/benchmark-prep/cut_clip.sh \
  -i /path/long.mp4 \
  --start-sec 80 --duration 45 \
  --preset-res 960w \
  -o ~/clips/hub_easy_960.mp4
```

## Batch from marks JSON

1. Watch the long video; note in/out (player clock or any scrubber).
2. Copy `example_marks.json`, set `source` + `clips`.
3. Export:

```bash
./scripts/benchmark-prep/cut_clip.sh \
  --marks ~/clips/marks.json \
  --out-dir ~/clips/out
```

## After export → Benchmark

1. Point FunGen at the **same cut** (or trim its `.funscript` to match).
2. Everyday Create on the short clip (CSRT) → candidate `.funscript`.
3. Bench tab: pick the short clip → **Suggest beside video** (fills
   `clip.funscript` + `clip__hub.funscript` when present) → **Score vs
   reference**, or CLI:

```bash
go run ./cmd/cli benchmark \
  --reference REF.funscript \
  --candidate CAND.funscript \
  --video ~/clips/hub_easy.mp4 \
  --json --labels-out ~/clips/labels.jsonl
```

4. Optional: add the clip to a golden manifest (`generator/testdata/benchmark/example_manifest.json`).

See `docs/GOLDEN_CLIPS.md`, `generator/testdata/benchmark/README.md`.

## Tests

```bash
python3 scripts/benchmark-prep/cut_clip_test.py
```

## Copy-paste ffmpeg (no script)

Same filters the script builds (adapt times / paths):

```bash
ffmpeg -y -ss 80 -i long.mp4 -t 45 \
  -vf "scale='min(1280,iw)':-2:flags=lanczos" \
  -c:v libx264 -preset veryfast -crf 20 -pix_fmt yuv420p \
  -c:a aac -b:a 128k -movflags +faststart \
  hub_easy.mp4
```
