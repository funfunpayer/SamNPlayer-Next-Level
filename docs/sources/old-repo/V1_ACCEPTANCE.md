# SamNPlayer V1.0 — visual and functional acceptance checks

Reference: Owner-approved navy/cyan/orange Flow mockup (11 October 2026).
Source of truth for features: `docs/V1_PLAN.md`. **This checklist is not evidence of passing tests.**

## Global acceptance (every step)
- [ ] Every visible interactive control performs an actual action, or is clearly labeled unavailable.
- [ ] At 1366×768 and 1920×1080, navigation, video, transport, inspector, and timeline remain reachable without horizontal document scrolling.
- [ ] Real channels only: Stroke, Vibration, Suction. Audio Level is analysis data, **not** a device axis.
- [ ] Keyboard focus visible; buttons have accessible names; toggles expose state; reduced-motion disables decorative motion.
- [ ] No example video, fabricated data, or people in the empty player stage.
- [ ] Static glows/gradients preferred over GPU-heavy animated backgrounds; no blur effects requiring continuous repaint.
- [ ] Test on real Wails build before release; browser preview with mocked bindings is only visual/smoke evidence.
- [ ] Compare screenshot against approved Flow reference, record differences, and obtain Owner review.

## S2 — App shell
- [ ] Top tabs: Player, Generator, Training, Scene Map, Tools; keyboard navigation and active tab work.
- [ ] Folder and gear open real destinations. No legacy rail competing for space.
- [ ] Brand typography: SamN orange, Player cyan; tagline GENERATE PLAY TRAIN.

## S3 — Three-column Player
- [ ] Library left, video center, Inspector right; timeline below.
- [ ] Panel collapse/restore preserves state, playback and event listeners.
- [ ] Narrow layout does not cover the video or block the controls.

## S4 — Timeline
- [ ] Time ruler, playhead, zoom and audio envelope share the same time scale.
- [ ] Stroke, Vibration and Suction have distinguishable colors and labels.
- [ ] Audio Level appears only with linked video and is excluded from axis commands.
- [ ] Scene markers, thumbnail strip and add action work; optional Player scene visuals default off as planned.

## S5 — Transport
- [ ] Play/pause, previous/next, repeat, volume, fullscreen, sleep timer are connected.
- [ ] Playback speed 0.5×–2× synchronizes video and device script time.
- [ ] Transport is operable by keyboard and its disabled states are meaningful.

## S6 — Library
- [ ] Recent, Favorites, Playlists and Imported/Generated/Edited filter real local items.
- [ ] File/video selector and Quick Access drop zone work.
- [ ] Protected library content remains protected; UI never implies encryption that backend does not provide.

## S7 — Inspector
- [ ] Real device channels only, each with enable/level/state wired to backend.
- [ ] Profile, mode, sync, effects and limits reflect real device behavior.
- [ ] Unsafe or unsupported device operations cannot be triggered through decorative controls.

## S8 — Audio and script tools
- [ ] Audio BPM detection and vibration clearly distinguish measured data from script values.
- [ ] Auto sync offers an offset suggestion and requires explicit confirmation.
- [ ] Write into script is not confused with non-destructive preview.

## S9 — Branding
- [ ] Owner-approved triangular Flow logo used consistently, not a circular replacement.
- [ ] 1024 PNG, Windows ICO (16–256) and banner validated at target sizes.
- [ ] Empty player stage contains brand art only, not demo media.

## S10 — Other tabs
- [ ] Generator, Training, Scene Map and Tools share tokens, spacing and focus states.
- [ ] Their existing flows and backend bindings still work.

## S11 — Release
- [ ] 1366×768 and 1920×1080 screenshots from a running desktop build.
- [ ] Automated frontend checks and backend tests pass on release commit.
- [ ] Version, changelog, handbook and site agree with the actual release artifacts.
- [ ] Owner signs off on final screenshot comparison and functional checklist.

## Evidence record template
For each step: commit/PR, screenshot path, resolution, tests executed, pass/fail, remaining gaps, reviewer, date.
