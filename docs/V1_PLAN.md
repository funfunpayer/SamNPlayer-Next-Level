# SamNPlayer 1.0 — build plan (Owner, 11 Oct 2026)

**Target:** the Owner's final mockup of 11 Oct, "this is the new look". The Owner repeated it on 11 Oct ("so soll das am Ende aussehen"): every step is measured against that mockup, minus the items under *Not in 1.0*. The
release is **Version 1.0**, built together by Claude and ChatGPT. The mockup
image is the visual reference. It contains no unsafe media, but stays out of
the repo (it is a raster concept, not an asset).

## Owner decisions (11 Oct)

| Topic | Decision |
|---|---|
| Look | Flow theme (navy / cyan / warm orange), airy, not boxed. Done in #480 |
| Top tabs | **Player · Generator · Training · Scene Map · Tools** (exactly as the mockup) |
| Tagline | GENERATE · PLAY · TRAIN |
| Channels | **Only real channels:** Stroke · Vibration · Suction. No "Axis 3 Thrust / Axis 4 Rotation" (the Sam Neo 2 has no such motors) |
| Playback speed | **Build** 0.5× – 2×. Video and device stay in sync (script time scaled) |
| Auto sync (audio) | **Build**: estimate the script offset from the video audio. It is a suggestion; the user confirms |
| Functions | Every control in the mockup that maps to a real feature works. New features are built where decided. Nothing is shown that does nothing |

## Target layout (from the mockup)

- **Top bar:**
  - SN Flow icon + "SamNPlayer" (SamN orange, Player cyan) + tagline;
  - tabs Player / Generator / Training / Scene Map / Tools;
  - right: open file (folder), Settings (gear).
- **Left: Library**
  - Local Files, Recent, Favorites, Playlists;
  - Scripts: Imported / Generated / Edited;
  - Tools: Scene Map, Audio Analysis, BPM Detection;
  - Quick Access drop zone.
- **Centre: Player**
  - video file selector (with recent files);
  - icons: note/comment, bookmark, snapshot, fullscreen;
  - the video;
  - progress bar;
  - transport: loop, sleep timer, speed, previous, big circular play, next, volume, fullscreen.
- **Right: Inspector** with tabs **Devices · Audio · Playback · View**
  - Devices:
    - Enable device + profile (strength preset);
    - a row per real channel: toggle, level %, live mini wave, settings;
    - Mode (Recipe / Axes), Sync (video sync).
  - Effects / Curve / Limits / Advanced:
    - smoothing, *Sync with audio* (audio vibration), Invert, Reduce peaks (max speed);
    - knobs Intensity / Response.
  - Cards:
    - **Audio Analysis**: BPM with waveform, "From audio", Audio Vibration;
    - **Script Tools**: Write into script, Apply offset ±, Auto sync (audio).
- **Bottom: Timeline** with tabs **Timeline · Chapters · Bookmarks · Scene Map · Audio Analysis**
  - time ruler;
  - lane name column with colour dots;
  - stacked lanes (Stroke / Vibration / Suction) + Audio Level;
  - shared playhead;
  - scene markers (diamonds) + scene thumbnail strip, with "+" to add a marker.

## Player workspace (Owner 11 Oct, evening) — supersedes the five top tabs

The Generator was growing into the Player, so the Owner merged them: **top tabs
Player · Training · Tools**. The **Player** tab is one workspace with the view
switch **Play · Create · Edit**; **Scene Map** lives inside Create (and its
markers are on in Create / Edit, opt-in in Play). Coordination: #477.

| # | Step | Owner | Notes |
|---|---|---|---|
| W1 ✅ | Workspace shell: `#ws-view` (Play · Create · Edit), Create = Generator panel (same IDs), Scene Map button inside Create, Edit = Player with curve editing + scene strip on, Generator result opens in Edit; top bar reduced to three tabs | Claude | `data-tab="generator"` / `"scenemap"` stay as routes (hidden shim buttons), so `switchTab()` and old clicks keep working |
| W2 ✅ | Edit view: undo / redo for curve dots and range tools (Cap / Scale / Delete), Ctrl+Z / Ctrl+Y; scene strip + "+" on in Edit (W1) | Claude | History per loaded script (50 steps), axis before / after, written back through SaveScriptAxisActions |
| W3 ✅ | One clip for Create and Play: Create step 1 offers the Play video ("Use Player video"), a clip chosen in Create is in Play's recent videos; a Generator result opens in Edit (W1), Play from there | Claude | Events `gen:video` (Create chose a clip); nothing starts automatically (no tip-find on a switch) |
| W4 (structure ✅) | Training and Tools in the same shell: full width, two columns (Training: settings · live; Device: connect · tests), no right info column on Training / Tools, Tools sub-nav aligned; look by ChatGPT; QA 1366×768 / 1920×1080 | Claude + ChatGPT | Hooks: `.tr-layout` / `.tr-col-settings` / `.tr-col-live`, `.dev-layout` / `.dev-col-connect` / `.dev-col-test` (IDs unchanged) |
| G | **Graphics** (Owner: ChatGPT generates them): icon set, background art, empty-state illustrations, no-video poster — list in #477 | ChatGPT | Files under `frontend/src/assets/flow/`; Claude wires them in |

## Steps — one PR each, merged on green CI

| # | Step | Owner | Notes |
|---|---|---|---|
| S1 | Flow theme + compact Play layout | Claude | ✅ #480 |
| S2 | **App shell**: top bar with tabs (Player, Generator, Training, Scene Map, Tools), folder + gear, logo/wordmark/tagline; rail removed; Tools sub-nav kept | Claude | IDs and `data-tab` values unchanged; Scene Map tab shows the Generator scene-map section for the current video |
| S3 ✅ | **Player 3-column shell**: Library left, Inspector right (tabs Devices / Audio / Playback / View), timeline tabs bottom; existing controls moved into their tab, IDs unchanged | Claude | Collapsible panels, state remembered |
| S4 ✅ Player | **Timeline core**: time ruler, lane names with colour dots, Audio Level lane (envelope), scene markers (diamonds) + "+" add, thumbnail strip | Claude | Go: thumbnails via ffmpeg (cached), audio envelope reused. **Owner 11 Oct:** scene images/markers are mainly for the **Generator** (on there); the Player has them too but **off by default** (toggle under View). S4a ruler + Audio Level lane (#484); S4b scene strip: thumbnails (`GetVideoThumbnails`, memory cache), diamonds = chapter starts, "+" adds one. Generator side: with S9b |
| S5 ✅ | **Transport**: big circular play, loop, previous / next, volume, fullscreen, **speed 0.5–2×** (Go: playback time scale), sleep timer | Claude | Speed is new core; device sync tested |
| S6 ✅ | **Library**: Recent, Favorites, Playlists, Scripts Imported / Generated / Edited (Go library index), Tools shortcuts, Quick Access drop zone, video selector dropdown | Claude | Local only (localStorage). Favorites (star), recent videos menu, Scripts: Imported (opened) / Generated (Generator result) / Edited (saved edit). Drops work app-wide (video → Generator, script → Player) |
| S7 (S7a ✅, S7b ✅) | **Inspector, Devices + Effects**: per-channel enable + level + live wave, profile, Mode, Sync; Effects / Curve / Limits / Advanced; knobs | Claude | Per-channel level is new core (device-safe). S7a: channel toggle + level + live bar, video bar with snapshot (#487). **S7b (Owner 11 Oct):** knobs Intensity = overall output of vibration + suction (master over the channel levels, live), Response = inverse of Play smoothing (applies from the next Play). Effects also shows **Sync with audio** (= the real "Vibration from video audio" switch). **Reduce peaks** (Owner 11 Oct: needed): soft limiter in the player core — vibration / suction above 60 % are compressed 0.4×, so 100 % comes out at 76 %; live, files unchanged. Not built: *Invert* — the Neo 2 has no stroke motor, so it would change only some profiles and nothing in axes mode. Timeline: lane-name column (Stroke · Vibration · Suction · Audio Level · Scene Markers) |
| S8 (S8a ✅, S8b ✅) | **Audio Analysis + Script Tools cards**, incl. **Auto sync (audio)** (Go: offset estimate audio↔script, suggest-only) | Claude | Needs a measurement on real clips. S8a: Audio Analysis card — whole-video waveform, BPM readout + beat grid, From audio, click seeks. **S8b (Owner 11 Oct: build now, measure later):** Auto sync in Script Tools — script motion speed vs. sound loudness, first 10 min, ±3 s; confidence high / low / none; Apply sets the normal offset. Thresholds still to be measured on real clips |
| S9 (icon ✅) | **Brand assets**: Flow icon (EXE ICO 16–256, 1024 PNG), banner, website/README | Claude integrates; ChatGPT + Owner supply the final files | Exact files, no redraw. **Owner 11 Oct:** the Flow banner (ChatGPT) is the brand; ChatGPT's app icon (transparent rounded tile, made for the EXE) is the top-bar, app and EXE icon, scaled only (`logo-universal.png`, `build/appicon.png`, `build/windows/icon.ico` 16–256, `docs/media` + `website/media` logo); banner on README |
| S9b | **Editor mode, Generator first** (Owner, 11 Oct): the Library gets a Tools → **Editor** entry (Player: curve edit of the loaded script, done with S4a). The editor in the **Generator** must become clearly better: a review/edit step with the same curve editor (drag / add / delete dots, range tools, undo), the multi-axis lanes, ruler and audio lane | Claude | Scope to be confirmed with the Owner before building |
| S10 | **Other tabs** in the same style: Generator, Training, Scene Map, Tools (Device / Log / Settings) | Claude | Shared cards, buttons, spacing |
| S11 | **Release 1.0**: `VERSION` 1.0.0, CHANGELOG, handbook, screenshots, QA at 1366×768 and 1920×1080; the Owner pushes the tag | Claude + ChatGPT | Full test matrix (`docs/QA_CHECK_*`) |

**Split (Owner, 11 Oct): Claude builds the core and the structure; ChatGPT builds the look.**
ChatGPT works on its own branch, ideally in a **new** stylesheet
(`src/theme_flow.css`, imported once by Claude) so nothing can be overwritten.
ChatGPT never pushes to `claude/*` branches.

**ChatGPT, per step:**
- acceptance checklist and visual review on each PR;
- copy and labels;
- handbook and README text for the new names;
- brand asset handover (S9);
- public-site alignment after S9.

ChatGPT doesn't edit `playback.js`, `main.js`, `index.html` or `style.css` while a Claude step PR is open. Hooks are listed in `docs/PLAY_UI_CONTRACT.md`.

## Not in 1.0 (unless the Owner asks)
- Thrust / Rotation axes **in 1.0**. Owner, 11 Oct: other devices (The Handy, multi-axis strokers, vibrators) must be fully supported, and their curves appear when the device is connected. That is the next block after W4: steps D1–D5 in `docs/DEVICE_SUPPORT.md`. Until then every V1 step keeps channels data-driven, not hard-wired to the Neo 2.
- The "Frequency" knob: Neo 2 vibration has intensity only, so the mockup knob row becomes Intensity · Response.
- Picture-in-picture.
- Frameless window controls: the min / max / close buttons in the mockup are the OS frame.

## Rules
- Every step:
  - keeps all IDs and bindings;
  - has tests for new behaviour;
  - runs the full frontend suite, Go for both editions, and Python.
- Before/after screenshots at 1366×768 and 1920×1080.
- Board row before each step (`docs/AGENT_COORD.md`).
