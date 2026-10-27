# Lessons from SamNPlayer 0.7 for SamNPlayer 2

Draft of 16 Oct 2026. Built from the old repo (`/home/user/SamNPlayer`, branch `origin/main`, 0.7.0 plus "Unreleased"), read against this repo at the time of writing. Nothing here is a new decision: decisions live in [`DECISIONS.md`](DECISIONS.md) (IDs `D-xxx`), and this file points to them where a lesson is already a decision.

## Why this file exists, and how to use it

SamNPlayer 0.7 took more than 550 PRs (the numbers reach #553) and dozens of point releases to learn what is written down here. Many of these lessons cost days: a feature that "worked" in the source tree and not in the package, a fix that made a clip worse, a plausible explanation that a five-minute measurement refuted. SamNPlayer 2 rebuilds the same functions in C++/Qt, so it can repeat every one of these mistakes unless they are written next to the code that could repeat them.

**Before you build or change an area, read its section here** (and the "Tested and rejected" table if you are about to try something that sounds clever). **Before you merge**, check the entries of the area you touched: each has a rule, and each rule says whether SamNPlayer 2 already follows it.

How an entry reads:

- **Learned:** what happened, with the numbers the source has. "The source is vague" is said where it is.
- **Source:** the file and section in the old repo (paths relative to `/home/user/SamNPlayer`), or a PR/issue number.
- **Rule:** what to do, or never do, in SamNPlayer 2.
- **Status** (exactly one of):
  - `covered (where)`: SamNPlayer 2 already follows the rule; the code or test is named. I checked the code in this repo (grep of `core/`, `app/`, `tests/`, CI) before writing it.
  - `todo (phase)`: not covered yet. The text says what exists and what is missing; the phase is `P1`..`P5` of [`PLAN.md`](PLAN.md).
  - `n/a (why)`: the lesson belongs to a technology SamNPlayer 2 does not use.

Conventions: "0.7" is the old app; "the Owner" is the project owner; "golden clips" are the Owner's real test videos (never in the repo, D-073). Old-repo issue/PR numbers (`#nnn`) are the old repo's. Ids: `PS` Player & sync, `FF` File formats, `DV` Devices, `GD` Generator / detection / tracking, `AU` Audio, `TR` Training, `UI` GUI / look, `BC` Build / CI / Windows, `PF` Performance, `PR` Process, `REJ` Tested and rejected, `GAP` gaps.

Source keys for short references: `HANDOFF` = `HANDOFF.md`; `CONTRIB` = `CONTRIBUTING.md`; `NEXT` = `docs/NEXT.md`; `FTT` = `docs/FINDINGS_TIMING_TF.md`; `COORD` = `docs/AGENT_COORD.md`; `CL` = `CHANGELOG.md`; `R26` = `docs/ROADMAP_2026-10.md`; `HO15` = `docs/next/HANDOFF_2026-10-15.md`; `S23` = `docs/SESSION_HANDOFF_2026-09-23.md`; `AS` = `docs/AGENT_STATE.md`. Other docs are named in full.

Adult-media rule: this file uses neutral technical terms only ("tip", "partner region", "contact point", "body-part class").

---

## 1. Player & sync

### PS-01 The offset is applied in exactly one place
- **Learned:** a per-script offset exists because imported scripts rarely match the user's video cut. 0.7 applies it only where video time becomes script time (`ReportVideoPosition`). Applying it a second time to the displayed curve makes the curve and the device drift apart.
- **Source:** `HANDOFF` "What works".
- **Rule:** one conversion `video ms -> script ms` feeds the playhead, the device and every mark; no second subtraction anywhere (Editor, Generator video, loop, chapters).
- **Status:** covered (`app/src/player_controller.cpp` `setVideoPositionMs`: one `script_time(videoMs, offsetMs_)` value goes to the curves, the heatmap and `output_.update(ph)`; `tests/app/player_controller_test.cpp` `offsetSavedPerScriptAndPlayhead`). New views (Editor, Generator preview, P3/P4) must reuse it.

### PS-02 The device stops when the video stops
- **Learned:** with "device follows video", pausing or a buffering stall stopped the position updates and the device kept its last level until play resumed. Fixed twice: a 1.5 s stale-position watchdog sets the levels to 0 (0.5.42); the video `pause`/`waiting` events zero the device immediately instead of after 1.5 s, and soft start resumes on `playing` (0.5.45). A rejected second session start must not overwrite the live session (0.5.10).
- **Source:** `CL` 0.5.42 and 0.5.45 "Fixed"; `COORD` Decision log 29 Sep "Bug hunt, playback".
- **Rule:** silence is the default; no position for 1.5 s means 0; pause means 0 now.
- **Status:** covered (`app/src/playback_output.cpp` `kStaleMs` = 1500 and `goQuiet`; tests `staleWatchdogStopsTheDevice`, `pauseStopsAndMovesHome`, `connectionIsRefusedWhilePlaying`). Buffering is handled by the watchdog only, not immediately.

### PS-03 Soft seek: the first stroker move eases in
- **Learned:** after a start, a pause or a jump, the device used to slam to the new position in as little as 20 ms. Now the first move takes at least 0.5 s per full stroke (less for a shorter way); later moves follow the script.
- **Source:** `CL` 0.7.0 "Fixed"; `docs/DEVICE_SUPPORT.md` "How D2 drives a Buttplug device". D-041.
- **Rule:** any discontinuity of the playhead (start, resume, seek, loop jump) sets the "ease in" flag.
- **Status:** covered (`playback_output.cpp` `kSoftSeekFullStrokeMs` = 500, `kSoftSeekJumpMs`, `kSoftSeekBackMs`; test `seekEasesIn`).

### PS-04 Soft start of the vibration/suction levels, and pause inside the ramp
- **Learned:** 0.7 has a soft start (default 500 ms) for the channel levels. Two bugs: the soft start watched the wrong channel (positions), and pausing inside the ramp left the device mid-ramp because the normal quiet path was a no-op while `firstFrame`/`quiet` was still set. Fix: idle/pause zeros immediately and Stop plus home also work during the ramp.
- **Source:** `CL` 0.5.45 "Fixed" ("Play: pause during soft-start left device mid-ramp") and Unreleased "Move home on pause during soft-start".
- **Rule:** no state flag may turn a stop into a no-op; test "pause inside the ramp" with a mock device.
- **Status:** covered: `PlaybackOutput` soft start (`playback.soft_start_ms`, 10 steps, ramp also after a pause); pause, watchdog and home move inside the ramp stop at once (`playback_output_test` `pauseInsideTheSoftStartStopsAtOnce`, `watchdogInsideTheSoftStartStops`, `homeOnPauseDuringSoftStart`; mock only).

### PS-05 A jump while the video is still loading must reach the video
- **Learned:** a jump issued while the video was loading only moved the curve (0.7.0). Also, `seeked` must push the position to the device at once while playing, instead of waiting for the next time update (0.5.41).
- **Source:** `CL` 0.7.0 "Fixed" ("Player: a jump while the video is still loading..."); 0.5.41 "Play seek / script sync".
- **Rule:** a seek request before the media is ready is queued and applied on `LoadedMedia`.
- **Status:** covered (#35): `PlayerPage.qml` `seekTo()` queues a seek until `LoadedMedia`; test `player_page_qml_test` `seekBeforeLoadLandsAfterLoad`.

### PS-06 Number keys 1-9 jump to n x 10 % of the script
- **Learned:** the old formula was (n-1)/9: key 1 jumped to 0 %, key 9 only to 88.9 %.
- **Source:** `CL` 0.5.22 "Fixed".
- **Rule:** n x 10 % of the timeline, in video time including the offset.
- **Status:** covered (`PlayerController::jumpTenthVideoMs`; test `player_controller_test.cpp` line ~207).

### PS-07 Play list rules
- **Learned:** removing the active item must reload and highlight correctly; the video's end advances the list only if the stop was not the user's; a failed device connect or a decode error must not auto-advance.
- **Source:** `CL` 0.5.5 "Fixed" (Playlist); `docs/PLAYER_CODECS.md` "Qualitaetshinweise" (no auto-advance after a decode error).
- **Rule:** auto-advance only on a natural end of a playable item.
- **Status:** todo (P2): `core/include/samn/playlist.h`, `playlist_test.cpp` and `player_controller_test.cpp` `playlist` cover add, remove keeps the active item, next, repeat, shuffle and a multi-drop list; the "connect failure / decode error does not advance" part is missing: nothing stops auto-advance after a media error (`PlayerPage.qml` advances on `EndOfMedia`) and no test pins it. Needs the device connect at Play (P2) and PS-09's error handler.

### PS-08 Swapping the video after the first load
- **Learned:** in 0.7 a second video chosen with "Video..." left the player on the first file because the WebView cached the URL; fixed with a cache-busting epoch.
- **Source:** `CL` 0.5.44 "Fixed" (#444).
- **Rule:** keep a controller test "open A, then B in one session".
- **Status:** n/a (WebView URL cache; QtMultimedia reloads on a source change). Keep the test anyway.

### PS-09 Load errors must be visible and named
- **Learned:** a failed `LoadFunscript` was a silent rejection; video decode errors now name the media error class and clear when the video plays (0.5.41). The same pattern hid the update-check failure (an empty `.catch`) and a chapters load error that `documentFromLoaded` swallowed.
- **Source:** `CL` 0.5.41 "Play load errors"; `HANDOFF` "Fixed bugs affecting users" (update check).
- **Rule:** every failure ends in a visible, specific message; nothing is swallowed.
- **Status:** covered (#35): `PlayerPage.qml` `onErrorChanged` → `PlayerController::reportMediaError` names the class and the file, cleared on play; tests `mediaErrorsAreReportedAndClearOnPlay`, `player_page_qml_test` (broken file).

### PS-10 Dropped files must never vanish silently
- **Learned:** dropping several videos kept the first and discarded the rest with no sign (a dead `drop:videos` event); 0.7 now shows the ignored count and warns "Drop ignored" for unsupported files.
- **Source:** `HANDOFF` "Fixed bugs affecting users"; `CL` 0.3.0 "Fixed"; `docs/PARITY.md` section 1 (drop routing).
- **Rule:** say what was used and what was ignored; several videos open the Create queue (P3).
- **Status:** covered for the Player: `PlayerController::openPaths` names extra videos and unsupported files, and a drop with nothing usable says so (test `dropsNeverVanishSilently`). The Create queue for several videos stays todo (P3).

### PS-11 Zero is a value, not "unset"
- **Learned:** at least six bugs where a slider value 0 became a default: Extended-O minimum 0 ran the device at full power (the player treated scale 0 as "not set"); Extended-O Amplitude/Restore 0 -> 0.1/500; Contact strength 0 -> 1 (twice); Expert Smooth/Peak distance 0 -> 11/150.
- **Source:** `CL` 0.5.10, 0.5.42 "Fixed"; `COORD` 29 Sep "Bug hunt, player".
- **Rule:** model "unset" explicitly (`std::optional` / separate flag); never `value || fallback` and never "0 means default" on a path where 0 is a legal setting. For power scales store the reduction so that the zero value of the struct is full height only when that is intended.
- **Status:** covered for Extended-O: minimum 0 silences (`output_levels_test` and `playback_output_test` `extendedOMinLevelZeroSilences`), stored zeros stay zero and broken values take the defaults (`safetyChoicesAreRemembered`, `brokenSafetySettingsTakeTheDefaults`). `EffectiveContactSpan` still keeps 0.7's "0 means default" for the span (todo, decide when the contact UI comes).

### PS-12 Non-finite values at the display and device boundary
- **Learned:** vibration/suction meters could show `NaN%` when frame events carried non-finite numbers.
- **Source:** `CL` 0.5.45 "Fixed" ("Play/sidebar: vib/suction meters could show NaN%").
- **Rule:** coerce NaN/inf to 0 where a value leaves the core.
- **Status:** covered (`device_protocol_test.cpp` "ScaleToRange rounds half away from zero, clamps, NaN is 0"; `playback_output.cpp` `std::isfinite` on settings).

### PS-13 Timeline layers share one time scale and are crisp on HiDPI
- **Learned:** the scene strip and chapter bands were 30 px narrower than the curve, so markers and curve did not share a time scale (0.7.0); curves drawn as a 1x bitmap were blurry on HiDPI (0.5.22).
- **Source:** `CL` 0.7.0 "Player timeline", 0.5.22 "curve/heatmap render crisp on HiDPI".
- **Rule:** one plot rectangle and one `msAtX` mapping for curve, ruler, bands, heatmap and strip; the backing store follows `devicePixelRatio`.
- **Status:** covered (`PlayerPage.qml` chapter bands use `curve.msAtX`; `curve_item.cpp` uses `effectiveDevicePixelRatio`). Heatmap and ruler DPR handling not checked; there is no automated layout test.

### PS-14 Editor state after a range tool, and dragging a point
- **Learned:** after a speed cap or range delete the editor kept stale `rawActions` and could overwrite the capped file; the second speed-cap segment must use the original timestamps; a dragged point must be clamped to its neighbours or it jumps in list order after sorting.
- **Source:** `CL` 0.5.5 "CapSpeedRange editor stale state/overlap", 0.5.22 "curve editor clamps a dragged point".
- **Rule:** one model behind the editor; reload it after every destructive tool; clamp drags.
- **Status:** todo (P4). The tools are ported (`core/include/samn/range_edit.h`, tests ported from Go); the Editor UI is not built.

### PS-15 Media probing: unknown stays unknown, and the proxy is bounded
- **Learned:** the lean MP4 probe reported an unknown codec as `h264`/`yuv420p`, so "Make playable" was never offered (MEDIA-01). WebView decode was limited (HEVC, raw mkv/avi, MPEG-4 part 2); a proxy copy is made with ffmpeg. ffmpeg calls without a context could hang forever; two overlapping "Make playable" requests started two encoders writing the same file.
- **Source:** `HO15` task 2 (MEDIA-01); `docs/PLAYER_CODECS.md`; `CL` 0.5.23 "FFmpeg process hygiene".
- **Rule:** missing codec info is "unknown", never a default; every ffmpeg/ffprobe call has a timeout and a kill; one in-flight proxy per file; fallbacks never replace a better probe (`docs/SELF_BUILD.md`).
- **Status:** todo (P1). The video path is QtMultimedia (FFmpeg backend, Qt 6.7), which plays more than the WebView did; the proxy fallback and the probe are open (`ROADMAP.md`).

---

## 2. File formats

### FF-01 Corrupt times in the wild
- **Learned:** one script starts at -922 337 203 565 470 ms and ran 0.7 out of memory. Found by sending FredTungsten's public collection (758 scripts) through every step. Fix: drop actions before 0 or after 48 h on load.
- **Source:** `CL` 0.7.0 "Fixed"; D-076; `docs/FREE_RELEASE_QA_AND_CLEANUP_2026-10-12.md` (758/758 without findings after the fix).
- **Rule:** the corpus is a standing test; no loader may allocate from an unvalidated time.
- **Status:** todo (P1): the drop rule is covered (`core/src/funscript.cpp` `MaxActionMs`; `core/python/tests/test_funscript_parity.py` line 36). The 758-script corpus sweep (load, curve, heatmap, chapters, frames) is not in `tests/`; add it behind an env var for the local folder.

### FF-02 OFS durations and numbers
- **Learned:** OpenFunscripter writes `metadata.duration` in seconds with decimals (`631.8`); 0.7 expected whole milliseconds and rejected every such file ("invalid JSON"). Scripts with decimal `at`/`pos` failed the same way. A duration like `"10:31"` was read as 10 ms by the C++ port because `std::stod` stops at the colon. Rules now: decimals are rounded, seconds are scaled when the value is under 1 % of the last action, an unreadable duration is ignored, saved files use whole numbers. Parse of 200 000 actions went from 0.13 s to 0.28 s.
- **Source:** `CL` 0.5.44 "Loading OFS scripts"; `AS` M1 (the colon); `COORD` 30 Sep "script loading".
- **Rule:** the parser accepts what other tools write and never rejects a file for an optional field.
- **Status:** covered (`core/src/funscript.cpp` `flex_time_ms`, `parse_clock_ms`; `tests/core/funscript_test.cpp` "clock-string duration"; `core/python/tests/test_funscript_parity.py` lines 26 and 49-52).

### FF-03 OFS chapter and bookmark times
- **Learned:** OFS 3 writes chapter/bookmark times as text (`"00:01:23.456"`) and some tools as float seconds; loading failed, Play showed none, and the next companion export deleted them.
- **Source:** `CL` 0.5.44 "Chapters / bookmarks with OFS time stamps", 0.5.5 "OFS bookmarks/chapters: accept float seconds".
- **Rule:** one clock parser (`[[H:]M:]S[.fff]`) for every time field.
- **Status:** covered (`core/src/marks.cpp` `ParseClockMs`; `tests/core/marks_test.cpp` "timestamp strings", "float seconds").

### FF-04 The `inverted` flag has a lifecycle
- **Learned:** top-level `"inverted": true` (OFS) was dropped on parse, so Play used raw positions (0.5.44). Then "Invert" flipped stored points and cleared the flag, so the played curve did not change; a `.samn` made from an inverted file lost the inversion; the companion export kept the flag on points that were already flipped, so other players flipped twice. Final rules: playback uses `100 - pos` when the flag is set; Review/Invert flips what is shown and removes the flag; conversion to `.samn` bakes played positions; export drops the flag; the polarity hint looks at the played curve; a setting `playback.honor_inverted` (default on) switches the behaviour.
- **Source:** `CL` 0.5.44 (#453), 0.5.45 "Review Invert did nothing", ".samn from an inverted .funscript"; `COORD` 30 Sep "OFS inverted stays, but switchable".
- **Rule:** the flag is read, honoured at playback, baked on conversion and never written to a file whose points are already as played.
- **Status:** todo (P1/P4): `Script.inverted`, `PlaybackActions`, `FromFunscript` baking and the export (drops `inverted`) are covered (`funscript.cpp`, `samn_file.cpp`, `samn_file_test.cpp` "from_funscript bakes inverted"). The `playback.honor_inverted` setting, the Invert tool and the polarity hint are not in this repo.

### FF-05 A companion export must keep what it does not manage
- **Learned:** every `.samn` edit rewrote the companion `.funscript` wholesale, so `audio_check` (the Speech-Hold strip reads it), `ai_opinion` and the OFS keys (title, tags, performers, inverted, range) were gone after the first edit; "Bake Neo axes" did the same to a script the user had loaded. Fix: keep every key the export does not manage; the managed list is `creator, duration, profile, device_recipe, tracking_gaps, contact_marks, trajectory, quality_*, chapters, bookmarks, oMarkers, samn_axes` (they follow the document, including being dropped). `scene_map` never goes into the community export (size).
- **Source:** `CL` 0.5.44 "Companion .funscript lost its extra data"; `COORD` 30 Sep "companion export"; `docs/SAMN_FORMAT.md`.
- **Rule:** read-merge-write with an explicit managed-key list; test with a foreign key.
- **Status:** covered (`core/src/samn_file.cpp` `ExportFunscript` `kManaged`; `samn_file_test.cpp` "export_funscript keeps foreign metadata").

### FF-06 Every write is atomic, through one writer
- **Learned:** direct overwrites truncated scripts on a crash (native funscript write #428, metadata patchers #450, settings, project sidecar). Settings were rewritten in place on every knob change; a crash left a half file, the app started with defaults and the next change overwrote everything.
- **Source:** `CL` 0.5.42 "Native funscript write atomic", 0.5.44 "Metadata patchers write .funscript atomically", 0.5.45 "Settings lost after a crash".
- **Rule:** temp file in the same directory, flush, rename; one function for all callers.
- **Status:** covered (`core/include/samn/fileio.h` `WriteFileAtomic`; used by settings, marks, `.samn`, project; tests `fileio_test.cpp`, `marks_test.cpp` "save is atomic").

### FF-07 A broken settings file is kept, not overwritten
- **Learned:** an unreadable `settings.json` is renamed to `settings.json.corrupt-<time>` instead of being overwritten; a file containing `null` must not crash the next save; several related keys are saved in one write (`SetMany`) so a failure cannot leave a half-updated file.
- **Source:** `CL` 0.5.45 "Settings lost after a crash", Unreleased "Stroke filter settings".
- **Rule:** keep the evidence; one write per logical change.
- **Status:** covered (`core/src/settings.cpp`: non-object file is moved aside; `set_many`; tests `settings_test.cpp`). The aside name is fixed (`settings.broken.json`), so a second corruption replaces the first; 0.7 uses a timestamp.

### FF-08 Unsorted and duplicate times: look before you sort
- **Learned:** the Script Doctor "unsorted timestamps" check was dead code because it ran after the sort. It now inspects the original order, and `generator.ScriptQuality` reads file order instead of `Load`/`Parse` so spliced scripts are still flagged. The stroke filter sorts and de-duplicates its input.
- **Source:** `CL` 0.5.0 (17 Sep) "Script Doctor unsorted-timestamp check", Unreleased "Stroke filter input".
- **Rule:** the quality check sees the file as written.
- **Status:** covered (`core/include/samn/script_quality.h` `ScriptQualityOfFile` reads the file order and flags unsorted and duplicate times before any sort, #30; `tests/core/script_quality_test.cpp` "out of range, unsorted, duplicates, gaps", "Script quality of a file keeps the file order; .samn general; errors"; the stroke filter side is `stroke_filter.cpp` sort + `dedupeActionTimes`). `Parse` itself still sorts silently, which is fine because the Doctor does not use it.

### FF-09 Device-compatibility limits come from the community, not from us
- **Learned:** 100 ms minimum action spacing (the transmission threshold of the launchcontrol project), 900 ms for the slowest useful full stroke, position range 5-95, and intensity `500 x |dpos| / |dt|` (the funscript-utils definition, also used in OpenFunscripter, Funscript.io, XBVR). Minimum spacing was only reported at first; on noisy signals up to 45 % of actions were under 100 ms apart, so it is enforced during generation.
- **Source:** `HANDOFF` "Device compatibility", "Minimum spacing"; D-045.
- **Rule:** adopt the published metrics; enforce, do not only report.
- **Status:** covered (`core/include/samn/script_quality.h` `kMinActionIntervalMs` = 100, `kSlowestFullStrokeMs` = 900, `kUsablePositionMin`/`Max` = 5/95, `DeviceCompatMetrics` with `500 x |dpos| / dt` intensity, `EvaluateDeviceCompat`; `script_quality_test.cpp` "Device compat: actions too close are flagged", "a clean script has no warnings"). Missing: no test asserts the 900 ms slow-stroke count, the 5-95 range count or the intensity value, and the limits are reported by the Script Doctor card (`InspectorPanel.qml`), not enforced on output (todo, P2/P3.6).

### FF-10 Round trips need real data and must not truncate
- **Learned:** a `.sam` round trip silently dropped `Profile` and `DeviceRecipe`, so a distance-profile script fell back to hub-style mapping on real hardware, with no error. Found only by round-tripping a real excerpt. `ToFunscript` also truncated positions instead of rounding.
- **Source:** `NEXT` section 9 "Tested with real project data"; `CL` 0.3.0 "Fixed".
- **Rule:** every format conversion gets a round-trip test with a real-shaped file, and rounding is explicit (half away from zero).
- **Status:** covered (`tests/core/samn_file_test.cpp` round trips, byte-identical with Go; `marks_test.cpp` "rounding is half away from zero").

### FF-11 Unknown fields survive where users edit by hand
- **Learned:** O-markers are merged into `metadata` as raw JSON so a field another tool added is never dropped (`TestSaveOMarkersPreservesUnknownFields`). The `.samn` v2 spec makes the same rule for axes: unknown keys survive, `axes` wins on conflict, an unknown companion file becomes `x-<name>` and is never dropped.
- **Source:** `NEXT` priority 7; `docs/next/specs/P5_SAMN_V2_AXES.md` "Rules".
- **Rule:** read-modify-write of foreign JSON keeps unknown keys.
- **Status:** todo (P4): O-markers are covered (`omarker_test.cpp` "unknown fields kept"); `.samn` v1 deliberately drops unknown fields like Go (`samn_file_test.cpp` "unknown fields are dropped like Go"), which the v2 axes work (D-105) must change.

### FF-12 Names built from user text become paths: sanitize and detect collisions
- **Learned:** a training script name went into the session-log file name as typed, so `/` (and on Windows `:` or `?`) could not be created and the session was not logged. `scriptFileSlug` strips to `[a-z0-9-]`, so "My Routine!" and "My Routine?" collapsed to one file and the second save silently overwrote the first; saves now compare the stored name. Detector label paths rebuilt by splitting on `/` lost the leading `/` on Unix and drive letters on Windows.
- **Source:** `CL` 0.5.42 "Training: sessions of custom scripts missing from the history", 0.5.22 "Custom training script names could silently collide", 0.5.36 "AI Train review boxes missing"; `HANDOFF` "Script editor".
- **Rule:** one `SafeFileStem()` helper (path traversal, Windows reserved characters, collisions checked against the stored name); never rebuild a path by splitting strings; use `std::filesystem`/`QDir`.
- **Status:** partly covered: Training script file names are slugged and collisions are detected (`core/src/training_store.cpp`; `training_test.cpp` "script file slugs are safe file names", "custom scripts: save, edit in place, collisions, list, find, delete"); `core/src/script_finder.cpp` handles tolerant names and Unicode folding. Still todo (P4): AI Train label and thumbnail paths.

### FF-13 A generated script never overwrites an existing one unasked
- **Learned:** Create refused to overwrite only when a `.funscript` existed; a companion `.samn` was missed (0.5.10). An early "script exists" error could clear a newer running Create (0.5.45).
- **Source:** `CL` 0.5.10, 0.5.45 "Create: early 'script exists'..."; D-075 (beat bar never overwrites).
- **Rule:** the guard checks both files; its error carries the run's sequence number.
- **Status:** todo (P3.3) (`docs/P3_GENERATOR.md` "Rules carried over").

### FF-14 Users move between 0.7 and 2
- **Learned:** formats and settings keys stay; scripts never expire; `.samn` is extended only backwards-compatibly.
- **Source:** D-033, D-105; `docs/SAMN_FORMAT.md` "Compatibility".
- **Rule:** byte-identical output where Go's was; additive fields only.
- **Status:** covered (`tests/core/samn_file_test.cpp` "byte identical to Go", `marks_test.cpp`, `project_test.cpp`).

---

## 3. Devices

### DV-01 Keepalive repeats both channels; unchanged packets are not resent
- **Learned:** the keepalive remembered only the last packet. If that packet was suction, vibration was not maintained, and the other way round, most visibly during pauses and Extended-O. Both channel states are now stored and repeated. Separately, every tick wrote both channels: up to about 40 GATT writes with response per second for two channels updated every 50 ms. Levels are integers on the device, so 100 ramp steps give at most 11 distinct packets; the rest were useless round trips.
- **Source:** `HANDOFF` "Fixed bugs affecting users" (Keepalive); `docs/TEAM_STAND.md` section 1.
- **Rule:** device state is `{vibration, suction}`; keepalive repeats both; compare against the last sent packet; coalesce queued levels (newest wins).
- **Status:** covered (`tests/core/device_samneo2_test.cpp` "keepalive repeats BOTH channels after 4 s", "unchanged packets are not sent"; `tests/app/device_controller_test.cpp` `levelsAreCoalescedNewestWins`).

### DV-02 Raw and test writes go through the same bookkeeping
- **Learned:** the raw-value writes of the Device-tab test and the diagnostics sweep bypassed the remembered channel state. About 4 s later the keepalive replayed the last normal value, so after a sweep that ended with raw 0 the device started again by itself, and a following normal command with that old value was skipped as "unchanged". `Stop()` had been fixed first; the raw writers were the same bug.
- **Source:** `CL` 0.5.42 "Sam Neo 2: device restarting itself after the diagnostics test"; `COORD` 29 Sep "Bug hunt, device".
- **Rule:** one write path per channel; raw writes always send and update the state.
- **Status:** covered (`device_samneo2_test.cpp` "raw writes update the channel state", "Stop always writes both channels and remembers them").

### DV-03 Someone must read the server's replies (Intiface)
- **Learned:** Buttplug answers every command and ping with "Ok". After connecting nothing read them, the replies backed up until the server blocked, and the next write blocked for good while holding the connection lock: playback, the status display and Disconnect all hung. It took about 200 000 commands on Linux loopback and far fewer with Windows' smaller socket buffers. Battery readings must come through the same reader.
- **Source:** `CL` 0.5.42 "Intiface: playback hanging after a while"; `COORD` 29 Sep "Bug hunt, Intiface".
- **Rule:** one reader per connection; no blocking write may hold a lock that Disconnect needs; test with a server that bursts megabytes of replies.
- **Status:** covered (`core/src/device_intiface.cpp` `PumpIncoming`; `device_intiface_test.cpp` "PumpIncoming drains server replies"; the Qt transport receives on the worker's event loop). Check that the transport's incoming queue is bounded.

### DV-04 The second channel uses the type the device reported
- **Learned:** suction on Oscillate/Inflate devices was always commanded as "Constrict", which Buttplug rejects for those features. The adopted actuator type must be used.
- **Source:** `CL` 0.5.42 "Intiface: suction on Oscillate/Inflate devices".
- **Rule:** map channels from the reported features, never from the device name; table in `docs/DEVICE_SUPPORT.md` ("How D2 drives a Buttplug device").
- **Status:** covered (`device_intiface_test.cpp` "suction uses the adopted actuator type", "drives every motor", "motor levels").

### DV-05 Ask the device what it can do; never assume ranges
- **Learned:** the 0-10 vibration and 0-5 suction ranges come from Buttplug's device configuration (step counts), not necessarily from the firmware. Buttplug reports a range per feature, so the number of levels must be read, not fixed. The GUI must not rely on a device table.
- **Source:** `HANDOFF` "Actual device resolution is unknown"; `docs/SAM_NEO_2_RESEARCH.md` sections 5, 9a; D-036.
- **Rule:** capabilities come from the connection; raw encoders exist to probe the real device; present step counts as "reported", not "verified".
- **Status:** covered (`core/include/samn/device/capabilities.h`, `protocol.h` raw encoders; tests `device_capabilities_test.cpp`, `device_protocol_test.cpp`).

### DV-06 Device facts have evidence tiers; most are still unmeasured
- **Learned:** connection, playback and training were only ever verified against the mock and simulated protocol behaviour. Unknown for the Sam Neo 2: real resolution, minimum step, maximum command rate, latency, jitter, rise/fall time, pressure curve, channel interaction, reconnect time, timeout behaviour. Objective measures that need no sensor (write latency, accepted values, errors) are automated in the diagnostics; perceived intensity and physical rise time are not. After Owner hardware smoke on 24 Sep ("works great") the raw-value profile work was parked.
- **Source:** `HANDOFF` "Known limitations"; `CONTRIB` ("do not present values without hardware evidence as verified device properties"); `docs/SAM_NEO_2_RESEARCH.md` sections 9, 11, 18; `COORD` decision log 24 Sep. D-049.
- **Rule:** tag each device fact OFFICIAL / REGULATORY / PROTOCOL / COMMUNITY / MEASURED / HYPOTHESIS; MEASURED beats the others; never guess a number into a profile.
- **Status:** todo (P2). Diagnostics are covered (`core/include/samn/device/diagnostics.h`, `device_diagnostics_test.cpp`); the evidence-tag convention is not used in this repo's docs, and the Owner hardware test is pending (`AGENT_STATE.md` "Waiting on the Owner").

### DV-07 Distance-profile suction has no second floor
- **Learned:** with actions already clamped to 20-90 at generation and `MinSuction = 0.20`, applying a runtime `liftFloor` remapped resting suction 0.20 -> 0.36 and peak 0.90 -> 0.92. The generation-time clamp is the floor. Recipe metadata still reports `min_suction: 0.20`. Locked by `TestRecipeTJSuctionNoDoubleFloor` (pos 20 -> about 0.20, pos 90 -> about 0.90). A feel check on hardware is still open.
- **Source:** `FTT` "Tf/Tj suction double-floor"; `CL` 0.5.0 (17 Sep); `docs/TF_TJ.md` "Neo 2".
- **Rule:** in `SuctionPosition` mode suction is `pos/100` with no further floor.
- **Status:** covered (`core/src/mapper.cpp` skips `LiftFloor` for `SuctionPosition`; `tests/core/recipe_test.cpp` "recipe: tj suction has no double floor", #36).

### DV-08 Contact vibration is derived from the script itself
- **Learned:** "contact" is the top slice (default top quarter, span 0.75) of the script's own `pos` range, so duration and strength follow the clip; no fixed threshold. A script with under 5 points of `pos` variation disables the effect. Tracking-gap windows force vibration to 0 (a held last distance would buzz forever); `Confidence ~ 0` mutes vibration; envelope/smoothing must not bleed into gaps; the envelope smoothing (default 0.45) damps tracker jitter on the vibration channel only. Curves: linear, soft (t squared), peak (square root), impulse. A 50 ms graze and a 300 ms contact reach the same peak (>= 0.9) but the graze stays above 0.5 for far fewer frames.
- **Source:** `NEXT` priority 5; `CL` 0.5.4 (Fixed), 0.5.35 (`impulse` argparse); `docs/SAMN_FORMAT.md`.
- **Rule:** derive from the script's own range; fail quiet (0) when unsure; additive and gated (off by default for older recipes).
- **Status:** covered (`core/src/mapper.cpp` `contact_vib_at`, `in_gap`, `ContactVibrationMinSpan`; `tests/core/recipe_test.cpp` "recipe: tj contact vibration is off by default", "...tracks the position", "...brief graze", "...needs a position span", "...is muted in a tracking gap", "tracking gap wins over envelope and smoothing"; `player_controller_test.cpp` `tjScriptWithoutVideoPlays` for the device path, #36 and #42).

### DV-09 Who owns the connection
- **Learned:** playback/training used to demand a manual disconnect of the Device-tab connection even though BLE allows one connection; they now reuse it, and disconnecting while a session uses it is rejected. A TOCTOU gap in the test-connect guard and an `activeDevice` data race were fixed. `shutdown()` stopped only the test connection, not the active session's device. A rejected second session start overwrote the live session's player/device (0.5.10).
- **Source:** `CL` 0.4.1, 0.3.0, 0.5.10 "Fixed".
- **Rule:** one owner of the connection at a time; the controller says why an action is refused; quitting the app stops the device first.
- **Status:** covered (`tests/app/device_controller_test.cpp` `sessionActiveBlocksConnectAndTests`, `secondConnectIsRefused`, `destroyingWhileConnectingReturnsPromptly`, `shutdownStopsTheConnectedDevice`, `shutdownWithoutSessionIsSafe` (#39); `DeviceWorker::Shutdown` destroys the session with a stop).

### DV-10 Stop, emergency stop and precedence
- **Learned:** Emergency stop, Disconnect, Pause, Stop and device caps work in every build and win over AI, batch and previews. `Stop()` always writes both channels and remembers them. Pause "home" moves a stroker slowly to the bottom; after an emergency stop no home move happens.
- **Source:** D-037; `CL` 0.3.0 (Stop bookkeeping); `docs/DEVICE_SUPPORT.md` (stroker filter).
- **Rule:** nothing queued may survive a stop; a later stop never throws.
- **Status:** covered (`playback_output_test.cpp` `emergencyStopDropsQueuedLevelsAndStaysSilent`, `emergencyStopWithoutDeviceIsHarmless`; `device_controller_test.cpp` `emergencyStopDropsQueuedLevels`).

### DV-11 Stroker output
- **Learned:** the next action at each position is sent once as "move to pos within the time until it is due"; playback speed shortens the move; pause stops; a seek resends. Safety range (bottom/top at least 10 % apart) scales the stroke but not the companion axes. The smoothing filter (PCHIP/MAKIMA, speed limit, idle fill, home on pause) is applied to what the device gets, never to the file. Minimum spacing of device commands is 100 ms.
- **Source:** `docs/DEVICE_SUPPORT.md`; D-040, D-042, D-045; `CL` 0.7.0.
- **Rule:** the file is never changed by playback filters; filter input is sorted and de-duplicated (FF-08).
- **Status:** covered (`playback_output_test.cpp` `strokerGetsTheNextActionWithSafetyRange`, `rangeKeepsTenPercentApart`, `playSpeedShortensTheMoves`, `speedLimitFilterShapesWhatIsSent`, `strokeOffSendsNothing`; `core/python/tests/test_stroke_filter_parity.py`).

### DV-12 Bluetooth details that bit
- **Learned:** the Sam Neo 2 takes GATT writes with response, service `ffe0`, characteristic `ffe1`, no init handshake (the older Sam differs). The BLE scan callback blocked when more matching advertisements arrived after the device was found and could hang the scan after `StopScan`/timeout. Windows builds without a Bluetooth adapter must still start (Intiface on another machine).
- **Source:** `docs/SAM_NEO_2_RESEARCH.md` 9a; `COORD` 29 Sep "Bug hunt, device"; `HANDOFF` ("Two device connection options").
- **Rule:** bounded waits everywhere; no blocking in callbacks; a missing adapter is a clear error.
- **Status:** covered (`app/src/device_io/ble_transport.cpp` bounded waits; `device_transports_test.cpp` `bleScanWithoutAdapterFailsCleanly`, `device_controller_test.cpp` `bleConnectWithoutDeviceFailsAndDoesNotHang`). Real BLE hardware untested.

### DV-13 Intiface and TCode connection errors say what to check
- **Learned:** errors distinguish a local server that has not started from a wrong network, IP or firewall; the address field accepts `192.168.1.50`, `host:port` and `ws://`; type and address are remembered after a success. TCode is write-only: UDP/TCP (default port 8000) or USB serial at 115200, `DSTOP` on pause and emergency stop, extra axes L1/L2/R0-R2.
- **Source:** `HANDOFF` "Two device connection options"; `docs/DEVICE_SUPPORT.md`.
- **Rule:** keep the hint texts (they are what users act on).
- **Status:** covered (`device_intiface_test.cpp` "hint distinguishes local and network", "NormalizeIntifaceUrl"; `device_tcode_test.cpp`; `device_controller_test.cpp` `intifaceRefusedGivesHint` and the TCode cases).

### DV-14 Multi-axis companion files
- **Learned:** companion files `name.surge/.sway/.twist/.roll/.pitch.funscript` beside the script are read (inversion honoured), shown as lanes and sent to TCode devices as L1/L2/R0-R2. "+ Axis..." never overwrites; editing keeps other fields and the `inverted` flag. Extra axes come only from real files or video evidence, never invented.
- **Source:** `docs/DEVICE_SUPPORT.md` D4; D-038, D-086.
- **Rule:** an axis exists only if it was imported, measured or written by the user.
- **Status:** todo (P2). Output to TCode axes is covered (`playback_output_test.cpp` `extraAxesFollowTheScript`); `AGENT_STATE.md` lists "companion axis files into the player" as open; editing is P4.

### DV-15 The UI follows what the connected device can do
- **Learned:** rows, lanes and Training channels hide what the device lacks; a stroker with neither vibration nor suction cannot start a training that would do nothing. The old mockup's "Axis 1/2/3" does not apply to the Sam Neo 2, whose channels are Stroke, Vibration and Suction.
- **Source:** `CL` 0.7.0 "Training follows the device"; `docs/PLAY_UI_CONTRACT.md` section 6; D-035, D-132.
- **Rule:** show real channels only; without a device show what the script carries.
- **Status:** covered (`playback_output_test.cpp` `lanesFollowTheDevice`, `noScriptNoLanes`; Training: `training_controller_test.cpp` `withoutADeviceBothChannelsStay`, `aOneMotorDeviceLeavesOneChannel`, `aStrokerBlocksStartUnlessMock`; `training_qml_test.cpp` counts visible rings).

### DV-16 Mock tests are not hardware tests
- **Learned:** changes to the device layer should include a hardware test, and mock-only testing must be said so. Mock suites with 0, 1, 2 and 6 axes are the agreed stand-in; physical approval is the Owner's job.
- **Source:** `CONTRIB` workflow step 5; D-044; `docs/FREE_RELEASE_QA_AND_CLEANUP_2026-10-12.md` G5.
- **Rule:** every device PR states "mock only" or "hardware tested on ...".
- **Status:** partly covered: `MockDevice` and `MockTransport` exist (`device/mock.h`) and `CONTRIBUTING.md` rule 10 says mock-only tests are marked as such. Still todo (P2): the capability-set mock suite (0/1/2/6 axes); `device_capabilities_test.cpp` covers capability parsing only.

---

## 4. Generator / detection / tracking

All of this is P3 (the Generator with its Editor, [`P3_GENERATOR.md`](P3_GENERATOR.md)) unless a status says otherwise. As of this writing none of the tracking code is in this repo, so every status here is `todo`. The "Tested and rejected" table (section 11) has the measurements of every approach that failed.

### GD-01 One box writes the stroke
- **Learned:** the measured Everyday path is: find the tip region, track it with CSRT, write Stroke plus Contact vibration. On `clip_ausschnitt` (50 s, 1280x720, windowed 10 s r against the reference without the reference's own detector): CSRT hub 0.590, region fusion with tip ROI 0.562, grid LK 0.494, CSRT tip-tight 0.468, four-zone fusion 0.363, flow at half size 0.231. Four-zone fusion also has a documented failure: on `clip_voll` at 0:53-2:12 the general, suction and vibration axes collapsed together to a 5-9 point band (46-100 elsewhere). The cause: zones are fused by an EMA "activity" weight, so when the true target's activity dips against hair or cloth jitter the weight moves to a zone whose own position is nearly static; the 12 % `minLocalSpan` gate in the dynamic-range step then passes the weak segment through unchanged (it cannot tell "lost target" from "calm scene").
- **Source:** `docs/TFTJ_PROFILE_DIRECTION.md` "Step 4 measure", "Method matrix"; `COORD` "Owner decision - 4-Zone -> 1-Zone" and "real-clip finding ... 23 Sep"; `docs/EVERYDAY_GENERATE.md`. D-050, D-064.
- **Rule:** one tracked box writes the curve; fusion, zones and detectors may only suggest where that box should be; a segment with a collapsed range is flagged, not rescued by invented amplitude.
- **Status:** todo (P3.1/P3.5). `P3_GENERATOR.md` "Better detection" item 1 (run backends together with confidence-weighted fusion) must beat 0.7 on the goldens before it is a default (D-135) and must not repeat the weight-collapse failure.

### GD-02 Tracker availability: one factory, no silent downgrade
- **Learned:** CSRT creation has three API shapes across OpenCV builds (`cv2.legacy.TrackerCSRT_create`, `cv2.TrackerCSRT_create`, class-based `TrackerCSRT.create`); a real contrib install had only the third, and one unguarded call site crashed dataset bootstrap on Windows (0.4.1). OpenCV 5.0 lacked CSRT in some builds, the code fell back to KCF/MIL, and generation quietly ran on a weaker tracker. Installing the AI training package replaced `opencv-contrib-python` by plain `opencv-python`, which has no CSRT (#94/#119). Decision: one strong tracker (CSRT), a hard error naming the OpenCV version when it is missing, no soft degrade to NCC/KCF/MIL; later also no silent "re-init on last box" when reacquire fails (0.5.45).
- **Source:** `CL` 0.4.1, 0.5.0 (16 Sep), 0.5.5, 0.5.11, 0.5.45 "Recognition honesty"; `docs/WINDOWS_OPENCV.md`; `docs/KI_TRAINING.md` "Installation".
- **Rule:** exactly one `CreateTracker()`; a startup self-check and a CI test assert that CSRT can be created and run on every platform; the log names tracker and OpenCV version; a missing tracker is an error.
- **Status:** partly covered (P3.0): one `CreateTracker()` in `vision/src/tracker.cpp` with no fallback, `VisionSelfCheck()` and `OpenCvVersion()`; test `vision_tests` "tracker: the self-check passes on this build" runs in CI on Linux (apt) and Windows (vcpkg `opencv4[contrib,dnn,ffmpeg]`). Todo (P3.3): run the self-check at app start and log tracker and version.

### GD-03 Every option must reach the output
- **Learned:** `--backend` was silently ignored whenever `--roi2` was set (byte-identical outputs for CSRT and grid LK); `--flow-downscale` was ignored in the direct CLI dispatch; `peak_prominence`, `dynamic_range_ms` and `min_action_interval_ms` were never passed to `positions_to_funscript`, so the soft-tissue profile was silently ignored; the GUI sent `impulse` but the CLI parser accepted only `linear|soft|peak` (exit 2); stroke profiles with a Zone 2 mix failed with "no discernible motion". "Twice in this project a feature was implemented but did nothing: once an insertion landed in the wrong file, once a parameter had no effect anywhere."
- **Source:** `CL` 0.3.0, 0.5.18, 0.5.35; `NEXT` priority 8 follow-up and "Go-native post-tracking"; `CONTRIB` "What a contribution must include"; `COORD` Lane E findings.
- **Rule:** each option gets a test that fails when the option is not wired (a spy stage that records its arguments); enumerations shared between layers (UI, controller, core) come from one definition.
- **Status:** todo (P3).

### GD-04 Quality is two axes; neither predicts the other
- **Learned:** Signal Quality (jitter, jumps, loss, device compatibility; Quality Doctor/Script Doctor) is not Motion Fidelity (rhythm, phase, amplitude against the video or a reference). Measured three times: the run with the best own quality (0.95, 2 % lost frames) had the worst r (0.070, lag pinned at -1000 ms) against the reference, while two older, messier runs gave 0.336 and 0.356; grid LK scored 1.00 on its own quality and r 0.125 against CSRT's 0.179; on `clip_voll` hub had quality 0.90 and tj 0.57 with the same fidelity.
- **Source:** `docs/SIGNAL_VS_FIDELITY.md`; `NEXT` priority 2 follow-ups and priority 8; `FTT`. D-066.
- **Rule:** two separate numbers in every report and in the UI copy; never say a Quality Doctor score "matches the video".
- **Status:** todo (P3.6, P3.8).

### GD-05 Comparing with a reference: the benchmark itself had bugs
- **Learned:** the first comparison script had four bugs that push correlation toward zero by themselves: candidates de-duplicated by path instead of content, two files aligned by index after resampling instead of on a shared absolute timeline, `0.0` returned for a constant reference instead of "undefined", and no check for an inverted polarity match. Further traps: a lag window of 3000 ms made short clips pin to the boundary (lag +-2800-3000 ms); narrowing to 500 ms moved the mean from 0.696 to 0.486, the default is now 1000 ms and results with under 30 samples (3 s at a 100 ms step) are flagged `low_confidence`; the dataset scan must recurse; a stale `--max-frames 900` left over from another task produced `n=150` instead of `n=421` and a withdrawn r=0.310 (true 0.179), noticed only because a second run had a different `n`; a reference 54 s shorter than the video made lag searches pin to the boundary; two FunGen exports of the same clip contradicted each other on orientation in some windows (r -0.23/-0.19, -0.50) and agree with each other only at r about 0.45 windowed; a file labelled as SamNPlayer output was FunGen output because a re-save stamped `creator`/`native_pipeline` onto it (first reported as an "internal consistency check", corrected the same day).
- **Source:** `NEXT` priority 2 (Work Package 1, "methodology risk", "Correction"), "Native Go pipeline vs. real FunGen2"; `COORD` decision log 23 Sep (rhythm grid orientation); `docs/FUNGEN_PARITY_PLAN.md`; `docs/GOLDEN_CLIPS.md`.
- **Rule:** the P3.8 comparison controller uses an absolute timeline, a +-1000 ms default lag window, boundary-pinned lags marked untrusted, a low-confidence flag, windowed r, listed exclusions, the same `n` for compared runs, a manifest with content hash, ROIs, parameters, generator version and source cut, and orientation judged only where the references agree. Never use a per-clip best offset or inversion as the production score. A fair comparison uses identical clips and ROIs (the reference run with its own detector disabled for classical-vs-classical).
- **Status:** partly (P3.8): `samn_compare` follows the rule (absolute timeline, +-1000 ms, pinned and low-confidence flags, windowed r, n per pair and an n-mismatch flag, manifest with SHA-256 of video, reference and 0.7 output, box, profile); open: the source cut and generator version in the manifest, orientation only where references agree.

### GD-06 Lag aliasing: swinging lag does not mean timing drift
- **Learned:** the weak whole-clip r (0.06 and 0.27-0.44 on two clips) with higher windowed r (0.22-0.35 and 0.53-0.59), lag swinging from -2600 to +2800 ms and mid-clip orientation flips was blamed on VFR frame-index drift. A direct `ffprobe` check refuted that: the error is a constant 41.02 ms at every sampled frame (0, 100, 600, 1198) of a genuine 24 fps CFR clip, and a constant offset cannot make the lag swing by +-900 ms. A synthetic test confirmed the real mechanism: a 280 ms-period sine with an injected constant 300 ms lag gives whole-clip best lag -1140 ms (-(300 + 3 x 280)) at r=1.0000, and 10 s windows swing -1420..+1240 ms with one orientation flip; a non-periodic signal recovers -300 ms in every window. The diagnostic that flags this (`AliasingRisk`) first never fired on the real clips because of two bugs: the dominant-period search took a global maximum over a smoothly declining autocorrelation (so it returned the smallest lag) and `minLag = MinDominantPeriodMs / step` rounded down (150/40 = 3 -> 120 ms). After the fix it fires on 1 of 4 real pairs (period 1500 ms, alternates 700/800 ms). An earlier "half-period" explanation of the flag's silence was also wrong. Decision: no tie-breaking of the reported lag (D-065); a flag plus a human glance is the stopping point.
- **Source:** `FTT` section F-003; `COORD` "Open question - F-003 periodicity aliasing"; `NEXT` (21 Sep entries). D-065.
- **Rule:** report windowed r and the aliasing flag together; ship the synthetic ground-truth test (known constant lag, periodic and non-periodic signal) as a characterization test; check a hypothesis with the cheapest direct measurement before building on it.
- **Status:** ported (core `samn/motion_fidelity.h`, tests `tests/core/motion_fidelity_test.cpp` with the synthetic characterisation; `samn_compare` reports windowed r with the aliasing flag).

### GD-07 Timebase: real PTS, absolute video time, accurate seek
- **Learned:** tracking timestamps were `frame_index x 1000 / fps`, and the frame type (`GrayFrame`) carries no per-frame PTS, which VFR clips and a PTS timeline need. With a start time (GUI seek past an intro) the engine's windows, marks and results counted from the first tracked frame while marks and contact points were absolute: SceneMarks (#377), the rhythm grid's exclude/source marks and followed-mark positions, the saved scene map (shifted by the start), and contact-verify windows all acted at the wrong moment (0.5.42, 29 Sep). A fast ffmpeg seek returned empty or unreadable preview PNGs and needed an accurate retry (`-ss` after `-i`) (0.5.41); remaining progress after a seek must be preserved.
- **Source:** `HO15` "Findings to keep in mind"; `CL` 0.5.41, 0.5.42 "Fixed"; `COORD` 29 Sep "Bug hunt ... rhythm grid clock after a seek"; `FTT`. D-109 (integer microsecond PTS).
- **Rule:** the frame reader returns `{frame, pts_us}`; every public result (positions, gaps, marks, maps, progress) is in video time at the API boundary; every feature that takes times is tested with a start offset greater than 0.
- **Status:** partly covered (P3.0): `samn::vision::FrameReader` returns `{frame, index, pts_us}` in video time; tests `vision_tests` "every frame in order with its video time" and "seek is frame-exact, forwards and back" on a generated clip. Todo: a start-offset clip (stream start > 0) once the test can mux one, and video time at every later API boundary (P3.1+).

### GD-08 Two-point distance: geometry and partner handling
- **Learned:** `track_two_points` measured only the Y distance of the two box centres, giving a near-dead signal (1.0 px instead of 80 px) whenever both regions sit at the same height; the fix is the full 2D distance (`hypot`) and a horizontal test. Common translation (camera pan, shared movement) cancels in a distance, zoom does not (pixel distance changes with zoom, the old code comment claimed otherwise): on a pan + shared motion + oscillating-gap video, correlation with the true distance was -0.01 with one tracker and +0.62 with two. Lost partners must not feed stale boxes into the minimum; tip loss or no usable partner holds the last good distance and sets a gap flag; brief misses coast about 8 frames (last velocity/hold) before exclusion; distance uses the tip-box point nearest the partner; the topology is a star (each partner against the tip), not a pairwise graph.
- **Source:** `NEXT` priority 2 (Work Package 2), `HANDOFF` "Two-point measurement"; `CL` 0.5.15, 0.5.23 "Tf/Tj partner coast"; `docs/TF_TJ.md` "Signal".
- **Rule:** port with the `two_point_axis` and pan/shared-motion tests; treat distance as zoom-sensitive.
- **Status:** partly (P3.4): `TipPartnerDistance` + `TrackOptions.distance_stroke` for single gold partner (Tf/Tj); extras multi / FuseTipPartners still open.

### GD-09 Never commit an unvalidated region; bound region growth
- **Learned:** a 22-clip batch with an invented second region (the first shifted by 1.2 box widths) produced near-zero correlation and could not even separate ROI from parameter effects (issue #8). `find_two_rois` was measured insufficient (+0.17 to +0.28 against +0.62 for hand-placed regions). Region growth from the strongest cells (`_peak_regions`) raised a close synthetic case from +0.01 to +0.63, but on a real clip it proposed the whole 256x144 frame as ROI1 until the growth was capped (cells <= 1/8 of the grid, spans <= 1/3 per axis); after that both backends landed near zero. The growth parameter is not a smooth dial (0.4 and 0.6 good, 0.5 gave -0.03, 0.7 gave -0.80 on the close case). A tight, deliberate ROI2 cut lost frames from 75 % to 2 % but not the match; the anchor choice (neck vs lower anchor) nearly doubled r (0.070 -> 0.123). Owner decision 28 Sep: the second region may be filled by the AI only with the opt-in setting "Apply AI setup automatically" (default off, shown, undoable, user values win, never with a distance profile).
- **Source:** `NEXT` priority 3 (all subsections) and priority 2 ablation; `docs/GOLDEN_CLIPS.md` "Tf/Tj + AI two-ROI bake-off". D-055, D-056, D-054.
- **Rule:** suggestions are proposals shown with their uncertainty; a parameter sweep is judged by its shape, not its best point; a suggestion replaces a manual choice only if it matches or beats it on both quality and agreement.
- **Status:** todo (P3.3/P4).

### GD-10 Appearance memory and reacquire: fail closed
- **Learned:** remembering what the region looks like and searching the whole frame after a loss or cut (remember every 25 frames, keep 8 templates, template 0 = the user-confirmed ROI is never evicted, minimum match 0.55) recovered a three-scene test (true motion 90/100/80 px): without memory 90.5/1.5/5.0 px, with memory 90.5/101.0/80.0; on an occlusion video lost frames fell from 323 to 148 of 500 (the rest is truly absent and is reported). Below the minimum similarity it deliberately does not re-init, because a guessed position looks like a measurement. Reacquire must fail closed unless the candidate still resembles the tip seed, so a poisoned bank cannot hard-land on a distractor. Scene-cut detection used histogram correlation < 0.5 or resized-frame mean difference > 12.0.
- **Source:** `HANDOFF` "Appearance memory"; `NEXT` Go-native section; `CL` 0.5.41, 0.5.45.
- **Rule:** report unsuccessful searches; never substitute a guess.
- **P3.1 finding (17 Oct):** the templates are crops of the whole box. With 0.7's usual loose box (120x160 around a 70 px subject) most of a template is background, so after a cut to another scene, or when the subject moves far, no candidate reaches 0.55 and reacquire never fires (C++ and Go alike). With a tight box it works (test "after a cut the memory finds the subject at its new place"). Improvement candidate for P3.9: templates of the subject, not of the box.
- **Status:** ported in P3.1 (`engine/src/appearance_memory.cpp`, gate, fail-closed reacquire, tests in `tests/engine/track_box_test.cpp`); the two-point use follows in P3.4.

### GD-11 Long-clip CSRT drift is invisible per frame
- **Learned:** on the 280 s `clip_voll` the tracked point drifted about 1000 px and the drift reached the final curve (normalization does not catch it). Two independent mechanisms: CSRT gradually walks off the target over 100+ s while every individual frame looks normal (median per-frame displacement about 1.5 px, `TrackerLostFrames = 0` on all three clips of 6723, 1199 and 19 263 frames: it fails by wrong place, not by loss), and the box shrinks from about 171x216 to 50x65 over about 175 s. Shipped: a displacement guard (never fired on that clip, kept as defense) and a `matchesOriginal`-gated `remember()` with template 0 seeded from the true frame-0 ROI; the first version sized its search window from the shrinking box, so from about 160 s the check returned `score=0.000, known=false` every time and silently defeated itself (window now sized from the original template). Rejected: active correction (see REJ-20). Residual drift remains; a real fix needs a more discriminative signal (the rhythm grid, GD-15, or a trained detector).
- **Source:** `COORD` decision log 23 Sep "CSRT long-clip drift"; `S23`; `docs/owner/motion-recognition-next.md` priority 1.
- **Rule:** never trust the tracker's own confidence; test on 5-minute clips; capture the trajectory (opt-in, byte-identical output when off) to see drift.
- **Status:** partly: the displacement guard and the original-seeded gate are ported in P3.1, and a 5-minute synthetic clip tracks bit-identical to 0.7 (`docs/benchmarks/P3_1_TRACK_PARITY.md`); the residual drift on real clips stays open (P3.4 rhythm grid, P3.9).

### GD-12 Default detrend for stroke profiles
- **Learned:** the standard profile had no detrend, so every tracker jump moved the baseline of the rest of the curve: 62 % of 10 s windows on `clip_voll` sat in a band under 25 points. A rolling mean over whole periods removes only what is slower than the stroke; best window about two stroke periods (1 Hz -> 2000 ms, 1.5 Hz -> 1500 ms), clamped 1.5-4 s with 3 s if the tempo is unknown. Windowed r 0.275/0.261 -> 0.386/0.552 and stuck windows 62 % -> 3 % on `clip_voll`; `clip_ausschnitt` (two windows, weak evidence) 0.475/0.285 -> 0.449/0.712. Distance profiles are excluded (absolute distance is contact). `DynamicRangeMs` 3000 alone was worse (69 % stuck).
- **Source:** `COORD` decision log 23 Sep "CSRT drift round 2"; `S23`; `docs/GENERATE_HEURISTICS.md`. D-060, D-122 (announced in CHANGELOG, not silent).
- **Rule:** default on for stroke profiles, off for distance profiles, `<0` = explicit off; applied before the Go/Python fork in 0.7, so apply it in the one pipeline.
- **Status:** ported in P3.2 (`engine/src/signal_chain.cpp` `ApplyDefaultDetrend`, tests in `tests/engine/signal_chain_test.cpp`; whole chain equal to 0.7 in `docs/benchmarks/P3_2_PIPELINE_PARITY.md`); the stroke preview that sets the window follows with P3.6.

### GD-13 Post-processing facts that were measured
- **Learned:** peaks and valleys are found separately, so a minimum spacing inside each list does not give spacing between a peak and the next valley; on noisy signals up to 45 % of actions were under 100 ms apart. Enforcing it removes the point with the smaller deviation from the line through its neighbours: reconstruction error 0.084 -> 0.086, clean signals unchanged. Global normalization used only 30 of 100 points in half of all 6 s windows; moving-window dynamic range lifts average motion from 10.1 to 26.2. Smoothing attenuates realistic stroke frequencies by only 0-8 %; the marked region size does not matter (112, 111, 110 px for 40x40, 70x70, 120x120). A prominence requirement is needed for soft tissue (impulses every 800 ms with 4 Hz ringing: 81 keyframes without, 42 with prominence 0.35; reconstruction error 0.094 -> 0.176 because ringing is excluded on purpose); clean signals are exactly unchanged (42 or 80 keyframes), so it is a profile, not the default. The Savitzky-Golay edge mode must be "interp" (window shifted, not mirrored) to match scipy; ported post-processing is held to the Python goldens (dense curve within 0.05 position units). `--axis` defaults to automatic (both axes are tracked anyway; the larger range wins).
- **Source:** `HANDOFF` "Minimum spacing", "Motion profiles", "Known limitations"; `NEXT` Go-native post-tracking, `--axis`; `docs/FUNSCRIPT_ALGOS.md`.
- **Rule:** keep the stage order of `docs/FUNSCRIPT_ALGOS.md`; every new stage gets a before/after run on the same clip.
- **Status:** todo (P3.2).

### GD-14 Rhythm and quality metrics: dense, detrended, windowed, as fractions
- **Learned:** measure rhythm only on the dense signal (reducing to peaks and valleys turns any signal into a zigzag that looks rhythmic). A global rhythm measure assumes one rhythm: two real scripts for the same 77 s film scored 0.203 and 0.039 globally (both "noisy"), 0.396 and 0.324 windowed (8 s, median). Without detrending a linear drift scored 0.289, above the 0.20 reject threshold, and a sine with drift fell from 1.000 to 0.425. A minimum cycle count penalizes slow valid motion; the active-time fraction separates cases (one excursion 0.27, two slow cycles 0.86, continuous 1.00). For camera compensation use the RANSAC inlier fraction (0.24 vs 0.84-0.90) not the absolute match count (random matches on noise raised amplitude to 233 px).
- **Source:** `HANDOFF` "Tested and rejected", "Pitfalls", "Quality Doctor".
- **Rule:** metrics are fractions of something measurable, computed on the dense detrended signal in windows.
- **Status:** todo (P3.6).

### GD-15 Rhythm grid (anti-drift signal source)
- **Learned:** what failed: per-cell brightness rhythm (dominated by lighting; a random sine at the right tempo scores almost as well), "box sits in a weak-rhythm cell" as a drift detector (Spearman 0.07/0.04 with brightness, 0.29/0.20 with flow), the globally best cell (the whole body rocks, the partner's thigh wins on one clip: `clip_ausschnitt` 0.391/0.811, orientation 50 %), cell hysteresis (x1.5/x2). What worked: Farneback flow at 320 px width with the per-frame median subtracted (camera), a 16x9 cell grid, 8 s windows every 2 s, tempo f0 from the most active cells (0.6-2.5 Hz), score E^2/total with E the power at f0 and 2 f0, the best cell within 3 cells of the CSRT box, the sign from the CSRT motion (never from the reference); below |r| 0.1 a chunk keeps the orientation already written (4 of 140 chunks were a coin toss; 0.25+ also overrode informative signs). Results: `clip_voll` 0.386/0.552 -> 0.411/0.767 and `clip_ausschnitt` 0.449/0.712 -> 0.466/0.877, runtime +18 % (70 -> 83 s); it relies on the default detrend. The per-shot target lock (#248) was merged without a golden-clip measurement and regressed both clips (0.411/0.767 -> 0.427/0.626 and 0.466/0.877 -> 0.383/0.666): it seeds the cell nearest the ROI centre, which sat on the breast/hand beside the stroke, and kept it after its rhythm died. A release rule (drop the lock when its score is under the best cell in radius divided by K) fixed it: K=2-10 fail the thigh test, K=20-50 pass, 30 chosen (`clip_voll` 0.455/0.752, `clip_ausschnitt` 0.486/0.887); 10 breaks the in/anti-phase thigh test, so 30 is pinned from both sides. Default stays off until at least 4-5 clips improve r with no orientation regression, judged against the stronger reference.
- **Source:** `COORD` decision log 23 Sep (three rows), 26 Sep, 27 Sep "IdLock"; `docs/SCENE_MAP_PLAN.md`. D-061.
- **Rule:** opt-in until the Owner gate; a change to a tracker/engine default needs a golden measurement in the PR; pin a tuning constant from both sides with tests.
- **Status:** todo (P3.4/P4); D-061.

### GD-16 Detector output belongs in the search anchor, and a hybrid check beats trust
- **Learned:** on the two golden clips "where" is already solved (the chosen cell lies inside the hand-labelled contact box in 118/140 and 24/25 windows), so a hand-labelled oracle gave no gain (labels as `source` marks 0.393/0.662 and 0.454/0.867; thigh/hand `exclude` = baseline; re-seed-when-outside-box 0.444/0.770 and 0.454/0.867). The value shows on a multi-person 642 s clip at 256x144, where the engine sat on the wrong person (chosen cell on the real contact in 3 of 237 windows, r 0.304): labels as marks 0.305, as a search anchor 0.425 (62 % hit), anchor + marks + re-seed 0.417 (85 % hit), with the goldens unchanged (0.467/0.771 and 0.482/0.888). A gated anchor (only when CSRT is more than 3 cells away) took 0.304 -> 0.406; using the moving body part from scene roles at confidence >= 0.5 gave 0.440, but typing every window (0.449) cost `clip_voll` 0.752 -> 0.702, hence the 0.5 floor. The engine "verifies the teacher" (keep a contact point only if the local rhythm cell is strong enough, K=1.5): `clip_voll` 0.703 -> 0.752 (= baseline). A weakness signal was measured first: chosen/strongest-cell ratio flags 65 of 118 right windows (too blunt), tracker |r| flags too few wrong ones. On a 1573 s clip: none 0.328, detector 0.320, + verify 0.323, scene roles + verify 0.349; the 642 s clip: 0.293 -> 0.396 -> 0.404 -> 0.445. Scene type was right for 15 of 21.
- **Source:** `COORD` decision log 27-28 Sep and 10 Oct; `docs/VLM_TEACHER_PLAN.md` "Oracle ceiling", "Multi-person clip"; `docs/SCENE_UNDERSTANDING_PLAN.md`; `docs/VLM_MODELS.md`.
- **Rule:** a detector hooks into the search anchor, gated and verified by the engine; judge it on clips where today's result is wrong and require no regression on the goldens; agreement of at least two independent teachers and human review before anything becomes training data.
- **Status:** todo (P3.7/P4).

### GD-17 AI proposes; rules, class limits, licences
- **Learned:** AI proposes ROI, profile, quality opinion; classical tracking measures and writes; an AI result is checked by a verifier against video evidence, timebase and device limits (D-059), and later drafts of the curve are opt-in (D-129). A strict target must not be replaced by another class when the wanted class is missing, weak or ambiguous. A pretrained 80-class COCO taxonomy has no hand/mouth/body-part classes, so filtering by class cannot create them; an ONNX export that loads is not proof of equivalent detections; ONNX runtime providers fall back silently, so log what was selected. The old training package is AGPL-3.0 (weights included, by its reading) and fine for local experiments but not for shipping; the chosen shipped detector has Apache-2.0 code and weights; weights licences differ from framework licences. Weights are never bundled or downloaded automatically; the cloud agent environment blocked the weight host, so teacher probes run on the Owner's PC. Frame data stays local.
- **Source:** `docs/AI_ADAPTER.md`; `docs/MT_SPEED_NOTES.md` "Classes before tracking", "FP16 / half and ONNX"; `docs/VLM_MODELS.md` "Our own model"; `COORD` 27-28 Sep; `docs/KI_TRAINING.md`. D-028, D-057/058/059, D-129, D-134.
- **Rule:** check licence of code and weights before adding a detector; record the class map; compare original-backend and ONNX boxes on identical frames before timing; AI never becomes a final authority or a device-command source.
- **Status:** todo (P3.7).

### GD-18 Learned models need data first
- **Learned:** the learned quality model (logistic regression over seven metrics, plain numpy so weights are readable) is adopted only if it beats the fixed rules in leave-one-out cross-validation, with at least 12 rated runs and at least 4 per class; "borderline" counts as failure; it improved 50 % -> 100 % on a constructed case and has never seen real data. The motion-signature profile model uses eight features; measured distances: same motion at different speed 0.075, vertical vs horizontal 0.26-0.32, vs panning 0.28-0.29, so threshold 0.15; above it nothing is assigned (a wrongly transferred parameter set is worse and harder to notice); the region-count feature returned the same value for all test videos and carries no information. Auto-retry is implemented but no case in the calibration set improves with it (all failures were tracking problems, which it correctly skips).
- **Source:** `HANDOFF` "Learned quality assessment", "Motion signature", "Known limitations"; `docs/KI_TRAINING.md`; `docs/ENGINEERING_STANCE.md` "Owner gates". D-063.
- **Rule:** reject unfamiliar or ambiguous inputs; suggestions need Apply; every learned stage needs a golden win before it is a default.
- **Status:** todo (P4).

### GD-19 Tracking gaps: "lost" is not the same as "wrong"
- **Learned:** `tracking_gaps` came only from the lost flags of two-point/multi-point tracking, so on the single-ROI path "Heal tracking gaps" had nothing to heal; and CSRT never reports loss when it is wrong (GD-11). Healing with a straight line loses the strokes inside the gap; a rhythm bridge (turning points within +-8 s, count fitted to the far-side point) beat the line wherever the stroke is regular: `clip_voll` 4 s gap r -0.11 -> 0.43 and MAE 20.6 -> 8.6, `clip_ausschnitt` 6 s r 0.01 -> 0.93, 10 s -0.02 -> 0.31, erratic multi-person clip a tie. Other bugs: Fill overwrote the Heal point count with 0 ("gaps ok" while windows were rewritten); windows must be cleared after healing or vibration stays muted forever; Improve left quality score and the `.sam` sidecar describing the pre-edit curve.
- **Source:** `COORD` 29 Sep "GapFill measured before code"; `CL` 0.5.24, 0.5.38 "Fixed"; `NEXT` priority 5.
- **Rule:** recompute derived data (quality, sidecars) after every edit; measure a repair on synthetic gaps cut from references before shipping; opt-in.
- **Status:** partly (D-158): the single-ROI track now carries per-frame lost flags (`TrackResult::lost`), and the opt-in *Bridge short tracking gaps* bridges losses up to 500 ms with a line and leaves longer ones without actions (`PlanGaps`, tests "spec T6", "spec T7", "spec U2" on synthetic gaps). Still todo (P3.3/P3.6): the rhythm bridge, and the measurement on gaps cut from the golden references before it can become a default.

### GD-20 Stroke preview is a hint, not a tracker
- **Learned:** a sparse probe estimates extrema and stroke rate, cuts and pan share in about 1.1 s for 50 s of 1280x720 (analysis at 12 fps), with 62 up/62 down extrema; peak overlap within 300 ms was 14/18 = 0.78 against the committed hub script and 16/25 = 0.64 against the reference. It first read 2-3x the real stroke rate because codec noise at turning points moved the motion centroid; fix: ignore differences under 8 grey levels and hold the last value when motion-free (a clean 1 Hz bounce reads 1 Hz; before it read 2-3 Hz depending on the encoder's thread count, which also made CI flaky). Stage B steers only: a high cut rate (over 4 per minute) enables per-scene ROI for that run, a high pan share (over 0.4) re-enables camera compensation; the suggested peak distance stays advisory. Stage C (extrema as the main path) is rejected until A/B prove value.
- **Source:** `NEXT` "Stroke preview"; `CL` 0.5.46 "Fixed"; `docs/GENERATE_HEURISTICS.md`.
- **Rule:** the preview never writes actions; noise floors are explicit constants with a test.
- **Status:** todo (P3.6/P3.9).

### GD-21 Optical-flow backend facts
- **Learned:** the flow backend overestimates amplitude even with a still camera (137.8 px for a true 110, correlation 0.941); position-based camera correction gives 100.2 px on a pan (vector correction 145.2). Claims in code and UI were wrong: "about 4x faster" measured 1.9x on a real clip (25.1 s vs about 48 s) with quality 0.55 on a busy scene; "18 ms/frame" does not hold at 720p: 223 ms/frame full resolution (26.7 s for 120 frames) vs 6.67 s at half size. In the bake-off, flow "hung" for 3-5 minutes with zero progress lines; it was cost, not a deadlock, and a wall-clock kill with no progress output could not tell the two apart. Fix: progress on frame 1 and every frame for the first 20, per-stage timing on request, an always-printed summary line, default downscale 0.5.
- **Source:** `HANDOFF` "Known limitations"; `NEXT` priority 8 (flow, 22 Sep trace); `COORD` Lane E findings; `CL` 0.5.22 "Flow duration traced".
- **Rule:** long loops report progress from the first frame; no speed claim without resolution and hardware.
- **Status:** todo (P3.5).

### GD-22 Test material for tracking
- **Learned:** noise-background test videos are useless for camera compensation (no stable features, a random walk); moving objects must use world coordinates so they follow a pan; all quality thresholds were calibrated on clean synthetic sine motion and real material scores lower; synthetic success (mean r about 0.52-0.58 on 48 synthetic clips) did not predict real clips (r 0.05-0.13 at first). Real clips are fixed-ROI sets of 2-3 clips with a documented reference, ROIs marked once and reused; a minimum golden set is one easy single-subject clip, one two-body clip, one with a cut or camera move.
- **Source:** `HANDOFF` "Pitfalls", "Known limitations"; `NEXT` priority 2; `docs/GOLDEN_CLIPS.md` "Minimum set"; `docs/TEAM_STAND.md` "Test clips".
- **Rule:** generate synthetic clips inside the tests (textured background, world coordinates, known ground truth); no real media in the repo; keep synthetic and real results in separate rows.
- **Status:** todo (P3.0) (`P3_GENERATOR.md` "Rules carried over").

### GD-23 Memory bugs surface far from their cause
- **Learned:** in the cgo layer, a bare `0` passed where `cv::calcHist` expects a `const int*` channels array was undefined behaviour that corrupted the heap and aborted much later inside an unrelated `VideoCapture` close; a `defer tracker.Close()` captured the pointer value, so the scene-cut path (which replaces the tracker) double-freed. Only the one test with scene-cut and appearance memory enabled caught either; the other four passed, "a green build with narrow option coverage proves less than it looks like".
- **Source:** `NEXT` "THE REAL PORT (`track.go` ...)".
- **Rule:** RAII (`unique_ptr`) for every tracker and capture, no raw literals for pointer parameters, option-matrix tests, and an AddressSanitizer/UBSan CI job on the tracking tests.
- **Status:** partly covered: CI job `sanitizers` runs all core tests under ASan + UBSan (clean on 257 cases, 17 Oct). Still todo (P3.0): the OpenCV layer and the app tests under sanitizers, RAII ownership checks in review, and `-Wall -Wextra` beyond `samn_core`.

### GD-24 Known limitation: weaker motion than the reference
- **Learned:** on the same film the median action jump was 6.5 vs 44.5, jumps above 50 points 0.0 % vs 34.7 %, time in the 40-60 band 33.3 % vs 14.2 %. The post-processing is not the cause (a synthetic stroke with varying speed reached intensity 162.1 vs an ideal 161.4); the measured quantity is: one region measures that area's displacement, while real material often depends on the relative motion of two bodies, which can be much larger. The exported script alone cannot settle it; the original video is needed.
- **Source:** `HANDOFF` "Known limitations".
- **Rule:** do not tune post-processing to close a gap that comes from the signal; compare amplitude statistics, not only r.
- **Status:** todo (P3.8).

### GD-25 Long-running jobs: cancel, sequence ids, no stale results
- **Learned:** many bugs of one family: generation could not be aborted (fix: context-cancel kills the child process and the native path checks it each frame); closing the app left training and the golden-clip benchmark running invisibly; a second start ran in parallel and both appended to one history file (now refused); Cancel during tip-find left Create locked; Cancel cleared a newer run; stale results from a previous video were applied; a superseded run unlocked the wrong Create; queues stayed stuck after a failure; ffmpeg frame grabs had no timeout; every spawned Python/ffmpeg call flashed a console window on Windows; CLI Ctrl-C did not reach tracking, ffmpeg or Python children; `runPhaseRepeat` left one channel's goroutine writing after the other failed.
- **Source:** `CL` 0.5.0, 0.5.10, 0.5.23, 0.5.43, 0.5.45 (several), 0.5.22; `COORD` 29-30 Sep bug hunts.
- **Rule:** every job has a cancel token, a run sequence id that all events carry, a single-instance guard and a "kill children on app quit" path; a failure always frees the UI; a cancelled run says so ("canceled", not an error).
- **Status:** todo (P3.3): the device layer already has a cancel hook (`app/src/device_io/qt_transport.h` `SetCancel`, `DeviceController::cancelDiagnostics`); no job runner exists yet.

---

## 5. Audio

### AU-01 Audio never invents the curve
- **Learned:** loudness is not a 0-100 position. The audio tempo check compares the script's stroke tempo (Hz) with the audio energy tempo after tracking and only warns (never changes actions or the Quality Doctor score); when the preview is weak or Quality Doctor fails and audio tempo is clear, the user gets a hint ("audio suggests about X Hz - check ROI/axis"). "No motion -> write from audio" was rejected. A pre-pass tempo hint only after the goldens show help > harm.
- **Source:** `docs/AUDIO_WORKFLOW.md`; `docs/GENERATE_HEURISTICS.md` "Audio". D-079.
- **Rule:** audio is a warning, a spacing hint or a vibration source, never a position source.
- **Status:** todo (P3.6). `core/include/samn/audio_envelope.h` gives the loudness envelope only.

### AU-02 Speech-Hold and segment labels are review hints
- **Learned:** classical multi-band short-time energy on the same decoded mono samples (impact about 50-250 Hz, speech 250-2000 Hz, high 2-4 kHz) labels segments `holding/gentle/intense/climax` and a `speech_hold_ms` total; no machine-learning tagger; they are stored in `metadata.audio_check`, which the companion export must preserve (FF-05).
- **Source:** `docs/AUDIO_WORKFLOW.md`; `CL` 0.5.44.
- **Rule:** display only; stay out of the curve.
- **Status:** todo (P3.6).

### AU-03 Decode in the app, cancel with the job
- **Learned:** the audio-check ffmpeg extract kept running after Generate was cancelled (#420); "Vibration from video audio only" drives vibration while suction stays on the script; the sound curve uses a 50 ms loudness envelope; BPM from audio uses the first 10 minutes.
- **Source:** `CL` 0.5.42; `docs/PLAY_UI_CONTRACT.md` section 6; `docs/PARITY.md`.
- **Rule:** one decoder with a cancel token; the envelope is cached per file.
- **Status:** covered for the envelope (`core/include/samn/audio_envelope.h`, `app/src/audio_level.cpp` Qt decoder cached per file; test `player_controller_test.cpp` `audioLevelFromSound`); todo (P3) for the audio check and (P1) for "BPM from audio".

### AU-04 Beat-bar videos: one stroke per beat
- **Learned:** the reader finds the strip itself (horizontal or at a side edge), turns every dot that hits the target into a stroke (down on the beat, up halfway, at most 500 units/s, pauses over 2 s rise once and hold) and never overwrites a script. Open: an audio-onset fallback for rounds without a visible bar and a beat-bar overlay in the Editor.
- **Source:** `R26` section 3b; `AS` "Done recently" (#552); D-075.
- **Rule:** keep the parity tests; the onset fallback is a separate, measured step.
- **Status:** covered for the reader and strokes (`core/include/samn/beatbar.h`, `beat_strokes.h`; `core/python/tests/test_beatbar_parity.py`, `test_beat_strokes_parity.py`); todo (P3/P4) for the onset fallback and the overlay.

---

## 6. Training

Two things share this name: the device **Training** mode (stop-start, plateau, scripts; P2) and the **AI training** of detectors (P4, with the Python tools of D-134). Both are `todo` unless stated: neither exists in this repo yet.

### TR-01 Multi-phase scripts with independent channels
- **Learned:** one technique plus one curve could not express "light vibration ramping up and down, then suction alone with light vibration". The model is an ordered list of phases, each with an optional vibration and suction curve (`start/peak/end level`, ramp up/hold/ramp down); a missing curve keeps the level the previous phase left. Stop-Start and Plateau became the two single-phase built-ins. A curve stored under the wrong channel field raced the real curve for the same physical channel; `NormalizeTrainingScript` closes it and runs unconditionally (editor and built-ins alike).
- **Source:** `docs/TRAINING_MODE_RESEARCH.md` "Follow-up design", "Implementation status"; `COORD` lane G handoff 22 Sep.
- **Rule:** normalize before save and before run; do not trust a field that can disagree with its slot.
- **Status:** covered (`core/include/samn/training.h` `Script` / `Phase` / `ChannelCurve`: optional curve per channel, an absent curve keeps the level; `training_test.cpp` "an untouched channel keeps its level into the next phase, then winds down", "built-in scripts are valid, described, categorised and level 1-3", "invalid requests are refused before the device is touched"; `validate_script` runs before a run, #37). Slots are typed, so a curve cannot sit in the wrong channel field and there is no separate `NormalizeTrainingScript`.

### TR-02 Arousal feedback is one factor applied to every active channel
- **Learned:** with two channels, "both get weaker" is no longer automatic. Compute the factors once per cycle (asymmetric: dampen hard on the way up, ease gently on the way down) and apply them to every active channel and to the shared rest. A report of 9 or more also interrupts the current cycle (done in the GUI layer, not inside the player, to avoid racing `drain()` at cycle start). A session ceiling (3 h) ends a mistyped session with a log line, not an error; Escape stops a cycle from anywhere in the tab.
- **Source:** `docs/TRAINING_MODE_RESEARCH.md` proposals B, C, D; `CL` 0.5.22 "Feedback scales StartLevel".
- **Rule:** a test asserts a high report weakens both channels and lengthens the shared rest; flat or descending curves must not resume above the damped peak.
- **Status:** covered (#37): `training_test.cpp` "feedback weakens both channels and lengthens the shared rest", "arousal adjusts peak, hold and rest asymmetrically", "scaled_curve keeps start and end proportional to the damped peak"; `training_controller_test.cpp` `feedbackIsValidatedAndHighValuesInterrupt` (9 or more interrupts, done in the controller), `theSafetyLimitEndsALongSession` (3 h ceiling). Escape interrupts a running cycle from anywhere on the page (`TrainingPage.qml` window shortcut, off while the mode deck or the editor is open), test `training_qml_test` `escapeInterruptsTheRunningCycle`.

### TR-03 Interrupt must always cut, in every state
- **Learned:** "Interrupt now" was ignored or mishandled in several states: during the ramp down (Stop-start) or the ramp to the plateau the level froze for the whole pause; in a script, a press during a pause with a channel still on was never seen (`waitOrDone`) and was dropped by the next repeat's `drain()`; one early interrupt switched off the closing wind-down of the whole session (a sticky flag); a stop during a suction-only phase did not zero a vibration level carried from an earlier phase ("nil curve = preserve previous" must still clear on interrupt).
- **Source:** `CL` 0.5.42 "Training: 'Interrupt now' ignored in the pause", "Training scripts: the ending was abrupt", 0.5.22 "Training Interrupt clears carried channels"; `COORD` 29 Sep "Bug hunt, training mode".
- **Rule:** one stop path that every wait honours; state tables tested for each (technique x state x interrupt).
- **Status:** covered (#37): `training_test.cpp` "stop_cycle interrupts the hold, zeroes, and the session continues", "...interrupts the ramp up without reaching the peak", "an Interrupt during the ramp down cuts to 0 and waits the full rest", "stop_cycle in a script zeroes both channels, also a carried one", "after an Interrupt the closing ramp is skipped", "an earlier Interrupt does not switch off the closing ramp of later repeats", "an Interrupt during an elevated rest ends the cycle at once"; `training_controller_test.cpp` `interruptEndsTheCycleAndTheSessionGoesOn`.

### TR-04 How a session ends, and where a plateau climbs from
- **Learned:** a script that finished with a channel above 0 sat there until the abrupt device stop; now both channels ramp to 0 over 2 s on a natural end, never after a user interrupt (which already cuts). Plateau (Owner, 29 Sep): each cycle climbs from the actual level (the plateau floor), not from 0; in scripts this is an opt-in `startFromCurrent` on the built-in plateau vibration curve only, other scripts keep their written jumps.
- **Source:** `CL` 0.5.27 "Fixed"; `COORD` decision log 23 Sep, 29 Sep "Plateau climbs from the plateau".
- **Rule:** explicit per-curve flag instead of a global behaviour change.
- **Status:** covered (#37): `core/src/training.cpp` `Stage::Closing` winds elevated channels to 0 over 2 s on a natural end only; `training_test.cpp` "an untouched channel keeps its level into the next phase, then winds down", "plateau: the next cycle climbs from the plateau, not from 0", "start_from_current climbs from the plateau and survives scaling" (`ChannelCurve::start_from_current` is the per-curve flag).

### TR-05 Real-hardware feedback changed the built-in scripts
- **Learned:** four built-ins (`stop-start`, `plateau`, `vibration-massage`, `variable`) never touched suction at all (not a design choice, the Owner said); every built-in now has a light suction layer from the start. A 20 s static hold at 0.7 suction was too much and became 6 s at 0.6. Training had only ever run against the mock. Numeric levels ("light" = 0.2) are starting guesses until the device is measured; a learned intensity model must not be built before that measurement.
- **Source:** `CL` 0.5.27 "Training scripts: suction no longer missing or stuck"; `COORD` decision log 23 Sep "Training suction fixes"; `docs/TRAINING_MODE_RESEARCH.md` proposal F.
- **Rule:** tests assert every built-in has a suction layer; hardware feedback from the Owner outranks design intent.
- **Status:** covered (#37): `training_test.cpp` "no built-in holds suction long at a high level; every built-in drives suction", "built-in scripts are valid, described, categorised and level 1-3". The numeric levels are still starting guesses until the Owner hardware test (DV-06).

### TR-06 Concurrency inside a session
- **Learned:** `runPhaseRepeat` returned on the first channel error without draining or cancelling the sibling, so one channel's goroutine could keep writing after `Stop()`; late `levels` events after `done` revived the meter ring; a start that finished before its promise resolved had to leave the session stopped; a rejected second start must not overwrite the live session; the session log was opened before `tryStartSession` succeeded (an empty file and an open handle after a rejected start).
- **Source:** `CL` 0.5.22, 0.5.23, 0.5.10; `COORD` 29 Sep "Bug hunt, training GUI".
- **Rule:** a shared stop token for both channel workers and a join of both before returning; open logs after the session is accepted; ignore events whose session id is not current.
- **Status:** covered (#37): the engine is a single-threaded state machine driven by `advance(dt)`, so there are no sibling workers to join and no shared stop token is needed; `TrainingController::finishSession` drops the session and zeroes the meter, `beginSession` opens the log only after the start is accepted; tests `startIsIgnoredWhileActiveAndStopWhenIdleIsSilent`, `unknownProgramDoesNotStart`, `aConnectThatNeverAnswersEndsWithAnError`, `pauseHoldsTheDeviceAtZeroAndResumeContinues`.

### TR-07 History feeds the next session only with visible, reversible consent
- **Learned:** the session history (cycles, mean peak, early stops, mean arousal) first only displayed. The nudge now is explicit: difficulty tier Gentle x0.8 / Standard x1 / Intense x1.2 and a history factor (ease-off after early stops x0.85, clean streak x1.1), combined and clamped to [0.55, 1.35], applied to the real run and the plan preview alike; an opt-out checkbox (default on). Tiers are a scale factor, not three copies of each script.
- **Source:** `docs/TRAINING_MODE_RESEARCH.md` proposal A; `HANDOFF` "Training history".
- **Rule:** preview shows the curve that will actually run.
- **Status:** covered (#37): `training_test.cpp` "history adjustment: ease off after an early stop, nudge up after clean sessions", "combined intensity is tier x history, clamped 0.55-1.35, 1 for Custom"; `training_controller_test.cpp` `tierScalesTheProgramAndNamesIt`, `historyNudgesTheNextSessionAndExportsCsv`; the opt-out is the `autoAdjust` toggle in `TrainingPage.qml`; the plan preview uses the scaled script.

### TR-08 Pause is a real pause; training follows the device
- **Learned:** Play/Pause in Training sends both channels to 0 at once and the session clock stands still; Resume restores the levels. The stage shows a ring only for channels the device has, and a device with neither vibration nor suction cannot start (the mock can). The live meter comes from the levels actually written to the device, not from the plan.
- **Source:** `CL` 0.7.0 "Changed" (Training); 0.5.22 "Live intensity for ring + clip".
- **Rule:** meters show what was sent.
- **Status:** covered (#37): `training_test.cpp` "pause holds both channels at 0, the clock stands still, resume restores the level", "on_levels reports every write, and 0 while paused"; `training_controller_test.cpp` `pauseHoldsTheDeviceAtZeroAndResumeContinues`, `aOneMotorDeviceLeavesOneChannel`, `aStrokerBlocksStartUnlessMock`; the meter is fed by `session_->on_levels` in `TrainingController::beginSession`.

### TR-09 Ideas that were deliberately not built
- **Learned:** random jitter of cycle timing (clinical protocols are regular; it would hide the cause of a history change; an explicit opt-in "Variable" preset is a different thing), merging Training with video playback (it is a content-independent control loop), biofeedback hardware as an arousal input (no evidence it beats self-report; a second Bluetooth peripheral).
- **Source:** `docs/TRAINING_MODE_RESEARCH.md` "Explicitly not proposed, and why".
- **Rule:** do not re-open without a stated reason and a measurement.
- **Status:** n/a (decided against; nothing to build).

### TR-10 AI-training data hygiene
- **Learned:** only reviewed samples train (auto-labels start `reviewed:false` and stay out until accepted); the bootstrap box size is fixed, so boxes must be corrected in review; a missing validation (or train) split crashed training, so one image + label is copied over and zero labels is a clear error; the batch size was fixed at 16 and ran out of memory on a 4 GB card (`batch=-1` auto-sizes); split by clip when there are at least 3 clips, else by frame with a warning (adjacent frames must not straddle the split); class IDs are canonical (aliases normalized, scene-role typing missed IDs that marks already emitted); review thumbnails lost their boxes because label paths were rebuilt by splitting on `/` (FF-12); an empty dataset must fail fast with a clear message, not a raw traceback.
- **Source:** `CL` 0.4.1, 0.4.2, 0.5.10, 0.5.36, 0.5.43, 0.5.44; `docs/KI_TRAINING.md` "Tips"; `docs/VLM_MODELS.md` "The training loop, as built (V3)".
- **Rule:** the gate for "trains on this" is a flag, not a folder name.
- **Status:** todo (P4).

### TR-11 Training environments and long runs
- **Learned:** training pulls a deep-learning framework and replaces the computer-vision package (CSRT disappears, GD-02), so it lives in its own environment; heavy optional packages are checked up front and the button stays disabled with an install hint; the Windows Store Python stub must not be mistaken for a real interpreter; training runs for minutes to hours, so it needs cancel and must be killed at app close (GD-25); GPU backends are chosen automatically (CUDA, DirectML, MPS, CPU).
- **Source:** `CL` 0.4.1, 0.5.11, 0.5.45 "AI Train: training could not be stopped"; `docs/KI_TRAINING.md`; `docs/next/inventory/ENG-01_PYTHON_RUNTIME.md`.
- **Rule:** training tools stay in `tools/ai/` (D-134) with their own environment; the app never imports them.
- **Status:** todo (P4).

### TR-12 Learning data is opt-in, local, deletable
- **Learned:** learning export is off by default, writes JSON/JSONL under one folder only, has a delete button, and is not the detector training set (that needs reviewed marks). Scene-map scan runs on a button only and marks default to the current scene.
- **Source:** `docs/KI_TRAINING.md` "Folders"; `docs/owner/issue-290.md`; `docs/ENGINEERING_STANCE.md`. D-029, D-062.
- **Rule:** nothing is collected, sent or downloaded unasked.
- **Status:** todo (P4).

---

## 7. GUI / look

### UI-01 QML shows, controllers decide, hooks are a contract
- **Learned:** the 0.7 Play page was split between "core" (data, drawing, device logic, tests) and "layout" (look) with a written contract of regions, state attributes, theme tokens and ids; layout work relied only on what the contract lists and tests pinned it. Fixed DOM order = tab order.
- **Source:** `docs/PLAY_UI_CONTRACT.md` sections 1-5; `docs/QA_CHECK_2026-10-11.md` section 3.
- **Rule:** no logic in QML; controllers expose everything; a layout change never edits a controller.
- **Status:** covered (rule in `docs/MODULES.md` "Rules", `docs/PLAN.md` principle 4; QML load tests `tests/app/inspector_qml_test.cpp`, `player_page_qml_test.cpp`, `training_qml_test.cpp`, `shell_qml_test.cpp` `everyModeLoadsWithoutWarnings`).

### UI-02 Only real functions
- **Learned:** an audit found 333 controls all wired but 12 backend bindings never called by the GUI (superseded or internal); the Owner's mockup contained controls with no backend (axis 1-3 for a device with none, a frequency knob, picture-in-picture, a frameless window) that were mapped as "exists / exists under another name / needs work / not supported" and not shown as working. "GUI load rule": a shipped feature must appear in the UI and every list shows real data.
- **Source:** `docs/GUI_AUDIT_2026-10.md` sections 1-2; `docs/PLAY_UI_CONTRACT.md` section 6. D-080, D-093.
- **Rule:** a mode or control that is not built says so; every `PARITY.md` tick has a test or screenshot.
- **Status:** covered (`app/qml/NotYetPage.qml`; `docs/PARITY.md` tick rule).

### UI-03 Progressive disclosure for the Generator
- **Learned:** the Advanced panel grew into a flat list: useful opt-ins next to a dead dropdown (tracking method had one option), the same switch in two places (impulse), and expert numbers (smoothing, RDP) that Everyday seldom needs. Fix: groups, an Expert section closed by default, the dead control hidden, one source of truth for duplicates, "AI draft" collapsed, defaults and payload ids unchanged.
- **Source:** `docs/owner/advanced-generator-audit.md`; `NEXT` priority 4.
- **Rule:** an option needs a home and a default before it ships; hide dead controls.
- **Status:** todo (P3.3).

### UI-04 English only, and stale copy
- **Learned:** leftover German strings were fixed at least six times (0.5.6, 0.5.7, 0.5.10, 0.5.45 twice, cancel messages, training errors, update texts), and after the tabs were renamed, texts still sent users to a "Device tab", "Create tab" or "left rail" (nine such strings, 11 Oct).
- **Source:** `CL` 0.5.7, 0.5.10, 0.5.45; `docs/QA_CHECK_2026-10-11.md` bug 7; `docs/LANGUAGE.md`. D-089.
- **Rule:** every user-reachable string is English and goes through `tr()`; renaming a mode means grepping all copy.
- **Status:** covered (#35): core error and diagnostics texts are English; tests updated.

### UI-05 Text from files and users is plain text, never markup
- **Learned:** a file name containing `<` broke the Create queue list (names must always be shown as text).
- **Source:** `CL` 0.7.0 "Create queue: a file name containing `<` no longer breaks the list".
- **Rule:** every `Text`/`Label` that shows a file name, script title, chapter, bookmark or marker name sets `textFormat: Text.PlainText`; add a test with a name like `a<b>c.funscript`.
- **Status:** covered (#35): every `Text` is `PlainText` (the Script Doctor card is `StyledText` by design); `player_page_qml_test` fails on any `AutoText`.

### UI-06 Layout at 1366x768 and 1920x1080
- **Learned:** at 1366x768 the Play core view (curve, lanes, transport) fell below the fold; optional panels collapse, never the video, the timeline or Stop. UI acceptance needs native Windows 11 screenshots at both sizes (the cloud only had browser captures).
- **Source:** `docs/QA_CHECK_2026-10-11.md` bug 9; `docs/FREE_RELEASE_QA_AND_CLEANUP_2026-10-12.md` G3. D-092.
- **Rule:** layout tests at both sizes; Stop is always reachable.
- **Status:** todo (P5). `Main.qml` sets a minimum of 960x640 (0.7: 880x560); no test checks the fit.

### UI-07 Animation must not move what it decorates, and must be switchable
- **Learned:** a float animation moved the play mark of the empty Player to the top-left corner so only a corner showed (0.7.0); a continuously running animation made the test tool's "element is stable" check never pass (cosmetic drift, found by disabling all animations and re-sampling); a 3D tilt needs perspective on the parent and the rotation on the child. Reduced motion is a requirement (D-092).
- **Source:** `CL` 0.7.0 "Empty Player / Create stage"; `HANDOFF` "Pitfalls", "Reusable GUI pattern".
- **Rule:** animate transforms around a fixed anchor; a test hook switches animations off; honour a reduced-motion setting.
- **Status:** covered (Settings → General "Reduce motion", `Theme.reducedMotion` / `ui.reduced_motion`, `SAMN_NO_ANIM=1` for tests; decorative loops and `Theme.animFast`/`animNormal` honour it).

### UI-08 Keyboard, focus, contrast
- **Learned:** 0.7 had arrow-key/Home/End tab navigation, focus states and an accessibility pass (0.5.6); D-092 requires keyboard, contrast, focus and reduced motion.
- **Source:** `docs/PARITY.md` section 1 (main tabs); `CL` 0.5.6; D-092.
- **Rule:** every `Flow*` control is focusable, shows focus and has an `Accessible` role/name.
- **Status:** todo (P4/P5). In `app/qml/` only `FlowTextField` and `FlowCombo` read `activeFocus`; there is no `activeFocusOnTab`, `Keys` or `Accessible` anywhere.

### UI-09 Resources must be in the package
- **Learned:** the logo and the partner graphic pointed at source paths that were not in the build (broken images in the packaged app, 0.6.0); inactive panels showed through because a CSS `display` rule overrode `hidden`.
- **Source:** `CL` 0.6.0 "Fixed".
- **Rule:** assets come from the resource system; the smoke test fails on a QML warning, a missing image or an `Image.Error`; run it on the installed layout too (BC-04).
- **Status:** todo (P5). The CI smoke test runs on the build tree and fails on a broken QML file; there is no installed-layout run. (The CSS part is n/a: QML `visible: false` also removes input.)

### UI-10 Dense scripts are drawn as a calm wave
- **Learned:** strokes closer than they can be read looked like a solid block or a zig-zag of dots. 0.7 draws a labelled tempo wave, a fine line without glow when dense, dots only while editing or zoomed in, and the real strokes return on zoom.
- **Source:** `CL` 0.7.0 "Player curve, dense scripts"; D-087.
- **Rule:** level of detail by density, binary search not full scans.
- **Status:** covered (`core/include/samn/curve_geometry.h`, `app/src/curve_item.cpp`; `tests/core/curve_geometry_test.cpp`, `player_controller_test.cpp` `curveModes`).

### UI-11 Keep UI files small
- **Learned:** `generator.js` (5059 lines) and `playback.js` (4102) were the weak spots; the proposed split (curve/lanes/editor, markers, audio) was never done because it needs a green suite before and after.
- **Source:** `docs/GUI_AUDIT_2026-10.md` section 3.
- **Rule:** one controller and one page per area; split before P3 adds the Generator and Editor.
- **Status:** todo (P3). Today `player_controller.cpp` has 1020 lines, `device_controller.cpp` 947, `PlayerPage.qml` 870, `samn_file.cpp` 1825.

### UI-12 A live two-value meter (the Training ring)
- **Learned:** nested groups so a static rotation and an animated transform do not replace each other; one shared full-circle track for two overlapping values; alpha blending (reduced opacity on the top layer), not a screen blend that washes two pastel colours to white; colour families per value; amplitude driven by the live value, not by swapping classes; perspective on the parent plus rotation on the child for a real 3D tilt; the local channel (vibration) animates its own element, the whole-object channel (suction) animates the shared wrapper.
- **Source:** `HANDOFF` "Reusable GUI pattern: the training-tab intensity ring".
- **Rule:** reuse for the Training page; do not re-derive.
- **Status:** covered (#37): `app/qml/TrainingRing.qml` (one glowing ring per channel, level arc, live state, redrawn on level change) used by `TrainingPage.qml`; `training_qml_test.cpp` counts the visible rings. The CSS tricks of the lesson (nested groups, 3D tilt) do not apply to a QML `Shape`.

### UI-13 Screenshots and design references are test patterns only
- **Learned:** the Owner's mockup contained sexualized imagery and was therefore not committed (the acceptance rule: no unsafe mockup media); screenshots come from the real build with test patterns.
- **Source:** `docs/PLAY_UI_CONTRACT.md` section 6; `AS` "Never". D-096, D-119.
- **Rule:** the `SAMN_SCREENSHOT` hook on test patterns is the only way screenshots enter the repo.
- **Status:** covered (`app/src/main.cpp` hook; `AGENT_STATE.md` "Never").

---

## 8. Build / CI / Windows

### BC-01 Test every push, on both systems, from day one
- **Learned:** until late, tests ran only at release time; with several contributors that shows problems long after the context of the code is forgotten. POSIX-only calls (`os.fchmod`) broke on Windows. The Windows OpenCV job was needed to see CSRT problems.
- **Source:** `HANDOFF` "Collaboration"; `docs/ROADMAP.md` (here) lessons.
- **Rule:** CI on push and PR for Linux and Windows.
- **Status:** covered (`.github/workflows/ci.yml`, ubuntu + windows; `core/src/fileio.cpp` has both POSIX and Windows paths).

### BC-02 A skipped test is a failed test, and a test must be able to fail
- **Learned:** a skipped native test exited 0 and hid a missing module. Tests also passed for the wrong reason: the "edition" test had its controls in closed sections and did not load the real CSS; canvas tests clicked elements that were off-screen. Rule from `CONTRIB`: write the test, see it pass, revert the change, see it fail, restore.
- **Source:** `AS` M1; `docs/QA_CHECK_2026-10-11.md` bugs 2 and 6; `CONTRIB`. D-113, D-123.
- **Rule:** `SAMN_NATIVE_REQUIRED=1`; no "SKIP with exit 0"; every behaviour PR shows the test failing without the change.
- **Status:** covered for the CI rule (`ci.yml` `SAMN_NATIVE_REQUIRED`); the fail-without-change check is a review rule (PR-01).

### BC-03 CI environment gotchas
- **Learned:** the CMake generator must not be hardcoded (it broke on the runner; handle `build/Release` multi-config explicitly); Qt Multimedia needs the system libraries (`libpulse0`, `libegl1`, ...) on Linux; a test that crashes on Windows loses buffered output, so failed app tests are re-run function by function; an arousal-report test raced `takeArousal` (report before starting the session) and a stroke-preview test depended on encoder threads.
- **Source:** `docs/ROADMAP.md` (here) lessons; `.github/workflows/ci.yml`; `CL` 0.5.22, 0.5.46.
- **Rule:** keep those steps; fix the cause of a flaky test, never retry it.
- **Status:** covered (`ci.yml`: default generator, system libraries, per-function re-run on failure).

### BC-04 The package must contain everything the source tree has
- **Learned:** four Python modules were embedded but only two were written to the temporary directory. Invisible in the source tree, it caused three unrelated failures in the packaged executable (`--backend flow`, the learned model never found, device checking silently skipped inside a `try/except`). The fix reads the real imports and checks each reaches the temp dir; a hand-kept list would repeat the mistake. The training tab's clip frames were also missing from the portable build (directory URL instead of one `new URL` per file).
- **Source:** `HANDOFF` "Fixed bugs affecting users"; `CL` 0.5.23; `docs/TEAM_STAND.md` section 3.
- **Rule:** the manifest is computed (Qt deployment tool, `qmlimportscanner`), then a test starts the installed layout and exercises each feature that imports a plugin (multimedia, bluetooth, serial, websockets, ONNX).
- **Status:** todo (P5).

### BC-05 One version source, and tags are not moved
- **Learned:** `VERSION` and `update.BaseVersion` had to be equal (a test checked it); dev builds show `-dev`, release builds get the tag version at link time; the release workflow stops if tag, `VERSION` and `BaseVersion` disagree. Versions were bumped and never tagged (0.4.1, 0.5.32, 0.5.46, 0.6.0): their content shipped in the next tag and CHANGELOG sections stayed as bookkeeping. Published tags are never moved. A successful build does not replace a hardware test or a self-update test with an installed app. Fixes that landed under an already released heading had to be moved to Unreleased (11 entries).
- **Source:** `CONTRIB` "Versions"; `COORD` 27 Sep Rel33 prep, 29 Sep; `docs/FREE_RELEASE_QA_AND_CLEANUP_2026-10-12.md`.
- **Rule:** CMake `project(VERSION)` feeds the app; CI checks tag = version; new fixes go under "Unreleased".
- **Status:** todo (P5). `CMakeLists.txt` has `VERSION 2.0.0` and nothing reads it at run time.

### BC-06 Internal files never reach a public release
- **Learned:** the release workflow would have attached the internal build to the public release (first release since 0.6.0); artifacts of a public repository are downloadable by any logged-in user, so "upload as artifact" is not private. Fix: compile-check only, drop it from uploads/checksums/release files, and a guard step that fails if a file to publish has a forbidden name. Also never publish datasets, models or benchmarks by accident.
- **Source:** `HO15` task 1; `docs/next/specs/N1_RELEASES.md`; `docs/FREE_RELEASE_QA_AND_CLEANUP_2026-10-12.md` cleanup 5. D-010.
- **Rule:** publish from an allow-list of file names; a dry-run job lists the publish set. With D-127 there is no internal edition, but golden clips, models and benchmark data still must not ship.
- **Status:** todo (P5).

### BC-07 The updater
- **Learned:** matching assets by name suffix picked the wrong edition (`...-suite-gui-...` ends like `...-gui-...`): match the exact name. A missing or empty release means "no update", not a warning. The startup check failed silently for years (an empty `catch`); now it logs and a button shows the real result. No blocking browser `confirm()`; on Windows a title of `//` came from a nested `cmd /C` quote: apply and restart through a temp `.cmd` helper; reject slash-only error text. Never hide a full update behind a patch. Installed apps check the repository they were built with, so moving releases needs a bridge release first.
- **Source:** `CL` 0.6.0, 0.5.10, 0.5.38; `HANDOFF` "Fixed bugs"; `docs/owner/update-popup-fix.md`; `docs/owner/patch-system.md`; `docs/next/specs/N1_RELEASES.md`. D-012, D-017.
- **Rule:** exact asset name; visible errors; no shell quoting; test "release without our asset = no update, no error dialog".
- **Status:** todo (P5).

### BC-08 OpenCV on Windows
- **Learned:** cross-compiling OpenCV with the tracking module from Ubuntu to MinGW was painful; the proven path is building on a Windows runner (MSYS2) and shipping the DLLs beside the exe. `libgfortran` must be installed before `opencv` (pacman silently skipped OpenCV after a package rename); the DLL collector must run in the MinGW shell, not Git Bash; Linux and Windows used different pkg-config names (opencv4 vs opencv5). Until Windows linked OpenCV, Windows ran a weaker path (GD-02). A Go OpenCV binding (GoCV) failed to compile because its contrib package pulled unrelated modules (a missing header, a newer API).
- **Source:** `docs/WINDOWS_OPENCV.md`; `CL` 0.5.16; `NEXT` Go-native section.
- **Rule:** one OpenCV, built the same way on both systems, with the tracking module; a CI step creates a tracker and loads an ONNX model there.
- **Status:** todo (P3.0). `P3_GENERATOR.md` plans vcpkg `opencv4[contrib,dnn,ffmpeg]` with a binary cache; check that the contrib `tracking` module is in that feature set.

### BC-09 No console windows from child processes
- **Learned:** every Python/ffmpeg/ffprobe call flashed a console window on Windows (about 11 call sites); fixed with one `command()` wrapper that sets the hide flag, used everywhere.
- **Source:** `NEXT` priority 4 ("Also fixed ..."); `CL` 0.5.10 "ffmpeg console flash".
- **Rule:** a single process-launch helper (`CREATE_NO_WINDOW`) with timeout and kill (GD-25).
- **Status:** todo (P3). No `QProcess` exists in `app/` or `core/` yet.

### BC-10 Do not leak the build machine's paths
- **Learned:** without `-trimpath` the build machine's path, including the user name, is embedded in the executable.
- **Source:** `HANDOFF` "Pitfalls"; `CONTRIB`.
- **Rule:** `-ffile-prefix-map` / MSVC `/pathmap` and no debug paths in release binaries.
- **Status:** todo (P5). No prefix-map option in any `CMakeLists.txt`.

### BC-11 Licences and notices ship with the app
- **Learned:** the Qt toolkit is LGPL and shipped as shared libraries, unmodified; FFmpeg builds, OpenCV and fonts have notices; the EULA is shown once per version on first start; donation links stay hidden until a URL is set.
- **Source:** `CL` 0.5.46/0.6.0; `docs/CLOSED_SOURCE.md`. D-020, D-021, D-022.
- **Rule:** third-party notices are a build output, checked in CI.
- **Status:** todo (P5).

### BC-12 A fresh clone must build
- **Learned:** the Wails GUI embedded an untracked build directory, so a fresh clone failed with "no matching files found" until the frontend was built; a build harness and a binary were committed by accident (#467).
- **Source:** `CONTRIB` "After cloning"; `docs/QA_CHECK_2026-10-11.md` bug 3.
- **Rule:** the build starts from a clean checkout; generated files are untracked and produced by CMake.
- **Status:** covered (`CMakeLists.txt` `FetchContent` with `URL_HASH`, vendored `third_party/doctest`; every CI run is a clean checkout). `.gitignore` exists; no check for accidentally committed binaries.

### BC-13 Error tracking is for development builds
- **Learned:** Sentry may be used for development builds only and must never be in the shipped app; it needs a project and DSN from the Owner.
- **Source:** `AS` "Open" item 7. D-030.
- **Rule:** compile-time switch, off in release.
- **Status:** todo (P5).

---

## 9. Performance

### PF-01 Profile before porting: where the time goes
- **Learned:** real 256x144 42 s clip (2527 frames), single ROI, no cache: about 49 s wall, CSRT `update()` 44.4 s (about 90 %), frame decode 0.54 s, camera motion 2.2 s, imports 1.4 s; in the summary table CSRT is about 100 ms/frame and 97.5 % of the run, decode 0.7 %. Synthetic 1080p, 30 fps, 60 s (1800 frames), 4 cores: 174 s (2.9x slower than real time): CSRT 81 s (46 %), camera-shake estimate 71 s (41 %), scene-cut check 17 s (10 %), frame read 4 s, Go code under 0.1 s; FFmpeg decode used another 53 s of CPU in its own threads. So about 99 % was already C/C++ and a rewrite of the glue cannot speed it up. Candidates, each behind the gate: camera estimate on a second thread (same result by construction, up to about -40 % wall on 4 cores), scene-cut check on a small picture (size barely matters), CSRT on a cropped region (measure quality first).
- **Source:** `NEXT` priority 8; `docs/owner/generator-profile-2026-10.md`; `HANDOFF` "Measured performance". D-070.
- **Rule:** SamNPlayer 2 is C++, so the language gives nothing; speed comes from what runs, at which size, on how many threads. The real ranking on the Owner's golden clips is still unmeasured (the numbers above are synthetic).
- **Status:** todo (P3.8): add per-stage timers to the pipeline from P3.1 and take the Owner-clip profile.

### PF-02 The performance gate
- **Learned:** a speed change is accepted only if quality is equal first and time is better second: 3 warm-up and 5 measured runs, median and p95, peak RAM, cold load reported separately, GPU work synchronized when timing. Quality noise is the spread of r over the 5 runs; no clip's median r may drop by more than that. A half-size camera estimate (-39 % time) failed it (REJ-24).
- **Source:** `docs/next/specs/P4_GOLDEN_BENCHMARK.md`; `docs/MT_SPEED_NOTES.md` "Reproducible measurement handoff". D-068.
- **Rule:** no "faster" claim in a PR without the gate output.
- **Status:** todo (P3.8).

### PF-03 CPU, RAM and GPU: what the measurements say
- **Learned:** one CSRT `update()` already uses about 2.2 of 4 cores through OpenCV's own thread pool, so Python-level parallelism had no spare capacity (REJ-13); more cores should scale automatically (not verified beyond 4); RAM is not a lever (compute-bound, small frames); stock OpenCV has no GPU CSRT and the pip wheels have no CUDA at all; filters, optical flow and DNN do have GPU variants. "Use the machine" means measured useful work.
- **Source:** `NEXT` priority 8 ("Asked directly"); `docs/PLATFORMS.md` "CPU / GPU / RAM".
- **Rule:** never claim CSRT is GPU-bound; GPU is for DNN inference and filters, with a CPU fallback and identical results.
- **Status:** todo (P3.7).

### PF-04 Speed claims need resolution and hardware
- **Learned:** "flow is about 4x faster" and "18 ms/frame" were wrong at 720p (GD-21). A cache hit was 36x faster (31.7 s -> 0.88 s). A documented figure without its resolution and machine is not a bound.
- **Source:** `HANDOFF` "Measured performance"; `COORD` Lane E findings.
- **Rule:** write numbers with clip size, frame count, machine and threads.
- **Status:** todo (P3.8).

### PF-05 Caches carry the algorithm version
- **Learned:** changing `track_roi` or camera compensation without bumping `TRACK_CACHE_VERSION` made the cache return results from the old code, silently.
- **Source:** `HANDOFF` "Pitfalls"; `CONTRIB`.
- **Rule:** the cache key is content hash + algorithm version + every option; a test changes one option and expects a miss.
- **Status:** covered for raw tracks (D-158: `engine/src/track_cache.cpp`, key = content of the first and last MiB + size + time + `kTrackCacheVersion` + OpenCV version + every `TrackOptions` field; test `tests/engine/spec_generator_test.cpp` "spec FR-11" changes one option and expects a miss). The audio level is cached per file (`app/src/audio_level.cpp`).

### PF-06 Interactive drawing at scale
- **Learned:** the curve uses binary search instead of full scans; a static layer is cached and playing only moves the playhead; the editor must stay smooth for a 1-hour video with 100 000+ points at 1366x768 and 1920x1080 (E10). Flexible parsing cost time (200 000 actions 0.13 s -> 0.28 s), so parse stays one pass.
- **Source:** `docs/MODULES.md` (`curve_geometry.h`); `R26` section 4 E10; `COORD` 30 Sep. D-083.
- **Rule:** a benchmark with 100 000 points guards the Editor.
- **Status:** todo (P4): drawing is covered (`curve_item.cpp` layer cache, `curve_geometry.cpp`); no large-script benchmark exists.

### PF-07 Read frames sparsely when you only need a few
- **Learned:** the scene-map scan reads a few windows (about 6 x 8 s), not the whole clip; training stills and previews need random access, with accurate seeking (GD-07), cancel-safe child processes and bounded buffers.
- **Source:** `docs/ENGINEERING_STANCE.md` "Bewirtschaften" B1, B2.
- **Rule:** `FrameReader::ReadAt(pts)`; it must match the ffmpeg seek cost on `clip_voll` before replacing it.
- **Status:** todo (P3.0).

### PF-08 Detection cadence is not tracking
- **Learned:** running a detector every N frames is not free tracking in between: a tracker called once per sampled image counts processed updates, not source frames, so a lost-track buffer of 30 updates spans about four seconds at N=4 and 30 fps. Budget: mean cost per source frame is about `D + (M + A + U)/N` (decode, model, association, transport). Provisional target: p95 frame-PTS-to-proposal age of 250 ms with no unbounded queue; keep the model loaded; measure decode, resize, copies and postprocessing, not only inference; report loss durations in milliseconds from PTS.
- **Source:** `docs/MT_SPEED_NOTES.md`.
- **Rule:** store both source-frame index and processed-update index; expire stale proposals.
- **Status:** todo (P3.7).

### PF-09 Bound memory
- **Learned:** an unvalidated time (FF-01) ran 0.7 out of memory; frame buffers and incoming message queues need bounds; cache directories live under the user config with a clear-on-exit setting.
- **Source:** `CL` 0.7.0; `docs/PLATFORMS.md` "CPU / GPU / RAM".
- **Rule:** every queue and buffer has a limit.
- **Status:** todo (P3).

---

## 10. Process

### PR-01 A behaviour change needs a test that fails without it
- **Learned:** twice a feature was implemented and did nothing (an insertion in the wrong file; a parameter with no effect anywhere). Measurements exposed both. The procedure: write the test, see it pass, revert the change, see it fail, restore it. Documentation-only changes need verified facts, not artificial tests.
- **Source:** `CONTRIB` "What a contribution must include".
- **Rule:** the PR says how the negative check was done.
- **Status:** todo (P1): a review rule; nothing in CI enforces it. (`docs/TEST_PARITY.md` exists for ports.)

### PR-02 Measurements, not impressions
- **Learned:** plausible explanations were wrong again and again: camera compensation was not broken, the test material was; smoothing did not destroy amplitude; an extra estimator made the result worse; the VFR-drift explanation (GD-06); the "half period" explanation of a silent flag; "the flow backend hangs" (it was slow, GD-21); the first reading of a whole-clip r of 0.06 as "Tf/Tj is noise".
- **Source:** `CONTRIB`; `HANDOFF` "Guiding principles"; `FTT`.
- **Rule:** state a hypothesis, then the cheapest direct measurement that could refute it, before building on it.
- **Status:** covered (D-125 "Nothing is claimed as built unless it is built"; D-135 "measured, never assumed").

### PR-03 Document negative results
- **Learned:** rejected ideas go into one table with the measurement so nobody repeats the experiment three months later.
- **Source:** `CONTRIB`; `CL` 0.3.0 "Investigated and explicitly not shipped".
- **Rule:** section 11 of this file; add to it in the same PR that rejects an approach.
- **Status:** covered (this file, section 11).

### PR-04 Do not add unused functionality
- **Learned:** `fusion.py` (tested, never connected: fusion always lands between its sources) and `videox` (a second external binary without a demonstrated benefit over the OpenCV decode path) stayed in the old tree with documented reasons as "an exception, not the intended pattern". The rule from the target concept: no module without a demonstrably improved processing path. Same for the self-build gate: equal or better on clips, else keep the existing path; a lean fallback (e.g. the MP4 probe) is a spare tire, never a silent downgrade.
- **Source:** `HANDOFF` "Why `fusion.py` and `videox` are not connected"; `CONTRIB`; `docs/SELF_BUILD.md`. D-024.
- **Rule:** every module has a user or a gate; every dependency a reason (`PLAN.md` lists each).
- **Status:** covered (`docs/PLAN.md` "Dependencies, each with a reason"; D-024).

### PR-05 Verify against code; docs go stale
- **Learned:** old chat logs and plans overstated: a "next step" paragraph stayed after it was done; a GUI hint claimed 4x for a 1.9x backend; an `18 ms/frame` docstring; a "Stale as of ..." correction was needed in the task list; the training doc said suction needed no change while hardware said otherwise; the architecture doc described the plugin host as a product that was cancelled. When sources disagree, code and reproducible tests win; state the version and date of a fact.
- **Source:** `CONTRIB` "Which document is authoritative?"; `NEXT` (several "Correction" notes).
- **Rule:** one authoritative place per fact; update it in the PR that changes it.
- **Status:** covered (`docs/AGENT_STATE.md` rule "update in every PR"; `DECISIONS.md` marks conflicts).

### PR-06 Gate unmeasured merges; one clip is a direction, not a result
- **Learned:** the per-shot target lock (#248) was merged "without golden-clip measurement per its own PR text" and regressed both golden clips (GD-15). A default change needs at least 4-5 real clips with r up and no orientation regression, judged where the references agree; one clip is directional ("one data point does not establish a trend"). Golden clips cannot show the value of a feature that fixes a failure they do not have (GD-16). The two references of one clip agree with each other only at r about 0.45.
- **Source:** `COORD` decision log 26-27 Sep; `NEXT` priority 2; `docs/ENGINEERING_STANCE.md` "Owner gates". D-061, D-122, D-135.
- **Rule:** a tracker/engine default changes only with the golden table in the PR and an announcement in the changelog.
- **Status:** todo (P3.8).

### PR-07 Fair comparisons or no comparison
- **Learned:** comparative claims against FunGen need identical clips, identical cuts, identical ROIs, and for classical vs classical the reference run with its own detector disabled. Absolute correlation is not the goal (opposite phase is not equivalent). Do not remove the device range restriction to improve a metric (distance profiles span 20-90, references 0-100).
- **Source:** `docs/FUNGEN_PARITY_PLAN.md`; `NEXT` (Sep 16 `region_fusion`). D-125.
- **Rule:** see GD-05.
- **Status:** todo (P3.8).

### PR-08 Third-party code policy
- **Learned:** FunGen 1 is PolyForm Strict (no reuse, and no structurally equivalent ports), FunGen 2 is closed; ScriptPlayer is GPL, BetterPlayer BSL/GPL, ScriptPlayer+ closed (ideas only, own versions); Funscript Flow Apache-2.0 (reuse with attribution); funscript-utils and launchcontrol permissive (metrics adopted); Buttplug BSD-3 (protocol facts yes, code with attribution); MultiFunPlayer MIT only with a notice. Public descriptions of behaviour may inform independent designs. This is the project's policy, not a legal assessment. Salvaged code was reviewed per package and parts with measured flaws were left out (a camera-signal integrator, a smoothness-as-periodicity measure).
- **Source:** `HANDOFF` "Third-party code policy", "Code salvaged"; `CONTRIB` "Third-party code"; `R26` section 3. D-019.
- **Rule:** check the licence before reading code you may reuse.
- **Status:** covered (D-019; `PLAN.md` dependency list).

### PR-09 Working with several agents
- **Learned:** three to four agents on one board collided in rebases and lanes (22 Sep "stop the chaos"); a second wave collided on 15 Oct. A foreign agent's first pushes replaced `tests.yml` and two docs with the literal text `$(cat /tmp/...)`, one file ended in "[truncated ...]", four CI jobs were dropped; another overwrote `generator.js` and the changelog through a file-URI. Practices that worked: claim a lane before coding; one theme per agent; one tip owner per branch; rebase once after a foreign merge; no cherry-pick ladders; never force-push another agent's tip; announce before editing; findings first, fixes second in a QC pass; "no silent tracker/profile default changes"; merge only on green CI. Conflicting rules existed (who may merge, whether the tip owner may rebase); the later written rule wins and is logged.
- **Source:** `AS` "Who works how"; `COORD` "Rules", "Rebase hygiene", "Tri-agent pre-release QC"; `docs/QA_CHECK_2026-10-11.md` section 3. D-114..D-117, D-122.
- **Rule:** run `git diff --stat` before every push; never write a file from a substitution that may be empty; never regenerate a workflow from scratch.
- **Status:** covered (`AGENT_STATE.md` "Rules"; D-114..D-117).

### PR-10 Release hygiene
- **Learned:** before every tag: bugfix the tip (leftover user-facing German, broken seeks/handlers, missing bindings) and rerun the playback suite on the clip fixture; do not tag on any regression; after a merge reconcile the task list, citing the PR; delete old branches only after verifying their changes are integrated; the Owner pushes tags and runs the release checklist (hardware, native Windows, self-update).
- **Source:** `CONTRIB` Workflow 6-7; `docs/FREE_RELEASE_QA_AND_CLEANUP_2026-10-12.md` gates G1-G9. D-118, D-126.
- **Rule:** a green workflow, a version bump or a docs merge alone is not release confirmation.
- **Status:** todo (P5).

### PR-11 Hardware honesty
- **Learned:** values without hardware evidence are not verified device properties; mock-only testing is stated as such; physical approval is the Owner's job and CI cannot give it. Cloud agents have neither the Owner's media nor a device nor (in one environment) the model-weight host.
- **Source:** `CONTRIB`; `HANDOFF` "Known limitations"; `COORD` decision log 22 and 27 Sep. D-044, D-126.
- **Rule:** see DV-06, DV-16; list what was not verified.
- **Status:** covered (D-044; D-080).

### PR-12 Data that must stay out of the repo
- **Learned:** golden clips are local (licence and size); only manifests with content hashes live in git (`SAMN_GOLDEN_ROOT`-style root, never absolute paths); no adult media in the repo or in screenshots; mockups with unsafe imagery are not committed; real excerpts of references may be committed as small `.funscript`/`.samn` files with a README.
- **Source:** `docs/GOLDEN_CLIPS.md`; `docs/next/specs/P4_GOLDEN_BENCHMARK.md` rule 1; `AS` "Never". D-073, D-119.
- **Rule:** tests generate their own clips (GD-22).
- **Status:** covered (D-073, D-119; `AGENT_STATE.md`).

### PR-13 Provenance of every dataset file
- **Learned:** a loaded and re-saved script carried `creator`/`native_pipeline` metadata of the app that re-saved it, so one dataset was first labelled "internal consistency check" and later corrected to a real FunGen comparison; candidate files were first de-duplicated by path, inflating the sample count.
- **Source:** `NEXT` "Native Go pipeline vs. real FunGen2, `clip_ausschnitt`"; `COORD` Lane E findings (the cause was not root-caused; inspection of the save paths found no overwrite).
- **Rule:** record origin in the manifest and verify by content hash; do not trust embedded metadata for provenance.
- **Status:** todo (P3.8).

### PR-14 Privacy and dependency intake
- **Learned:** local-first: no telemetry, no auto-download, no bundled model weights, no cloud call without opt-in; learning data opt-in and deletable. Outside libraries and essays are filtered: ideas yes, dependencies no unless a golden gate says so (GoCV, MOSSE/KCF, cloud video APIs, webcam/V4L2 and streaming stacks rejected; sparse random-access frames, bounded pipelines with cancel, and "mark what matters" UX were borrowed as ideas).
- **Source:** `docs/ENGINEERING_STANCE.md`; `docs/LOCAL_MODEL_SETUP.md`. D-025, D-027..D-031.
- **Rule:** a new dependency needs a reason in `PLAN.md`.
- **Status:** covered (D-025, D-027..D-031; `PLAN.md`). Telemetry/Sentry switch: see BC-13.

---

## 11. Tested and rejected

Every row has a measurement. Do not repeat these without a new hypothesis for *why* the result should differ. "Origin" says whether the failure came from the stack (Python, Go, browser, a library) or from the idea itself; where the stack was the cause, a C++ re-test is allowed, but only through the golden-clip gate and compared with 0.7 (see section 12).

| Id | Idea | Result (numbers) | Origin | Source |
|---|---|---|---|---|
| REJ-01 | Divergence as a motion-centre estimator (Funscript Flow) | 41 px for a true 110; correlation 0.829 vs 0.904/0.917; worse in combination | idea | `HANDOFF` rejected table |
| REJ-02 | Symmetric projection weighting | 110.5 vs 110.1 px: no difference | idea | same |
| REJ-03 | Integrated optical flow as a fusion input | correlation 0.27-0.78, amplitude off by up to 2.4x; integration accumulates errors | idea | same |
| REJ-04 | Camera correction by the median flow field (vectors) | ineffective for the flow backend; replaced by feature-based position correction (panning amplitude 145.2 -> 100.2 px, correlation 0.744 -> 0.805) | idea | same, "Measured performance" |
| REJ-05 | Fusion of CSRT and flow | always between the sources, never above the best: clean 1.000/0.926 -> 0.985; occluded 0.293/0.747 -> 0.620; agreement is symmetric, it shows disagreement not which is right | idea | same |
| REJ-06 | Minimum cycle count as periodicity criterion | penalizes slow valid motion; active-time fraction separates better (0.27 / 0.86 / 1.00) | idea | same |
| REJ-07 | Rhythm measured globally | two real scripts of one 77 s film: 0.203 / 0.039 global (both "noisy"), windowed 8 s median 0.396 / 0.324 | idea | same |
| REJ-08 | Rhythm without detrending | drift 0.289 (above the 0.20 reject threshold); sine + drift 1.000 -> 0.425 | idea | same |
| REJ-09 | Absolute RANSAC match count as quality metric | random matches on noise raised amplitude to 233 px; the fraction separates (0.24 vs 0.84-0.90) | idea | same |
| REJ-10 | Finer training ramps | rejected for the wrong reason; the real device resolution is still unknown (raw-value test) | untested | same |
| REJ-11 | CUDA through pip-installed OpenCV | `opencv-python`/`contrib` wheels are built without CUDA; needs a custom build or OpenCL. Also: no GPU CSRT exists in the OpenCV tracker API | library | same; `NEXT` priority 8 |
| REJ-12 | KCF / MOSSE instead of CSRT | speedup 4-14x (hub ROI 154x72: CSRT 88.2 s score 0.90, KCF 17.6 s, MOSSE 6.4 s score 1.00; 1200 frames: 42.8 / 9.9 / 3.2 s), but on an 18x16 px tip ROI they collapse: 99.4 % (KCF) and 99.8 % (MOSSE) of 800 frames lost; a user cannot judge in advance | idea (quality cliff) | `NEXT` priority 8 |
| REJ-13 | Two-tracker CSRT parallelization for distance profiles | isolated microbenchmark 1.68x (GIL released), end-to-end 33.0 s / 33.6 s sequential vs 34.4 s / 35.2 s threaded for 1200 frames; a second, lower-overhead hand-off gave the same; OpenCV already uses about 2.2 of 4 cores inside one call | Python threading layer plus OpenCV internals; a C++ re-test must be end-to-end | `NEXT` priority 8 |
| REJ-14 | Upscaling before tracking a hard ROI | 2x / 3x cubic: lost frames 2 % -> 6 % -> 34 %; gentle 1.15x tripled the loss rate (16.90 % -> 52.30 %), 1.3x cut it to 1.58 %: no smooth dose-response, reads as resampling sensitivity at one factor | idea | `NEXT` priority 2 follow-ups |
| REJ-15 | Unsharp masking before tracking | easy ROI 2.0 % -> 1.9 % (0.5) -> 1.8 % (1.0); hard ROI baseline 16.90 %, amount 2.0 gave 18.05 %; never checked against reference agreement; not shipped | idea (incomplete) | same |
| REJ-16 | WebGL / canvas sharpening of the `<video>` | Laplacian variance native 331 vs WebGL 126 (no sharpening) / 154 (0.8) / 203 (1.5); Canvas 2D `drawImage` 5.3; a static PNG control 6.44 vs 6.45: the loss is in extracting a frame from a live video element, before any shader | browser (headless, software decode; never tried on real hardware) | `NEXT` priority 10; `HANDOFF` |
| REJ-17 | `grid_lk` for two-point distance | r 0.07-0.08 pinned at the lag boundary vs fresh CSRT r 0.524 (inverted, n=422) although its own quality was 0.90-1.00; as a single-ROI opt-in it is 15x faster with a 6x6 grid (3x3 collapsed: 59.3 % lost, one block from frame 1039; 4x4 and 6x6 0.4 %) | idea (quality) | `HANDOFF` Measured performance; `NEXT` priority 8 |
| REJ-18 | Flow as the default backend | 25.1 s vs about 48 s (1.9x, not 4x), quality 0.55 on a busy scene; amplitude 137.8 vs 110 px with a still camera (correlation 0.941); full-resolution 720p at 223 ms/frame | Python/cv2 loop speed; quality finding stands | `NEXT` priority 8; `COORD` Lane E |
| REJ-19 | Four-zone fusion / region fusion as the stroke writer; bake-off backends | 4-zone windowed r 0.363 vs tip CSRT 0.468 vs hub 0.590; region fusion with tip ROI 0.562 (close, no port); bake-off `clip_voll` windowed: CSRT 0.349, `grid_lk` 0.241, `region_fusion` 0.248; `clip_ausschnitt` whole-clip r 0.121/0.105 and 0.105/0.112 vs 0.273/0.440; flatline root cause in GD-01 | idea (quality) | `docs/TFTJ_PROFILE_DIRECTION.md`; `docs/SAM_ARCHITECTURE.md` "Bake-off result" |
| REJ-20 | Active drift correction (re-anchor by template match every 25 frames when `matchesOriginal` is low) | 10 large jumps up to 924 px vs 2-3 before; skin-toned self-similar textures give too many false matches | idea | `COORD` decision log 23 Sep |
| REJ-21 | CSRT scale lock, slower appearance learning, `DynamicRangeMs` alone | scale lock: mixed r, 4 large jumps vs 3; `filter_lr` 0.005: 0.289/0.465 (worse); `DynamicRangeMs` 3000 alone: 69 % stuck windows | idea | same |
| REJ-22 | Rhythm-grid variants | per-cell brightness rhythm; weak-cell drift detector (Spearman 0.07/0.04, 0.29/0.20); global best cell (orientation 50 %); cell hysteresis x1.5/x2 (smaller gain, x2 flipped orientation to 50 %); lock release K=2-10 (fails the thigh test) | idea | `COORD` decision log 23-27 Sep |
| REJ-23 | Automatic second-region search by grid-cell pairs; invented second region | +0.17..+0.28 vs +0.62 manual; invented shift of 1.2 box widths: near-zero; region growth without caps proposed the whole frame | idea | `NEXT` priority 3 |
| REJ-24 | Camera estimate on a half-size picture | 174 s -> 106 s (-39 %) but r 0.94 -> 0.71 | idea (quality) | `docs/owner/generator-profile-2026-10.md`. D-069 |
| REJ-25 | VFR / frame-index drift as the cause of swinging lag; lag tie-breaking | constant 41.02 ms at every sampled frame; aliasing explains the swing; tier 2 not implemented | idea | `FTT`. D-065 |
| REJ-26 | Audio as a position source; audio pre-pass | not a 0-100 signal; "no motion -> write from audio" rejected | idea | `docs/GENERATE_HEURISTICS.md` "Audio" |
| REJ-27 | Stroke preview extrema as the main path | rejected until stage A/B prove value; preview overlap 0.78 / 0.64 is directional only | idea | `NEXT` "Stroke preview" |
| REJ-28 | Frame-difference tip finder; a default partner CSRT; a per-frame detector as stroke writer; ByteTrack by default | breaks per-frame spatial identity / not measured to help; identity layer only if IDs die on the goldens | idea | `docs/ENGINEERING_STANCE.md`. D-064 |
| REJ-29 | Technology: GoCV, a C#/.NET/Avalonia rewrite, Wails v3 / frameless window, WebCodecs, cloud video APIs, webcam, streaming stacks | compile/packaging problems (GoCV contrib pulled unrelated modules, a missing header, a newer API), no measured win, local-first conflict | stack choices of 0.7 | `CL` 0.3.0; `docs/ENGINEERING_STANCE.md`; `docs/ROADMAP.md` (old) "Evaluated 19 Sep". D-025, D-094 |
| REJ-30 | A chat/cloud language model as the stroke writer; retrieval-first generation | breaks per-frame spatial identity; Generate needs measured tracks. A local, opt-in draft with a verifier is allowed (D-129) | idea | `docs/ENGINEERING_STANCE.md`. D-032 |
| REJ-31 | Randomized training cycle timing; Training merged with video playback; biofeedback input | see TR-09 | idea | `docs/TRAINING_MODE_RESEARCH.md` |
| REJ-32 | Tracking lost-frame rate or own quality as proxy for agreement | tight ROI: lost 75 % -> 2 % and quality 0.60 -> 0.95, but r fell to 0.070 | idea | `NEXT` priority 2 |

---

## 12. Removed on purpose, and limits that came from the stack

**Rule (Owner, 16-17 Oct):** a feature that 0.7 removed on purpose stays out of SamNPlayer 2, and the reason it was removed is the lesson. The list, with what went wrong, what was learned and the C++ note per feature, is [`VERSIONS_AND_PLANS.md`](VERSIONS_AND_PLANS.md) section 2.2 (rows a1-a13); it is not repeated here. Where an old limit came from Go, Python or the browser, a C++ Generator (OpenCV, ONNX) may do better, but only if it is measured against 0.7 on the golden clips through the gate (PF-02, D-068, D-122).

Six lessons from that list matter beyond the list:

### RM-01 Business infrastructure before the product
- **Learned:** the licence system, the paid Studio plan and the expiring preview were built or planned before price and product were settled; the plan was written then closed, a development key sits in the history.
- **Source:** `VERSIONS_AND_PLANS.md` a1, a2; D-001..D-003, D-006, D-015.
- **Rule:** no licence code, no trial limits, no timed expiry.
- **Status:** covered (D-001, D-002, D-006; nothing of it in `core/` or `app/`).

### RM-02 Side products dilute the core job
- **Learned:** a character/plugin product (Virtual Person) was built and then cancelled; only the generic plugin drop folder was kept. It is in no SamNPlayer 2 decision yet (`VERSIONS_AND_PLANS.md` section 2.3).
- **Source:** `VERSIONS_AND_PLANS.md` a3; `CL` 0.5.35.
- **Rule:** one theme at a time (D-122).
- **Status:** todo (P4): add a decision row that the product stays out.

### RM-03 More backends in the dropdown did not mean a better stroke
- **Learned:** four-zone, flow, `grid_lk` and `region_fusion` were removed from the GUI after measurement (REJ-18, REJ-19); the slowness of flow and fusion partly came from the Python/cv2 loop.
- **Source:** `VERSIONS_AND_PLANS.md` a4, a5, a6.
- **Rule:** the C++ versions (`P3_GENERATOR.md` P3.5) are admitted only through the gate; the quality findings (fusion lands between sources; weight collapse) still stand.
- **Status:** todo (P3.5).

### RM-04 A switch must never choose the weaker path
- **Learned:** the OpenCL checkbox forced the weaker Python path; the template-matching tracker and the Python tracker ran on Windows because OpenCV was not linked; the cause was a missing library, not the idea.
- **Source:** `VERSIONS_AND_PLANS.md` a7, a12; `docs/PRODUCTION_ROADMAP.md` G0.3.
- **Rule:** hardware choice is automatic and logged; one tracker, a hard error when it is missing (GD-02).
- **Status:** todo (P3.0).

### RM-05 No knob without evidence
- **Learned:** the AI "second opinion" on quality had no field data and a dependency on a local server; Tf and Tj were one recipe under two names, and the distance profile (r about 0.06-0.10) did not beat stroke mode (0.18-0.26).
- **Source:** `VERSIONS_AND_PLANS.md` a8, a9; D-053, D-080.
- **Rule:** Quality Doctor stays rule-based; AI quality only with held-out proof.
- **Status:** covered (D-053, D-080).

### RM-06 Polish must not change identity
- **Learned:** light mode, a frameless window and a card layout would have broken the settled look; browser-only sharpening failed for a browser reason.
- **Source:** `VERSIONS_AND_PLANS.md` a10, a11; D-090, D-094.
- **Rule:** keep the Flow look; Qt would make a frameless window easy, which is still no reason.
- **Status:** covered (D-090, D-091).

### Index: lessons whose limit came from the stack

| Lesson | Stack origin | May C++ do better? | How to find out |
|---|---|---|---|
| PF-01, REJ-18, REJ-19 (flow/fusion speed) | Python/cv2 per-frame loop | yes, speed | gate PF-02 against 0.7, quality first |
| REJ-13 (parallel trackers) | Python threads and the GIL | maybe, end-to-end only | 3+5 runs, median/p95 |
| REJ-16, PS-08 (video sharpening, URL cache) | browser/WebView | different path in Qt; not shown to help | real-hardware measure before any shader |
| PS-15 (codecs, proxy) | WebView codec limits | QtMultimedia plays more | probe + playable-list test |
| GD-02, BC-08 (tracker availability, Windows) | Python wheels, Go binding, missing OpenCV on Windows | yes, one OpenCV | CI self-check |
| GD-07 (PTS) | Go `GrayFrame` without PTS, ffmpeg subprocess | yes | `FrameReader` returns PTS |
| GD-23 (memory bugs) | cgo boundary | RAII in C++ | sanitizer job |
| BC-04 (package completeness) | `go:embed` list vs Python imports | Qt deployment tools | installed-layout test |
| BC-09 (console flash) | `exec.Command` without hide flag | same fix | launch helper |
| AU-01..AU-03 (audio analysis) | ffmpeg subprocess and Python tempo check | in-process decode in C++ | `AudioLevelDecoder` is already in-process; check tempo parity |
| UI-04, UI-05 (strings, markup) | JS/HTML | Qt `Text` has its own AutoText trap | tests in UI-04/UI-05 |

---

## 13. Gaps found

Lessons that matter and are not yet covered in SamNPlayer 2, ordered by risk (user-visible bug today and device safety first, then silent-wrong-result risks of the coming Generator, then release risks). Each line: the entry, then what to do.

| # | Entry | What to do |
|---|---|---|
| GAP-01 | PS-05 seek while loading | `PlayerPage.qml` `seekTo()` clamps to `media.duration` (0 before the media has loaded), so restored/project seeks become 0. Queue a pending seek, apply on `LoadedMedia`, add a test. A bug that exists today. **Done (#35).** |
| GAP-02 | UI-05 plain text | None of the 95 `Text` items sets `textFormat`; file, chapter and bookmark names that look like tags render as rich text. Set `Text.PlainText`, test with `a<b>c.funscript`. Exists today. **Done (#35).** |
| GAP-03 | UI-04 language | Core error texts are German and reach the error bar (`funscript.cpp`, `marks.cpp`, `project.cpp`; `project_test.cpp` pins one). Translate, update the test, add a scan of user-visible strings. Exists today. **Done (#35).** |
| GAP-04 | PS-09 media errors | No `MediaPlayer` error handler: a decode failure is silent. Show the error class; clear it on a successful play. **Done (#35).** |
| GAP-05 | PS-04, PS-11, DV-07, DV-08 device safety (P2) | Before the Owner hardware test: soft start with a pause-in-ramp test; Extended-O with minimum 0 stored as a reduction; a test for "no second floor" in `SuctionPosition`; tests for contact vibration (off by default, gaps force 0, low-span guard, brief graze). **Part done (#36):** the DV-07 and DV-08 tests are in `recipe_test.cpp` (no double floor, contact vibration off by default, gaps, span guard, brief graze). **Done:** PS-04 soft start with the pause-in-ramp tests and PS-11 Extended-O with minimum 0 (this PR). |
| GAP-06 | GD-23 native memory safety (P3.0) | Add an ASan/UBSan CI job and RAII ownership before the OpenCV port; the 0.7 cgo layer had a heap-corrupting argument and a double free that only an option-matrix test caught. `-Wall -Wextra` is only on `samn_core`. **Started: core sanitizer job in CI; extend to the OpenCV layer in P3.0.** |
| GAP-07 | GD-02 tracker availability (P3.0/P3.1) | One `CreateTracker()`, a CI self-check that CSRT and an ONNX model run on Linux and Windows (vcpkg `opencv4[contrib,...]`), hard error instead of any fallback. |
| GAP-08 | GD-03 options reach the output (P3) | A spy-stage test per generator option; one shared definition of enumerations used by UI, controller and core. |
| GAP-09 | GD-07 timebase (P3.0) | `FrameReader` returns PTS; every public time is video time; tests with a start offset greater than 0 for marks, gaps, maps and progress. |
| GAP-10 | GD-05, GD-06, PF-02, PR-06 benchmark (P3.8) | Build the comparison controller and the gate (absolute timeline, lag window 1000 ms, boundary-pinned and low-confidence flags, windowed r, aliasing flag, 3+5 runs, manifest with hashes) before any default is chosen. |
| GAP-11 | GD-11, GD-12, GD-15 drift defenses (P3.2/P3.4) | Port the detrend default, the displacement guard and the original-seeded appearance-memory gate with their tests; keep the rhythm grid opt-in with the release-factor test pinned from both sides. |
| GAP-12 | FF-08, FF-09 script quality (P3.6) | Port Script Doctor and the device-compatibility metrics (100 ms spacing, 900 ms, 5-95, `500 x abs(dpos)/abs(dt)`). **Done (#30):** `core/src/script_quality.cpp` reads the file order, flags unsorted and duplicate times, and has the device-compat metrics (100 ms, 900 ms, 5-95, `500 x abs(dpos)/abs(dt)`); tests in `script_quality_test.cpp`. **Left:** assertions for the 900 ms, 5-95 and intensity values. |
| GAP-13 | FF-01 corpus (P1) | Add the 758-script sweep (load, curve, heatmap, chapters, frames) behind an environment variable. |
| GAP-14 | PS-10 multi-drop (P1/P3) | `openPaths` ignores extra videos and unsupported files without a word; report the ignored count; several videos go to the Create queue. **Done for the Player; the Create queue stays P3.** |
| GAP-15 | GD-25, BC-09 job runner (P3.3) | One job runner: cancel token, run sequence id on every event, single-instance guard, kill children at quit, `CREATE_NO_WINDOW` launch helper with timeout. |
| GAP-16 | TR-02, TR-03, TR-06 training (P2) | Port Training with the interrupt state table, the both-channel arousal test and the shared stop token; the 0.7 bugs were all in these seams. **Mostly done (#37):** the engine, the interrupt state table, the both-channel arousal test and the session seams are in `training_test.cpp` and `training_controller_test.cpp`; there is no shared stop token because the engine is a single-threaded state machine. Escape interrupts from anywhere on the page (test `escapeInterruptsTheRunningCycle`). **Done.** |
| GAP-17 | BC-05, BC-06, BC-07 release (P5) | Single version source read by the app, tag = version check, publish allow-list, exact-asset-name updater with visible errors and a "no asset = no update" test. |
| GAP-18 | BC-04, UI-09 installed layout (P5) | Smoke test on the installed layout (plugins, QML modules, images), not only the build tree. |
| GAP-19 | UI-08, UI-07 accessibility and motion (P4/P5) | Focus handling and `Accessible` on all `Flow*` controls; a reduced-motion setting and a test switch for animations (D-092). |
| GAP-20 | FF-07 settings aside name | `settings.broken.json` is overwritten by a second corruption; use a timestamped name as 0.7 does. |
| GAP-21 | FF-11 unknown `.samn` fields (P4) | `.samn` drops unknown fields like Go; the v2 axes work must keep them. |
| GAP-22 | DV-14, DV-16 companion axes and mock suites (P2) | Companion axis files into the player; 0/1/2/6-axis mock device suites; state "mock only" in device PRs. **Part done:** `CONTRIBUTING.md` rule 10 says mock-only tests are marked as such. **Left:** the other two. |
| GAP-23 | DV-06 evidence tags (P2) | Use OFFICIAL / PROTOCOL / COMMUNITY / MEASURED / HYPOTHESIS in device docs and the future device profile. |
| GAP-24 | PF-05, PF-09 caches and bounds (P3) | Cache keys with algorithm version and all options; a bound on every queue and buffer. |
| GAP-25 | BC-10 path leakage (P5) | Add `-ffile-prefix-map` / `/pathmap` to release builds. |
| GAP-26 | RM-02 plugin product decision (P4) | Add a decision row that the cancelled character/plugin product stays out; keep only the generic drop folder. |
| GAP-27 | FF-04 `inverted` flag tools (P1/P4) | The `playback.honor_inverted` setting, the Invert tool and the polarity hint on the played curve are missing. |

---

## Counts

Lesson entries per area: Player & sync 15, File formats 14, Devices 16, Generator / detection / tracking 25, Audio 4, Training 12, GUI / look 13, Build / CI / Windows 13, Performance 9, Process 14, Removed on purpose 6 (plus the stack-origin index): 141 entries. Status: covered 66, partly covered 3 (GD-23, FF-12, DV-16; the missing part is named in each line), todo 70, n/a 2. Tested and rejected: 32 rows. Gaps: 27.
