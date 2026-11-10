# What is left: SamNPlayer 2 against 0.7.0 and 0.8

Made on 8 Nov 2026 from the audit in [`PARITY.md`](PARITY.md) (113 open and 139 partly lines of 678; 73 % by the script `ci/parity_status.py`). References are to `PARITY.md` sections; "after 0.7.0" is its last section. Effort: S = hours, M = a day or two, L = several days or a slice of its own. Order inside each group is by user value first, effort second. Not built or run for this list; it is read from the code and tests on `main`.

## (a) 0.7 gaps

### Quick wins (S)

1. ~~**Player key tests**~~ — done (`headerKeysAndShortcuts`: Space, arrows, `+` `-`, L, T, O; header + keys hint).
2. ~~**Undo / Redo on the Player's curve edit**~~ — done earlier (Ctrl+Z / Ctrl+Y + Undo/Redo under Curve edit).
3. **Plain settings keys** (§2.3): `generator.reportPath`, `device.diagnosticsHistoryPath`, the `playback.feel_heatbands` pair; plus the diagnostics history list in Settings → Device (§8, §13 `GetDiagnosticsHistory`).
4. ~~**Window and brand wording**~~ — replaced (D-150 / D-151): "SamNPlayer Next", 1440×900, two-colour wordmark + Next.
5. ~~**Player polish**~~ — header "Player" / "No Emotion Script selected", lane hover tips, heatmap wheel zoom + `pb-marker-hint` chip, keys-hint chip (remaining: soft-curve edit-dots hint belongs to the Generator editor).
6. **Settings → Hardware and metrics** (§2.2 last collapsible, §13 `GetHardwareInfo`): "Check hardware" is Settings → System (D-162 / D-167). Still missing: `generator.reportPath`, "Show summary", "Show model", "Learn from judgments".

### Medium (M)

7. ~~**Scene strip thumbnails in the Player**~~ — done (#211; 72 px decode quality pass follows).
8. **Script analysis** (§3 analysis summary, §9 `AnalyzeScript`, §13 `ScriptChapters`): motion states (pause, build, steady, crescendo, wind-down) in core with a test, shown in Script Doctor.
9. ~~**Polarity suggestion, ring-down, apply O-zone**~~ — done (`ringdown.h`; Player → Playback → Suggest: Check polarity, Apply suggested O-zone, Ring-down at playhead / 2 cycles). `polarity.h` and `ozone.h` (`AutoOZoneMarkers`) were already in.
10. **Improve panel** (§4 review panel, §9 Improve ops, §13 `ImproveGeneratedScript`): Start / End trim, align fill to audio tempo, audio check, rhythm bridge, repair span. Fill and heal already run through Optimize for.
11. **Generator Advanced leftovers** (§4 Advanced groups): Auto-Retry, Per-scene ROI, the audio check switch, the MT-Debug trajectory switch and line overlay, the live probe.
12. **Create queue from drops** (§1 drop routing, §4 queue and `drop:video`): several videos dropped build the queue; posters and durations in the list.
13. **Feedback card and run metrics** (§4 feedback card, §9 metrics, §13 `SubmitFeedback`, `ReportSummary`, report methods): a local JSONL per run and the three-verdict card.
14. **Quality Doctor extras** (§4 quality box): gap-window count, audio tempo check, Speech-Hold line.
15. **Speech-Hold / Feel strip and bands** (§3 segments panel and bands, §4 strip): needs `audio_check` segments from the engine first (`audio_envelope.h` exists).
16. **Right sidebar cards** (§1): Device, Script, Quality with the two live bars; or decide to drop them (the Inspector covers most of it) and log the decision.

### Large blocks (L), in the order the Owner is likely to want them

17. **AI tip detection in the Generator** (§4 AI assist, pending target, Suggest Tip+2nd; §13 `DetectExpectedTipROI`, `ListAIDetections`, `CheckAIRoiAvailable`; §2.2 AI region detection and class settings): a model path, strict class search, a proposal that needs Apply. `vision/` and the NudeNet teacher exist; the Generator does not call them yet. The trained student from AI Train should plug in here.
18. **Scene Map and rhythm grid** (§7, §4 scene-map overlay and marks, §9 `.samn` `sceneMap`, §13 `ScanSceneMap` and the learning export methods): the Editor already has scene cards; the rhythm heatmap, marks and persistence are missing (needs the rhythm grid of P3.4).
19. **AI Train and Benchmark, plugin mode** (§6, §13 benchmark and ROI methods): label editor, export, set-up and training run (`TrainController`) are in. Left: Compare page (`ScoreScriptPair` UI, `SuggestBenchmarkPairBesideVideo`), golden-clip runner page and history (the command line `samn_compare` and `samn_bench` exist), wheel zoom and pan in the label editor, counts per class and split, DirectML / Apple MPS, "Use in Compare".
20. **Plugins** (§2.2 Plugins collapsible, §9 plugin host, §13 `PluginsDir` and three more): drop-folder packs with a manifest and "Install pack…". Today a plugin mode is a built-in page behind a switch (D-155); D-163 wants extensible drivers.
21. **Help texts and handbook** (§12, §1 help chips, after 0.7.0 handbook line): the 189 texts exist in `HelpTexts.js`; Player Inspector also places Curve/Drive, contact curve, strength, Auto Extended-O, Softer/Stronger scale, secondary O-marker intensity, Extended-O Minimum, contact-marks toggle, speed-hl threshold, stroker Motion/Speed limit; Timeline places chapter/bookmark Add action chips; Settings Device places Raw value test and Device diagnostics chips (plus earlier Loop/BPM/Contact off/FPS-Snap and Generator/Settings chips). Still missing: more placements, Training and the Library; handbook parts for audio vibration, BPM from audio, Sound Curve, Auto sync.
22. **Opt-in AI helpers** (§4 power-user scene memory, AI draft, contact points; §13 `SuggestProfile`, `LabelScene`, `TrainMotionProfileModel`, `DraftAIScript`, `GenerateContactPointsForVideo`): lowest value; each needs an Owner go-ahead (D-057 keeps AI from writing the curve).

### Needs an Owner call first

- **`.sam` sidecar** (§9): 0.7 writes and reads it; SamNPlayer 2 maps contact intensity live instead. Keep, port or drop?
- **Donate links** (§1 Donate pill, §2.5): wait for the URLs (D-022); the GitHub link is in (D-153).
- **Device shell graphic and ring polish** (§8 status block, §11): pure look; judge against `docs/visual-target/`.

## (b) 0.8 gaps

From the after-0.7.0 section and `ROADMAP.md` (b). The ROADMAP rows were not re-audited in this pass; their state is what the ROADMAP says, checked only where a feature name was easy to grep.

1. ~~**Hide panels button and Alt+B**~~ — done (#212: top-bar button + Alt+B).
2. **Handbook text for 0.8 features** (after 0.7.0). S, see (a) 21.
3. **Tip coast and "fail closed" rules** (after 0.7.0, generator rules): cancel and stale results are tested; tip coast and the no-silent-fallback rule are not. M, belongs to the engine slices in `AGENT_STATE.md` (camera compensation next).
4. **Release contract** (after 0.7.0, D14): only `ci.yml` exists, no release workflow; write the guard with the first release. M.
5. **Audio files with a script (MP3, FLAC)**, **Library as tiles**, **skip long pauses**, **shortcut sheet and custom shortcuts**, **device wizard** (ROADMAP v0.8 items 3 to 6): all open. Library tiles and the shortcut sheet are the best next ones (S to M each); the wizard is M.
6. **Beat bar: audio onset fallback** and the beat-bar overlay in the Editor (ROADMAP): M.
7. **Editor E1 to E10** (ROADMAP): undo, range tools, zoom, invert and the lane editor are in the Generator; lasso / select-by-peaks, clipboard, transform library, patterns, compare lane, device preview "Arm", export pack and the 100 000-point scale target are not confirmed. L, one step at a time.
8. **Next Level capabilities CAP-01 to CAP-12** and the golden benchmark gate (ROADMAP): the gate is partly there (`samn_bench` baseline of 6 Nov, `samn_compare`); Project Recovery, Smart Batch, Quality Compare and the rest are specified only. L.
9. **Native Handy, follow an external player, session recap, Vorze CSV, VR** (ROADMAP "Later"): not started, no plan yet.
10. **Windows code signing, Sentry for development builds** (ROADMAP): need the Owner (certificate, DSN).

## (c) Dropped or replaced on purpose

Each has a decision in `DECISIONS.md`; the matching `PARITY.md` lines are marked replaced or dropped.

- **No Suite / edition split** (D-127, D-141): one build, GPL-3.0-or-later, no EULA gate or edition switch.
- **No Tools Home page, runtime health, cache, "Install video tools"** (D-148): FFmpeg and OpenCV are linked in; Settings → System (D-162) covers the health check.
- **Modes and navigation** (D-137, D-145, D-150): Player · Generator · Training tabs, the Generator is the Editor, the Scene Map is part of the Editor, no mode deck or `M` key, no Play / Create / Edit control; Settings, Log and About behind the gear (D-146).
- **Updates: check and link only** (D-147): no patch channel, download or restart.
- **AI Train as a plugin mode with its own tools** (D-155): label tracks and the label editor replace the 12-box sample grid and the review grid; RF-DETR in its own pinned environment replaces the requirement files and the ultralytics set-up.
- **No local video server, no separate Generator video element** (replaced, D-154): QtMultimedia plays the file, the Generator's centre is the Player's stage.
- **Training stage** (D-152): abstract Flow figure instead of the 24 pixel frames; 0.7's own plan-preview SVG was already hidden in 0.7.0 (#538).
- **Not ported** (D-006): the seven-day community preview.
