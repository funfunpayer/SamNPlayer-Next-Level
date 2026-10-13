# EDIT-00 — What the Free editor does today, and the gap to the Next editor

**Status:** inventory · Free `main` at `52d0529` · 12 Oct 2026 · **snapshot:** device/axis
work has moved on since (SamNPlayer #526, #527 imported here; #529 open in the
old repo). Re-check the gap table against the current code before building
from it.
Target: `sources/STUDIO_EDITOR_COMPETITIVE_BLUEPRINT_2026-10-12.md` §1.

## Today (Free)

### Go bindings (`cmd/gui-wails`)

| Binding | File | Does |
|---|---|---|
| `GetScriptActions` / `SaveScriptActions` | `app_editor.go` | Read / replace the stroke (`general`) actions |
| `GetScriptAxisActions(axis)` / `SaveScriptAxisActions(axis, actions)` | `app_samn.go` | Same per axis; axes are `general`, `vibration`, `suction` |
| `GetSpeedHighlights` | `app_editor.go` | Fast segments for colouring |
| `SaveLoadedAsSamn` / `ExportLoadedFunscript` | `app_samn.go` | Save `.samn` / export `.funscript` (atomic write in `funscript/atomic_write.go`) |
| `GetPlaybackSource` / `SetPlaybackSource` | `app_samn.go` | `recipe` vs `axes` |
| `BakeNeoAxesOnLoaded` | `app_samn.go` | Recipe → explicit vibration/suction curves |
| `GetStrengthPresets` / `SetActiveStrength` | `app_samn.go` | Playback-time scaling |
| `GetOMarkers` / `SaveOMarkers`, `SaveMarker` / `GetMarker` | `app_markers.go` | O-markers, range marker |
| `ScriptChapters` | `app_chapters.go` | Chapters |

The loaded script is one in-memory `*funscript.Script` (`app_script_state.go`).

### Frontend (`frontend/src/playback.js`)

- Edit mode: keyframe dots — drag, add, delete.
- One range marker (start/end) as the only selection.
- Range tools: speed cap, scale, delete, invert (range or whole curve).
- FPS snap (`#pb-fps-snap`), zoom by factor / to selection, time ruler.
- Lanes: Stroke · Vibration · Suction · Audio Level · Scene markers; ghost curve.
- Undo/redo (V1 W2): JS arrays, max 50 entries; each entry stores the **whole
  axis before and after**; undo writes back through `SaveScriptAxisActions`.
  Per loaded script, lost on reload. Ctrl+Z / Ctrl+Y.
- Keyboard: seek ←/→ (Shift = fine), `,` / `.` ±200 ms, `+` / `-`, `l` loop.

## Gap to the Next editor

| Blueprint item | Free today | Gap | Work package |
|---|---|---|---|
| Any number of axes, unknown axes kept | 3 fixed names | New axis map | P5 → EDIT-01 |
| Companion `.surge/.twist/…` files | not loaded | Import/export pack | P5 → EDIT-01, D4 |
| Time in integer µs PTS, VFR-correct seek | ms, no PTS (no `pts`/`vfr` in editor code) | Timeline index from decoded PTS | EDIT-03 |
| Undo as commands, persisted journal, multi-axis transactions | Whole-axis snapshots in JS, 50 steps, not persisted, one axis per entry | Go `EditTransaction` + `HistoryStore`; diff-based entries | EDIT-02 |
| Multi-select, lasso, select peaks/valleys/scene/chapter | one range marker | Selection model in Go | EDIT-03/05 |
| Copy/paste with relative time | none | | EDIT-04 |
| Transforms (stretch, mirror, equalize, smooth with preview, retime by BPM, ripple, repeat…) | cap, scale, delete, invert | `CurveOps` library, pure + tested | EDIT-04 |
| Numeric point edit, keyboard nudge by frame/position | none | | EDIT-05 |
| A/B compare, candidate overlay, diff | ghost curve only | Compare view | EDIT-06 |
| Quality overlay + fix preview | Quality Doctor at generate time | Live in editor | EDIT-06 |
| Device preview behind explicit Arm | Player plays to device | Separate mock preview path | EDIT-07 |
| 1 h video, 100k+ actions | untested; whole-axis snapshot per edit costs O(n) memory per step | Benchmark | EDIT-08 |

## Notes for the build

- Keep every Free binding working: Next's editor adds a Go `EditorService`
  next to them; the old bindings become thin wrappers over transactions.
- The history must move from JS to Go first (EDIT-02). All later tools record
  through it, so doing it late means touching every tool twice.
- Element IDs and `data-*` hooks are listed in Free `docs/PLAY_UI_CONTRACT.md`;
  keep them until the Next UI replaces the view.
