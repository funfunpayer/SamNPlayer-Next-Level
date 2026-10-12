# Play UI contract (issue #475)

Split agreed with the Owner on 11 Oct 2026: **Claude builds the core**
(`playback.js`: data, drawing, device/script logic, keyboard, tests).
**ChatGPT builds the layout and look around it** (the Play section of
`style.css`, visual assets).

This file is the interface between the two. A layout may change anything
visual, but it relies only on what is listed here. If a hook is missing, ask
in #475 and Claude adds it. Don't work around a missing hook in JS.

## 1. Regions — `data-pb-region`

| Region | Element | Contents |
|---|---|---|
| `head` | `.pb-head` | Title, script path `#pb-script-path`, `#pb-choose`, `#pb-queue-add` |
| `empty` | `#pb-empty` | Empty state (`#pb-choose-empty`) — shown when `data-pb-state="empty"` |
| `media` | `.pb-media` | Left/main column: video, transport, timeline, playlist |
| `video` | `#pb-video-stage` | `<video id="pb-video">`, 0–100 gauge, video chrome, no-video panel |
| `transport` | `.pb-transport` | `#pb-play`, `#pb-stop`, `#pb-eo-trigger`, `#pb-next`, `#pb-progress`, live `#pb-vib` / `#pb-suc` |
| `timeline` | `.pb-timeline` | Wraps legend, curve, lanes, heatmap, zoom |
| `legend` | `#pb-axis-legend` | Axis chips `[data-axis=general\|vibration\|suction]` (role toolbar) |
| `curve` | `#pb-curve` | Main curve canvas (shown axis + stroke ghost) |
| `lanes` | `#pb-axis-lanes` | Feel-channel lanes canvas (focusable, ↑/↓) |
| `ruler` | `#pb-ruler` | Time ruler (mm:ss) above the curve |
| `scenes` | `#pb-scene-strip` | Scene strip: thumbnails `.pb-scene-thumb`, diamonds `.pb-scene-mark` (chapter starts, click seeks), `#pb-scene-add` ("+"). Off by default in the Player (`#pb-scenes-toggle`) |
| `videobar` | `.pb-videobar` | Above the video: `#pb-vb-video` (name, opens picker), `#pb-vb-bookmark`, `#pb-vb-snapshot`, `#pb-vb-fs` |
| `audio-analysis` | `#pb-audio-card` | Audio Analysis card: `#pb-aa-bpm`, `#pb-aa-detect`, waveform `#pb-aa-wave` (click seeks) |
| `heatmap` | `#pb-heatmap` | Heatmap / selection canvas |
| `zoom` | `#pb-curve-zoom` | Zoom buttons + `#pb-zoom-label` |
| `playlist` | `#pb-playlist` | Playlist (shuffle / repeat / auto-advance) |
| `tools` | `#pb-tools` | Tools column (`#pb-tools-toggle`, fold with **T**) |
| `tool-group` | `#pb-axis-row` (`data-pb-group="feel"`), `#pb-offset-row` (`data-pb-group="timing"`) | Grouped tool rows |

**DOM order is fixed:** video → transport → timeline → playlist, then tools.
This is the keyboard tab order. A layout may reorder visually with CSS grid
areas or `order`, but must not move elements in JS.

## 2. State attributes on the Play root (`#tab-playback`)

| Attribute | Values | Meaning |
|---|---|---|
| `data-pb-state` | `empty` / `loaded` | Whether a script is loaded |
| `data-pb-tools` | `open` / `folded` | Tools column folded with ⟩ or **T** (remembered) |
| `data-pb-playing` | `true` / `false` | Playback running |
| `data-pb-video` | `true` / `false` | A playable video is linked |
| `data-pb-video-path` | path / empty | The linked video file |
| `data-pb-scenes` | `on` / `off` | Scene strip shown |
| `data-pb-bottom` | `timeline` / `chapters` / `bookmarks` / `audio` | Bottom tab (V1 layout) |

The older classes (`pb-tools-collapsed`, `has-script`, `.is-playing`,
`.has-video` on the stage) still work. New CSS should use the attributes.

## 2a. Player workspace (V1 W1, Owner 11 Oct)

- **Top tabs:** Player · Training · Tools. `#rail .tab-btn[data-tab="generator"|"scenemap"]`
  stay in the DOM as hidden routes (`data-ws-shim`).
- **View switch:** `#ws-view` (role tablist, `hidden` outside the Player) with
  `.ws-view-btn[data-ws-view="play|create|edit"]` and `#ws-scenemap`
  (Create only, `aria-pressed`).
- **State:** `body[data-ws-view]` and `#tab-playback[data-ws-view]`
  (`play` / `create` / `edit`; `none` outside the Player); `body[data-tab-group="player"]`
  for all three views. Play / Edit is remembered (`localStorage ws.view`).
- **Create** shows `#tab-generator` (unchanged). **Edit** shows `#tab-playback`
  with curve editing on, Inspector on View, Timeline tab, scene strip on.
- **Edit history (W2):** `#pb-history` with `#pb-undo` / `#pb-redo` next to "Edit curve"; root `data-pb-undo` = steps available.
- **Events:** `ws:view` (detail.view) after every switch; `ws:set` asks for a
  view (a Generator result sends `edit`).

## 2b. App tabs (Owner decisions 11 Oct; V1 shell S2)

The top bar holds the brand (icon, "SamNPlayer", tagline GENERATE · PLAY ·
TRAIN), the tabs, and the actions: device pill, Donate, version, folder,
gear. The left rail is gone.

- **Tabs:** `#topbar #rail .tab-btn[data-tab="playback|generator|training|scenemap|tools"]`.
  The labels are **Player · Generator · Training · Scene Map · Tools**.
- **Scene Map:** `switchTab('scenemap')` shows `#tab-generator` with
  `data-view="scenemap"`. That is a focused view: the video chooser and the
  Generator's scene-map section (`#gen-advanced-scenemap`, same IDs, same
  state). `main.js` marks the path with `data-sm-path`.
- **Tools sub-navigation:** `#tools-nav` (role tablist, `hidden` outside
  Tools) holds `.tab-btn.tools-sub-btn[data-tab="device|log|settings"]`.
  Studio builds add `benchmark` and `roi-training`.
- **Top-bar actions:** `#topbar-open` opens the Player's file picker
  (`#pb-choose`); `#topbar-settings` opens Tools → Settings.
- **`body[data-tab]`** names the visible view (`playback`, `scenemap`,
  `device`, …). **`body[data-tab-group]`** is `tools` for all Tools sections.
- **Old entry points keep working:** `switchTab('device')` and
  `.tab-btn[data-tab="log"].click()`.

## 3. Theme tokens (CSS custom properties, read by JS on every redraw)

| Token | Default | Used for |
|---|---|---|
| `--axis-stroke` | `#5fd0c8` | Stroke curve, chip, live dot |
| `--axis-vibration` | `#f0a050` | Vibration curve and lane |
| `--axis-suction` | `#c39bff` | Suction curve and lane |
| `--pb-playhead` | `#ffffff` | Playhead line (curve + lanes) |
| `--pb-grid` | `rgba(255,255,255,0.10)` | 0 / 50 / 100 guide lines |
| `--pb-curve-width` | `1.5` | Curve line width in CSS px |

Set them on `#tab-playback` or `:root`. The chip colours in CSS
(`.pb-axis-chip[data-axis=…] { --axis-c }`) should use the same values.

## 4. Sizing

The canvases (`curve`, `lanes`, `heatmap`) **follow their CSS size**. A
`ResizeObserver` redraws them sharply on HiDPI whenever the layout changes
their size, so a layout never calls JS. The lanes canvas sets its own height
(34 px per channel); override it with CSS if needed.

## 5. Must not change

- Every element `id` and `data-axis` / `data-help` attribute (tests and
  bindings rely on them).
- `playback.js`, apart from changes Claude makes (ask in #475).
- Real behaviour: the layout must not show controls that do nothing. See §6
  for what exists.

## 6. Owner mockup (11 Oct) mapped to real features

The Owner's mockup is a full-player concept (dark, cyan/orange glow). It is
**not committed**, because it contains sexualized imagery (#475 acceptance:
"no unsafe mockup media"). Below, each element is mapped to what the app
really has.

Legend:
- ✅ exists (use this ID);
- 🟡 exists, but under another name or in another place;
- 🔧 core work needed (Claude, follow-up PRs);
- ⛔ not supported. Don't show it as working.

### Top bar
| Mockup | Status | Real feature / note |
|---|---|---|
| Logo + "SamNPlayer" | ✅ | Rail brand (`.brand-lockup`); mark/font redesign pending (#474 comment) |
| Tagline "Generate · Play · Train" | ✅ | **Decided 11 Oct:** "Generate · Play · Train"; *Train* = stamina Training (`#brand-tagline`, README, site) |
| Tabs Player / Generator / Training / Scene Map / Tools | ✅ | **Final mockup, 11 Oct:** top-bar tabs exactly as the mockup (§2b, V1 S2) |
| Folder icon / gear icon | ✅ | `#pb-choose` / Settings tab |
| Window controls (min/max/close) | ⛔ | Native window frame; frameless would be a Go/Wails change. Not planned. |

### Left: Library
| Mockup | Status | Real feature / note |
|---|---|---|
| Local Files | ✅ | `#pb-choose` (file picker) |
| Recent | 🔧 | New: remembered recent scripts/videos (pref + binding) |
| Favorites | ⛔ | Not planned (no library index) |
| Playlists | ✅ | `#pb-playlist`, `#pb-queue-add`, shuffle / repeat / auto-advance |
| Scripts: Imported / Generated / Edited | ⛔ | No script library categories. Could follow a later library index. |
| Tools: Scene Map | 🟡 | Exists in **Create → Advanced** (scene map), not in Play |
| Tools: Audio Analysis | ✅ | Play audio segments `#pb-audio-segments`, `#pb-audio-seg-strip` |
| Tools: BPM Detection | ✅ | `#pb-bpm-auto` ("From audio"), `#pb-bpm`, `#pb-bpm-grid` |
| Video card: name, time | ✅ | `#pb-script-path`, video time |
| Video: resolution + codec, duration | ✅ | `ProbePlaybackVideo` → `width`, `height`, `codec`, `durationSec` |
| Audio codec, file size | 🔧 | Extend `PlaybackVideoInfo` with `audioCodec`, `sizeBytes` |

### Centre: video + transport
| Mockup | Status | Real feature / note |
|---|---|---|
| Video / script selectors | 🟡 | `#pb-video-change` ("Video…") / `#pb-choose`. Dropdowns need **Recent** 🔧 |
| Bookmark icon | ✅ | Bookmarks `#pb-bookmark-add` (+ list) |
| Image (snapshot) icon | 🔧 | New: save the current frame as PNG |
| Fullscreen | ✅ | `#pb-video-fs` |
| Play / pause | ✅ | `#pb-play`, `#pb-stop` (Space) |
| Previous / next | 🟡 / ✅ | `#pb-next` exists; **previous** 🔧 |
| Step back / forward | 🟡 | Keys ←/→ and ,/. exist; buttons 🔧 |
| Loop | ✅ | `#pb-loop` (loop selection) |
| Shuffle | ✅ | `#pb-playlist-shuffle` |
| Volume | 🔧 | Native video volume; dedicated slider 🔧 |
| Speed 0.5× / 1× / 1.5× / 2× | ⛔ | Device timing follows script time; changing speed needs backend time scaling. Not planned. |
| Picture-in-picture | ⛔ | Not planned |

### Right: Devices / Audio / Playback / View
| Mockup | Status | Real feature / note |
|---|---|---|
| **Axis 1 / Axis 2 / Axis 3** | ⛔ | **Sam Neo 2 has no extra axes.** The real channels are **Stroke** (script position, drives the recipe), **Vibration**, **Suction**. Show those three, never "Axis 1–3". |
| Vibration / Suction rows with level % | 🟡 | Live levels `#pb-vib`, `#pb-suc`; global strength `#pb-strength`; contact intensity `#pb-contact-intensity` |
| Per-channel on/off toggle + level slider | 🔧 | New (mute / scale vibration or suction in playback). Needs device-side care. |
| Mini waveform per channel | ✅ | The lanes (`#pb-axis-lanes`) |
| Smoothing | ✅ | `#pb-smoothing` |
| Sync with audio | 🟡 | **"Vibration from video audio only"** `#pb-audio-vibration` (vibration only; suction stays on the script) |
| Invert | 🟡 | Settings → Play "honor inverted" (`playback.honor_inverted`) |
| Reduce peaks | 🟡 | Max speed `#pb-maxspeed`; cap range `#pb-cap-speed` / `#pb-cap-intensity` |
| Knob Intensity | 🟡 | Strength `#pb-strength` / contact intensity |
| Knob Response | 🟡 | Soft start `#pb-softstart` / smoothing |
| Knob Frequency | ⛔ | No equivalent |
| Tabs Effects / Curve / Limits / Advanced | 🟡 | Today these are `<details>` sections in the tools column. Grouping is a layout job. |

### Bottom: timeline
| Mockup | Status | Real feature / note |
|---|---|---|
| Tabs Timeline / Chapters / Bookmarks / Scene Map / Audio Analysis | 🟡 | Chapters `#pb-chapter-*`, Bookmarks `#pb-bookmark-*`, Audio segments `#pb-seg-*` exist as tools sections. Scene Map is in Create. |
| Zoom buttons | ✅ | `#pb-zoom-in`, `#pb-zoom-out`, `#pb-zoom-sel`, `#pb-zoom-reset` |
| Time ruler (mm:ss) | 🔧 | New: ruler drawn on the timeline |
| Stacked lanes with names on the left | 🟡 | `#pb-axis-lanes` (Vibration, Suction) + main curve (Stroke). A name column 🔧 |
| Markers (triangles) | 🟡 | O-markers / bookmarks / chapters exist and are drawn as bands. Triangles on the lanes 🔧 |
| Playhead | ✅ | Curve + lanes |
| Scenes thumbnail strip | 🔧 | New: ffmpeg thumbnails along the timeline (local only) |

### Right bottom: Audio Analysis / Script Tools
| Mockup | Status | Real feature / note |
|---|---|---|
| BPM value + "From audio" | ✅ | `#pb-bpm`, `#pb-bpm-auto` (first 10 minutes) |
| "Audio Vibration" toggle | ✅ | `#pb-audio-vibration` |
| "Analyze first 10 minutes" | 🟡 | Fixed behaviour of From audio (not a setting) |
| Write into script | ✅ | `#pb-offset-bake` |
| Apply offset −/+ ms | ✅ | `#pb-offset-minus`, `#pb-offset`, `#pb-offset-plus`, `#pb-offset-reset` |
| Auto sync (audio) | ⛔ | Doesn't exist. It would be a new feature (estimate the offset from audio), so the Owner decides. |

## 7. Core follow-ups (Claude, one PR each, in this order)

1. Time ruler and lane name column on the timeline, with marker triangles.
2. Transport additions: previous item, step ±, volume slider (video), snapshot PNG.
3. Video info: `audioCodec`, `sizeBytes` in `PlaybackVideoInfo` and the video card data.
4. Recent scripts/videos (pref + binding), feeding the selectors.
5. Per-channel mute / level for vibration and suction (playback only, device-safe).
6. Scene thumbnail strip (ffmpeg, cached, local).

Items marked ⛔ stay out unless the Owner asks for them as new features.
