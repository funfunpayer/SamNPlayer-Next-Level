# Learning from OpenFunscripter (OFS) — in Go

OpenFunscripter is a desktop **script editor**. SamNPlayer stays
**generate → inspect → play**, but we **port the best OFS ideas into Go**.

Official project (archived): [OpenFunscripter/OFS](https://github.com/OpenFunscripter/OFS).

## Shipped

| Idea | Where |
|---|---|
| Intensity `500 × \|Δpos\| / \|Δt\|` | `funscript/device_compat.go` |
| Curve edit | Wiedergabe + `app_editor.go` |
| Heatmap / auto chapters / bookmarks UI | Player + `motionx` |
| Max-speed highlights on curve | `funscript.SpeedHighlights` + Play **HL** slider / show toggle (`GetSpeedHighlights`) |
| Chapters/bookmarks in `.funscript` metadata | `funscript/bookmarks.go` |
| Heatmap PNG export (+ chapter ticks) | `funscript.ExportHeatmapPNG` / `ExportScriptHeatmapPNG` |
| Project sidecar `.snp.json` | `funscript/project.go` / Save + **Load** in Play (`SavePlaybackProject` / `LoadPlaybackProject` / `PickPlaybackProject`) |
| Frame snap (`SnapMs`) | `SnapTimeMs` + Play FPS-Snap range + readout (`off` at 0) |
| Range delete / speed-cap / scale | Play OFS row: Delete / **Speed-cap** (adjustable Cap intensity 150–800, default 400) / **Scale** (×0.5–×1.5, optional soft edges) on the **active Curve axis** (`EditDeleteRange`, `EditCapSpeedRange`, `EditScaleRange`) |
| Feel / Speech-Hold display bands | Play heatmap (+ thin curve underlay): filtered `audio_check` segments as translucent bands; “Show on heatmap” + Opacity slider (display only) |
| BPM / tempo grid overlay | Play OFS row: optional beat lines on the curve from BPM or `audio_check` Hz×60 (display only — no snap/rewrite) |
| Curve / heatmap zoom | Shared time window: Zoom in/out · Zoom selection · Reset · wheel on curve (display/nav only) |
| Bookmark / chapter rename | Play list rows: Rename beside Seek / Remove (OFS-style CRUD) |
| O-marker secondary intensity | Play Intensity range + readout (authoring; default 0.5) |

## Still optional later

- WebSocket bridge to external players

## Not copying

- Full ImGui OFS shell / multi-axis 3D studio  
- Lua extension host as a core dependency  
- OFS source tree or binaries  

## Workflow reminder

AI/ONNX proposes ROIs only. Classical tracking writes the funscript.
OFS-inspired tools improve **inspect / repair** after generation.
