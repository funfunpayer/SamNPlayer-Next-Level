# V1 target — feature map (Claude, 12 Oct 2026)

This file maps every visible element of the five Owner-approved target images
(`docs/visual-target/*.png`, spec `docs/V1_VISUAL_TARGETS_2026-10-12.md`) to
real behaviour. It is required by item 2 of the spec's completion criteria.

## Rule for every element

Each element in the images gets one status:

| Status | Meaning |
|---|---|
| ✅ | **Exists** today. It may need a look pass. |
| 🔧 | **Core work (Claude).** It is real, but not wired or not built yet. |
| 🎨 | **Look (ChatGPT).** The function exists; only layout or graphics are missing. |
| ⛔ | **Not shown.** There is no real behaviour behind it, so it stays out until there is. |

Mockup values are never invented. Examples are "4 / 4 connected", "All Systems Operational", CPU / memory meters and sample BPM.

The IA stays as merged (#494, #498):
- **Top bar:** Player · Training · Tools.
- **Player views:** Play · Create · Edit.
- **Scene Map** lives inside Create / Edit.

The target images still show the old top bar; it is reinterpreted in that style.

## Player (`player.png`)

| Element | Status | Where / what |
|---|---|---|
| Library (Local Files, Recent, Favorites, Playlists) | ✅ | `player_layout.js` |
| Scripts: Imported / Generated / Edited | ✅ | Real origin (#488) |
| Library Tools (Scene Map, Audio Analysis, BPM Detection, Editor) | ✅ | |
| Quick Access drop zone | ✅ | Drops work app-wide |
| Quick Access user entries ("Demo Scenes", "My Projects") | 🔧 | Needs a real "pin folder" feature |
| Video bar: select + recent videos, bookmark, snapshot, fullscreen | ✅ | |
| Video bar: note / comment icon | 🔧 | A note at the playhead (script metadata); shown once built |
| Video bar: ⋯ menu | 🔧 | Use it for the video info (codec / size) |
| Idle stage with the Flow mark | 🎨 | Graphic from the #477 list |
| Big Play ⇄ Pause / Resume, Stop separate | ✅ | #497 |
| Loop, speed 0.5–2×, previous, volume, fullscreen | ✅ | |
| Skip ±10 s buttons | ✅ | Same as ←/→ |
| Next item | ✅ | `#pb-next`; currently shown only with a playlist |
| Sleep timer | ✅ | Stops after N minutes of playing |
| Inspector: Devices · Audio · Playback · View | ✅ | |
| "Enable Devices" toggle | ✅ | Map to connect / disconnect state |
| "Custom Profile" dropdown | ✅ | Strength presets (`#pb-strength`) |
| Channel rows: toggle, level, live, settings ⚙ | ✅ | Rows show the **connected device's channels** from D3; Axis 3/4 only on devices that have them |
| Mode / Sync | ✅ | Recipe / Axes, video sync |
| Effects sub-tabs (Effects · Curve · Limits · Advanced) | ✅ | Curve = smoothing + sync mode, Limits = Max-Speed + Reduce peaks, Advanced = tick + soft-start (Drive stays next to Curve / Strength) |
| Effects preset dropdown ("Smooth") + Save Preset | ✅ | Named presets in the settings file (`playback.effect_presets`); Custom / Default / user presets — no invented factory presets |
| Intensity / Response knobs | ✅ | #491 |
| Frequency knob | ⛔ | The Neo 2 has none; shown only if a device reports a frequency channel |
| Sync with audio | ✅ | #492 |
| Reduce peaks | ✅ | #493 |
| **Invert output** | 🔧 | Owner wants it. Real for strokers: position 100−x (D2). For Neo 2 feel channels it is defined as an "invert curve" (see below). |
| Audio Analysis card: BPM, From audio, "first 10 minutes" | ✅ | #488 |
| Audio Analysis tabs Spectrum / Waveform | 🔧 | Waveform exists; Spectrum needs an FFT of the decoded audio |
| Script Tools: Write into script, Apply offset ±, Auto sync | ✅ | |
| Timeline tabs, lane names, Audio Level lane | ✅ | |
| Scene Markers row with thumbnails and "+" | ✅ | Off by default in Play, on in Edit |
| **Sound Curve** (curve from the video's sound) | ✅ | Audio Analysis → "Write as vibration curve": loudness → Vibration curve (smoothed, thinned), lane, undoable |
| **Invert Curve** | ✅ | Edit: "⇅ Invert" — shown channel, marked range or whole script, undoable |

## Create (`create.png`)

| Element | Status | Where / what |
|---|---|---|
| Steps 1–5 (Import · Detect · Build · Refine · Export) | ✅ | Generator steps; labels to align |
| Project header (thumbnail, resolution, fps, duration) | ✅ | `ProbeCreateVideo`: poster frame + resolution · fps (variable marked) · duration · codec; no invented "created" date or project name |
| Source clips grid, Import | ✅ | The generate queue is a grid of clip cards: poster, name, duration, status; loaded clip highlighted; click loads a clip and keeps the queue |
| Preview with transport | ✅ | Generator preview |
| Curve preview with channel lanes + Audio Level | ✅ | A Create result opens in **Edit** with all lanes, Audio Level and scenes (no duplicate lane view in Create) |
| Scene markers row in Create | ✅ | Same as above: the result opens in Edit with the scene strip |
| Right side: Generation Preset, Motion Detection, Curve Generation, Audio Assist (BPM), Export | ✅ | These exist as steps / Advanced; regroup them into cards (🎨 + small 🔧) |
| Tabs Generator · Templates · Presets · Batch | 🔧 | Batch = queue ✅; Presets = motion profiles ✅; Templates: only if real |
| "Generate Curves" button | ✅ | Create button |
| "4 Axes (Standard)" output channels | ⛔ / 🔧 | Only the channels the generator really writes; more with D4 |

## Edit / Scene Map (`scene-map.png`)

| Element | Status | Where / what |
|---|---|---|
| Scene map heatmap, marks | ✅ | Generator scene map |
| Chapter bands on the timeline | ✅ | Coloured band per chapter over the scene strip (open end → next chapter / script end); click seeks; band under the playhead marked; "Chapters" lane name |
| Scene thumbnails grid with selection | ✅ | Player bottom tab **Scene Map**: one card per chapter (thumbnail nearest the start, name, range, duration); click / arrow keys select, double-click or Enter seeks; playing scene marked |
| Scene tags / labels | 🔧 | Scene marks have kind / class; free tags need a small schema extension |
| Scene details inspector (time range, chapter, notes, markers list, technical info) | ✅ / 🔧 | Built: range, duration, n / N, prev/next, Go to scene, video file / resolution / frame rate / codec (`ProbeCreateVideo`). Open: notes and a markers list (need storage) |
| Auto Scene Detect | 🔧 | Scene-change detection exists in the generator (cuts); expose it as an action |
| Scene Merge, Export Scene Map | ✅ / 🔧 | Export exists (learning export); merge would be new |

## Training (`training.png`)

| Element | Status | Where / what |
|---|---|---|
| Programs / presets list | ✅ | `ListTrainingScripts`; render as a list |
| My Programs (own scripts) | ✅ | Script editor / `SaveTrainingScript` |
| History | ✅ | `TrainingHistory`, CSV export |
| Vibration and suction rings | ✅ | One combined ring today; split into two rings (🎨 + small 🔧) |
| Phase stepper (Warm Up · Build · Peak · Cool Down) | 🔧 | Multi-phase presets exist; expose the current phase from the engine |
| Session time / time left | 🔧 | Engine knows cycles and timing; send it as an event |
| Play / Pause / skip | ✅ / 🔧 | Start, Interrupt and End exist; skip phase is new |
| Live training curves | 🔧 | Preview curve exists; a live plot of real output is new |
| Session settings (duration, intensity, vibration, suction, randomization) | ✅ / 🔧 | Peak intensity, progression and jitter exist; per-channel max is new |
| Safety: Max Vibration / Max Suction, Auto Cool Down, Pause on Limit | 🔧 | The session ceiling exists; per-channel caps are real core work |
| Emergency Stop | ✅ | End session; make it a conspicuous Stop |
| Session timeline (phases with times) | 🔧 | From the script phases |
| Endurance / Control / Edge Play categories | 🔧 | Only as filters over real presets |
| **Training with a video** (Owner, 12 Oct) | ✅ | Besides the pixel clip, any film can run on the training stage. Training drives the device, the video plays along (loop / muted option). Video picker + recent videos are shared with Player |

## Tools (`tools.png`)

| Element | Status | Where / what |
|---|---|---|
| Device manager: device(s), channels, connect | ✅ | Tools → Device plus D1 channel chips |
| "4 Devices Connected", per-axis "Connected" | ⛔ | One device at a time today; multi-device is future work |
| Audio diagnostics from the microphone (spectrum, input level, noise floor) | ⛔ | No microphone input in the app. Video-audio spectrum is possible later (🔧) |
| BPM detection tools (BPM, confidence) | ✅ / 🔧 | BPM from audio exists; confidence / stability values need exposing |
| Script utilities (open, validate, recent) | ✅ | Script Doctor / quality, recent files; Tools → Home lists recent scripts and has Open script… |
| Profile manager (Default / Intense / Relaxed …) | ✅ | Strength presets |
| Tools status overview (green dots) | ✅ | **Tools → Home**: every row is a real check — device state (live), runtime dependencies (ffmpeg …, required = red when missing, optional = amber), app folders, last device diagnostics run, cache, version and edition. Rows that were never checked stay grey |
| Performance: CPU / memory / latency | ⛔ / 🔧 | Device latency is real (diagnostics); CPU / memory are not shown |
| Quick tools: Calibrate, Run audio analysis, Detect BPM, Validate, Export / Backup config | ✅ / 🔧 | Diagnostics, BPM and validate exist; config export / backup is new |
| Configuration toggles (updates, logging …) | ✅ | Settings |
| Logs, Settings | ✅ | |

## Build order (Claude)

1. **Visual targets in the repo** (this PR). Docs: devices LELO F1S V3 and Lovense Max 2.
2. **Invert Curve** in Edit, with undo (backend exists), plus skip ±10 s and sleep timer.
3. **Sound Curve:** write the audio envelope as a vibration curve (with undo) and show it as a lane.
4. **Effects sub-tabs** (Effects · Curve · Limits · Advanced) and effect presets.
5. **Training:** engine events for phase and time, split rings, live curve, per-channel safety caps, **training with any video**.
6. **Create:** project header, source grid, curve lanes and scene strip.
7. **Scene Map view:** chapter bands, thumbnail grid, details inspector.
8. **Tools home:** real status only.
9. **Devices:** D2–D5 (`docs/DEVICE_SUPPORT.md`).

Each step lands as its own PR on green CI. ChatGPT matches the look against the images in parallel.
