# SamNPlayer V1 — Owner-approved five-screen visual targets (12 Oct 2026)

**Binding visual acceptance specification — Owner direction, 12 October 2026.** The five concept images shown in the ChatGPT conversation on 12 October are the **target for the final GUI**, not general inspiration. Claude and ChatGPT should match them closely while ensuring every visible operation maps to implemented behavior. Do not treat mockup illustrations as proof that a function already exists.

## Reference images (binary upload requested)

The Owner approved five separate visuals, in this order:
1. `docs/visual-target/player.png` — Player page with video stage, Flow brand, devices, effects, multichannel curves and scene-strip timeline.
2. `docs/visual-target/create.png` — Generator / Create, 5-step workflow, sources, preview, channel-curve preview and controls.
3. `docs/visual-target/training.png` — Training Studio, animated circular meters, phases, controls, live graphs, session timeline.
4. `docs/visual-target/scene-map.png` — Scene-map navigation, thumbnails, chapter ranges, markers and scene properties.
5. `docs/visual-target/tools.png` — Tools with device management, audio analysis, BPM, script utilities, diagnostics.

**Image attachment status:** These filenames are *reserved targets*, **not committed image assets**. The current GitHub connector can write this UTF-8 specification but cannot directly access the ChatGPT image-generation binary files. Claude / a maintainer should upload the five approved PNGs to these exact paths and link them in this document; avoid substituting recreated or other images without Owner approval. The detailed visual reference is the set of five image outputs in the Owner's ChatGPT conversation of 12 Oct, 08:18–08:24 UTC.

## Owner's intent (12 Oct, refined)

- The UI **must look like the approved images**: no drifting to generic/plain forms. Prioritize meaningful fidelity to layout geometry, glass surfaces, negative space, colors, illuminated controls, logo and visual hierarchy.
- **Features must be genuine.** Curves, audio/sound curves, invert-curve operations, presets, training views and device capabilities should appear where supported. Do not remove an achievable feature just because a reference picture lacks it.
- **Capability-dependent axes are ALLOWED and desired:** dynamically show extra axes/channels when the detected connected device/profile/engine supports them; do not hard-code three axes as a general UI ceiling. For devices with only Stroke, Vibration, Suction, show those and hide unsupported channels. The mockup's four axes are illustrative, **not** a mandate to fabricate axis data.
- **Sound Curve and Invert Curve:** provide controls/workflows when supported by the actual processing path, distinguish curve inversion from output inversion and audio-vibration where necessary. If not yet implemented, add a clearly scoped real implementation task rather than a fake clickable control. Never delete real functionality to satisfy image symmetry.
- Use the technical environment that achieves the target. First evaluate Wails/Chromium WebView CSS/SVG/Canvas/WebGL feasibility (gradients, curved graphs, animated rings, effects are technically possible here). Switching to a Go-native GUI or another UI toolkit needs a measurable blocker, explicit migration tradeoff, and Owner approval; **do not use framework choice to excuse visual non-delivery**.

## Existing merged IA is authoritative where mockups differ

The drawings retain an older `Player / Generator / Training / Scene Map / Tools` top nav. The *working* #494/#498 structure is `Player / Training / Tools`, with `Play / Create / Edit` inside Player, and Scene Map inside relevant workspace/views. **Keep this functioning routing** rather than regress it to the old static mockup. Reinterpret the new navigation using the same lighting, spacing and icon treatments as the images. Do not change architecture without Claude / Owner coordination.

## Per-view visual acceptance

### Player (Play)
- Left compact Library with file groups, recents and favorites; prominent central stage, empty/idle stage uses triangular Flow mark and cyan/orange trails; when playing, real video occupies stage.
- Central large circular Play ↔ Pause/Resume button with animated Flow styling; Stop remains a separate discrete action. Play/Pause/Stop must convey state accessibly.
- Inspector on the right, device-specific channel rows and controls, effect group, audio analysis, BPM and scripting tools.
- Full-width lower multi-lane **real data** curve timeline with distinct axes, scrubbing and meaningful scene thumbnails when available. Audio/sound curve should be displayed when supported.
- Avoid claims of connected devices or made-up meter values.

### Create (Generator)
- Project/source header; luminous steps 1–5; central video/source preview and source selection; right purposeful presets/settings grouped in clean glass cards; lower multichannel curve preview with markers; prominent generate action.
- Reuse existing `generator.js` model, queue, validation and real functional controls. Display only modes actually available, including audio-derived curves where implemented.

### Edit and Scene Map
- Preserve dedicated **Edit** interaction model, linked to Player and shared script state, with undo/redo/range tools and focused curve editing.
- Scene Map visual destination: chapter-colored bands, scene thumbnails with selection, markers/tags, preview and details inspector as supported by actual scene metadata. Blank or unavailable data is communicated, not invented.
- Scene Map is not required as a standalone top tab.

### Training
- Training programs/presets on left; centered animated vibration/suction rings or a channel-adaptive equivalent; phase/progress/time; major Play/Pause control and conspicuous actual emergency Stop; right session settings, **real** safety limits; below live curves and session timeline/history.
- Reuse training engine's actual programs, script library, sessions and history. The rings respond to real channel output. Safety controls must reflect actual supported backend operations.

### Tools
- Manage real devices/profiles, connected states, audio/BPM analysis, script tools, diagnostics and settings. Use dense but legible glass card grouping, not simulated healthy status or arbitrary sample results.
- Where capabilities are unavailable, use clear empty/setup or unavailable state, not fake green ticks.

## Completion criteria (mandatory)
1. Add all five actual approved PNG reference images to `docs/visual-target/` and compare native screenshots against them.
2. Audit and preserve every existing real function/curve across all pages; add capability-based channel rendering and provide proper Sound Curve / Invert Curve paths where supported or explicitly track missing implementation.
3. Audit control wiring, keyboard access, useful aria labels, disabled states, reduced-motion, focus, color contrast and performance.
4. Screenshot smoke in at least **1366×768** and **1920×1080**, plus real Wails Windows/WebView validation. Compare the page *and empty/loading/playing/paused/disabled/device-unavailable* states.
5. Claude owns routing/core/HTML/JS/Go/testing and declares actual feature mapping; ChatGPT owns CSS/visual polish and comparison. Changes via own branch + PR; don't alter other agent's active files.
6. No declaration that any image was uploaded, UI completed or native validation passed until evidenced by commits/artifacts/tests.
