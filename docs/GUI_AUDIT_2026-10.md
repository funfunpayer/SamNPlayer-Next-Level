# GUI audit — 11 Oct 2026

Owner request (11 Oct): check at the end that every function is in the GUI,
how modular it is, and how AI is anchored. State: `main` after #472.

## 1. Every control is wired

- **333** buttons / inputs / selects with an `id` in `index.html` and
  `frontend/src/*.js`. Every one is referenced by code (listener or read).
  The 8 apparent misses (`*-seg-f-holding|gentle|intense|climax`) are built in
  a template loop and wired the same way.
- **107** Playwright frontend tests, all green on `main`.
- Language: all product copy English (`docs/LANGUAGE.md`).

## 2. Backend API not reached from the GUI

191 Wails bindings; **12** are never called by the frontend:

| Binding | File | Status | Recommendation |
|---|---|---|---|
| `GetScriptActions`, `SaveScriptActions` | `app_editor.go` | Superseded by `Get/SaveScriptAxisActions('general')`; covered by tests | Keep for now (tests, OFS compat); remove in a later cleanup |
| `GetPlaybackSource` | `app_samn.go` | Setter used, getter not; tests | Keep (cheap) or read it on load |
| `GetVibrationCurve` | `app_playback.go` | Replaced by `GetVibrationCurvePreview` | Remove later |
| `SuggestOZone` | `app_suggest.go` | Used internally by Go; not exposed in UI | Keep (internal) |
| `BootstrapRoiTrainingSample(Ex)` | `app_roi_training.go` | Replaced by `BootstrapRoiTrainingRegions`; studio-gated | Remove later |
| `WriteAIRequirementFiles` | `app_roi_training.go` | Studio-gated, no UI | Remove or wire into AI Train |
| `InvertScriptAtPath`, `LabelScene`, `SuggestBackend`, `SupportSignalsAvailable` | various | No UI, no tests | Remove in a cleanup PR |

None of these is a missing feature: each has a newer, wired replacement or is
internal. Cleanup is optional and low risk.

## 3. Modularity

- **Go:** domain packages are separate from the GUI — `funscript`, `samn`,
  `sam` (device feel), `device`, `player`, `generator`, `motionx`, `videox`,
  `update`, `legal`, `edition`, `pluginhost`, `logging`. The Wails layer
  (`cmd/gui-wails`, 44 files) is one thin `app_*.go` file per area.
- **Frontend:** one module per tab (`playback.js`, `generator.js`,
  `training.js`, `roi_training.js`, `benchmark.js`, `device.js`,
  `settings.js`, `log.js`) plus shared helpers (`legal.js`, `help.js`,
  `notify.js`, `ozone_ui.js`, ...).
- **Weak spot:** `generator.js` (5 059 lines) and `playback.js` (4 102 lines)
  are large. Suggested later split: Play → `pb_curve.js` (curve, lanes,
  editor), `pb_markers.js` (O-markers, bookmarks, chapters), `pb_audio.js`
  (BPM, segments, audio vibration). Create → steps, scene map, contact
  points. Pure refactor; no behaviour change; do it only with the existing
  tests green before and after.

## 4. How AI is anchored

AI is **optional everywhere**: classical CV (CSRT / optical flow) creates
scripts without any AI install, and Play never needs AI.

| Layer | Where | Customer build | Studio build (`-tags studio`) |
|---|---|---|---|
| AI ROI detection (YOLO/ONNX, tip find) | `generator/ai_roi.py`, `auto_roi.py`; Settings → AI setup | ✅ optional install | ✅ |
| Local AI server (scene suggestion, VLM) | `CheckAIServerAvailable`; Create → Suggest | ✅ optional, user names the server | ✅ |
| Contact points (NudeNet / scene roles) | `contact_detector.py`, Create → Contact points | ✅ | ✅ |
| AI draft from own exports | Create → "AI draft (experimental)" | hidden (depends on learning exports) | ✅ |
| ROI model training, motion-profile model, quality model | AI Train tab, Settings → Learn from judgments | ❌ tab hidden + `requireStudio()` | ✅ |
| Learning data (scene-map export, teacher candidates, AI script imitation) | Create → Scene map → Learning | ❌ hidden + gated | ✅ |
| Golden-clip benchmark | Benchmark tab | ❌ tab hidden + gated | ✅ |

- **Two locks:** the UI hides `[data-studio]` (CSS via `html[data-edition]`)
  *and* every studio method returns `errStudioOnly` in the customer build
  (37 gated call sites in 8 files). Bindings are identical in both builds, so
  the Wails API stays stable.
- **Open item:** the embedded training Python is still compiled into both
  builds (ChatGPT lane *StudioEmbed*, criteria in the #467 comment). Not
  reachable in the customer build because of the gates, but it ships bytes it
  never uses.
- **Privacy:** nothing leaves the device unless the user names a server;
  learning data stays local (EULA §9).

## 5. Done in this round (11 Oct)

- #469 Branding: lilac SN logo (vector, app + exe icon), calmer palette,
  Settings → About with the partner graphic (URL placeholder).
- #470 Play layout: bigger video, collapsible tools column (⟩ / **T**), info
  column hidden on Play.
- #472 Multi-axis view: colour per channel, legend chips, feel-channel lanes,
  stroke ghost.

## 6. Owner-gated

- Push tag `v0.5.46` (proxy blocks it for Claude).
- Patreon / PayPal URLs → `legal.PatreonURL` / `legal.PayPalURL`.
- Partner website URL → `legal.PartnerSiteURL`.
