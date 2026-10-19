# Test parity: SamNPlayer 0.7 tests -> SamNPlayer 2

**Purpose.** Nothing that 0.7 once proved may get lost. This file maps every test of the old Go/Python project to the SamNPlayer 2 tests and lists what is missing.

**Rule.** When a module is ported, its old tests are ported with it, in the same PR. A port is not done while its row in this file says `todo-now`. When a module is ported and a row is still `todo-later`, fix the row in the same PR (it becomes `todo-now`, then `ported`).

**Related files.** `PARITY.md` lists the 0.7 *functions* (the "what"); this file lists the 0.7 *tests* (the "proof"). `MODULES.md` says what is ported. `ROADMAP.md` and `P3_GENERATOR.md` name the phases used below.

## How to use this file

1. **Porting a module?** Open the package table of its old package, find every `todo-later` row of that module, port those tests with the code, and change the row to `ported` (with the new test file and case name).
2. **Looking for work?** Go to [Port now](#4-port-now-work-list). Every item there is code that already exists in SamNPlayer 2 but has no test yet.
3. **Adding a test?** Put the origin in a comment, as the existing tests do: `// Go: TestXxx` or `// 0.7 funscript/..._test.go`. This file is built from those comments; a test without one is hard to find.
4. **Dropping a test on purpose?** Mark it `n/a` with a one-line reason, and record the decision in `DECISIONS.md` if it drops behaviour.

### Status words

| Status | Meaning |
|---|---|
| **ported** | The same check exists in a new test (file and case named). A note says `partial` where an assertion of the old test is missing; those gaps are in list B of Port now. |
| **covered** | An equivalent check exists in another form (for example a Qt Test on the controller instead of a Go test on the Wails `App`). The row says where. |
| **todo-now** | The code under test is already in `core/` or `app/`, but the test is missing. These matter most. |
| **todo-later** | The code is not ported yet. The row names the phase (`PLAN.md`: P1-P5, `P3_GENERATOR.md`: P3.0-P3.9). |
| **n/a** | The test is about Go, Wails, a build tag, the embedded Python, ffmpeg handling or the JS DOM and has no meaning in C++/Qt. The row says why. |

### What is counted

- A **test** is one `func TestXxx` (Go) or one `def test_xxx` (Python). Python scripts that have no `def test_` (almost all of the frontend and many generator tests are a `main()` with many `check(...)` calls) count as **one test per file**; the number of checks is in the table. Subtests (`t.Run`) are not counted separately.
- Old suite: **222** Go test files and **209** Python test files (431 files). They hold **821** Go test functions and **320** Python tests (script files counted once). `cmd/gui-wails/counting_device_test.go` has no test function (a helper type), so it is listed as n/a with 0 tests.
- New suite: `tests/core/*_test.cpp` (doctest, 29 files), `tests/app/*_test.cpp` (Qt Test, 11 files) and `core/python/tests/*.py` (5 files). Evidence for a mapping is one of: a Go name in a comment of the new test, the identical test name, or the same behaviour checked (named in the row).
- Method: the old tree was read with `git show origin/main:<path>`; the new tree with `grep` over `tests/`, `core/` and `app/`. "Ported code" means a function exists in `core/include/samn`, `core/src` or `app/src`.

## 1. Summary

### Totals

| | Tests | ported | covered | todo-now | todo-later | n/a |
|---|---:|---:|---:|---:|---:|---:|
| **All** | **1141** | 229 | 66 | **0** | 794 | 52 |
| Go | 821 | 210 | 32 | 0 | 532 | 47 |
| Python | 320 | 19 | 34 | 0 | 262 | 5 |

### Per old package

| Old package | Files | Tests | ported | covered | todo-now | todo-later | n/a |
|---|---:|---:|---:|---:|---:|---:|---:|
| `funscript` | 36 | 170 | 95 | 1 | 0 | 72 | 2 |
| `device` | 7 | 41 | 39 | 2 | 0 | 0 | 0 |
| `player` | 7 | 61 | 5 | 9 | 0 | 44 | 3 |
| `samn` | 2 | 5 | 5 | 0 | 0 | 0 | 0 |
| `sam` | 7 | 37 | 9 | 0 | 0 | 28 | 0 |
| `videox` | 6 | 26 | 0 | 0 | 0 | 17 | 9 |
| `update` | 3 | 14 | 0 | 0 | 0 | 14 | 0 |
| `motionx` | 2 | 10 | 4 | 0 | 0 | 6 | 0 |
| `pluginhost` | 2 | 10 | 0 | 0 | 0 | 10 | 0 |
| `edition` | 1 | 1 | 0 | 0 | 0 | 0 | 1 |
| `legal` | 1 | 2 | 0 | 0 | 0 | 2 | 0 |
| `logging` | 1 | 1 | 0 | 0 | 0 | 1 | 0 |
| `cmd/cli` | 7 | 18 | 0 | 0 | 0 | 12 | 6 |
| `cmd/gui-wails (Go)` | 68 | 188 | 16 | 18 | 0 | 141 | 13 |
| `generator (Go)` | 72 | 237 | 37 | 2 | 0 | 185 | 13 |
| `generator (Python)` | 56 | 151 | 0 | 0 | 0 | 151 | 0 |
| `native/tests (Python)` | 4 | 13 | 13 | 0 | 0 | 0 | 0 |
| `scripts/benchmark-prep (Python)` | 1 | 8 | 0 | 0 | 0 | 8 | 0 |
| `frontend (Python/Playwright UI tests)` | 148 | 148 | 6 | 34 | 0 | 103 | 5 |
| **Total** | **431** | **1141** | **229** | **66** | **0** | **794** | **52** |

`generator (Go)` includes the sub-packages `aiscript`, `bodyparts`, `posttrack`, `profilemodel`, `simpletrack`, `strokepreview`, `trackcv`, `trackutil`.

### Generator (Go) by sub-package

| Sub-package | Files | Tests | ported | covered | todo-now | todo-later | n/a |
|---|---:|---:|---:|---:|---:|---:|---:|
| `generator/(top level)` | 44 | 122 | 9 | 1 | 0 | 99 | 13 |
| `generator/aiscript` | 3 | 12 | 0 | 0 | 0 | 12 | 0 |
| `generator/bodyparts` | 1 | 4 | 0 | 0 | 0 | 4 | 0 |
| `generator/posttrack` | 3 | 9 | 6 | 0 | 0 | 3 | 0 |
| `generator/profilemodel` | 1 | 6 | 0 | 0 | 0 | 6 | 0 |
| `generator/simpletrack` | 5 | 12 | 0 | 0 | 0 | 12 | 0 |
| `generator/strokepreview` | 3 | 9 | 0 | 0 | 0 | 9 | 0 |
| `generator/trackcv` | 11 | 57 | 17 | 1 | 0 | 39 | 0 |
| `generator/trackutil` | 1 | 6 | 5 | 0 | 0 | 1 | 0 |

## 2. Where the old tests went, per package

One row per old test function (grouped when a whole file shares the same answer). `Where / why` names the new test (`tests/core`, `tests/app`, `core/python/tests`) or the phase / reason.

### 2.1 `funscript`

36 files, 170 tests: 95 ported, 1 covered, 0 todo-now, 72 todo-later, 2 n/a.

| Old file | Test function(s) | Status | Where / why |
|---|---|---|---|
| `actions_test.go` | `TestSaveActionsRoundtrip`, `TestSaveActionsClampsPosition`, `TestSaveActionsRejectsEmptyAndNegativeTime`, `TestSaveActionsPreservesUnknownFields`, `TestSaveActionsPreservesForeignCreatorNoNativePipeline` | todo-later | P4 Editor: `SaveActions` / `SaveAxisActions` (write edited points back into a .funscript, keep unknown metadata) is not ported; only `.samn` saving (`SaveDocument`) and the marks writers exist. |
| `atomic_write_test.go` | `TestWriteFileAtomicReplacesExistingFile` | ported | tests/core/fileio_test.cpp "fileio: atomic write creates, replaces and leaves no temp file" |
| `atomic_write_test.go` | `TestWriteFileAtomicCleansTempAfterRenameFailure` | ported | tests/core/fileio_test.cpp "fileio: a failed rename leaves the destination and the folder clean" |
| `audio_chapters_test.go` | `TestChaptersFromAudioSegments`, `TestChaptersFromAudioSegmentsNil` | todo-later | P3.6 (audio segments): `ChaptersFromAudioSegments` is not ported. |
| `axes_test.go` | `TestBakeAxesAndExplicitPlayback`, `TestRecipeModeIgnoresAxes` | ported | tests/core/recipe_test.cpp "axes: bake from the recipe and play from explicit axes", "axes: recipe mode ignores the stored axes" |
| `axes_test.go` | `TestSaveAxisActionsAndPlaybackSource` | todo-later | P4 Editor: `SaveAxisActions` / `SavePlaybackSource` (write `samn_axes` into a .funscript) are not ported. |
| `backend_hint_test.go` | `TestSuggestBackendAlwaysCSRT` | todo-later | P3.5 (backends): `SuggestBackend` is not ported. |
| `beat_strokes_test.go` | `TestBeatStrokesDownOnEveryBeat` | ported | core/python/tests/test_beat_strokes_parity.py::test_down_on_every_beat |
| `beat_strokes_test.go` | `TestBeatStrokesSpeedLimitAndHold` | ported | core/python/tests/test_beat_strokes_parity.py::test_speed_limit_and_hold |
| `companion_test.go` | `TestCompanionPath`, `TestLoadCompanionAxis`, `TestSaveCompanionAxisKeepsFieldsAndInversion`, `TestCreateCompanionAxis` | todo-later | P4 (companion axis files editable) and P2 (loading `<script>.<axis>.funscript` into the player): `CompanionPath`, `LoadCompanionAxis`, `SaveCompanionAxis`, `CreateCompanionAxis` are not ported. `PlaybackOutput::setScript` already takes axis tracks. |
| `compare_dataset_test.go` | `TestCompareDatasetPairsAndReport`, `TestCompareDatasetSkipsFungenBinary` | n/a | Replaced by design: the P3.8 controller takes an explicit manifest with content hashes (`samn_compare`, `engine/include/samn/engine/compare.h`, tests `tests/engine/compare_test.cpp`) instead of pairing FunGen exports by file name, which GD-05 names as a source of wrong pairs. |
| `contact_marks_test.go` | `TestContactMarksHasAreas` | todo-later | P3.6 (contact marks): `ContactMarks.HasAreas` is not ported (the struct exists in `samn_file.h`, no helper). |
| `contact_preview_test.go` | `TestPreviewContactVibrationSpanEarlier`, `TestPreviewContactVibrationImpulseQuieter` | todo-later | P3.3 (Generator preview): `PreviewContactVibration` is not ported. |
| `contact_recipe_test.go` | `TestDefaultSyncForProfile`, `TestSaveContactRecipeStrokeEmptySyncStaysIndependent`, `TestSaveContactRecipeTjEmptySyncUsesSuctionPosition`, `TestSaveContactRecipeRoundtrip` | ported | tests/core/recipe_test.cpp "recipe: profile names and default sync" (`TestDefaultSyncForProfile`); tests/core/samn_file_test.cpp "samn_file: contact settings fill an empty stroke sync with independent", "...fill an empty tj sync with suction_position", "samn_file: contact recipe round trip, switching off clears span and curve" (adapted to `Document::ApplyContactRecipe`) |
| `dense_quality_test.go` | `TestEvaluateDenseQualityReconstruction`, `TestEvaluateDenseQualityTrackerLost`, `TestEvaluateDenseQualityMotionRange`, `TestSpectralConcentrationSineVsNoise` | todo-later | P3.6 (Quality Doctor): `EvaluateDenseQuality` / `SpectralConcentration` are not ported. |
| `feel_scene_test.go` | `TestNormalizeSceneFeelAliases`, `TestSceneFeelOffKeepsOldMapping`, `TestSceneSuctionShapes`, `TestOralSceneRaisesSuction`, `TestVibrationFollowsSuction`, `TestBalance`, `TestSaveFeelRecipeRoundTrip` | ported | tests/core/recipe_test.cpp "feel: scene names and aliases", "feel: scene feel off keeps the old mapping", "feel: suction shapes per scene", "feel: oral scene raises suction", "feel: vibration follows suction", "feel: balance"; tests/core/samn_file_test.cpp "samn_file: feel settings round trip into the map options" (`TestSaveFeelRecipeRoundTrip`, adapted to `Document::ApplyFeelRecipe`). `NormalizeSceneFeel` is now declared in `samn_file.h`. |
| `feel_spatial_mapper_test.go` | `TestSpatialFeelBoostsShallowStroke`, `TestPreferSpatialAttenuatesDepthAwayFromMark`, `TestPreferSpatialKeepsSpatialPeak`, `TestPreferSpatialShortensEnvelopeOnHit` | todo-later | P3.6 (contact marks + trajectory): the spatial geometry (`SpatialContactIntensity`) is not ported, so `MapOptionsFromScript` never sets `spatial_contact`. The mapper half (depth x `SpatialDepthWeight`, envelope 0.18) exists and can be tested with the `spatial_contact` hook. |
| `feel_spatial_test.go` | `TestProximityInsideCore`, `TestProximityOutsideMargin`, `TestSpatialContactIntensityUsesTrajectory`, `TestSpatialIgnoresTipBoxOnly` | todo-later | P3.6 (contact marks + trajectory): `Proximity` / `SpatialContactIntensity` are not ported. |
| `funscript_test.go` | `TestParseQualityMetadata` | ported | tests/core/samn_file_test.cpp "samn_file: quality score and verdict are separate fields" |
| `funscript_test.go` | `TestParseDropsCorruptTimes` | ported | core/python/tests/test_funscript_parity.py::test_corrupt_times |
| `improve_test.go` | `TestTrimActionsMiddle`, `TestFillGapsInsertsPoints`, `TestFillGapsAudioHzStep`, `TestImproveScriptAutoSecondPass`, `TestImproveScriptNoDensifySlowStroke`, `TestImproveScriptTrimAndFill`, `TestHealTrackingGapsStripsInteriorAndBridges`, `TestHealTrackingGapsKeepsBoundaryAnchors`, `TestHealTrackingGapsDoesNotDensifyOutside`, `TestHealTrackingGapsMergesOverlappingAndInverts`, `TestHealTrackingGapsRefusesWhenWouldDestroyScript`, `TestImproveScriptHealTrackingGaps`, `TestImproveScriptHealPlusFillKeepsHealPointsCount`, `TestStampTrackingGapsClears` | todo-later | P3.3 (Review: trim, fill gaps, heal tracking gaps): `ImproveScript`, `TrimActions`, `FillGaps`, `HealTrackingGaps`, `StampTrackingGaps` are not ported. |
| `inverted_playback_test.go` | `TestParseTopLevelInverted` | ported | core/python/tests/test_funscript_parity.py::test_inverted (and `FunscriptView::inverted` in samn_file_test.cpp) |
| `inverted_playback_test.go` | `TestPlaybackActionsFlipsWhenInverted`, `TestPlaybackActionsPassthroughWhenNotInverted`, `TestToIntensityCurveHonorsInverted` | ported | tests/core/recipe_test.cpp "inverted: playback actions flip when inverted", "inverted: playback actions pass through when not inverted", "inverted: the intensity curve honours the inverted flag" |
| `inverted_playback_test.go` | `TestWriteInvertedClearsAndSetsFlag` | todo-later | P4 Editor (Invert / honor_inverted): `WriteInverted` is not ported. |
| `mapper_test.go` | `TestMapperStandstill` | ported | tests/core/mapper_test.cpp "mapper: standstill" |
| `mapper_test.go` | `TestMapperFastStrokeReachesFull` | ported | tests/core/mapper_test.cpp "mapper: fast stroke reaches full" |
| `mapper_test.go` | `TestMapperFloorPreservesGradations` | ported | tests/core/mapper_test.cpp "mapper: floor preserves gradations" |
| `mapper_test.go` | `TestMapperFloorKeepsCeiling` | ported | tests/core/mapper_test.cpp "mapper: floor keeps ceiling" |
| `mapper_test.go` | `TestMapperFloorRespectsChannelModes` | ported | tests/core/mapper_test.cpp "mapper: floor respects channel modes" |
| `mapper_test.go` | `TestMapperIndependentUsesPositionForSuction` | ported | tests/core/mapper_test.cpp "mapper: independent uses position for suction" |
| `mapper_test.go` | `TestMapperSmoothingStaysInRange` | ported | tests/core/mapper_test.cpp "mapper: smoothing stays in range" |
| `mapper_test.go` | `TestMapperEdgeCases` | ported | tests/core/mapper_test.cpp "mapper: edge cases" |
| `mapper_test.go` | `TestMapperFrameTimeline` | ported | tests/core/mapper_test.cpp "mapper: frame timeline" |
| `omarker_test.go` | `TestLoadOMarkersEmptyWhenAbsent` | ported | tests/core/omarker_test.cpp "o-markers: empty when absent" |
| `omarker_test.go` | `TestSaveAndLoadOMarkersRoundtrip` | ported | tests/core/omarker_test.cpp "o-markers: save and load, sorted by start" |
| `omarker_test.go` | `TestSaveOMarkersPreservesUnknownFields` | ported | tests/core/omarker_test.cpp "o-markers: same bytes as 0.7, unknown fields kept, empty list writes null" |
| `omarker_test.go` | `TestSaveOMarkersRejectsInvalidMarker` | ported | tests/core/omarker_test.cpp "o-markers: invalid markers are rejected and the file stays untouched" |
| `omarker_test.go` | `TestSaveOMarkersEmptyListClearsMarkers` | ported | tests/core/omarker_test.cpp "o-markers: same bytes as 0.7, unknown fields kept, empty list writes null" (empty list writes null) |
| `ozone_test.go` | `TestSuggestOZonePicksLatePeak`, `TestSuggestOZoneRejectsFlatTail`, `TestSuggestOZoneShortScript`, `TestSuggestSecondaryOZonesFindsEarlierWeakerPeak`, `TestSuggestSecondaryOZonesRespectsMaxCount`, `TestSuggestSecondaryOZonesNoOverlap`, `TestSuggestSecondaryOZonesNoPrimaryNoSecondary`, `TestSuggestSecondaryOZonesFlatScriptFindsNone`, `TestSuggestSecondaryOZonesMaxCountZero` | ported | `tests/core/ozone_test.cpp` (`samn::SuggestOZone`, `SuggestSecondaryOZones`; 6 cases cover all 9). The Editor button follows in P4. |
| `parse_compat_test.go` | `TestParseOFSStyleMetadata` | ported | core/python/tests/test_funscript_parity.py::test_duration_ofs; tests/core/funscript_test.cpp "funscript parse: actions, clock-string duration" |
| `parse_compat_test.go` | `TestParseFloatActions` | ported | core/python/tests/test_funscript_parity.py::test_float_actions |
| `parse_compat_test.go` | `TestLoadChaptersAndBookmarksTimestampStrings` | ported | tests/core/marks_test.cpp "marks: load chapters and bookmarks timestamp strings" |
| `phase_test.go` | `TestBestLagCorrelationShiftedSine`, `TestBestLagCorrelationInverted`, `TestBestLagCorrelationConstantUndefined`, `TestBestLagCorrelationIrregular`, `TestBestLagCorrelationLowConfidence`, `TestDiagnosePhaseTimingVsShape`, `TestWindowedBestLagCorrelationConstantLag`, `TestWindowedBestLagCorrelationDriftingLag`, `TestPeriodicityAliasingCharacterization`, `TestAliasingRiskFlag_doesNotChangeReportedLag`, `TestDominantPeriodMsRequiresGenuineLocalPeak` | ported | `tests/core/motion_fidelity_test.cpp` (`samn/motion_fidelity.h`); the aliasing characterisation is strict here (deterministic: lag -860 = -300 - 2 x 280, windows swing and flip). |
| `pipeline_hint_test.go` | `TestSuggestPipelineTwoROIUsesStroke`, `TestSuggestPipelineSmallUsesCSRTGo`, `TestSuggestPipelineLargeUsesCSRT` | todo-later | P3.5 (backends): `SuggestPipeline` is not ported. |
| `polarity_test.go` | `TestSuggestPolarityLowFirstHalf`, `TestSuggestPolarityHighFirstHalf`, `TestSuggestPolarityTooFewPoints` | todo-later | P3.3 (Review) / P4 Editor (Suggest polarity): `SuggestPolarity` is not ported. |
| `project_test.go` | `TestBookmarksRoundTrip` | ported | tests/core/marks_test.cpp "marks: bookmarks round trip" |
| `project_test.go` | `TestBookmarksAcceptFloatSeconds` | ported | tests/core/marks_test.cpp "marks: bookmarks accept float seconds" |
| `project_test.go` | `TestChaptersRoundTrip` | ported | tests/core/marks_test.cpp "marks: chapters round trip" |
| `project_test.go` | `TestProjectSidecar` | ported | tests/core/project_test.cpp "TestProjectSidecar" |
| `project_test.go` | `TestSnapMs` | ported | tests/core/range_edit_test.cpp "TestSnapMs" |
| `project_test.go` | `TestDeleteRange` | ported | tests/core/range_edit_test.cpp "TestDeleteRange" |
| `project_test.go` | `TestCapSpeedRange` | ported | tests/core/range_edit_test.cpp "TestCapSpeedRange" |
| `project_test.go` | `TestScaleRangePosFadeSoftEdges` | ported | tests/core/range_edit_test.cpp "TestScaleRangePosFadeSoftEdges" |
| `project_test.go` | `TestCapSpeedRangeCapsAllInRangeSegments` | ported | tests/core/range_edit_test.cpp "TestCapSpeedRangeCapsAllInRangeSegments" |
| `project_test.go` | `TestExportHeatmapPNG` | ported | tests/core/heatmap_image_test.cpp "TestExportHeatmapPNG" |
| `recipe_test.go` | `TestNormalizeProfile`, `TestRecipeTJSuctionOnlyNoVibration`, `TestRecipeTJSuctionNoDoubleFloor`, `TestRecipeTJContactVibrationOffByDefault`, `TestRecipeTJContactVibrationTracksPosition`, `TestRecipeTJContactVibrationBriefGraze`, `TestRecipeTJContactVibrationNeedsSpan`, `TestRecipeTJContactVibrationSpanEarlier`, `TestRecipeTJContactVibrationCurves`, `TestApplyContactCurveImpulsePeaks`, `TestRecipeTJContactVibrationMutedInTrackingGap`, `TestRecipeTJContactGapWinsOverEnvelopeAndSmoothing`, `TestRecipeStandardContactVibrationTracksPosition`, `TestRecipeTFEqualsTJ` | ported | tests/core/recipe_test.cpp (14 cases named "recipe: ..."; `TestNormalizeProfile` is "recipe: profile names and default sync", without the `IsStrokeProfile` / `AllowsContactSettings` checks, which have no C++ counterpart yet) |
| `rhythm_bridge_test.go` | `TestRhythmBridgeRestoresRegularStrokes`, `TestRhythmBridgeReadsDenseScripts`, `TestRhythmBridgeLeavesSpanWithoutRhythm`, `TestImproveHealRhythmIsOptIn`, `TestImproveRepairSpansFallsBackToLine` | todo-later | P3.3 / P3.4 (Review: heal rhythm, repair spans; rhythm grid): the rhythm bridge is not ported. |
| `ringdown_test.go` | `TestRingDownEndsAtZeroAndKeepsPrefix`, `TestRingDownClampsCycles` | todo-later | P4 Editor (Apply ring-down): `RingDown` is not ported. |
| `scene_map_test.go` | `TestEncodeDecodeSceneMapScore` | todo-later | P4 Scene Map: `EncodeSceneMapScore` / `DecodeSceneMapScore` are not ported (`score_b64` is kept as an opaque string). |
| `scene_map_test.go` | `TestSceneMapDataJSONRoundTrip` | covered | tests/core/samn_file_test.cpp "samn_file: round trip funscript <-> samn" and "samn_file: Go file reads and rewrites identically" (a SceneMapData with window, marks and score survive the .samn round trip; the snake_case keys are checked by "JSON keys and order match Go") |
| `score_test.go` | `TestScorePairIdenticalIsGood`, `TestScorePairShiftedIsReviewTiming`, `TestScorePairUnrelatedIsBad`, `TestScorePairQualityFailForcesReview` | todo-later | P3.8 (benchmark): `ScorePair` is not ported. |
| `script_quality_golden_test.go` | `TestEvaluateScriptQualityMatchesPythonGoldens` | todo-later | P3.6 (Quality Doctor): `EvaluateScriptQuality` goldens; needs the Go-written golden file. |
| `script_quality_test.go` | `TestEvaluateDeviceCompatTooClose`, `TestEvaluateDeviceCompatClean`, `TestEvaluateScriptQualityRejectsEmpty`, `TestEvaluateScriptQualityFlagsOutOfRange`, `TestEvaluateScriptQualityKind`, `TestEvaluateMotionFidelityKind`, `TestEvaluateScriptQualityFlagsUnsorted`, `TestEvaluateScriptQualityAcceptsCleanStroke`, `TestScriptQualityViaTempFile` | todo-later | P3.6 (Quality Doctor): `EvaluateScriptQuality` / `EvaluateDeviceCompat` / `EvaluateMotionFidelity` are not ported. |
| `shift_test.go` | `TestShiftActionsLaterAndEarlier` | ported | core/python/tests/test_funscript_parity.py::test_shift_actions |
| `shift_test.go` | `TestShiftBookmarksAndChapters` | todo-later | P1 (open: "Write offset into script", `BakeScriptOffset`): `ShiftBookmarks` / `ShiftChapters` are not ported (only `ShiftActions`). |
| `speed_highlight_test.go` | `TestSpeedHighlightsFindsFastSegment` | ported | tests/core/curve_geometry_test.cpp "SpeedHighlights finds a fast segment" |
| `speed_highlight_test.go` | `TestSpeedHighlightsRespectsThreshold` | ported | tests/core/curve_geometry_test.cpp "SpeedHighlights respects the threshold" |
| `speed_highlight_test.go` | `TestSpeedHighlightsEmpty` | ported | tests/core/curve_geometry_test.cpp "SpeedHighlights: empty, single point, equal times, rounding" |
| `stroke_filter_test.go` | `TestStrokeFilterOffChangesNothing` | ported | tests/core/stroke_filter_test.cpp "Stroke filter off changes nothing and leaves the input alone" and core/python/tests/test_stroke_filter_parity.py::test_off_changes_nothing |
| `stroke_filter_test.go` | `TestStrokeFilterSmoothKeepsPointsAndStaysInRange` | ported | tests/core/stroke_filter_test.cpp (all checks of the Go test) and core/python/tests/test_stroke_filter_parity.py::test_smooth_keeps_points |
| `stroke_filter_test.go` | `TestPCHIPNoOvershoot`, `TestNormalizeSmooth`, `TestStrokeFilterSortsAndDedupes` | ported | tests/core/stroke_filter_test.cpp "PCHIP does not overshoot monotone data", "NormalizeSmooth", "Unsorted input is sorted and the last duplicate time kept" |
| `stroke_filter_test.go` | `TestStrokeFilterSpeedLimit` | ported | tests/core/stroke_filter_test.cpp (all checks of the Go test) and core/python/tests/test_stroke_filter_parity.py::test_speed_limit |
| `stroke_filter_test.go` | `TestStrokeFilterIdleFill` | ported | tests/core/stroke_filter_test.cpp (all checks of the Go test) and core/python/tests/test_stroke_filter_parity.py::test_idle_fill |

### 2.2 `device`

7 files, 41 tests: 39 ported, 2 covered, 0 todo-now, 0 todo-later, 0 n/a.

| Old file | Test function(s) | Status | Where / why |
|---|---|---|---|
| `battery_test.go` | `TestFormatBatteryPct` | ported | tests/core/device_protocol_test.cpp "device/battery: FormatBatteryPct" |
| `battery_test.go` | `TestConnectionInfoBatteryDefaults` | ported | tests/core/device_protocol_test.cpp "device/battery: ConnectionInfo battery defaults" |
| `capabilities_test.go` | `TestButtplugFeaturesNeo2Like` | ported | tests/core/device_capabilities_test.cpp "device/capabilities: Buttplug features, Neo 2 like" |
| `capabilities_test.go` | `TestButtplugFeaturesMultiAxis` | ported | tests/core/device_capabilities_test.cpp "device/capabilities: Buttplug features, multi axis" |
| `capabilities_test.go` | `TestIntifaceAdoptsStroker` | ported | tests/core/device_intiface_test.cpp "device/intiface: adopts a stroker" |
| `capabilities_test.go` | `TestIntifaceDrivesEveryMotor` | ported | tests/core/device_intiface_test.cpp "device/intiface: drives every motor" |
| `capabilities_test.go` | `TestCapabilitiesOfNeo2AndMock` | ported | tests/core/device_capabilities_test.cpp "device/capabilities: Neo 2 and Mock" |
| `capabilities_test.go` | `TestIntifaceMotorLevels` | ported | tests/core/device_intiface_test.cpp "device/intiface: motor levels" |
| `diagnostics_test.go` | `TestRunDiagnosticsWithRawCapableDevice` | ported | tests/core/device_diagnostics_test.cpp "device/diagnostics: raw capable device" |
| `diagnostics_test.go` | `TestRunDiagnosticsIncludesConnectLatency` | ported | tests/core/device_diagnostics_test.cpp "device/diagnostics: connect latency is the first entry" |
| `diagnostics_test.go` | `TestRunDiagnosticsSkipsRawSweepWithoutRawCapable` | ported | tests/core/device_diagnostics_test.cpp "device/diagnostics: raw sweep skipped without RawCapable" |
| `diagnostics_test.go` | `TestRunDiagnosticsStopsOnCanceledContext` | ported | tests/core/device_diagnostics_test.cpp "device/diagnostics: stops on a cancelled run" |
| `intiface_test.go` | `TestIntifaceConnectAndControl` | ported | tests/core/device_intiface_test.cpp "device/intiface: connect and control" |
| `intiface_test.go` | `TestIntifaceClampsValues` | ported | tests/core/device_intiface_test.cpp "device/intiface: clamps values" |
| `intiface_test.go` | `TestIntifaceWithoutDeviceFails` | ported | tests/core/device_intiface_test.cpp "device/intiface: without a device connect fails" |
| `intiface_test.go` | `TestIntifaceVibrationOnlyDevice` | ported | tests/core/device_intiface_test.cpp "device/intiface: vibration-only device" |
| `intiface_test.go` | `TestIntifaceNoServer` | ported | tests/core/device_intiface_test.cpp "device/intiface: no server" |
| `intiface_test.go` | `TestNormalizeIntifaceURL` | ported | tests/core/device_intiface_test.cpp "device/intiface: NormalizeIntifaceUrl" |
| `intiface_test.go` | `TestIntifaceHintDistinguishesLocalAndNetwork` | ported | tests/core/device_intiface_test.cpp "device/intiface: hint distinguishes local and network" |
| `intiface_test.go` | `TestIntifaceMarksDeadOnWriteFailure` | ported | tests/core/device_intiface_test.cpp "device/intiface: marks dead on write failure" |
| `intiface_test.go` | `TestIntifaceOneShotReconnect` | ported | tests/core/device_intiface_test.cpp "device/intiface: one-shot reconnect" |
| `intiface_test.go` | `TestIntifacePingStopReplacedOnReconnect` | covered | tests/core/device_intiface_test.cpp "device/intiface: ping survives a one-shot reconnect" (adapted: the Go test checks a goroutine/channel; the app owns the ping timer) |
| `intiface_test.go` | `TestIntifaceOneShotReconnectSpent` | ported | tests/core/device_intiface_test.cpp "device/intiface: one-shot reconnect is spent after a failure" |
| `intiface_test.go` | `TestIntifaceBatteryLevelLocked` | ported | tests/core/device_intiface_test.cpp "device/intiface: battery level" |
| `intiface_test.go` | `TestIntifaceDrainsServerReplies` | covered | tests/core/device_intiface_test.cpp "device/intiface: PumpIncoming drains server replies" (adapted: no reader goroutine, `PumpIncoming` drains) |
| `intiface_test.go` | `TestIntifaceSuctionUsesAdoptedActuatorType` | ported | tests/core/device_intiface_test.cpp "device/intiface: suction uses the adopted actuator type" |
| `protocol_test.go` | `TestSamNeo2Protocol_EncodeVibration` | ported | tests/core/device_protocol_test.cpp "device/protocol: Sam Neo 2 EncodeVibration" |
| `protocol_test.go` | `TestSamNeo2Protocol_EncodeSuction` | ported | tests/core/device_protocol_test.cpp "device/protocol: Sam Neo 2 EncodeSuction" |
| `protocol_test.go` | `TestSamNeo2Protocol_UUIDsAndWriteMode` | ported | tests/core/device_protocol_test.cpp "device/protocol: Sam Neo 2 UUIDs and write mode" |
| `protocol_test.go` | `TestSamNeo2RawEncodingDoesNotQuantize` | ported | tests/core/device_protocol_test.cpp "device/protocol: raw encoding does not quantise" |
| `protocol_test.go` | `TestSamNeo2NormalEncodingStillQuantizes` | ported | tests/core/device_protocol_test.cpp "device/protocol: normal encoding still quantises" |
| `protocol_test.go` | `TestEncodingCollapsesNearbyIntensities` | ported | tests/core/device_protocol_test.cpp "device/protocol: encoding collapses nearby intensities" |
| `protocol_test.go` | `TestVibrationAndSuctionPacketsDiffer` | ported | tests/core/device_protocol_test.cpp "device/protocol: vibration and suction packets differ" |
| `samneo2_test.go` | `TestSamNeo2RawWritesUpdateChannelState` | ported | tests/core/device_samneo2_test.cpp "device/samneo2: raw writes update the channel state" |
| `tcode_test.go` | `TestTCodeValueAndMove` | ported | tests/core/device_tcode_test.cpp "device/tcode: value and move" |
| `tcode_test.go` | `TestParseTCodeAddress` | ported | tests/core/device_tcode_test.cpp "device/tcode: ParseTCodeAddress" |
| `tcode_test.go` | `TestTCodeOverTCP` | ported | tests/core/device_tcode_test.cpp "device/tcode: over TCP (lines, DSTOP, vibration no-op)" |
| `tcode_test.go` | `TestTCodeOverUDP` | ported | tests/core/device_tcode_test.cpp "device/tcode: over UDP (no scheme)" |
| `tcode_test.go` | `TestTCodeTCPConnectFails` | ported | tests/core/device_tcode_test.cpp "device/tcode: TCP connect fails with a clear message" |
| `tcode_test.go` | `TestTCodeExtraAxes` | ported | tests/core/device_tcode_test.cpp "device/tcode: extra axes" |
| `tcode_test.go` | `TestTCodeSerial` | ported | tests/core/device_tcode_test.cpp "device/tcode: serial" |

### 2.3 `player`

7 files, 61 tests: 5 ported, 9 covered, 0 todo-now, 44 todo-later, 3 n/a.

| Old file | Test function(s) | Status | Where / why |
|---|---|---|---|
| `axes_test.go` | `TestDriveAxesSendsEachAxisOnce` | ported | tests/app/playback_output_test.cpp `extraAxesSendEachAxisOnce` (one move per axis sorted by name, untaken and empty axes skipped, stroke off stops them, the stroke range does not scale them, a pause resends all); `extraAxesFollowTheScript` checks the first move |
| `axes_test.go` | `TestDriveAxesNeedsMultiAxisDevice` | ported | tests/app/playback_output_test.cpp `extraAxesNeedAMultiAxisDevice` (a Buttplug stroker gets the stroke only, no axis command, no error) |
| `extendedo_test.go` | `TestExtendedOScalesAmplitudeKeepsRhythm` | ported | tests/app/playback_output_test.cpp `extendedOScalesAmplitudeKeepsRhythm` (hold 1 s: the shortest the 0.7 slider allows); the ramp numbers: tests/core/output_levels_test.cpp |
| `extendedo_test.go` | `TestSetOutputAppliesIntensityScale` | ported | tests/core/output_levels_test.cpp "OutputLevel applies the Extended-O scale" (0.8 x 0.25 = 0.2, 0.4 x 0.25 = 0.1) |
| `extendedo_test.go` | `TestExtendedOMinLevelZeroSilences` | ported | tests/core/output_levels_test.cpp "scale 0 is silent", "min level 0 is off"; tests/app/playback_output_test.cpp `extendedOMinLevelZeroSilences` (Mock device sends 0.00 on both channels) |
| `extendedo_test.go` | `TestZeroValuePlayerPlaysFullHeight` | n/a | Go zero-value `Player` struct semantics; `PlaybackOutput` is always constructed. "No cycle" is its own state (`std::optional`), checked in `extendedOScalesAmplitudeKeepsRhythm` (scale 1 before the trigger). |
| `speed_test.go` | `TestClampSpeed` | todo-later | P2 (play speed 0.5-2x): `PlaybackOutput::setSpeed` only rejects values <= 0; the `MinSpeed`/`MaxSpeed` clamp is not in C++ (check QML) - add the clamp and the test together. |
| `speed_test.go` | `TestSpeedScalesClockAndReanchors` | covered | tests/app/playback_output_test.cpp `playSpeedShortensTheMoves` (2x halves a 1000 ms move). Clock re-anchoring has no meaning: time comes from the video position. |
| `speed_test.go` | `TestChannelLevelsDefaultFullAndClamp` | ported | tests/app/playback_output_test.cpp `channelLevelsDefaultFullAndClamp` (levels are 0-100 here: default 100, -3 gives 0, 7 and 700 give 100); `channelOffAndLevelScaleTheOutput` covers the scaling |
| `speed_test.go` | `TestReducePeak` | ported | tests/core/output_levels_test.cpp "ReducePeak: knee 0.6, ratio 0.4, monotonic" |
| `speed_test.go` | `TestSetOutputReducePeaks` | ported | tests/core/output_levels_test.cpp "OutputLevel: peak first, then the channel level"; tests/app/playback_output_test.cpp `reducePeaksSoftensTheOutput` (off by default, 0.38 / 0.40, off again 0.50) |
| `speed_test.go` | `TestPlayStartAtMsResumes` | n/a | Go `Player.Play` clock loop with `StartAtMs`; output is driven by video position reports. |
| `stroke_test.go` | `TestDriveStrokeSendsNextActionOnce` | covered | tests/app/playback_output_test.cpp `strokerGetsTheNextActionWithSafetyRange` (same target not resent within a move) |
| `stroke_test.go` | `TestDriveStrokeHonoursSpeedAndMinimum` | ported | tests/app/playback_output_test.cpp `playSpeedShortensTheMoves` (speed half), `lateStrokePositionStillGetsTheMinimumMove` (a position 5 ms before it is due gets the 20 ms minimum) |
| `stroke_test.go` | `TestDriveStrokeSoftSeek` | covered | tests/app/playback_output_test.cpp `seekEasesIn` |
| `stroke_test.go` | `TestDriveStrokeOnlyForLinearDevices` | ported | tests/app/playback_output_test.cpp `strokeOnlyForLinearDevices` (the Mock gets no position and no error; a stroker with a stroke-less script gets none, and does move once the script has a stroke) |
| `stroke_test.go` | `TestSyncDrivesStrokeAndStopsOnPause` | covered | tests/app/playback_output_test.cpp `pauseWithoutHomeOnlyStops` |
| `stroke_test.go` | `TestPlayDrivesStroke` | n/a | Go `Player.Play` (no video, player clock); the Qt output is only driven by the video position. |
| `stroke_test.go` | `TestStrokeRangeScalesPositions` | covered | tests/app/playback_output_test.cpp `strokerGetsTheNextActionWithSafetyRange` (positions land inside the safety range) |
| `stroke_test.go` | `TestClampStrokeRange` | covered | tests/app/playback_output_test.cpp `rangeKeepsTenPercentApart` |
| `stroke_test.go` | `TestSyncStrokeHomeOnPause` | covered | tests/app/playback_output_test.cpp `pauseStopsAndMovesHome` |
| `stroke_test.go` | `TestSyncStrokeHomeOnPauseDuringSoftStart` | ported | tests/app/playback_output_test.cpp `homeOnPauseDuringSoftStart` (DSTOP, then L0 to the bottom of 20-80 % in 1200 ms) |
| `sync_test.go` | `TestSyncGoesQuietWhenPositionsStop` | covered | tests/app/playback_output_test.cpp `staleWatchdogStopsTheDevice` |
| `sync_test.go` | `TestSyncGoesQuietOnIdleSentinel` | covered | tests/app/playback_output_test.cpp `playingSendsLevelsAndSessionIsActive` (pause stops at once). The idle sentinel itself is Wails/JS glue. |
| `sync_test.go` | `TestSyncIdleDuringSoftStartGoesQuiet` | ported | tests/app/playback_output_test.cpp `pauseInsideTheSoftStartStopsAtOnce` (pause), `watchdogInsideTheSoftStartStops` (no position); resume ramps again |
| `training_pause_test.go` | `TestPauseHoldsOutputAndResumeContinues`, `TestCancelWhilePausedEndsSession`, `TestPauseResumeIdempotent` | todo-later | P2 (Training): training sessions are not ported. |
| `training_test.go` | `TestStopCycleInterruptsHoldAndContinues`, `TestStopCycleInterruptsRamp`, `TestTrainingWithoutStopUnchanged`, `TestNilControlIsSafe`, `TestArousalAdjustsNextCycle`, `TestArousalIgnoresInvalidValues`, `TestArousalAppliesToOneCycleOnly`, `TestArousalReachesRunningSession`, `TestContextCancelDuringHoldStopsImmediately`, `TestRunTrainingScriptCarriesUntouchedChannelBetweenPhases`, `TestRunTrainingScriptArousalWeakensBothChannelsAndLengthensRest`, `TestRunTrainingScriptStopCycleStopsBothChannels`, `TestBuiltinTrainingScriptsAreValid`, `TestBuiltinTrainingScriptsNoLongHighSuctionHold`, `TestBuiltinTrainingScriptsHaveSuctionFromStart`, `TestRunTrainingScriptRampsDownElevatedChannelsAtNaturalEnd`, `TestRunTrainingScriptSkipsRampDownAfterInterrupt`, `TestRunTrainingScriptRejectsInvalidScript`, `TestRunTrainingScriptNormalizesMismatchedChannelField`, `TestScaleTrainingScriptScalesLevelsNotTiming`, `TestScaleTrainingScriptClampsToOne`, `TestScaleTrainingScriptNeutralFactorIsNoop`, `TestScaleTrainingScriptDoesNotMutateInput`, `TestRunPhaseRepeatCancelsSiblingChannelOnError`, `TestBuiltinTrainingScriptLookup`, `TestStopCycleClearsCarriedChannelFromEarlierPhase`, `TestScaledCurveScalesStartLevelWithFeedback`, `TestLevelMirrorReportsLiveWrites`, `TestStopCycleDuringRampDownCutsToZero`, `TestRunTrainingScriptStopCycleDuringElevatedRest`, `TestRunTrainingScriptRampsDownAtEndAfterEarlierStop`, `TestPlateauNextCycleStartsFromPlateau`, `TestRunTrainingScriptStartFromCurrentKeepsPlateau` | todo-later | P2 (Training): training sessions are not ported. |

### 2.4 `samn`

2 files, 5 tests: 5 ported, 0 covered, 0 todo-now, 0 todo-later, 0 n/a.

| Old file | Test function(s) | Status | Where / why |
|---|---|---|---|
| `convert_test.go` | `TestRoundTripFunscriptSamn` | ported | tests/core/samn_file_test.cpp "samn_file: round trip funscript <-> samn" |
| `convert_test.go` | `TestIsSamnPath` | ported | tests/core/samn_file_test.cpp "samn_file: is_samn_path" |
| `export_merge_test.go` | `TestExportFunscriptKeepsForeignMetadata` | ported | tests/core/samn_file_test.cpp "samn_file: export_funscript keeps foreign metadata" |
| `export_merge_test.go` | `TestExportFunscriptWithoutUsableTarget` | ported | tests/core/samn_file_test.cpp "samn_file: export_funscript without usable target" |
| `export_merge_test.go` | `TestFromFunscriptBakesInverted` | ported | tests/core/samn_file_test.cpp "samn_file: from_funscript bakes inverted" |

### 2.5 `sam`

7 files, 37 tests: 9 ported, 0 covered, 0 todo-now, 28 todo-later, 0 n/a.

| Old file | Test function(s) | Status | Where / why |
|---|---|---|---|
| `audio_vibration_test.go` | `TestApplyAudioVibrationKeepsSuctionAndShiftsByOffset`, `TestApplyAudioVibrationExtendsToAudioEnd`, `TestApplyAudioVibrationNoEnvelopeIsNoop` | todo-later | P2/P3 (vibration from the video's sound): `ApplyAudioVibration` is not ported; the loudness envelope itself is (`audio_envelope.h`). |
| `enrich_test.go` | `TestEnrichContactIntensityAndGaps`, `TestEnrichVelocity`, `TestFromFunscriptStillThin`, `TestRoundtripPreservesTrackingGaps` | todo-later | P3 (Generator writes the enriched `.sam` sidecar): `Enrich` is not ported. |
| `funscript_test.go` | `TestFromFunscriptPreservesTimingAndPosition`, `TestFromFunscriptHonorsInverted`, `TestToFunscriptPreservesTimingAndPosition`, `TestToFunscriptRoundsFractionalPosition`, `TestRoundtripFunscriptSAMFunscript`, `TestRoundtripPreservesProfileAndDeviceRecipe`, `TestRoundtripLargeFunscript` | todo-later | P3 (`.sam` model <-> funscript): the SAM model is not ported. |
| `playback_test.go` | `TestPlaybackFramesUsesSAMIntensity`, `TestPlaybackFramesFallsBackWithoutContact`, `TestPlaybackFramesHonorsOptsOnlyTrackingGaps`, `TestToDeviceFramesNoVibBelowContactThreshold` | ported | tests/core/playback_mapping_test.cpp "Contact path: …" (the four cases name their 0.7 test) |
| `playback_test.go` | `TestToDeviceFramesMutesOnConfidenceZero`, `TestHasContactIntensityRejectsThin` | todo-later | P3 (play from a `.sam` sidecar): confidence other than tracking gaps and the sidecar choice come with the sidecar. |
| `runtime_test.go` | `TestRuntimeAdjustIdentity`, `TestRuntimeAdjustScaleAndMute`, `TestRuntimeAdjustBoostClamp` | ported | tests/core/playback_mapping_test.cpp "RuntimeAdjust: the defaults change nothing", "RuntimeAdjust: scale and mute", "RuntimeAdjust: boost clamps to 1" |
| `runtime_test.go` | `TestDensifyMatchesClassicLinear`, `TestDensifySourceAndFrameCount` | ported | tests/core/playback_mapping_test.cpp "Contact path follows the mapper closely, gaps never leak" (`ContactDeviceFrames` holds Densify; partial: the SAM `Source` label has no counterpart, there is no SAM script object) |
| `sam_test.go` | `TestParseRequiresVersion`, `TestParseRequiresFrames`, `TestParseRejectsInvalidJSON`, `TestParseSortsFramesByTime`, `TestParseClampsOutOfRangeFields`, `TestParseIgnoresUnknownFutureFields`, `TestDuration`, `TestSaveLoadRoundtrip`, `TestLoadMissingFile`, `TestLoadMalformedFileOnDisk` | todo-later | P3 (`.sam` v0.1 format): `Parse` / `Save` / `Load` of the SAM model are not ported. |
| `sidecar_test.go` | `TestWriteAndLoadSidecar`, `TestLoadSidecarMissing` | todo-later | P3 (`.sam` sidecar next to the clip): not ported. |

### 2.6 `videox`

6 files, 26 tests: 0 ported, 0 covered, 0 todo-now, 17 todo-later, 9 n/a.

| Old file | Test function(s) | Status | Where / why |
|---|---|---|---|
| `bin_test.go` | `TestResolveToolPrefersEnv`, `TestResolveToolSiblingDir`, `TestCommandFFmpeg` | n/a | Finds the ffmpeg/ffprobe binaries (env, sibling dir). SamNPlayer 2 decodes with Qt Multimedia / OpenCV and has no ffmpeg dependency (see `audio_envelope.h`, `PLAN.md` Dependencies). |
| `cut_clip_test.go` | `TestParseClipPrepTime`, `TestResolveClipPrepMaxWidth`, `TestBuildClipPrepArgsMatchesScriptDefaults`, `TestBuildClipPrepArgsNoAudioAndRejectsBadWindow`, `TestExportClipPrepCut` | todo-later | P4 (Bench clip prep, Suite): `ExportClipPrepCut` builds an ffmpeg command line; decide with the Bench port whether ffmpeg stays for this. |
| `exec_windows_test.go` | `TestCommandContextHidesConsoleOnWindows` | n/a | Hides the console window of ffmpeg child processes on Windows; no child processes in the C++ app. |
| `mp4probe_test.go` | `TestProbeISOBMFFLean`, `TestProbeFallsBackWithoutFFprobe`, `TestProbeISOBMFFRejectsWebM`, `TestProbeISOBMFFMatchesFFprobeOnClip`, `TestProbePrefersFFprobeWhenPresent`, `TestProbeISOBMFFUnknownCodec` | todo-later | P3.0 (`samn/video/FrameReader`, video info): lean MP4/ISOBMFF header probe and ffprobe fallback are not ported. |
| `playable_test.go` | `TestInfoLikelyPlayable`, `TestProxyPath`, `TestIsH264Family`, `TestEnsurePlayableProxyRemuxMKV`, `TestEnsurePlayableProxyConcurrent` | n/a | Decides whether the Chromium webview can play a file and remuxes a proxy (MKV -> MP4). Qt Multimedia with the FFmpeg backend plays the file directly. |
| `videox_test.go` | `TestProbe`, `TestProbeMissingFile`, `TestGrayReaderScalesAndCounts`, `TestGrayReaderEarlyClose`, `TestGrayReaderCloseIsIdempotent`, `TestFitWidth` | todo-later | P3.0 (`FrameReader`, probe): `Probe`, `GrayReader`, `FitWidth` are not ported. |

### 2.7 `update`

3 files, 14 tests: 0 ported, 0 covered, 0 todo-now, 14 todo-later, 0 n/a.

| Old file | Test function(s) | Status | Where / why |
|---|---|---|---|
| `patch_test.go` | `TestParsePatchTag`, `TestIsPlainVersionTag`, `TestNormalizeVersionStr`, `TestSelectHighestUnappliedPatch`, `TestMarkPatchAppliedRoundTrip` | todo-later | P5 (updater switch): patch tags and applied-patch state are not ported. |
| `update_test.go` | `TestDescribe`, `TestErrNoReleaseSentinel`, `TestIsNewer`, `TestAssetForThisPlatform`, `TestValidateGitHubAssetURL`, `TestChangelogSummary`, `TestWindowsApplyBatch`, `TestReleaseBodyJSON` | todo-later | P5 (updater switch): release check, asset matching and Windows apply batch are not ported (ROADMAP: match the exact file name, never a suffix, D-017). |
| `version_test.go` | `TestSourceVersion` | todo-later | P5 (updater switch): not ported. |

### 2.8 `motionx`

2 files, 10 tests: 4 ported, 0 covered, 0 todo-now, 6 todo-later, 0 n/a.

| Old file | Test function(s) | Status | Where / why |
|---|---|---|---|
| `chapters_test.go` | `TestChaptersMapsStates`, `TestChaptersNil` | todo-later | P4 (Analysis, Scene Map): `motionx` chapters-from-states is not ported. |
| `motionx_test.go` | `TestSimplifyKeepsEndpoints`, `TestSimplifyRespectsTolerance`, `TestSimplifyStraightLineCollapses`, `TestSimplifyDeepCurveDoesNotRecurse` | ported | `tests/core/simplify_test.cpp` (`samn::SimplifyIndices`, used by the post-processing RDP step). |
| `motionx_test.go` | `TestDedup`, `TestClassifyStaticCurve`, `TestClassifyStartStop`, `TestClassifySegmentsAreContiguousAndLongEnough` | todo-later | P4 (Analysis, Scene Map): `Dedup`, `Classify` are not ported. |

### 2.9 `pluginhost`

2 files, 10 tests: 0 ported, 0 covered, 0 todo-now, 10 todo-later, 0 n/a.

| Old file | Test function(s) | Status | Where / why |
|---|---|---|---|
| `host_test.go` | `TestEnableStartsHost`, `TestTickNoOpWhenDisabled`, `TestEnableIdempotent` | todo-later | P4 (Plugins): plugin host is not ported. |
| `manifest_test.go` | `TestLoadManifestAndDiscover`, `TestRejectBadAPIVersion`, `TestInstallPackDir`, `TestStatusReflectsPack`, `TestInstallPackDirRejectsSanitizedIDCollision`, `TestInstallPackDirAllowsSameIDUpgrade`, `TestInstallPackDirRejectsSourceDestinationOverlap` | todo-later | P4 (Plugins): manifest, discovery and pack install are not ported. |

### 2.10 `edition`

1 files, 1 tests: 0 ported, 0 covered, 0 todo-now, 0 todo-later, 1 n/a.

| Old file | Test function(s) | Status | Where / why |
|---|---|---|---|
| `edition_test.go` | `TestNameMatchesSuite` | n/a | Checks a Go build-tag constant against the Suite flag. The edition split is an open decision (D-004, D-007); revisit when it is decided. |

### 2.11 `legal`

1 files, 2 tests: 0 ported, 0 covered, 0 todo-now, 2 todo-later, 0 n/a.

| Old file | Test function(s) | Status | Where / why |
|---|---|---|---|
| `legal_test.go` | `TestEULAEmbedded`, `TestDonateLinksOnlyHTTPS` | todo-later | P5/P4 (EULA gate, Settings donate links): not ported. |

### 2.12 `logging`

1 files, 1 tests: 0 ported, 0 covered, 0 todo-now, 1 todo-later, 0 n/a.

| Old file | Test function(s) | Status | Where / why |
|---|---|---|---|
| `buffer_test.go` | `TestRingRecordsAndCaps` | todo-later | P4 (Log viewer): the in-memory log ring is not ported. |

### 2.13 `cmd/cli`

7 files, 18 tests: 0 ported, 0 covered, 0 todo-now, 12 todo-later, 6 n/a.

| Old file | Test function(s) | Status | Where / why |
|---|---|---|---|
| `benchmark_cmd_test.go` | `TestRunBenchmarkIdenticalExit0`, `TestRunBenchmarkShiftedExitNonZero`, `TestRunBenchmarkMissingArgs`, `TestRunBenchmarkMixedPositionalAndNamedPaths`, `TestRunBenchmarkRejectsExtraPositionalPath`, `TestRunBenchmarkGoldenClipFixture` | todo-later | Later (CLI is not in `PLAN.md`; ROADMAP: CLI `generate`, `stroke-preview`, `benchmark`). |
| `cli_args_test.go` | `TestSplitCLIArgsNegativeFlagValue`, `TestSplitCLIArgsEqualsForm`, `TestSplitCLIArgsDoesNotSwallowNextOption`, `TestSplitCLIArgsDoubleDash`, `TestSplitCLIArgsSamOutputAfterFile`, `TestSplitCLIArgsBoolBeforePath` | n/a | Works around the Go `flag` package (negative values, `--`, bool flags before paths); a C++ CLI would pick its own parser. |
| `export_learning_cmd_test.go` | `TestExportLearningDeleteRejectsPathWithoutDeleting` | todo-later | Later (CLI) / P4 Suite (learning export). |
| `generate_cancel_test.go` | `TestRunGeneratePropagatesCanceledContext` | todo-later | Later (CLI `generate`) and P3.3 (cancel). |
| `scene_proposals_cli_test.go` | `TestApplySceneProposalsCLI`, `TestParseROI` | todo-later | Later (CLI) / P4 (scene proposals). |
| `stroke_preview_cancel_test.go` | `TestRunStrokePreviewPropagatesCanceledContext` | todo-later | Later (CLI `stroke-preview`) / P3.6. |
| `stroke_preview_cmd_test.go` | `TestStrokePreviewRejectsExtraVideoPaths` | todo-later | Later (CLI `stroke-preview`) / P3.6. |

### 2.14 `cmd/gui-wails (Go)`

68 files, 188 tests: 16 ported, 18 covered, 0 todo-now, 141 todo-later, 13 n/a.

| Old file | Test function(s) | Status | Where / why |
|---|---|---|---|
| `app_aiscript_test.go` | `TestExportAIScriptImitation`, `TestDraftAndKeepAIScript` | todo-later | P4 (AI draft script, opt-in D-058). |
| `app_analysis_test.go` | `TestAnalyzeScriptFindsStaticStretches`, `TestAnalyzeScriptWithoutScript`, `TestAnalyzeScriptSteadyMotion` | todo-later | P4 (Analysis shortcuts): `AnalyzeScript` needs `motionx`, not ported. |
| `app_audio_level_test.go` | `TestAudioLevelEnvelopeSharesDecodeWithAudioVibration` | todo-later | P2/P3 (vibration from sound): the Audio Level lane decodes once (`audioLevelFromSound`), but the shared decode with the vibration output is not built. |
| `app_audio_level_test.go` | `TestSetChannelLevelsStoresClampedCut` | covered | tests/app/playback_output_test.cpp `channelOffAndLevelScaleTheOutput`, `outputChoicesAreRemembered` (clamp: see `player/speed_test.go TestChannelLevelsDefaultFullAndClamp` in list A) |
| `app_audio_level_test.go` | `TestSetPeakReduceStored` | todo-later | P2 (Reduce peaks): not ported. |
| `app_audio_level_test.go` | `TestSetStrokeOutputStoresClampedRange` | covered | tests/app/playback_output_test.cpp `rangeKeepsTenPercentApart`, `outputChoicesAreRemembered` |
| `app_audio_segments_test.go` | `TestLoadFunscriptExposesAudioCheckSegments`, `TestFillScriptInfoAudioCheckFromCompanion`, `TestFillScriptInfoAudioCheckNilSafe` | todo-later | P3.6 (audio check segments). |
| `app_audio_vibration_test.go` | `TestAudioVibrationSettingSwapsVibrationOnly`, `TestAudioVibrationFallsBackToScript` | todo-later | P2/P3 (vibration from the video's sound swaps the vibration channel). |
| `app_autosync_test.go` | `TestEstimateSyncLagFindsDelay`, `TestEstimateSyncLagNoMatch`, `TestEstimateSyncLagPeriodicIsNotHigh`, `TestEstimateAudioSyncOffsetNeedsScriptAndVideo` | todo-later | P1 open item "auto sync (audio)" / P3.6: `EstimateSyncLag` is not ported. |
| `app_beatbar_test.go` | `TestCreateBeatBarScript` | todo-later | P3 (needs the video frame reader P3.0): `CreateBeatBarScript`; the pure parts `FindBeatBar`/`BeatStrokes` are ported. |
| `app_benchmark_cancel_test.go` | `TestBenchmarkRunClaimCancel`, `TestBenchmarkRunClaimIsExclusive` | todo-later | P4 (Bench, Suite) / P3.8. |
| `app_benchmark_clip_prep_test.go` | `TestSuggestBenchmarkClipOutput`, `TestResolveClipWindowFromStrings`, `TestExportBenchmarkClipRejectsMissingSource`, `TestExportBenchmarkClipRunsFFmpeg`, `TestParseBenchmarkClipTime` | todo-later | P4 (Bench clip prep, Suite). |
| `app_benchmark_test.go` | `TestGetBenchmarkHistorySortsNewestFirst`, `TestGetBenchmarkHistorySkipsBrokenLinesKeepsRest`, `TestGetBenchmarkHistoryMissingFileIsNotAnError`, `TestGetBenchmarkHistoryLimitsToMaxEntries`, `TestScoreScriptPairIdentical` | todo-later | P4 (Bench, Suite) / P3.8 (comparison). |
| `app_bookmarks_test.go` | `TestGetSaveScriptBookmarksFunscript` | covered | tests/core/omarker_test.cpp "script marks: .funscript metadata and .samn document", tests/core/marks_test.cpp (bookmarks); tests/app/player_controller_test.cpp `editMarks` |
| `app_bpm_test.go` | `TestBpmFromHzFoldsIntoInputRange`, `TestEstimatePlaybackAudioBPM` | todo-later | P3.6 (Audio Analysis: BPM from audio): not ported. The manual BPM grid is (`bpmGrid`). |
| `app_chapter_scenes_test.go` | `TestChapterScenesSuggestAndSave`, `TestChapterScenesFromContactMark` | todo-later | P4 (Scene Map: suggested feel per scene). |
| `app_chapters_marks_test.go` | `TestGetSaveScriptChapterMarksFunscript` | covered | tests/core/omarker_test.cpp "script marks: .funscript metadata and .samn document", tests/core/marks_test.cpp (chapters); tests/app/player_controller_test.cpp `editMarks`, `chaptersBookmarksHeatmapLanes` |
| `app_companion_test.go` | `TestGetScriptAxisActionsReadsCompanionFiles`, `TestCompanionAxesForPlayback`, `TestCreateScriptAxis` | todo-later | P2/P4 (companion axis files into the player): not ported. |
| `app_contact_preview_test.go` | `TestPreviewContactVibrationWiresThrough` | todo-later | P3.3 (Generator preview). |
| `app_contact_settings_test.go` | `TestSaveContactSettingsPersistsRecipe` | ported | tests/core/samn_file_test.cpp "samn_file: contact settings are stored in the tj recipe" (adapted to `Document::ApplyContactRecipe`) |
| `app_contact_settings_test.go` | `TestSaveContactSettingsStrokeProfile`, `TestSaveContactSettingsStrokeEmptySyncFillsIndependent` | ported | tests/core/samn_file_test.cpp "samn_file: contact settings keep sync independent on a stroke profile", "samn_file: contact settings fill an empty stroke sync with independent" |
| `app_create_video_test.go` | `TestProbeCreateVideoHeader` | todo-later | P3.0/P3.3 (Create video header probe). |
| `app_curve_test.go` | `TestGetScriptCurveKeepsExtremes` | n/a | Down-samples the curve payload for the JS canvas; the C++ `CurveItem` draws from the actions with its own level of detail (`curve_geometry_test.cpp` density switch). |
| `app_curve_test.go` | `TestGetScriptCurveShortScriptUnchanged` | n/a | Payload down-sampling for the JS canvas; no equivalent. |
| `app_curve_test.go` | `TestGetScriptCurveWithoutScript` | n/a | Payload for the JS canvas; the C++ items draw nothing without a script (`playback_output_test.cpp noScriptNoLanes`). |
| `app_curve_test.go` | `TestGetScriptCurveHonorsInverted` | todo-later | P4 Settings (honor OFS `inverted`): `PlayerController` does not flip inverted scripts yet (core `PlaybackActions` is ready). |
| `app_curve_test.go` | `TestGetVibrationCurveContactRecipe` | todo-later | P2: the vibration lane uses `DefaultMapOptions()`; it does not apply the script's recipe / contact vibration yet. |
| `app_curve_test.go` | `TestGetVibrationCurveAbsentWithoutFlag` | todo-later | P2: vibration lane preview flags (script carries vibration vs device preview). |
| `app_curve_test.go` | `TestGetVibrationCurvePreviewLiveScaleAndMute` | todo-later | P2 (Effects): live intensity scale and mute on the preview lane. |
| `app_curve_test.go` | `TestGetVibrationCurvePreviewSpanOverride` | todo-later | P2: contact span override on the preview lane. |
| `app_device_test.go` | `TestDeviceTabFlow` | covered | tests/app/device_controller_test.cpp `mockConnectDriveDisconnect` |
| `app_device_test.go` | `TestClaimTestDeviceConnectSerializes` | covered | tests/app/device_controller_test.cpp `secondConnectIsRefused`, `sessionActiveBlocksConnectAndTests` |
| `app_device_test.go` | `TestDeviceStatusCarriesCapabilities` | covered | tests/app/device_controller_test.cpp `mockConnectDriveDisconnect`, `intifaceStrokerLinear` (channels/axes in the status) |
| `app_edition_suite_test.go` | `TestSuiteBuildFlag` | n/a | Go build-tag / ldflags `Suite` flag. |
| `app_edition_test.go` | `TestCustomerBuildRefusesStudioTools` | n/a | Go customer-build tag refusing Studio tools. |
| `app_editor_test.go` | `TestAppScriptActionsRoundtrip` | todo-later | P4 Editor: `ScriptActions` get/save round trip. |
| `app_effect_presets_test.go` | `TestEffectPresetsRoundTrip` | covered | tests/app/player_controller_test.cpp `effectPresetsSaveApplyDelete` (fresh install has none, the 0.7 text loads, saved and read back by a new controller) and tests/core/playback_mapping_test.cpp "Effects presets: the 0.7 text reads and writes back" |
| `app_feel_test.go` | `TestSaveFeelSettingsOnFunscript` | todo-later | P2 (feel per scene) / P4 UI: the app method and its validation (unknown scene `dance` is refused) are not ported; the recipe part is in list A via `TestSaveFeelRecipeRoundTrip`. |
| `app_feel_test.go` | `TestSaveFeelSettingsRebakesEmotionScript` | ported | tests/core/samn_file_test.cpp "samn_file: a new feel changes the baked vibration axis" (adapted to `Document::ApplyFeelRecipe` + `BakeNeoAxes`) |
| `app_free_e2e_opencv_test.go` | `TestFreeChainGenerateSavePlay` | todo-later | P3.3 (end-to-end Create: generate, save, play); needs OpenCV. |
| `app_free_e2e_test.go` | `TestFreeChainImportSaveReloadPlayExport` | covered | tests/core/samn_file_test.cpp "samn_file: round trip funscript <-> samn", "export is byte identical to Go", "Go file reads and rewrites identically"; tests/app/player_controller_test.cpp |
| `app_free_e2e_test.go` | `TestFreeChainStopMidRunThenPlayAgain` | covered | tests/app/playback_output_test.cpp `emergencyStopDropsQueuedLevelsAndStaysSilent` ("Playing again works") |
| `app_generator_target_test.go` | `TestBeginROIRequestCancelsSupersededDetectorAndGatesSequence` | todo-later | P3.7 (AI tip find): ROI request sequencing. |
| `app_generator_test.go` | `TestApplyAutoOZoneMarkerWritesPrimaryMarker`, `TestApplyAutoOZoneMarkerWritesSecondaryMarkerWhenPresent`, `TestApplyAutoOZoneMarkerFlatScriptWritesNothing` | todo-later | P3.2 (auto O-zone after Generate): needs `SuggestOZone`. |
| `app_generator_video_test.go` | `TestGeneratorVideoServedSeparately` | n/a | Wails asset server serving the Generator video; Qt plays the file directly. |
| `app_improve_test.go` | `TestImproveGeneratedScriptHealTrackingGaps`, `TestImproveGeneratedScriptHealRhythmAndRepairSpans`, `TestImproveGeneratedScriptFillGaps`, `TestImproveGeneratedScriptRefreshesStaleQualityAndSidecar`, `TestImproveGeneratedScriptSamnRefreshesQuality`, `TestImproveGeneratedScriptTrim`, `TestImproveGeneratedScriptHealSamnClearsGaps` | todo-later | P3.3 (Review: improve generated script). |
| `app_legal_test.go` | `TestEULAAcceptOncePerVersion`, `TestOpenDonateLinkRefusesUnknownURL`, `TestAboutInfo` | todo-later | P5/P4 (EULA once per version, donate link whitelist, About info). |
| `app_markers_test.go` | `TestAppOMarkersRoundtrip` | covered | tests/core/omarker_test.cpp (all five O-marker cases); tests/app/player_controller_test.cpp `editMarks` |
| `app_motion_profile_test.go` | `TestLabelSceneWithProfileRejectsUnsupportedProfileBeforeVideoWork` | todo-later | P4 (AI profile labelling). |
| `app_offset_bake_test.go` | `TestBakeScriptOffsetMovesPointsAndResetsOffset` | todo-later | P1 open ("Write offset into script"): `BakeScriptOffset` is not ported; core has `ShiftActions` only. |
| `app_ofs_test.go` | `TestEditScaleRangeOnVibrationAxis`, `TestEditDeleteRangeDefaultsToGeneral` | todo-later | P4 Editor: `EditScaleRange` / `EditDeleteRange` app methods; the core range tools are ported (`range_edit_test.cpp`). |
| `app_open_media_test.go` | `TestFindScriptForVideo` | ported | tests/core/script_finder_test.cpp "find_script_for_video" |
| `app_open_media_test.go` | `TestFindScriptForVideoFoldersNamesVariants` | ported | tests/core/script_finder_test.cpp "find_script_for_video: folders, names, variants" |
| `app_open_media_test.go` | `TestVariantLabel` | ported | tests/core/script_finder_test.cpp "variant_label" |
| `app_open_media_test.go` | `TestSplitFolders` | ported | tests/core/script_finder_test.cpp "split_folders" |
| `app_optimize_neo2_test.go` | `TestOptimizeLoadedForNeo2FromFunscript`, `TestOptimizeLoadedForGeneralKeepsStrokeOnly`, `TestOptimizeLoadedForRejectsUnknownTarget`, `TestOptimizeLoadedForNeo2Target` | todo-later | P4 Editor / P3.3 ("Optimize for" a device): the app method is not ported; its parts (`FromFunscript`, `BakeNeoAxes`, `ApplyContactRecipe`) exist and are exercised by `samn_file_test.cpp`. |
| `app_playback_test.go` | `TestReportVideoPositionStopPlaybackRace`, `TestActivePlayerStartAndTriggerExtendedORace` | n/a | Go data-race tests of the Wails `App` (`go test -race`); no equivalent in C++/Qt. |
| `app_playback_video_swap_test.go` | `TestVideoFileURLChangesWhenPlaybackVideoSwapped`, `TestSetPlaybackVideoSamePathKeepsURL` | n/a | Wails video URL served by the asset server. |
| `app_pluginhost_test.go` | `TestPluginsDirHintWhenUnset`, `TestListInstalledPluginsEmpty`, `TestDiscoverInstalledPackRoundTrip` | todo-later | P4 (Plugins). |
| `app_postprocess_preview_test.go` | `TestPreviewPostprocessWiresThrough` | todo-later | P3.2 (post-processing preview). |
| `app_review_test.go` | `TestReviewGeneratedScriptPolarityAndOzone` | todo-later | P3.3/P4 (Review: polarity + O-zone). |
| `app_roi_training_cancel_test.go` | `TestRoiTrainingRunCancel`, `TestRoiTrainingRunClaimIsExclusive` | todo-later | P4 (AI Train, Suite; training stays Python, D-134). |
| `app_roi_training_test.go` | `TestRoiLabelPathForImage`, `TestRoiLabelPathForImageAbsoluteKeepsRoot`, `TestRoiLabelPathForImageDriveLetterStyle`, `TestRoiLabelPathForImageWithoutImagesDirIsAnError`, `TestListRoiTrainingSamplesLoadsBoxesFromAbsoluteDataset`, `TestReadRoiLabelFileRoundTrip`, `TestReadRoiLabelFileMissingFileReturnsEmpty`, `TestReadRoiLabelFileSkipsMalformedLines`, `TestListRoiTrainingSamplesFiltersByPrefix`, `TestUpdateAndDiscardRoiTrainingSample`, `TestUpdateRoiTrainingSampleRegistersAnyTaxonomyLabel`, `TestUpdateRoiTrainingSampleAllowsDuplicateNipples`, `TestGetRoiDatasetSummaryCountsPerClassAndSplit`, `TestGetRoiDatasetSummaryWithoutClassesJsonIsNotAnError`, `TestGetRoiTrainingSampleImageReturnsBase64`, `TestGetRoiTrainingSampleImageMissingFileIsAnError`, `TestRoiTrainingSamplePrefixIsFilenameSafeAndUnique` | todo-later | P4 (AI Train, Suite; ROI dataset browser). |
| `app_runtime_test.go` | `TestRuntimeDirsIncludeSamNPlayerTree` | todo-later | P4 (Tools home: runtime folders). |
| `app_runtime_test.go` | `TestEnsureRuntimeReadyCreatesMissingDirs` | todo-later | P4 (Tools home: Check folders). |
| `app_runtime_test.go` | `TestMaybeConnectSmokeTestRespectsSetting` | todo-later | P4 (connect smoke test setting). |
| `app_samn_omarker_test.go` | `TestSamnOMarkersAndContactRoundTrip` | covered | tests/core/omarker_test.cpp "script marks: .funscript metadata and .samn document"; tests/core/samn_file_test.cpp (contact marks in the document) |
| `app_samn_test.go` | `TestLoadSamnAndExportFunscript` | covered | tests/core/samn_file_test.cpp "samn_file: export is byte identical to Go", "Go file reads and rewrites identically", "write then read round trip" |
| `app_scene_map_export_test.go` | `TestResolveSamnForLearning`, `TestExportSceneMapLearningRequiresOptIn` | todo-later | P4 (Scene Map learning export, Suite, opt-in). |
| `app_scene_proposals_test.go` | `TestSceneProposalsPathBesideVideo`, `TestLoadSceneProposalAt`, `TestLoadSceneProposalsBesideVideo` | todo-later | P4 (scene proposals beside the video). |
| `app_script_state_test.go` | `TestLoadedScriptStateNoRace` | n/a | Go data-race test of the loaded-script state. |
| `app_session_guard_test.go` | `TestStartTrainingKeepsDeviceWhenSessionBusy` | todo-later | P2 (Training). |
| `app_session_guard_test.go` | `TestStartPlaybackKeepsPlayerWhenSessionBusy` | n/a | Go `App` pointer bookkeeping (`activePlayer` / `activeDevice`); the Qt rule "no connect while a session is active" is covered by `playback_output_test.cpp connectionIsRefusedWhilePlaying`. |
| `app_snapshot_test.go` | `TestSaveVideoSnapshot` | ported | tests/app/player_controller_test.cpp `snapshotNameAndSave` (cancel is the dialog's own) |
| `app_sound_vibration_test.go` | `TestBakeSoundVibrationFollowsLoudness`, `TestBakeSoundVibrationWithContactKeepsScriptVibration`, `TestBakeSoundVibrationRejects` | todo-later | P3 (Sound Curve: loudness written as the vibration curve). |
| `app_stroke_filter_test.go` | `TestStrokeFilterSettingsRoundTrip` | covered | tests/app/playback_output_test.cpp `outputChoicesAreRemembered`, `speedLimitFilterShapesWhatIsSent` |
| `app_suggest_test.go` | `TestSuggestPolarityAndInvertRoundtrip`, `TestSuggestOZoneOnLoadedScript`, `TestApplyRingDownOnLoadedScript`, `TestHonorInvertedSetting`, `TestInvertLoadedScriptExportsSamnCompanion` | todo-later | P4 Editor (Suggest polarity / O-zone, ring-down, honor inverted, invert + export .samn). |
| `app_tcode_test.go` | `TestConnectDeviceViaTCode` | covered | tests/app/device_controller_test.cpp `tcodeOverTcp`, `tcodeOverUdpDefaultScheme` |
| `app_tcode_test.go` | `TestConnectDeviceViaTCodeNeedsAddress` | covered | tests/app/device_controller_test.cpp `tcodeBadAddress` |
| `app_tcode_test.go` | `TestSetMotorLevelsClampsAndStores` | covered | tests/app/device_controller_test.cpp `motorLevelsAreAccepted` |
| `app_tcode_test.go` | `TestConnectDeviceViaTCodeSerialNeedsPort` | covered | tests/app/device_controller_test.cpp `tcodeSerialMissingPort` |
| `app_test.go` | `TestShutdownStopsActiveSessionDevice`, `TestShutdownWithoutActiveSessionIsSafe` | ported | tests/app/device_controller_test.cpp `shutdownStopsTheConnectedDevice`, `shutdownWithoutSessionIsSafe` |
| `app_thumbnails_test.go` | `TestThumbTimes`, `TestGetVideoThumbnails` | todo-later | P4 (Scene Map thumbnails, scene strip). |
| `app_training_history_test.go` | `TestSummarizeSessionLogAggregatesCycles`, `TestSummarizeSessionLogSkipsBrokenLinesKeepsRest`, `TestSummarizeSessionLogEmptyFileReturnsNil`, `TestTrainingHistorySortsNewestFirstAndSkipsNonJsonl`, `TestTrainingHistoryNoSessionsDirIsNotAnError`, `TestTrainingHistoryCSVFormatsRows`, `TestTrainingHistoryCSVEmptyHistoryIsJustHeader`, `TestOpenSessionLogSanitizesScriptName` | todo-later | P2 (Training history). |
| `app_training_intensity_test.go` | `TestResolveTrainingScriptAppliesIntensityFactor`, `TestResolveTrainingScriptNeutralFactorLeavesScriptUnchanged`, `TestResolveTrainingScriptUnknownNameErrors`, `TestTrainingScriptPreviewReflectsIntensityFactor`, `TestScriptPreviewPlateauStartsFromFloor` | todo-later | P2 (Training intensity). |
| `app_training_safety_test.go` | `TestTrainingDoneMessagesDistinguishesSafetyCeilingFromError`, `TestReportArousalHighValueAlsoStopsCurrentCycle`, `TestReportArousalModerateValueDoesNotStopCurrentCycle`, `TestReportArousalNoTrainingRunningIsAnError` | todo-later | P2 (Training safety stop, arousal). |
| `app_training_scripts_test.go` | `TestSaveTrainingScriptRoundTrips`, `TestSaveTrainingScriptRejectsInvalidOrBuiltinName`, `TestSaveTrainingScriptSanitizesPathTraversal`, `TestDeleteTrainingScript`, `TestSaveTrainingScriptNormalizesChannelField`, `TestSaveTrainingScriptRejectsSlugCollisionWithDifferentName`, `TestPreviewTrainingScriptDraftValidatesFirst`, `TestPreviewTrainingScriptDraftFlagsRandomJitter` | todo-later | P2 (Training scripts editor/store). |
| `app_training_video_test.go` | `TestTrainingVideoServedSeparately` | n/a | Wails asset server serving the training video. |
| `benchmark_pair_suggest_test.go` | `TestSuggestBenchmarkPairBesideVideoHubPair`, `TestSuggestBenchmarkPairBesideVideoGoldenSubdirs`, `TestSuggestBenchmarkPairBesideVideoOnlyStemIsCandidate`, `TestSuggestBenchmarkPairBesideVideoNone`, `TestSuggestBenchmarkPairBesideVideoRequiresPath` | todo-later | P4 (Bench, Suite). |
| `counting_device_test.go` | (no test function) | n/a | Helper type only (`countingDevice`), no test function. |
| `identifiers_test.go` | `TestNoPersonalIdentifiersInSource` | ported | ctest `identifiers` (tests/check_identifiers.cmake; also versioned model identifiers, D-120) |
| `settings_store_test.go` | `TestSettingsStoreKeepsCorruptFileAside` | ported | tests/core/settings_test.cpp "broken settings file is kept aside" |
| `settings_store_test.go` | `TestSettingsStoreNullFile` | ported | tests/core/settings_test.cpp "settings: a file holding null loads as empty and accepts a set" |
| `settings_store_test.go` | `TestSettingsStoreSetManyOneWrite` | ported | tests/core/settings_test.cpp "settings: set_many, defaults, atomic write leaves no temp files" |
| `settings_store_test.go` | `TestSettingsStoreSaveLeavesNoTempFiles` | ported | tests/core/settings_test.cpp "settings: set_many, defaults, atomic write leaves no temp files" (file count == 1) |

### 2.15 `generator (Go)`

72 files, 237 tests: 37 ported, 2 covered, 0 todo-now, 185 todo-later, 13 n/a.

| Old file | Test function(s) | Status | Where / why |
|---|---|---|---|
| `ai_setup_test.go` | `TestParseAISetupReport`, `TestInstallAISetupRejectsUnknownProfile`, `TestContactPointsArgs`, `TestRunEmbeddedPythonAISetupDryRun` | todo-later | P4 (Local AI setup check / install); the AI tooling stays Python (D-134). |
| `ai_target_test.go` | `TestExpectedTipAIArgsAreStrictAndDoNotUsePreferences`, `TestParseTipDetectionLine`, `TestParseStrictTipErrorLine`, `TestParseTipDetectionRejectsUnrelatedOutput`, `TestFindExpectedTipROIAIHonorsCanceledContext` | todo-later | P3.7 (AI tip find via ONNX). |
| `aiscript/export_test.go` | `TestExportImitationSample`, `TestExportImitationSampleRejectsShort` | todo-later | P4 (AI draft script, opt-in). |
| `aiscript/library_test.go` | `TestLoadImitationSamplesSkipsBad`, `TestPickBestSamplePrefersDurationAndQD`, `TestPickBestSamplePrefersTipAspect`, `TestTipAspect`, `TestStretchActions` | todo-later | P4 (AI draft script, opt-in). |
| `aiscript/writer_test.go` | `TestStatusForEmpty`, `TestStatusForPathStillClosedWithoutLibrary`, `TestStatusWithLibraryAvailable`, `TestDraftFailsClosedWithoutLibrary`, `TestDraftFromImitation` | todo-later | P4 (AI draft script, opt-in). |
| `args_test.go` | `TestGeneratorOptionsReachPython` | n/a | Checks that Go generator options reach the embedded Python script's command line. |
| `audio_segments_test.go` | `TestAnalyzeSpeechHoldSegmentsSpeechThenImpact`, `TestAnalyzeSpeechHoldSegmentsTooShort`, `TestAppendSpeechHoldHints`, `TestAppendSpeechHoldHintsClimax`, `TestMergeSegmentHintsMinDuration`, `TestPromoteClimax` | todo-later | P3.6 (audio segments, speech-hold hints). |
| `audio_vibration_test.go` | `TestWindowRMS` | ported | tests/core/audio_envelope_test.cpp "audio envelope: window RMS with a half-full tail" |
| `audio_vibration_test.go` | `TestNormalizeLoudnessSilenceLoudAndRelease` | ported | tests/core/audio_envelope_test.cpp "audio envelope: silence, loud and release" |
| `audio_vibration_test.go` | `TestNormalizeLoudnessAllSilent` | ported | tests/core/audio_envelope_test.cpp "audio envelope: an all-silent track stays 0" |
| `audio_vibration_test.go` | `TestAudioVibrationEnvelopeFromVideo` | covered | tests/app/player_controller_test.cpp `audioLevelFromSound` (decoder with a generated WAV; Qt decodes, no ffmpeg) |
| `audio_vibration_test.go` | `TestAudioVibrationEnvelopeCancelled` | todo-later | P3.6: cancelling the audio decode is not tested/ported for `AudioLevelDecoder`. |
| `audio_vibration_test.go` | `TestEstimateVideoAudioTempoHzClickTrack` | todo-later | P3.6 (audio tempo estimate). |
| `audiocheck_test.go` | `TestDominantFrequencyHz`, `TestEstimateScriptTempoHz`, `TestAppendQualityAudioHint`, `TestCompareTempo`, `TestAudioEnvelope`, `TestCheckAudioTempoNoAudio`, `TestExtractAudioSamplesContextAlreadyCanceled` | todo-later | P3.6 (audio check: tempo plausibility). |
| `auto_native_test.go` | `TestGenerateWithContextAutoNativeDefault` | todo-later | P3.3 (pipeline selection). |
| `beatbar_test.go` | `TestFindBeatBarAndBeats` | ported | core/python/tests/test_beatbar_parity.py::check_case (synthetic beat-bar cases, run from `main`) |
| `beatbar_test.go` | `TestFindBeatBarNoneInPlainFilm` | ported | core/python/tests/test_beatbar_parity.py::test_noise_rejected |
| `beatbar_test.go` | `TestDetectBeatBarBeatsFromVideo` | todo-later | P3.0/P3.3: reading the beat bar from a video file needs the frame reader. |
| `bodyparts/taxonomy_test.go` | `TestCanonicalCount`, `TestNormalizeAliases`, `TestPreferredClassesCSV`, `TestDefaultRole` | todo-later | P4 (AI body-part taxonomy; keep in sync with the Python copy). |
| `colibri_test.go` | `TestNormalizeColibriBaseURL`, `TestColibriAvailable` | todo-later | P4 (optional AI server client). |
| `contact_candidates_test.go` | `TestImportContactCandidates`, `TestImportContactCandidatesNeedsSceneMap` | todo-later | P4 (contact candidates from the scene map, Suite). |
| `contact_marks_test.go` | `TestBuildContactMarksMetaEmpty`, `TestBuildContactMarksMetaStrokeFeelOnly`, `TestBuildContactMarksMetaDistanceDrives`, `TestBuildContactMarksMetaExtraFollow`, `TestStampContactMarks` | todo-later | P3.6 (contact marks metadata). |
| `contact_points_test.go` | `TestLoadContactPointsFilterAndSort`, `TestLoadContactPointsRejectsBadInput` | todo-later | P4 (contact points from teachers JSON, Suite-like). |
| `contact_review_test.go` | `TestReviewAutoContactCandidateAcceptReject`, `TestSceneMarkAtMsRoundTrip` | todo-later | P4 (Accept/Reject contact candidates). |
| `detrend_default_test.go` | `TestAdaptiveDetrendTwoStrokePeriods`, `TestApplyDefaultDetrendRespectsCallerAndDistanceProfiles` | ported | `tests/engine/signal_chain_test.cpp`: "signal: detrend window is two stroke periods, clamped", "signal: default detrend respects the caller and distance profiles". |
| `distance_partners_test.go` | `TestDistancePartnersActive` | todo-later | P3.4 (two-point / multi-point distance). |
| `frame_dump_test.go` | `TestDumpFrameAtZeroAndMid`, `TestDumpFrameAtPastEndFailsCleanly` | todo-later | P3.0 (frame reader: dump a frame). |
| `generate_cancel_test.go` | `TestGenerateWithContextCancelPython`, `TestGenerateWithContextCancelNativeSimple` | todo-later | P3.3 (run with progress and cancel). |
| `generator_embed_test.go` | `TestAllImportedModulesAreEmbedded`, `TestTestFilesAreNotShipped` | n/a | Checks that Python modules are embedded in the Go binary and that test files are not shipped. |
| `generator_stderr_test.go` | `TestFindROIWithProgressDrainsAllStderr` | n/a | Drains the stderr of the embedded Python process. |
| `generator_test.go` | `TestFindPythonSkipsInterpreterWithoutPackages`, `TestCheckDependenciesListsAllCandidates`, `TestFindPythonErrorWhenNoneRunnable`, `TestIsWindowsAppsPythonStub`, `TestDemoteWindowsAppsStubs` | n/a | Finds a Python interpreter (Windows Store stub, missing packages); the C++ generator needs no Python. |
| `l1_exclude_priors_test.go` | `TestSuggestExcludePriorsInsufficientClips`, `TestSuggestExcludePriorsHotLowerLeft`, `TestSuggestExcludePriorsEmptyDir`, `TestSuggestExcludePriorsNeedsSize`, `TestRectToPriorCellsLowerLeft` | todo-later | P4 (AI Train: exclude priors from reviewed clips). |
| `native_availability_default_test.go` | `TestNativeTrackingUnavailableWithoutOpenCVTag` | n/a | Go build tag (no OpenCV tag). |
| `native_availability_opencv_test.go` | `TestNativeTrackingAvailableWithOpenCVTag` | n/a | Go build tag (`opencv`). |
| `native_contact_verify_e2e_test.go` | `TestGenerateNativeCSRTContactVerifyEndToEnd` | todo-later | P3.1/P3.4 (CSRT end-to-end with contact verify). |
| `native_e2e_test.go` | `TestGenerateNativeCSRTEndToEnd` | todo-later | P3.1 (CSRT end-to-end); equal output on synthetic clips is the P3.1 acceptance test. |
| `native_simple_e2e_test.go` | `TestGenerateNativeSimpleEndToEnd` | todo-later | P3.1 (simple tracker end-to-end). |
| `native_test.go` | `TestNativePipelineEligible`, `TestNativePipelineSkipReason` | todo-later | P3.3 (native pipeline eligibility and skip reasons). |
| `native_two_point_test.go` | `TestGenerateNativeTwoPointSimple` | todo-later | P3.4 (two-point tracking). |
| `native_write_test.go` | `TestWriteFileAtomicReplacesExistingFile` | ported | tests/core/fileio_test.cpp "fileio: atomic write creates, replaces and leaves no temp file" |
| `native_write_test.go` | `TestWriteFileAtomicCleansTempAfterRenameFailure` | ported | tests/core/fileio_test.cpp "fileio: a failed rename leaves the destination and the folder clean" (shared with funscript/atomic_write_test.go) |
| `posttrack/convert_test.go` | `TestPositionsToActionsMatchesPythonGoldens`, `TestLimitSpeedMatchesPythonGolden`, `TestProminenceReducesDampedRinging`, `TestPercentileBeatsMinMaxOnOutlier` | ported | `tests/engine/post_track_test.cpp`: "post: every golden gives the same actions and dense curve as 0.7" (13 goldens, actions exact, dense curve within 0.05; the goldens file is `tests/engine/testdata/posttrack_goldens.json`), "post: the speed limit equals 0.7", "post: prominence quiets damped ringing", "post: percentile normalisation beats min/max on an outlier". |
| `posttrack/filters_test.go` | `TestRollingDetrendRemovesSlowDrift`, `TestBandpassAttenuatesDCAndHighFreq` | ported | `tests/engine/post_track_test.cpp`: "post: detrend removes slow drift and keeps the stroke", "post: bandpass removes DC and keeps the stroke band". |
| `posttrack/preview_test.go` | `TestPreviewStatsDefaultKnobs`, `TestPreviewStatsProminenceReducesPeaks`, `TestPreviewStatsWideSmoothCalmerHint` | todo-later | P3.3 (the Expert preview belongs to the Generator page). |
| `profilemodel/model_test.go` | `TestAppendTrainLoadAndSuggest`, `TestSuggestRejectsUnknownAndAmbiguousSignatures`, `TestOldProfileLabelsRemainTrainable`, `TestAppendLabelRejectsInvalidInput`, `TestCorruptModelDoesNotBlockRetraining`, `TestAppendLabelSerializesConcurrentWriters` | todo-later | P4 (AI profile model). |
| `roi_still_classreg_test.go` | `TestEnsureClassIDsRegistersCanonicalLabels` | todo-later | P4 (AI Train: ROI class registry). |
| `roi_still_test.go` | `TestStillTrainingGetsValSplit`, `TestEnsureRoiDatasetSplitsHealsEmptyVal`, `TestPrepareRoiDatasetForTrainingEmpty`, `TestPrepareRoiDatasetForTrainingHealsThenOK` | todo-later | P4 (AI Train: YOLO dataset splits). |
| `roi_training_cancel_test.go` | `TestRunPythonScriptCtxStopsOnCancel` | n/a | Runs and cancels a Python script process from Go; AI training stays Python and is launched from Python/Qt process code (D-134). |
| `roi_training_install_test.go` | `TestInstallRoiTrainingDepsRepairsSharedOpenCVFiles` | n/a | Repairs shared OpenCV files of a Python install. |
| `roi_training_test.go` | `TestBuildBootstrapArgsSingleRegion`, `TestBuildBootstrapArgsNineRegions`, `TestBuildBootstrapArgsTwelveFreeTagsAllClasses`, `TestBuildBootstrapArgsStartSeconds`, `TestBuildTrainArgsDefaults`, `TestBuildTrainArgsExplicitValues`, `TestRoiArgFormatsAsCommaSeparated`, `TestDefaultRoiDatasetDirIsUnderSamNPlayerNamespace`, `TestRunRoiModelTrainingFailsFastWithoutDataYAML`, `TestRunRoiModelTrainingFailsFastOnEmptyDataset`, `TestBuildTrainArgsClampsEpochs` | todo-later | P4 (AI Train: bootstrap/train argument building and dataset checks). |
| `roi_verify_test.go` | `TestVerifyROIMotionPresent`, `TestVerifyROITooSmall`, `TestVerifyROIHugeBoxWarnsEditable` | todo-later | P3.7 (ROI verify). |
| `samn_write_test.go` | `TestWriteCompanionSamnStrokeContactFeel` | todo-later | P3.3 (generator writes the companion `.samn`; the document writer is ported, the Create-time logic is not). |
| `scene_map_export_test.go` | `TestExportSceneMapLearningRequiresOptIn`, `TestExportSceneMapLearningArtifacts`, `TestExportSceneMapLearningNoAgreementNoAuto`, `TestReviewedYOLOEligibilityGate`, `TestReviewedYOLOGroupsMarksByTimestamp`, `TestDeleteSceneMapLearningDataPreservesSibling` | todo-later | P4 (Scene Map learning export, Suite). |
| `scene_map_load_test.go` | `TestSceneMapFromPersistRoundTrip`, `TestLoadSceneMapBesideVideo`, `TestSceneMarkReviewedConfidenceRoundTrip`, `TestLoadSceneMapBesideVideoOldSamn` | todo-later | P4 (Scene Map load beside the video). |
| `scene_map_persist_test.go` | `TestBuildSceneMapMetaRoundTrip`, `TestBuildSceneMapMetaNilWithoutWindows` | todo-later | P4 (Scene Map persist). |
| `scene_proposals_test.go` | `TestLoadSceneProposals`, `TestLoadSceneProposalsErrors`, `TestApplySceneProposal` | todo-later | P4 (scene proposals). |
| `script_quality_samn_test.go` | `TestScriptQualityAcceptsSamnGeneral` | todo-later | P3.6 (Quality Doctor on a `.samn`). |
| `simpletrack/progress_test.go` | `TestProgressReporterThrottles`, `TestProgressReporterNilSafe`, `TestProgressTotalFramesAfterSeek` | todo-later | P3.1 (progress reporter). |
| `simpletrack/tip_distance_test.go` | `TestTipPartnerDistance_largePenisUsesNearEdge`, `TestTipPartnerDistance_overlapIsZero`, `TestFuseTipPartners_excludesLost` | todo-later | P3.4 (tip/partner distance). |
| `simpletrack/track_test.go` | `TestTrackROISynthetic`, `TestTrackROICancel` | todo-later | P3.1 (simple tracker). |
| `simpletrack/track_two_test.go` | `TestTrackTwoPointsSynthetic`, `TestTrackTwoPoints_fixedVsTrackedPartner` | todo-later | P3.4 (two-point tracker). |
| `simpletrack/trajectory_test.go` | `TestTrackROICaptureTrajectory`, `TestTrackTwoPointsCaptureTrajectory` | todo-later | P3.4 (trajectory capture). |
| `stroke_preview_phase_test.go` | `TestApplyStrokePreviewSynthetic`, `TestApplyStrokePreviewSkip`, `TestApplyStrokePreviewSteersCutRateEnablesPerSceneROI`, `TestApplyStrokePreviewSteersPanEnablesCamera`, `TestApplyStrokePreviewSteersNoopWhenAlreadyOn`, `TestSoftenPythonOnlyOptsClearsPerSceneWhenGoCSRT` | todo-later | P3.6 (stroke preview steering). |
| `strokepreview/hint_test.go` | `TestToHintSuggestsPeakDistance`, `TestToHintWeakSuggestsAudio` | todo-later | P3.6 (stroke preview). |
| `strokepreview/overlap_test.go` | `TestPeakOverlapMatchesNearbyUps` | todo-later | P3.6 (stroke preview). |
| `strokepreview/preview_test.go` | `TestAnalyzeSyntheticBounceFindsExtrema`, `TestFrameStatsIgnoresCodecNoise`, `TestClassifyWeakWhenNoMotion`, `TestStrokeHzFromPeaks`, `TestAnalyzeMissingFile`, `TestFindPeaksExportStillWorks` | todo-later | P3.6 (stroke preview). |
| `trackcv/appearance_memory_test.go` | `TestAppearanceMemoryMatchesOriginalAcceptsTheOriginalRegion`, `TestAppearanceMemoryMatchesOriginalRejectsDriftedRegion`, `TestAppearanceMemoryMatchesOriginalNotDefeatedByShrunkBox`, `TestAppearanceMemoryMatchesOriginalNoTemplatesYetAllowsRemembering`, `TestTrackROIStillWorksWithGatedAppearanceMemory` | ported | `tests/engine/track_box_test.cpp`: "parts: appearance memory knows the start region and fails closed" (original region, drifted region, shrunk box on / off target, no templates yet), "track: the gated memory does not lose easy material". |
| `trackcv/contact_points_test.go` | `TestApplyContactPointsGateAndHold`, `TestRhythmGridContactPointsReachFarStroke`, `TestRhythmGridContactPointsNearBoxBitIdentical`, `TestVerifyContactPointsKeepsOnlyWhereEngineMeasuresMore`, `TestVerifyContactPointsAtHonoursStartOffset` | todo-later | P4 (contact points; teacher JSON, verify with the engine). |
| `trackcv/contact_verify_e2e_test.go` | `TestTrackROIContactVerifyFiltersQuietTeacherPoints` | todo-later | P4 (contact verify, hybrid K=1.5). |
| `trackcv/contact_verify_edge_test.go` | `TestVerifyContactPointsUsesNearestOverlappingWindow`, `TestVerifyContactPointsClampsThreeByThreeAtImageEdges` | todo-later | P4 (contact verify). |
| `trackcv/dispguard_test.go` | `TestDispGuardFlagsImplausibleJumpsAtRealClipScale`, `TestDispGuardAllowsLargestGenuineRealClipMotion`, `TestDispGuardIgnoresJitterBelowFloorRegardlessOfRatio`, `TestDispGuardRatioScalesWithFastMotion`, `TestDispGuardAcceptTrimsWindow`, `TestDispGuardColdStartNeverFlags` | ported | `tests/engine/track_box_test.cpp`: "parts: the jump guard flags real-clip jumps, not real motion" (all six cases). |
| `trackcv/multi_recover_test.go` | `TestReacquireRejectsPoisonedDistractorVersusTipSeed`, `TestReacquirePrefersTipWhenVisibleDespiteDistractorTemplate`, `TestTrackTwoPointsHardTipAppearanceMemoryStable`, `TestRecoverOrCoastResetsCoastOnReacquire` | todo-later | P3.4 (re-acquire, coast). |
| `trackcv/progress_total_test.go` | `TestProgressTotalFramesAfterSeek` | ported | `tests/engine/track_box_test.cpp`: "parts: progress total after a seek". |
| `trackcv/rhythm_grid_test.go` | `TestRhythmGridRecoversStrokeDespiteDrift`, `TestRhythmGridKeepsConfirmedTargetWhenTrackerDriftsOntoThigh`, `TestRhythmGridRejectsInPhaseAndAntiPhaseHighAmplitudeThigh`, `TestRhythmGridReseedsConfirmedIdentityAfterSceneCut`, `TestRhythmGridFallsBackToTracker`, `TestRhythmGridBadInputReturnsTracker`, `TestRhythmGridRows`, `TestRhythmGridKeepsOrientationWhereTrackerIsUninformative`, `TestRhythmGridSplitBitIdentical`, `TestNormalizeScores`, `TestScoreWindowsQuickScanNoBox`, `TestRhythmGridExcludeSkipsHighScoringThigh`, `TestRhythmGridExcludeUsesFollowPath`, `TestRhythmGridSourceHintPreference`, `TestRhythmGridMarksNilBitIdentical`, `TestRhythmGridReleasesLockWhenLockedCellGoesQuiet` | todo-later | P3.4 (rhythm grid). |
| `trackcv/tip_distance_test.go` | `TestTipPartnerDistance_smallTipMatchesCenter`, `TestTipPartnerDistance_largePenisUsesNearEdge`, `TestTipPartnerDistance_overlapIsZero`, `TestFuseTipPartners_excludesLostPartner` | todo-later | P3.4 (tip/partner distance). |
| `trackcv/track_test.go` | `TestTrackROIBasicAmplitude`, `TestTrackROICameraCompensation`, `TestTrackROISceneCutDetection`, `TestTrackROIAxisSelection`, `TestTrackROIMaxFrames` | ported | `tests/engine/track_box_test.cpp`: "track: follows the amplitude on easy material", "track: camera compensation brings the amplitude closer", "track: a hard cut is found and motion continues after it", "track: the axis follows the motion", "track: max frames, start time, progress and cancel". Positions equal 0.7 bit for bit (`docs/benchmarks/P3_1_TRACK_PARITY.md`). |
| `trackcv/track_test.go` | `TestMain_videoWriterProducesReadableFile` | covered | every clip writer in `tests/engine/synthetic_clips.h` is checked with `REQUIRE` before tracking. |
| `trackcv/track_test.go` | `TestTrackROIRhythmGrid` | todo-later | P3.4 (rhythm grid). |
| `trackcv/types_test.go` | `TestSceneMarkRectAt_static`, `TestSceneMarkRectAt_nearestPath`, `TestCameraExcludeRects_usesPath`, `TestMarkShouldFollow`, `TestSceneMarkTimebaseIncludesStartOffset`, `TestSceneMarksShiftedKeepsSentinelAndInput` | todo-later | P3.4 (scene marks geometry). |
| `tracking_gaps_test.go` | `TestTrackingGapsFromFlags` | todo-later | P3.4 (tracking gaps from flags). |
| `trackutil/coast_test.go` | `TestCoastBridgesBriefLoss`, `TestCoastExhausts`, `TestCoastHoldsWithoutVelocity`, `TestCoastNoPosition`, `TestCoastBudgetReopensAfterObserveOK` | ported | `tests/engine/track_box_test.cpp`: "parts: coast moves on for 8 frames, then gives up". |
| `trackutil/coast_test.go` | `TestCoastResetAfterReacquire` | todo-later | P3.4 (`Reset` and `MaxFrames` come with the two-point tracker). |
| `truncate_test.go` | `TestTruncateActionsMs` | todo-later | P3.2 (`TruncateActionsMs`). |

### 2.16 `generator (Python)`

56 files, 151 tests: 0 ported, 0 covered, 0 todo-now, 151 todo-later, 0 n/a.

| Old file | Test function(s) | Status | Where / why |
|---|---|---|---|
| `ai_profile_test.py` | `main` script, 17 checks | todo-later | P4 (AI profile suggestion via the optional AI server); stays Python (D-134). |
| `ai_quality_cli_test.py` | `main` script, 9 checks | todo-later | P4 (optional AI second opinion on quality); stays Python (D-134). |
| `ai_quality_test.py` | `main` script, 11 checks | todo-later | P4 (optional AI second opinion on quality); stays Python (D-134). |
| `ai_roi_test.py` | `main` script, 41 checks | todo-later | P3.7 (ONNX ROI suggestion; inference moves to C++, training stays Python). |
| `ai_setup_test.py` | `test_nvidia_smi`, `test_probe_server_ollama_and_openai`, `test_recommended_models_by_vram`, `test_fresh_pc_gets_all_next_steps`, `test_ready_pc_only_misses_models`, `test_teachers_keep_opencv_contrib`, `test_train_goes_into_separate_venv_with_cuda_torch`, `test_train_venv_check`, `test_models_by_gpu` | todo-later | P4 (Local AI setup); stays Python (D-134). |
| `appearance_memory_test.py` | `main` script, 13 checks | todo-later | P3.4 (appearance memory); port as C++ test with the C++ tracker. |
| `audio_check_cli_test.py` | `main` script, 7 checks | todo-later | P3.6 (audio check CLI wiring); retired with the Python generator (D-134). |
| `audio_check_test.py` | `main` script, 25 checks | todo-later | P3.6 (audio/script tempo plausibility); port as C++ test. |
| `auto_roi_candidates_test.py` | `main` script, 6 checks | todo-later | P3.7 (auto ROI candidates). |
| `auto_roi_region_bounds_test.py` | `main` script, 6 checks | todo-later | P3.7 (auto ROI region bounds). |
| `auto_roi_tip_find_test.py` | `main` script, 12 checks | todo-later | P3.7 (auto tip find). |
| `auto_roi_two_point_test.py` | `main` script, 3 checks | todo-later | P3.7 (auto two ROIs). |
| `backends_test.py` | `main` script, 19 checks | todo-later | P3.5 (backend registry). |
| `bodyparts_test.py` | `test_canonical_count`, `test_normalize_aliases`, `test_preferred_csv_has_mouth_and_vagina`, `test_default_roles` | todo-later | P4 (AI body-part taxonomy; Python copy). |
| `bootstrap_yolo_dataset_test.py` | `main` script, 31 checks | todo-later | P4 (AI Train; stays Python, D-134). |
| `camera_compensation_test.py` | `main` script, 5 checks | todo-later | P3.1 (camera compensation). |
| `colibri_client_test.py` | `main` script, 5 checks | todo-later | P4 (optional AI server client; stays Python). |
| `contact_detector_test.py` | `test_clip_split_and_class_remap`, `test_multi_box_frame_keeps_every_box`, `test_class_filter_and_frame_split_for_few_clips`, `test_errors`, `test_train_calls_rfdetr_and_writes_model_card`, `test_missing_rfdetr_is_a_clear_error`, `test_decode_drops_background_and_thresholds`, `test_model_runs_session_and_returns_point` | todo-later | P3.7/P4 (contact detector; inference in C++, training Python). |
| `contact_film_bugfix_test.py` | `main` script, 26 checks | todo-later | P3.6 (contact vibration + SAM film-clip regression). |
| `contact_points_test.py` | `test_penis_wins`, `test_cleavage_between_pair`, `test_two_peoples_breasts_are_not_a_pair`, `test_no_face_rule_and_low_scores_ignored`, `test_distinct_teachers_close_in_space_and_time`, `test_nudenet_plus_vlm_consensus_and_go_format`, `test_no_teacher_is_an_error`, `test_parse_teacher`, `test_runs_each_backend_caches_and_skips_failures` | todo-later | P4 (contact points from teachers, Suite-like). |
| `create_tracker_test.py` | `main` script, 14 checks | todo-later | P3.1 (tracker factory). |
| `device_profile_test.py` | `main` script, 13 checks | todo-later | P3.6 (device compatibility check). |
| `export_yolo_onnx_test.py` | `main` script, 5 checks | todo-later | P4 (AI Train export to ONNX; stays Python). |
| `flow_backend_test.py` | `main` script, 11 checks | todo-later | P3.5 (optical-flow backend). |
| `flow_dispatch_test.py` | `test_cli_scale_reaches_backend`, `test_registry_flow_forwards_on_progress` | todo-later | P3.5 (flow dispatch). |
| `fungen_compare_test.py` | `main` script, 21 checks | todo-later | P3.8 (comparison controller). |
| `fungen_compare_windowed_test.py` | `main` script, 8 checks | todo-later | P3.8 (comparison controller). |
| `fusion_test.py` | `main` script, 11 checks | todo-later | P3.5 (fusion of several measurement sources). |
| `golden_clip_benchmark_cli_test.py` | `main` script, 5 checks | todo-later | P3 gate / P3.8 (golden clip benchmark runner). |
| `golden_clip_benchmark_test.py` | `main` script, 21 checks | todo-later | P3 gate / P3.8 (golden clip benchmark runner). |
| `grid_lk_backend_test.py` | `main` script, 16 checks | todo-later | P3.5 (grid LK backend). |
| `grid_lk_two_point_test.py` | `main` script, 6 checks | todo-later | P3.5 (grid LK two-point). |
| `motion_signature_test.py` | `main` script, 15 checks | todo-later | P3.5 (motion signature). |
| `multi_point_test.py` | `test_punch_exclude_boxes`, `test_estimate_camera_motion_accepts_extra_excludes`, `test_min_distance_logic_three_partners`, `test_track_multi_points_requires_targets` | todo-later | P3.4 (multi-partner distance). |
| `per_scene_roi_test.py` | `main` script, 9 checks | todo-later | P3.7 (per-scene ROI search). |
| `pose_observer_test.py` | `test_parse_roundtrip_keys`, `test_norm_to_pixel_clips`, `test_seed_proposals_include_hands_mouth_and_weak_tip`, `test_missing_people_yields_no_seeds`, `test_mediapipe_missing_model_is_soft_error`, `test_onnx_missing_model_is_soft_error`, `test_jitter_zero_when_identical`, `test_backend_status_keys`, `test_multi_person_uses_highest_confidence`, `test_malformed_partial_observation`, `test_confidence_filter_and_fuse_ranking`, `test_summarize_metrics_rows`, `test_unknown_backend_soft_error` | todo-later | P3.7/P4 (pose observer). |
| `process_one_kwargs_test.py` | `main` script, 0 checks | todo-later | P3.2 (profile signal kwargs into positions_to_funscript). |
| `profile_suggestion_test.py` | `main` script, 13 checks | todo-later | P4 (--label-scene / --suggest-profile wiring). |
| `quality_doctor_test.py` | `main` script, 51 checks | todo-later | P3.6 (Quality Doctor); port as C++ test with `quality_doctor`. |
| `quality_model_test.py` | `main` script, 16 checks | todo-later | P3.6/P4 (learned quality rating). |
| `region_fusion_auto_backend_test.py` | `main` script, 13 checks | todo-later | P3.5 (region fusion auto backend). |
| `region_fusion_backend_test.py` | `main` script, 14 checks | todo-later | P3.5 (region fusion backend). |
| `report_test.py` | `main` script, 16 checks | todo-later | P3.8 (measurement report, batch). |
| `roi2_dispatch_warnings_test.py` | `main` script, 15 checks | todo-later | P3.4 (--roi2 two-point dispatch warnings). |
| `samn_write_test.py` | `test_writes_tj_document` | todo-later | P3.3 (companion `.samn` writer next to generated scripts); the document writer is ported. |
| `scene_roles_test.py` | `test_decode_base64_and_keep_longest_per_sample`, `test_part_motion`, `test_mapping_breast_pair_and_derived_mouth`, `test_moving_mouth_on_penis_is_blowjob_mouth_primary`, `test_moving_penis_between_breasts_is_titjob_penis_primary`, `test_nipples_and_glans_type_titjob_keeping_class_ids`, `test_hand_1_alias_types_handjob`, `test_hidden_penis_uses_moving_part_with_low_confidence`, `test_far_apart_pair_and_no_motion`, `test_build_proposals_and_contact_points`, `test_recorded_per_window_and_agreement_counted`, `test_score_against_truth`, `test_truth_file_in_repo_is_valid` | todo-later | P4 (scene roles, stage 2). |
| `script_quality_test.py` | `main` script, 9 checks | todo-later | P3.6 (--script-quality wiring). |
| `support_signals_test.py` | `main` script, 6 checks | todo-later | P3.5 (support signals). |
| `tf_tj_meta_test.py` | `test_is_distance_profile`, `test_device_recipe`, `test_device_recipe_contact_vibration`, `test_device_recipe_contact_span_and_curve`, `test_clamp_contact_span`, `test_clamp`, `test_metadata`, `test_metadata_contact_vibration`, `test_metadata_stroke_contact_vibration`, `test_metadata_contact_curve`, `test_normalize_contact_curve_impulse` | todo-later | P3.3 (tf/tj recipe metadata written by the generator); the recipe itself is ported (`RecipeFor`). |
| `track_cache_test.py` | `main` script, 16 checks | todo-later | P3.3 (tracking result cache). |
| `tracking_gaps_test.py` | `test_empty`, `test_single_block`, `test_merge_close_blocks`, `test_separate_blocks` | todo-later | P3.4 (tracking gaps from flags). |
| `train_yolo_model_test.py` | `main` script, 42 checks | todo-later | P4 (AI Train; stays Python). |
| `two_point_axis_test.py` | `main` script, 4 checks | todo-later | P3.4 (two-point axis regression). |
| `two_point_test.py` | `main` script, 7 checks | todo-later | P3.4 (two-point measurement). |
| `vlm_probe_test.py` | `test_full_hd_to_896`, `test_multiples_of_28_never_upscaled`, `test_fenced_pixel_answer`, `test_dict_wrapper_swapped_corners_and_clip`, `test_bad_items_dropped`, `test_empty_refused_unparsed`, `test_labels`, `test_each_convention_is_recognised`, `test_unusable_answers_fail_closed`, `test_interval_plus_scene_midpoints_deduplicated`, `test_no_interval`, `test_end_to_end_with_auto_calibration`, `test_failed_calibration_stops_before_frames`, `test_errors_and_refusals_are_recorded_not_fatal`, `test_load_exemplars_from_file_and_cli`, `test_draw_reference_marks_boxes`, `test_references_first_query_last_follow_adds_previous_answer`, `test_no_exemplars_keeps_single_image_prompt`, `test_exclude_label_kept`, `test_clip_times_end_at_keyframe_oldest_first`, `test_parse_clip_answer`, `test_probe_sends_sequence_and_records_roles`, `test_clip_and_exemplars_rejected` | todo-later | P4 (VLM teacher experiments; stays Python). |
| `vlm_score_test.py` | `test_hit_miss_exclude_and_missing`, `test_largest_contact_box_counts`, `test_empty_probe`, `test_exemplar_keyframes_are_not_scored`, `test_iou`, `test_committed_labels_are_well_formed`, `test_oracle_scores_itself_perfectly` | todo-later | P4 (VLM teacher scorer; stays Python). |

### 2.17 `native/tests (Python)`

4 files, 13 tests: 13 ported, 0 covered, 0 todo-now, 0 todo-later, 0 n/a.

| Old file | Test function(s) | Status | Where / why |
|---|---|---|---|
| `test_beat_strokes_parity.py` | `test_down_on_every_beat`, `test_speed_limit_and_hold` | ported | core/python/tests/test_beat_strokes_parity.py (same file, same test names) |
| `test_beatbar_parity.py` | `test_noise_rejected` | ported | core/python/tests/test_beatbar_parity.py (same file, same test names) |
| `test_funscript_parity.py` | `test_float_actions`, `test_corrupt_times`, `test_duration_ofs`, `test_inverted`, `test_clamp_pos`, `test_shift_actions` | ported | core/python/tests/test_funscript_parity.py (same file, same test names) |
| `test_stroke_filter_parity.py` | `test_off_changes_nothing`, `test_smooth_keeps_points`, `test_speed_limit`, `test_idle_fill` | ported | core/python/tests/test_stroke_filter_parity.py (same file, same test names) |

### 2.18 `scripts/benchmark-prep (Python)`

1 files, 8 tests: 0 ported, 0 covered, 0 todo-now, 8 todo-later, 0 n/a.

| Old file | Test function(s) | Status | Where / why |
|---|---|---|---|
| `cut_clip_test.py` | `test_seconds_and_hms`, `test_scale_never_upscales_pattern`, `test_argv_matches_proxy_style`, `test_no_audio`, `test_rejects_bad_window`, `test_load_and_window`, `test_dry_run_marks`, `test_cut_synthetic` | todo-later | P4 (Bench clip prep): developer script that stays Python tooling; port its tests with the script if it is kept. |

### 2.19 `frontend (Python/Playwright UI tests)`

148 files, 148 tests: 6 ported, 34 covered, 0 todo-now, 103 todo-later, 5 n/a.

| Old file | Test function(s) | Status | Where / why |
|---|---|---|---|
| `benchmark_cancel_test.py` | `main` script, 9 checks | todo-later | P4 (Bench tab, Suite). |
| `benchmark_clip_prep_test.py` | `main` script, 17 checks | todo-later | P4 (Bench tab, Suite). |
| `benchmark_score_test.py` | `main` script, 10 checks | todo-later | P4 (Bench tab, Suite). |
| `benchmark_suggest_pair_test.py` | `main` script, 7 checks | todo-later | P4 (Bench tab, Suite). |
| `chapters_display_test.py` | `main` script, 5 checks | covered | tests/app/player_controller_test.cpp `chaptersBookmarksHeatmapLanes`, `editMarks` |
| `create_clip_grid_test.py` | `main` script, 9 checks | todo-later | P3.3 (Generator page, Create layout/knobs). |
| `create_feel_expert_prefs_test.py` | `main` script, 12 checks | todo-later | P3.3 (Generator page, Create layout/knobs). |
| `create_layout_test.py` | `main` script, 27 checks | todo-later | P3.3 (Generator page, Create layout/knobs). |
| `create_project_head_test.py` | `main` script, 10 checks | todo-later | P3.3 (Generator page, Create layout/knobs). |
| `curve_display_test.py` | `main` script, 5 checks | covered | tests/app/player_controller_test.cpp `curveModes`; tests/core/curve_geometry_test.cpp (Catmull-Rom, density switch, tempo wave) |
| `curve_editor_test.py` | `main` script, 11 checks | todo-later | P4 (Editor: curve dots drag, range tools). |
| `curve_editor_video_sync_test.py` | `main` script, 4 checks | todo-later | P4 (Editor: video follows a dragged point). |
| `device_display_test.py` | `main` script, 39 checks | covered | tests/app/device_controller_test.cpp, tests/app/inspector_qml_test.cpp (Inspector Devices). Open: Tools -> Device page, diagnostics UI, raw value test (listed in `AGENT_STATE.md`) |
| `device_tcode_serial_test.py` | `main` script, 9 checks | covered | tests/app/device_controller_test.cpp `tcodeSerialMissingPort`, `serialPortListArrives`; tests/app/device_transports_test.cpp `serialPortListIsSorted` |
| `device_tcode_test.py` | `main` script, 9 checks | covered | tests/app/device_controller_test.cpp `tcodeOverTcp`, `tcodeOverUdpDefaultScheme`, `tcodeRefusedGivesHint` |
| `drop_test.py` | `main` script, 24 checks | covered | tests/app/player_controller_test.cpp `playlist` (`openPaths`), `scriptFindsVideoBeside`, `videoFindsScriptInFolder`; drop overlay/routing in QML is not tested |
| `edition_switch_test.py` | `main` script, 7 checks | n/a | JS DOM: `html[data-edition]` toggling of `[data-suite]` controls; the edition split is an open decision (D-004, D-007). |
| `eula_gate_test.py` | `main` script, 6 checks | todo-later | P5 (EULA gate on first start). |
| `expert_smooth_peakdist_zero_test.py` | `main` script, 8 checks | todo-later | P3.3 (Generator page, Create layout/knobs). |
| `flow_assets_test.py` | `test_flow_assets` | ported | tests/app/shell_qml_test.cpp `bundledVectorAssetsAreSafe` (every bundled SVG: well-formed, `<svg>` root with viewBox, no script, foreignObject, `on*` handler or network `href`/`url()`; every SVG on disk is bundled), `iconNamesInTheQmlSourcesExist`. Left out: `aria-hidden` (Qt images have no accessibility tree) and the fixed 0.7 file list (the lint covers all files). |
| `flow_fx_test.py` | `test_flow_fx` | n/a | JS DOM smoke of the idle-stage CSS animation contract; the Qt stage is `FlowStage.qml` (covered by the CI screenshot smoke). |
| `flow_icons_test.py` | `test_flow_icons` | covered | tests/app/icon_provider_test.cpp `colorsAndRenders`, `unknownOrUnsafeNamesGiveNothing` |
| `flow_wiring_test.py` | `main` script, 17 checks | ported | tests/app/shell_qml_test.cpp `shellLoadsWithoutWarningsAndShowsItsIcons`, `everyModeLoadsWithoutWarnings`, `modeDeckOpensWithMAndPicksWithNumbers` (shell loads in every mode without QML warnings; top bar, deck and views show icons that exist and loaded; M opens, 1-4 pick, Esc closes). Left out: the Flow stage following Play / Pause / Stop (needs a video; `FlowStage.qml` stays with the CI screenshot smoke) and the JS-only checks (`/src/` image paths, page errors). |
| `gapfill_improve_ui_test.py` | `main` script, 20 checks | todo-later | P3.3 (Review: Fill gaps, Heal rhythm, Repair spans UI). |
| `gen_editor_test.py` | `main` script, 14 checks | todo-later | P4 (Generator with the editor built in, D-137). |
| `gen_video_test.py` | `main` script, 15 checks | todo-later | P3.3 (Generator video player). |
| `generator_ai_script_test.py` | `main` script, 11 checks | todo-later | P3.7/P4 (Generator: AI script / semantic target / profile suggestion / motion-candidate seed). |
| `generator_auto_candidates_test.py` | `main` script, 5 checks | todo-later | P4 (Scene Map in the Generator, needs P3 rhythm grid). |
| `generator_backend_auto_test.py` | `main` script, 7 checks | todo-later | P3.3 (Generator page workflow: tip find, queue, cancel, improve, marks, feel for device, contact). |
| `generator_backend_preserve_test.py` | `main` script, 2 checks | todo-later | P3.3 (Generator page workflow: tip find, queue, cancel, improve, marks, feel for device, contact). |
| `generator_backend_test.py` | `main` script, 7 checks | todo-later | P3.3 (Generator page workflow: tip find, queue, cancel, improve, marks, feel for device, contact). |
| `generator_cancel_tipfind_test.py` | `main` script, 7 checks | todo-later | P3.3 (Generator page workflow: tip find, queue, cancel, improve, marks, feel for device, contact). |
| `generator_contact_marks_test.py` | `main` script, 22 checks | todo-later | P3.3 (Generator page workflow: tip find, queue, cancel, improve, marks, feel for device, contact). |
| `generator_contact_points_test.py` | `main` script, 19 checks | todo-later | P3.3 (Generator page workflow: tip find, queue, cancel, improve, marks, feel for device, contact). |
| `generator_everyday_test.py` | `main` script, 19 checks | todo-later | P3.3 (Generator page workflow: tip find, queue, cancel, improve, marks, feel for device, contact). |
| `generator_feel_device_test.py` | `main` script, 22 checks | todo-later | P3.3 (Generator page workflow: tip find, queue, cancel, improve, marks, feel for device, contact). |
| `generator_improve_test.py` | `main` script, 31 checks | todo-later | P3.3 (Generator page workflow: tip find, queue, cancel, improve, marks, feel for device, contact). |
| `generator_manual_find_cancel_test.py` | `main` script, 8 checks | todo-later | P3.3 (Generator page workflow: tip find, queue, cancel, improve, marks, feel for device, contact). |
| `generator_mark_body_map_test.py` | `main` script, 10 checks | todo-later | P3.3 (Generator page workflow: tip find, queue, cancel, improve, marks, feel for device, contact). |
| `generator_mark_snack_test.py` | `main` script, 12 checks | todo-later | P3.3 (Generator page workflow: tip find, queue, cancel, improve, marks, feel for device, contact). |
| `generator_mt_seed_test.py` | `main` script, 15 checks | todo-later | P3.7/P4 (Generator: AI script / semantic target / profile suggestion / motion-candidate seed). |
| `generator_optimize_target_test.py` | `main` script, 7 checks | todo-later | P3.3 (Generator page workflow: tip find, queue, cancel, improve, marks, feel for device, contact). |
| `generator_pos_overlay_test.py` | `main` script, 8 checks | todo-later | P3.3 (Generator page workflow: tip find, queue, cancel, improve, marks, feel for device, contact). |
| `generator_queue_cancel_tipfind_test.py` | `main` script, 7 checks | todo-later | P3.3 (Generator page workflow: tip find, queue, cancel, improve, marks, feel for device, contact). |
| `generator_queue_postprocess_test.py` | `main` script, 19 checks | todo-later | P3.3 (Generator page workflow: tip find, queue, cancel, improve, marks, feel for device, contact). |
| `generator_rhythm_grid_test.py` | `main` script, 6 checks | todo-later | P3.3 (Generator page workflow: tip find, queue, cancel, improve, marks, feel for device, contact). |
| `generator_scene_map_follow_preview_test.py` | `main` script, 4 checks | todo-later | P4 (Scene Map in the Generator, needs P3 rhythm grid). |
| `generator_scene_map_load_test.py` | `main` script, 8 checks | todo-later | P4 (Scene Map in the Generator, needs P3 rhythm grid). |
| `generator_scene_map_p2_test.py` | `main` script, 4 checks | todo-later | P4 (Scene Map in the Generator, needs P3 rhythm grid). |
| `generator_scene_map_test.py` | `main` script, 7 checks | todo-later | P4 (Scene Map in the Generator, needs P3 rhythm grid). |
| `generator_scene_proposals_test.py` | `main` script, 7 checks | todo-later | P4 (Scene Map in the Generator, needs P3 rhythm grid). |
| `generator_semantic_target_test.py` | `main` script, 20 checks | todo-later | P3.7/P4 (Generator: AI script / semantic target / profile suggestion / motion-candidate seed). |
| `generator_soft_suggest_test.py` | `main` script, 5 checks | todo-later | P3.7/P4 (Generator: AI script / semantic target / profile suggestion / motion-candidate seed). |
| `generator_stroke_preview_steer_test.py` | `main` script, 6 checks | todo-later | P3.3 (Generator page workflow: tip find, queue, cancel, improve, marks, feel for device, contact). |
| `generator_tftj_test.py` | `main` script, 13 checks | todo-later | P3.3 (Generator page workflow: tip find, queue, cancel, improve, marks, feel for device, contact). |
| `generator_workflow_test.py` | `main` script, 22 checks | todo-later | P3.3 (Generator page workflow: tip find, queue, cancel, improve, marks, feel for device, contact). |
| `handbook_gui_test.py` | `main` script, 8 checks | todo-later | P4 (in-app user handbook). |
| `offset_test.py` | `main` script, 13 checks | covered | tests/app/player_controller_test.cpp `offsetSavedPerScriptAndPlayhead`, `loopOverMarkedRange`; tests/core/playback_test.cpp "offset saved per script", "loop". Keyboard shortcuts (`,` `.` +/- L 1-9) are QML and untested |
| `ofs_range_edit_reload_test.py` | `main` script, 12 checks | todo-later | P4 (Editor: Speed-Cap / Delete range reload the edit buffer). |
| `omarker_test.py` | `main` script, 11 checks | covered | tests/app/player_controller_test.cpp `editMarks`; tests/core/omarker_test.cpp |
| `open_media_test.py` | `main` script, 32 checks | covered | tests/core/script_finder_test.cpp; tests/app/player_controller_test.cpp `scriptFindsVideoBeside`, `videoFindsScriptInFolder`, `videoWithoutScriptThenFolderAdded`, `variantsSwitchAndKeepVideo` |
| `ozone_suggest_test.py` | `main` script, 6 checks | todo-later | P4 (Editor: Suggest O-zone, polarity buttons). |
| `pb_adv_knob_sliders_test.py` | `main` script, 10 checks | todo-later | P2 (Inspector Effects/Advanced: tick, max speed, smoothing, soft start, Intensity/Response, Extended-O, presets). |
| `pb_audio_card_test.py` | `main` script, 12 checks | todo-later | P3.6 (Audio Analysis card: waveform + BPM from audio). The Audio Analysis tab with the whole sound exists; the BPM readout does not. |
| `pb_audio_lane_test.py` | `main` script, 11 checks | covered | tests/core/curve_geometry_test.cpp (ruler steps/labels); tests/app/player_controller_test.cpp `audioLevelFromSound` |
| `pb_audio_vibration_test.py` | `main` script, 6 checks | todo-later | P2/P3 (vibration from the video's sound only). |
| `pb_autosync_knobs_test.py` | `main` script, 25 checks | todo-later | P2 (Inspector Effects/Advanced: tick, max speed, smoothing, soft start, Intensity/Response, Extended-O, presets). |
| `pb_axis_lanes_test.py` | `main` script, 15 checks | covered | tests/app/playback_output_test.cpp `lanesFollowTheDevice`, `noScriptNoLanes` |
| `pb_bookmarks_test.py` | `main` script, 9 checks | covered | tests/app/player_controller_test.cpp `editMarks`, `chaptersBookmarksHeatmapLanes`; tests/core/marks_test.cpp |
| `pb_bpm_auto_test.py` | `main` script, 4 checks | todo-later | P3.6 (BPM from audio). |
| `pb_bpm_grid_test.py` | `main` script, 7 checks | covered | tests/app/player_controller_test.cpp `bpmGrid` |
| `pb_chapter_bands_test.py` | `main` script, 12 checks | covered | tests/app/player_controller_test.cpp `chaptersBookmarksHeatmapLanes` (`chapterBands`); tests/core/marks_test.cpp "ChapterRanges: sorted, open ends run on, names filled" |
| `pb_chapters_test.py` | `main` script, 9 checks | covered | tests/app/player_controller_test.cpp `editMarks`; tests/core/marks_test.cpp |
| `pb_chart_tooltip_test.py` | `main` script, 3 checks | ported | tests/app/player_controller_test.cpp `heatTooltipShowsTimeAndValue` |
| `pb_clip7776_test.py` | `main` script, 6 checks | ported | tests/app/player_controller_test.cpp `tjScriptWithoutVideoPlays` (synthetic equivalent of the fixture; found that playback ignored the script's recipe, fixed in #42) |
| `pb_companion_lanes_test.py` | `main` script, 17 checks | todo-later | P2/P4 (companion axis files shown as lanes). |
| `pb_contact_vib_probe_test.py` | `main` script, 20 checks | todo-later | P2 (live contact-vibration probe in the Inspector). |
| `pb_curve_zoom_test.py` | `main` script, 10 checks | covered | tests/app/player_controller_test.cpp `zoomWindow`; tests/core/curve_geometry_test.cpp "view window: zoom as 0.7", "zoom to a range with a margin" |
| `pb_edit_extras_test.py` | `main` script, 9 checks | todo-later | P4 (Editor: invert curve, range tools, undo). |
| `pb_effects_presets_test.py` | `main` script, 20 checks | todo-later | P2 (Inspector Effects/Advanced: tick, max speed, smoothing, soft start, Intensity/Response, Extended-O, presets). |
| `pb_eo_knob_sliders_test.py` | `main` script, 11 checks | todo-later | P2 (Inspector Effects/Advanced: tick, max speed, smoothing, soft start, Intensity/Response, Extended-O, presets). |
| `pb_feel_heatbands_test.py` | `main` script, 15 checks | todo-later | P4 (Feel / Speech-Hold bands on the heatmap). |
| `pb_fps_snap_knob_test.py` | `main` script, 6 checks | todo-later | P4 (Editor FPS-Snap knob; `SnapMs` is ported). |
| `pb_load_error_test.py` | `main` script, 2 checks | covered | tests/app/player_controller_test.cpp `brokenFilesAreReported` |
| `pb_load_origin_test.py` | `main` script, 2 checks | covered | tests/app/library_controller_test.cpp `recentFavoritesAndScripts` |
| `pb_native_play_test.py` | `main` script, 5 checks | n/a | JS DOM: the browser's own `<video controls>`; Qt uses its own video output. |
| `pb_offset_bake_test.py` | `main` script, 4 checks | todo-later | P1 open ("Write offset into script"). |
| `pb_ofs_knob_prefs_test.py` | `main` script, 18 checks | todo-later | P4 (Play OFS / Feel knobs persisted in settings). |
| `pb_omarker_intensity_knob_test.py` | `main` script, 6 checks | covered | tests/app/player_controller_test.cpp `editMarks` (`oMarkerIntensity`) |
| `pb_pause_test.py` | `main` script, 12 checks | covered | tests/app/playback_output_test.cpp `playingSendsLevelsAndSessionIsActive`, `pauseStopsAndMovesHome` |
| `pb_playlist_advance_test.py` | `main` script, 12 checks | covered | tests/core/playlist_test.cpp; tests/app/player_controller_test.cpp `playlist` |
| `pb_project_load_test.py` | `main` script, 14 checks | covered | tests/app/player_controller_test.cpp `projectAndHeatmapExport`; tests/core/project_test.cpp, tests/core/range_edit_test.cpp. Scale-range UI is P4 |
| `pb_scene_panel_test.py` | `main` script, 28 checks | todo-later | P4 (Scene Map bottom panel). |
| `pb_scene_strip_test.py` | `main` script, 19 checks | todo-later | P4 (scene strip with thumbnails). |
| `pb_script_alone_test.py` | `main` script, 6 checks | covered | tests/app/playback_output_test.cpp (scripts are opened without a video) |
| `pb_seek_sync_test.py` | `main` script, 2 checks | covered | tests/app/playback_output_test.cpp `seekEasesIn`; tests/app/player_controller_test.cpp `zoomWindow` (heatmap seek) |
| `pb_sound_curve_test.py` | `main` script, 8 checks | todo-later | P3 (Sound Curve: loudness written as the vibration curve). |
| `pb_speed_highlight_knob_test.py` | `main` script, 8 checks | covered | tests/app/player_controller_test.cpp `speedHighlights` |
| `pb_stroke_output_test.py` | `main` script, 26 checks | covered | tests/app/playback_output_test.cpp, tests/app/inspector_qml_test.cpp `devicesTabLoadsWithoutWarnings` |
| `pb_tools_toggle_test.py` | `main` script, 11 checks | ported | tests/app/shell_qml_test.cpp `libraryAndInspectorCollapseAndTheVideoGrows` (both columns collapse to 44 px and expand again, the video grows and shrinks by the same amount, the choice is remembered by a new shell). Left out: the T key and the info sidebar (neither exists in SamNPlayer 2). |
| `pb_transport_test.py` | `main` script, 11 checks | covered | tests/app/player_controller_test.cpp `sleepTimer`; tests/app/playback_output_test.cpp `playSpeedShortensTheMoves`. Speed 0.5-2x clamp, +/-10 s, mute: see `player/speed_test.go TestClampSpeed` |
| `pb_ui_contract_test.py` | `main` script, 14 checks | n/a | JS DOM contract (layout hooks, `data-*` state attributes, CSS theme tokens); Qt has `Theme.h`. |
| `pb_undo_test.py` | `main` script, 14 checks | todo-later | P4 (Editor edit history). |
| `pb_video_swap_test.py` | `main` script, 8 checks | covered | tests/app/player_controller_test.cpp `projectAndHeatmapExport` (`attachVideo`), `variantsSwitchAndKeepVideo` |
| `pb_video_sync_idle_test.py` | `main` script, 5 checks | covered | tests/app/playback_output_test.cpp `staleWatchdogStopsTheDevice`, `playingSendsLevelsAndSessionIsActive` |
| `play_heatmap_size_test.py` | `main` script, 6 checks | n/a | JS DOM/CSS: heights of the curve and heatmap strips. |
| `playback_contact_marks_test.py` | `main` script, 4 checks | todo-later | P3.6/P4 (contact marks shown in the player). |
| `playback_load_retry_test.py` | `main` script, 3 checks | covered | tests/app/player_controller_test.cpp `brokenFilesAreReported` (error bar); a retry button is UI |
| `playback_optimize_neo2_test.py` | `main` script, 5 checks | todo-later | P4 (Editor: Optimize for a device). |
| `player_layout_test.py` | `main` script, 57 checks | covered | tests/app/inspector_qml_test.cpp, tests/app/library_controller_test.cpp; CI app smoke test (start, load, render, quit) |
| `quality_display_test.py` | `main` script, 12 checks | todo-later | P3.6 (Quality Doctor display). |
| `report_status_test.py` | `main` script, 4 checks | todo-later | P4 (Settings: measurement report status). |
| `roi_cancel_training_test.py` | `main` script, 8 checks | todo-later | P4 (AI Train / Settings AI & learning, Suite). |
| `roi_train_sample_prefs_test.py` | `main` script, 6 checks | todo-later | P4 (AI Train / Settings AI & learning, Suite). |
| `roi_training_test.py` | `main` script, 63 checks | todo-later | P4 (AI Train / Settings AI & learning, Suite). |
| `rt_epochs_cap_test.py` | `main` script, 3 checks | todo-later | P4 (AI Train / Settings AI & learning, Suite). |
| `rt_sample_knobs_test.py` | `main` script, 8 checks | todo-later | P4 (AI Train / Settings AI & learning, Suite). |
| `script_doctor_test.py` | `main` script, 13 checks | todo-later | P3.6 (Script Doctor / Quality Doctor button). |
| `settings_ai_server_test.py` | `main` script, 3 checks | todo-later | P4 (AI Train / Settings AI & learning, Suite). |
| `settings_ai_setup_test.py` | `main` script, 5 checks | todo-later | P4 (AI Train / Settings AI & learning, Suite). |
| `settings_ai_train_link_test.py` | `main` script, 3 checks | todo-later | P4 (AI Train / Settings AI & learning, Suite). |
| `settings_apply_ai_setup_test.py` | `main` script, 4 checks | todo-later | P4 (AI Train / Settings AI & learning, Suite). |
| `settings_audio_vibration_test.py` | `main` script, 5 checks | todo-later | P2/P3 (Settings: vibration from video audio). |
| `settings_honor_inverted_test.py` | `main` script, 4 checks | todo-later | P4 (Settings: honor OFS `inverted`). |
| `settings_learning_test.py` | `main` script, 5 checks | todo-later | P4 (AI Train / Settings AI & learning, Suite). |
| `settings_legal_test.py` | `main` script, 10 checks | todo-later | P4/P5 (Settings: licence section, donate buttons). |
| `settings_script_folders_test.py` | `main` script, 6 checks | covered | tests/app/player_controller_test.cpp `videoWithoutScriptThenFolderAdded` (`addScriptFolder`). The Settings page (list/remove) is P4 |
| `smoke_all_modes_test.py` | `main` script, 25 checks | covered | CI app smoke test (`SAMN_SCREENSHOT`: start, load, render, quit) |
| `tools_home_test.py` | `main` script, 22 checks | todo-later | P4 (Tools Home: status rows from real checks). |
| `tools_nav_test.py` | `main` script, 51 checks | ported | tests/app/shell_qml_test.cpp `modeDeckPillCardsAndCurrentMark`, `modeDeckOpensWithMAndPicksWithNumbers`, `onlyTheChosenModesPageShows` (pill opens the deck, three cards since D-145, current card marked, click outside closes, click on a card picks, M, Esc, keys 1-3 only while the deck is open, key 4 does nothing, `SAMN_MODE` hook). Left out: arrow keys, Tab and focus return (the deck has no keyboard focus handling, mouse and shortcuts only), the Tools sub-navigation and Play/Create/Edit view switch (gone in 2.0). |
| `topbar_device_test.py` | `main` script, 10 checks | todo-later | P2/P4 (top-bar device chip with Connect/Disconnect; listed as open in `AGENT_STATE.md`). |
| `tr_intensity_knobs_test.py` | `main` script, 6 checks | todo-later | P2 (Training mode). |
| `tr_timing_knobs_test.py` | `main` script, 7 checks | todo-later | P2 (Training mode). |
| `training_device_test.py` | `main` script, 16 checks | todo-later | P2 (Training mode). |
| `training_display_test.py` | `main` script, 19 checks | todo-later | P2 (Training mode). |
| `training_editor_test.py` | `main` script, 26 checks | todo-later | P2 (Training mode). |
| `training_findings_test.py` | `main` script, 12 checks | todo-later | P2 (Training mode). |
| `training_history_test.py` | `main` script, 10 checks | todo-later | P2 (Training mode). |
| `training_intensity_meter_test.py` | `main` script, 35 checks | todo-later | P2 (Training mode). |
| `training_intensity_test.py` | `main` script, 14 checks | todo-later | P2 (Training mode). |
| `training_layout_test.py` | `main` script, 34 checks | todo-later | P2 (Training mode). |
| `training_lifecycle_test.py` | `main` script, 7 checks | todo-later | P2 (Training mode). |
| `training_rings_test.py` | `test_training_rings` | todo-later | P2 (Training mode). |
| `training_script_test.py` | `main` script, 16 checks | todo-later | P2 (Training mode). |
| `training_video_test.py` | `main` script, 24 checks | todo-later | P2 (Training mode). |
| `update_check_test.py` | `main` script, 10 checks | todo-later | P5 (update check button). |
| `ws_edit_view_test.py` | `main` script, 11 checks | todo-later | P4 (Edit view: curve editing + scene strip). |
| `ws_shared_video_test.py` | `main` script, 6 checks | todo-later | P3.3 (one clip for Create and Play). |

## 3. Gaps inside new modules (not tied to one old test)

- `stroke_filter`, `beatbar` and `beat_strokes` are tested only through the Python bindings (`core/python/tests`, which skip when `samn_native` is not built unless `SAMN_NATIVE_REQUIRED=1`). They have no doctest in `tests/core`. Add doctest versions so `ctest` covers them without pybind11 (the cases of `test_stroke_filter_parity.py`, `test_beat_strokes_parity.py`, and the beat-bar synthetic frames).
- `PlayerController` methods without any test: `renameChapter`, `saveSnapshot` / `snapshotName`, `heatTooltip`, `formatTime`, `msAtFraction`, `zoomBy`, `uiValue` / `setUiValue`. (`snapshot*` and `heatTooltip` are on the work list below because an old test exists for them.)
- `mapper.h` still says in its header comment that `MapOptionsFromScript` is not ported; it is, in `samn_file.h`. Fix the comment when the recipe tests are added.

## 4. Port now (work list)

**0 tests** (all 56 are ported, see the `Ported` notes) whose code is already in SamNPlayer 2. Grouped by the new module; each line is `old file :: old test` followed by what it checks. Suggested target file in italics.

| New module | Tests to port |
|---|---:|
| core: `mapper.h` and `samn_file.h` (recipes, profiles, scene feel, contact settings, funscript metadata) | 0 |
| core: `stroke_filter.h` | 0 |
| core: `fileio.h` | 0 |
| core: `settings.h` | 0 |
| app: `PlaybackOutput` | 0 |
| app: `PlayerController` | 0 |
| app: `DeviceController` | 0 |
| app: QML shell and assets | 0 |
| repo / CI | 0 |
| **Total** | **0** |

### A. Missing tests

#### core: `mapper.h` and `samn_file.h` (recipes, profiles, scene feel, contact settings, funscript metadata) (35)

*Target: new `tests/core/recipe_test.cpp` (recipes, profiles, contact curves, scene feel, balance, inverted) and additions to `samn_file_test.cpp` (`ApplyContactRecipe`, `ApplyFeelRecipe`, quality metadata).*

1. `cmd/gui-wails/app_contact_settings_test.go` :: `TestSaveContactSettingsPersistsRecipe`. Adapt to `.samn`: contact on, span 0.5, curve soft are stored in the recipe of a tj document. [same behaviour as funscript/contact_recipe_test.go `TestSaveContactRecipeRoundtrip`] **Ported:** tests/core/samn_file_test.cpp "contact settings are stored in the tj recipe".
2. `cmd/gui-wails/app_contact_settings_test.go` :: `TestSaveContactSettingsStrokeProfile`. A stroke-profile document keeps `sync=independent` after contact settings are applied (span 0.55, curve peak). **Ported:** tests/core/samn_file_test.cpp "contact settings keep sync independent on a stroke profile".
3. `cmd/gui-wails/app_contact_settings_test.go` :: `TestSaveContactSettingsStrokeEmptySyncFillsIndependent`. A stroke-profile recipe with empty sync gets `independent` (never `suction_position`). **Ported:** tests/core/samn_file_test.cpp "contact settings fill an empty stroke sync with independent".
4. `cmd/gui-wails/app_feel_test.go` :: `TestSaveFeelSettingsRebakesEmotionScript`. After the feel changes (oral, follow-suction) re-baking the Neo 2 axes of a `.samn` gives a different vibration axis than before. **Ported:** tests/core/samn_file_test.cpp "a new feel changes the baked vibration axis".
5. `funscript/axes_test.go` :: `TestBakeAxesAndExplicitPlayback`. Bake vibration/suction axes from a recipe (suction high at contact, vibration > 0 with span 0.5); a script with `playback_source=axes` and `samn_axes` yields `use_explicit_axes`. **Ported:** tests/core/recipe_test.cpp "axes: bake from the recipe and play from explicit axes".
6. `funscript/axes_test.go` :: `TestRecipeModeIgnoresAxes`. With `playback_source=recipe` the stored `samn_axes` are ignored (`use_explicit_axes` stays false). **Ported:** tests/core/recipe_test.cpp "axes: recipe mode ignores the stored axes".
7. `funscript/contact_recipe_test.go` :: `TestDefaultSyncForProfile`. Distance profiles (tf/tj) default to `suction_position`, stroke profiles to `independent`. **Ported:** tests/core/recipe_test.cpp "recipe: profile names and default sync".
8. `funscript/contact_recipe_test.go` :: `TestSaveContactRecipeStrokeEmptySyncStaysIndependent`. Adapt to `.samn`: applying contact settings to a stroke-profile recipe with empty sync fills `independent`, never `suction_position`. **Ported:** tests/core/samn_file_test.cpp "contact settings fill an empty stroke sync with independent".
9. `funscript/contact_recipe_test.go` :: `TestSaveContactRecipeTjEmptySyncUsesSuctionPosition`. Adapt to `.samn`: a tj recipe with empty sync gets `suction_position` when contact settings are applied. **Ported:** tests/core/samn_file_test.cpp "contact settings fill an empty tj sync with suction_position".
10. `funscript/contact_recipe_test.go` :: `TestSaveContactRecipeRoundtrip`. Contact on/span/curve are stored (span via `EffectiveContactSpan`, curve normalised), switching off clears span and curve, the rest of the recipe stays. **Ported:** tests/core/samn_file_test.cpp "contact recipe round trip, switching off clears span and curve".
11. `funscript/feel_scene_test.go` :: `TestNormalizeSceneFeelAliases`. Aliases map to the four feels (`oral`, `Blowjob`->oral, `penetration`->intercourse, `titjob`->breast, `handjob`->hand); empty and unknown (`dance`) names give none. `NormalizeSceneFeel` is not in a header yet: expose it or test through `Document::ApplyFeelRecipe`. **Ported:** tests/core/recipe_test.cpp "feel: scene names and aliases".
12. `funscript/feel_scene_test.go` :: `TestSceneFeelOffKeepsOldMapping`. With no scene feel, follow-suction off and balance 0 the frames equal the plain mapping frame for frame. **Ported:** tests/core/recipe_test.cpp "feel: scene feel off keeps the old mapping".
13. `funscript/feel_scene_test.go` :: `TestSceneSuctionShapes`. Oral keeps a seal (>= 0.29) at the shallowest point; every feel pulls harder when deeper; hand weighs speed over depth, breast depth over speed; no feel returns nullopt. **Ported:** tests/core/recipe_test.cpp "feel: suction shapes per scene".
14. `funscript/feel_scene_test.go` :: `TestOralSceneRaisesSuction`. The oral feel raises the mean suction of a stroke script above the plain mapping. **Ported:** tests/core/recipe_test.cpp "feel: oral scene raises suction".
15. `funscript/feel_scene_test.go` :: `TestVibrationFollowsSuction`. With follow-suction on, every frame's vibration equals its suction (smoothing 0, intercourse feel). **Ported:** tests/core/recipe_test.cpp "feel: vibration follows suction".
16. `funscript/feel_scene_test.go` :: `TestBalance`. Balance +1 gives vibration 0 and suction > 0, -1 the reverse; 0.5 halves the mean vibration and leaves suction; `BalanceScales(NaN)` is neutral (1, 1). **Ported:** tests/core/recipe_test.cpp "feel: balance".
17. `funscript/feel_scene_test.go` :: `TestSaveFeelRecipeRoundTrip`. Adapt to `.samn`: scene feel (`blowjob` is stored as oral), follow-suction and balance 0.4 are stored and `MapOptionsFromScript` reads them back; other metadata (creator) is kept. **Ported:** tests/core/samn_file_test.cpp "feel settings round trip into the map options".
18. `funscript/funscript_test.go` :: `TestParseQualityMetadata`. `quality_score` and `quality_passed` are read as separate fields (passed=false with a score of 0.63 stays false; old scripts without `quality_passed` give nullopt; no metadata gives both empty). **Ported:** tests/core/samn_file_test.cpp "quality score and verdict are separate fields".
19. `funscript/inverted_playback_test.go` :: `TestPlaybackActionsFlipsWhenInverted`. With `inverted=true` every position becomes 100-pos (clamped), times unchanged, input not modified. **Ported:** tests/core/recipe_test.cpp "inverted: playback actions flip when inverted".
20. `funscript/inverted_playback_test.go` :: `TestPlaybackActionsPassthroughWhenNotInverted`. With `inverted=false` the actions come back unchanged. **Ported:** tests/core/recipe_test.cpp "inverted: playback actions pass through when not inverted".
21. `funscript/inverted_playback_test.go` :: `TestToIntensityCurveHonorsInverted`. A script with `inverted=true` and flipped data (100, 0, 100) produces exactly the frames of the plain script (0, 100, 0). **Ported:** tests/core/recipe_test.cpp "inverted: the intensity curve honours the inverted flag".
22. `funscript/recipe_test.go` :: `TestNormalizeProfile`. `TF` normalises to `tj`; tf/tj are distance profiles, `standard` is not; default sync per profile family. (`IsStrokeProfile` / `AllowsContactSettings` have no C++ counterpart yet.) **Ported:** tests/core/recipe_test.cpp "recipe: profile names and default sync".
23. `funscript/recipe_test.go` :: `TestRecipeTJSuctionOnlyNoVibration`. tj recipe: no vibration at all, suction follows position (max >= 0.7, floor about 0.20 from the 20-90 clamp, max about 0.90). **Ported:** tests/core/recipe_test.cpp "recipe: tj is suction only, no vibration".
24. `funscript/recipe_test.go` :: `TestRecipeTJSuctionNoDoubleFloor`. tj recipe: pos 20 maps to about 0.20 suction and pos 90 to about 0.90 (no second floor lift); recipe `min_suction` stays 0.20. **Ported:** tests/core/recipe_test.cpp "recipe: tj suction has no double floor".
25. `funscript/recipe_test.go` :: `TestRecipeTJContactVibrationOffByDefault`. tj recipe without contact vibration stays silent even at deep contact. **Ported:** tests/core/recipe_test.cpp "recipe: tj contact vibration is off by default".
26. `funscript/recipe_test.go` :: `TestRecipeTJContactVibrationTracksPosition`. tj + contact vibration: 0 far from contact, > 0.5 during sustained contact near the script's own maximum, never above 1. **Ported:** tests/core/recipe_test.cpp "recipe: tj contact vibration tracks the position".
27. `funscript/recipe_test.go` :: `TestRecipeTJContactVibrationBriefGraze`. A brief graze reaches the same peak (>= 0.9) as a sustained contact but stays above 0.5 over fewer frames. **Ported:** tests/core/recipe_test.cpp "recipe: tj contact vibration, brief graze".
28. `funscript/recipe_test.go` :: `TestRecipeTJContactVibrationNeedsSpan`. A script with almost no position range (50-52) never buzzes. **Ported:** tests/core/recipe_test.cpp "recipe: tj contact vibration needs a position span".
29. `funscript/recipe_test.go` :: `TestRecipeTJContactVibrationSpanEarlier`. At mid position 55 the default span 0.75 is still silent; span 0.45 already vibrates (>= 0.05). **Ported:** tests/core/recipe_test.cpp "recipe: tj contact vibration, a smaller span starts earlier".
30. `funscript/recipe_test.go` :: `TestRecipeTJContactVibrationCurves`. At the same position soft < linear < peak; impulse stays <= 0.05 mid-window. **Ported:** tests/core/recipe_test.cpp "recipe: tj contact vibration curves".
31. `funscript/recipe_test.go` :: `TestApplyContactCurveImpulsePeaks`. Impulse is 0 at t=0.5, rises sharply near the peak (0.9 is > 0 and below 1.0), ~1 at t=1; `NormalizeContactCurve("events")` gives impulse. **Ported:** tests/core/recipe_test.cpp "recipe: impulse curve peaks".
32. `funscript/recipe_test.go` :: `TestRecipeTJContactVibrationMutedInTrackingGap`. Vibration is 0 inside a tracking gap (650-850 ms) even while the held position is in the contact zone, and > 0.5 just outside it. **Ported:** tests/core/recipe_test.cpp "recipe: tj contact vibration is muted in a tracking gap".
33. `funscript/recipe_test.go` :: `TestRecipeTJContactGapWinsOverEnvelopeAndSmoothing`. Envelope and smoothing never carry the previous vibration into a tracking gap (350-650 ms stay 0). **Ported:** tests/core/recipe_test.cpp "recipe: tracking gap wins over envelope and smoothing".
34. `funscript/recipe_test.go` :: `TestRecipeStandardContactVibrationTracksPosition`. Stroke profile (independent) with contact: deep flat window is high, far flat window is low. **Ported:** tests/core/recipe_test.cpp "recipe: stroke profile contact vibration tracks the position".
35. `funscript/recipe_test.go` :: `TestRecipeTFEqualsTJ`. `RecipeFor("tf")` and `RecipeFor("tj")` have the same sync (`suction_position`) and `min_suction`. **Ported:** tests/core/recipe_test.cpp "recipe: tf equals tj".

#### core: `stroke_filter.h` (3)

*Target: new `tests/core/stroke_filter_test.cpp`.*

36. `funscript/stroke_filter_test.go` :: `TestPCHIPNoOvershoot`. PCHIP on monotone data (0, 10, 90, 100; step 25 ms) stays monotone (no overshoot). **Ported:** tests/core/stroke_filter_test.cpp "PCHIP does not overshoot monotone data".
37. `funscript/stroke_filter_test.go` :: `TestNormalizeSmooth`. `pchip` and `makima` are accepted, an unknown name (`cubic`) means smoothing off. `NormalizeSmooth` is internal to `stroke_filter.cpp`: test through `ApplyStrokeFilter`. **Ported:** tests/core/stroke_filter_test.cpp "NormalizeSmooth".
38. `funscript/stroke_filter_test.go` :: `TestStrokeFilterSortsAndDedupes`. Unsorted input with a duplicate time is sorted and the last duplicate wins (3 points: 0, 500/80, 1000/100), times strictly increase. **Ported:** tests/core/stroke_filter_test.cpp "Unsorted input is sorted and the last duplicate time kept".

#### core: `fileio.h` (2)

*Target: `tests/core/fileio_test.cpp`.*

39. `funscript/atomic_write_test.go` :: `TestWriteFileAtomicCleansTempAfterRenameFailure`. When the rename fails (destination is a non-empty directory) the write throws, the destination is unchanged and no `.tmp` file is left behind. **Ported:** tests/core/fileio_test.cpp "fileio: a failed rename leaves the destination and the folder clean".
40. `generator/native_write_test.go` :: `TestWriteFileAtomicCleansTempAfterRenameFailure`. Same as funscript/atomic_write_test.go: a failed rename leaves the destination and the folder clean. [duplicate of funscript/atomic_write_test.go] **Ported:** tests/core/fileio_test.cpp "fileio: a failed rename leaves the destination and the folder clean".

#### core: `settings.h` (1)

*Target: `tests/core/settings_test.cpp`.*

41. `cmd/gui-wails/settings_store_test.go` :: `TestSettingsStoreNullFile`. A settings file that contains `null` loads as empty and a later `set` works and persists. **Ported:** tests/core/settings_test.cpp "settings: a file holding null loads as empty and accepts a set".

#### app: `PlaybackOutput` (0 left, 5 ported)

*Target: `tests/app/playback_output_test.cpp`.*

42. `player/axes_test.go` :: `TestDriveAxesSendsEachAxisOnce`. Each extra axis gets one move per target (sorted by name, axes the device does not take are skipped), stroke off stops the extra axes too, the stroke range does not scale them, a pause/reset resends every axis. (`playback_output_test.cpp::extraAxesFollowTheScript` only checks the first move.) **Ported:** tests/app/playback_output_test.cpp `extraAxesSendEachAxisOnce`.
43. `player/axes_test.go` :: `TestDriveAxesNeedsMultiAxisDevice`. A device without extra axes (plain linear stroker) gets no axis commands and no error. **Ported:** tests/app/playback_output_test.cpp `extraAxesNeedAMultiAxisDevice`.
44. `player/speed_test.go` :: `TestChannelLevelsDefaultFullAndClamp`. Default level is full (100), values clamp to 0..100 (7 -> 100, -3 -> 0); `channelOffAndLevelScaleTheOutput` covers only scaling. **Ported:** tests/app/playback_output_test.cpp `channelLevelsDefaultFullAndClamp` (levels are 0-100 in C++).
45. `player/stroke_test.go` :: `TestDriveStrokeHonoursSpeedAndMinimum`. A position that is very late still gets the minimum move time (`kMinMoveMs`); the speed half is covered by `playSpeedShortensTheMoves`. **Ported:** tests/app/playback_output_test.cpp `lateStrokePositionStillGetsTheMinimumMove`.
46. `player/stroke_test.go` :: `TestDriveStrokeOnlyForLinearDevices`. A device without a linear channel (the Mock) gets no positions, and a script without stroke actions sends none. **Ported:** tests/app/playback_output_test.cpp `strokeOnlyForLinearDevices`.

#### app: `PlayerController` (3, ported)

*Target: `tests/app/player_controller_test.cpp`.*

47. `cmd/gui-wails/app_snapshot_test.go` :: `TestSaveVideoSnapshot`. Without a video the snapshot fails with an error; the suggested name is `<video stem>_MM-SS.png` (`My Clip_01-23.png` at 83.5 s); a name without extension gets `.png`; cancel is not an error.
48. `cmd/gui-wails/frontend/test/pb_chart_tooltip_test.py` :: `main`. Hovering the heatmap/curve gives a tooltip with time and value; `FormatTooltipTime` itself is tested, the controller wrapper is not. [`curve_geometry_test.cpp` tests `FormatTooltipTime`]
49. `cmd/gui-wails/frontend/test/pb_clip7776_test.py` :: `main`. Load the real tf/tj funscript fixture without a video and play it: no error, curve and lanes present. (Copy the fixture from the 0.7 repo or generate an equivalent.)

#### app: `DeviceController` (2, ported)

*Target: `tests/app/device_controller_test.cpp`.*

50. `cmd/gui-wails/app_test.go` :: `TestShutdownStopsActiveSessionDevice`. Closing the app while a device is connected and playing stops and disconnects it (the Mock logs `stop`).
51. `cmd/gui-wails/app_test.go` :: `TestShutdownWithoutActiveSessionIsSafe`. Destroying a controller that never connected (or already disconnected) is harmless.

#### app: QML shell and assets (0 left, 4 ported)

*Target: new `tests/app/shell_qml_test.cpp` (like `inspector_qml_test.cpp`) and a small asset lint.*

52. `cmd/gui-wails/frontend/test/flow_assets_test.py` :: `test_flow_assets`. All bundled vector assets are well-formed XML and contain no `<script>`, `on*=` handlers or network references (`href`/`url(http...)`). **Ported:** tests/app/shell_qml_test.cpp `bundledVectorAssetsAreSafe`, `iconNamesInTheQmlSourcesExist`.
53. `cmd/gui-wails/frontend/test/flow_wiring_test.py` :: `main`. The shell loads without QML warnings; the top bar, mode deck and views show their icons (no empty icon names); the mode deck opens with M, picks with 1-5, closes with Esc. **Ported:** tests/app/shell_qml_test.cpp `shellLoadsWithoutWarningsAndShowsItsIcons`, `everyModeLoadsWithoutWarnings`, `modeDeckOpensWithMAndPicksWithNumbers` (the deck has four modes, so keys 1-4).
54. `cmd/gui-wails/frontend/test/pb_tools_toggle_test.py` :: `main`. The Library and Inspector columns collapse to 44 px and expand again; the video area grows. **Ported:** tests/app/shell_qml_test.cpp `libraryAndInspectorCollapseAndTheVideoGrows`.
55. `cmd/gui-wails/frontend/test/tools_nav_test.py` :: `main`. The 0.7 main tabs (roving arrow keys, Home/End) are replaced by the mode deck: keys 1-5, M, Esc, arrows move focus, Tab stays inside, focus returns on close, the current card is marked. **Ported:** tests/app/shell_qml_test.cpp `modeDeckPillCardsAndCurrentMark`, `modeDeckOpensWithMAndPicksWithNumbers`, `onlyTheChosenModesPageShows`. Arrow keys, Tab and focus return are not in the deck (no keyboard focus handling), so they are left out.

#### repo / CI (1, ported)

*Target: a ctest or CI step.*

56. `cmd/gui-wails/identifiers_test.go` :: `TestNoPersonalIdentifiersInSource`. Source scan: no real name, old project name or forbidden identifier appears in code, docs, JSON or YAML (the same rule is in `AGENT_STATE.md`: no model identifiers, no personal data). Port as a ctest or CI step that greps the tree. [the rule exists in `docs/AGENT_STATE.md`, the check does not]

### B. Extend ported tests (assertions of the old test that the new test does not make)

These old tests count as `ported` above, but a part of their checks is missing. Add the assertions to the existing test.

1. `funscript/stroke_filter_test.go :: TestStrokeFilterSmoothKeepsPointsAndStaysInRange` (`core/python/tests/test_stroke_filter_parity.py::test_smooth_keeps_points`): at 600 ms the smoothed curve is still above the straight line (> 80), i.e. the curve is rounded after the top, for both pchip and makima. **Ported:** tests/core/stroke_filter_test.cpp.
2. `funscript/stroke_filter_test.go :: TestStrokeFilterSpeedLimit` (`test_speed_limit`): the point after the limited one catches up, and no step of the output is faster than the limit. **Ported:** tests/core/stroke_filter_test.cpp.
3. `funscript/stroke_filter_test.go :: TestStrokeFilterIdleFill` (`test_idle_fill`): a short gap (a two-point script, 0 to 1000 ms) is left alone, and the wave points of a long pause stay strictly inside the pause and within the wave amplitude. **Ported:** tests/core/stroke_filter_test.cpp.
4. `funscript/stroke_filter_test.go :: TestStrokeFilterOffChangesNothing` (`test_off_changes_nothing`): also assert that the input is not modified. **Ported:** tests/core/stroke_filter_test.cpp.
5. `device`/`app` equivalents of `player/axes_test.go :: TestDriveAxesSendsEachAxisOnce` are listed in A (the existing `extraAxesFollowTheScript` checks only the first move).

## 5. Not listed here

- Tests that exist only in SamNPlayer 2 (for example `curve_geometry`, `playlist`, `library`, `text`, `heatmap` pixel parity, `iconprovider`, the QML smoke tests, and the extra byte-parity cases of the device and `.samn` tests) have no old counterpart and are not in the tables.
- The Go suite is not a complete specification of 0.7: many behaviours (see `PARITY.md`) were only ever checked by hand. Closing this file does not close `PARITY.md`.

