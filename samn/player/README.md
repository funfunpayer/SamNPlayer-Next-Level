# samn.player

Playback logic without Qt: script offset (±10 s, saved per script under
`playback.offset.<path>` as in 0.7), script time from video time, the loop
range, the position on the curve, and the video found beside a script.

- **API:** `Session`, `LoopRange`, `script_time`, `position_at`,
  `find_matching_video`.
- **Depends on:** `samn.settings`, `samn.library`.
- **Go reference:** `cmd/gui-wails/app_playback.go`, `app.go`.
- **Status:** P1. The device sync loop comes in P2 (`samn.devices`).
