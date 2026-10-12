# SamNPlayer — project overview

This document deliberately lives **inside the repository**, rather than
in a separate PDF. It travels with the code and should be updated alongside
code changes so the two do not drift apart.

**Multi-agent work:** Cursor, Claude, and ChatGPT coordinate via
[`docs/AGENT_COORD.md`](docs/AGENT_COORD.md) (claim a lane before coding).

Status: September 2026.

---

## What the project is

A native desktop application (Go + Wails) that controls a SVAKOM Sam Neo 2
over Bluetooth, synchronized with a `.funscript` file. It also includes a
generator that creates `.funscript` files from video using classical
computer vision, without neural networks.

Guiding principles: **do not add what is not needed**, and support every
claim about quality or speed with measurements rather than estimates.

**Stack:** Go 1.25.0 or later (see `go.mod`), Wails v2.15, vanilla JavaScript
(no framework), and Python 3.9+ with OpenCV/scipy/numpy. Python scripts are
embedded with `go:embed` and launched as a subprocess at runtime.

---

## Structure

```text
cmd/gui-wails/     Wails GUI (main product), five tabs
cmd/cli/           CLI version (without generator)
device/            BLE protocol and transport, mock device
player/            Playback, synchronization, training mode
funscript/         Parser, metadata, mapping
generator/         Python pipeline and Go wrapper; trackcv (CSRT/cgo) +
                   posttrack (pure-Go signal path); NativePipeline preferred
                   for CSRT/Tf-Tj (audio check + ROI verify in Go post-hoc)
motionx/           RDP reduction and motion-state classification (salvaged)
videox/            ffprobe/ffmpeg: gray frames, playable remux/proxy
                   (Lanczos downscale); resolves bundled/portable tools
                   (`docs/FFMPEG_TOOLS.md`)
player/ device/    Playback + transports — mobile share surface later
                   (`docs/PLATFORMS.md`); generator stays desktop-only
logging/ update/   Logging, automatic updates through GitHub releases
```

---

## What works

**Playback:** load a script, play a synchronized video, display the
Funscript curve with a moving playhead below the video, heatmap, and markers.
Includes **script offset**, saved per script and applied immediately during
playback, because imported scripts rarely match the user's video cut
exactly. Also includes **section looping** and keyboard controls: Space,
←/→ for 5-second seeks or 1 second with Shift, `,`/`.` for fine stepping,
1–9 for jumps, `+`/`−` for offset, `L` for looping, and `E` for Extended-O.

The offset is applied in exactly **one** place: where video time maps to
script time (`ReportVideoPosition`). Applying it again to the frontend
display would make the curve and device drift apart.

**Two device connection options:** direct Bluetooth using the local adapter
without additional software, or WebSocket access to a running
**Buttplug server** (Intiface Central). The latter requires no Bluetooth
adapter on the player computer and works with devices supported by Buttplug.
Intiface handles connection, pairing, and reconnection, which are often the
most error-prone parts. The implementation follows the open protocol
specification without copying source code. `gorilla/websocket` was already
present through Wails, so no new dependency was needed.

The server can run **on another device**, for example Intiface Central on a
phone with the player on a computer. The address field accepts an IP such
as `192.168.1.50`, a host and port such as `192.168.1.50:12345`, or a complete
`ws://` address. Connection type and address are remembered after a
successful connection. Errors distinguish local from network connections
because likely causes differ: a server that has not started versus the
wrong Wi-Fi network, IP address, or a firewall.

Tested against a simulated Buttplug server in seven checks, including a
device without a suction channel, a missing server, address normalization,
and the distinction between local and network errors.

**Direct Bluetooth remains the default.** Intiface is an additional option.

**Device:** a dedicated tab provides connect/disconnect, device name, BLE
address, signal strength, vibration/suction function tests, and a raw-value test.

**Training:** Semans stop-start and a plateau variant; an interrupt control
ends the current cycle rather than the entire session. An arousal scale
from 1–10 adjusts the next cycle. Sessions are logged.

**Training scripts (multi-phase, per-channel):** the simple Technique/
Channel form can only shape one curve, applied identically to whichever
channel(s) are selected. `player.TrainingScript` generalizes that into an
ordered list of named phases, each with an independent vibration and
suction curve — vibration can ramp up then down while suction stays off,
followed by a phase where suction ramps and vibration holds at a light
constant level untouched from the previous phase. Five built-in scripts
(`player.BuiltinTrainingScripts`) are shipped, researched from vacuum/
cupping therapy technique names (static hold, pumping, gliding sweep) and
vibration-massager pattern vocabulary (escalating, wave, pulse, random) -
see `docs/TRAINING_MODE_RESEARCH.md` for sources and the full design.
`player.ScaleTrainingScript(script, factor)` multiplies every curve's
levels (not timing) by a factor — the Training tab combines an explicit
difficulty tier the user picks and an automatic nudge from that same
script's recent session history into one factor (`TrainingRequest.
IntensityFactor`), applied to both the real run and its plan preview via
`resolveTrainingScript`, so any profile (built-in or custom) scales
without needing separate tiered scripts. The
arousal-feedback math is unchanged (`adjustForArousal`, tested directly)
but is now applied as one shared factor to every active channel and to
the shared rest, closing a gap the single-curve form had: "both channels"
only got weaker together because it happened to send both the same
number, not because the feedback loop understood there were two. The GUI
adds a plan-preview curve (both channels drawn as two lines before
starting) and a live per-cycle "feedback effect" readout.

**Script editor:** a "Script editor" section on the Training tab builds
an arbitrary `player.TrainingScript` through form fields instead of only
picking a built-in preset — phases, and per phase two independent
**axes** (Vibration/Suction), each its own ramp/hold/ramp-down curve.
Saved as one JSON file per script under
`os.UserConfigDir()/SamNPlayer/training_scripts/`
(`cmd/gui-wails/app_training_scripts.go`), listed alongside the built-ins
and loadable back for editing. The script name is user text turned into
a filename, so it is sanitized against path traversal
(`scriptFileSlug`); `player.NormalizeTrainingScript` runs before saving,
the same guarantee `RunTrainingScript` itself applies, so a hand-edited
script cannot end up with two curves racing for the same physical
channel.

**Generator:** three interchangeable tracking backends (`generator/backends.py`)
— CSRT (default, one bounding box), flow (no region, dense optical flow,
~4x faster), and grid_lk (a grid of independently tracked points, median
as position, ~15x faster than CSRT and measurably more robust on small
regions — see "Measured performance" below). Includes automatic region
selection, scene-cut detection with region selection per scene, camera
compensation, adaptive keyframes, RDP, speed limiting, axis selection,
automatic retry, parallel batch processing, caching, and measurement
reports with user feedback. Optional contact-triggered vibration for
Tf/Tj: pulses proportional to the ROI1-ROI2 distance's own top slice,
derived from the script's own `pos` range rather than a fixed pulse.

**Appearance memory:** the tracker remembers what the region looks like
and searches the whole frame after tracking loss or a scene cut instead
of blindly reinitializing at the last position. This reproduces one useful
capability of object detectors using classical methods. On a three-scene
test video with true motion of 90/100/80 px, the measured ranges were
90.5 / **1.5** / **5.0** px without memory and
90.5 / **101.0** / **80.0** px with memory. On the occlusion video, tracking
loss fell from 323 to 148 out of 500 frames. The remaining 148 frames are
those where the object is actually absent; they are reported as unsuccessful
searches rather than hidden. Below the minimum similarity threshold the
tracker deliberately does not reinitialize: a guessed position would look
like a measurement without being one.

**Two-point measurement:** tracks two regions and uses their **distance**
as the signal. Common translation of both points, whether caused by camera
panning or shared object motion, cancels out instead of needing to be
removed afterward. Zoom can still change image-space distance and must not
be treated as inherently canceled. On a test video combining panning,
shared movement, and an oscillating gap, correlation with the true distance
was −0.01 for one tracker and +0.62 for two trackers.

**Motion profiles:** `--profile standard` for stroke motion and
`--profile weich` for soft tissue. The difference is physical: a rigid
stroke is one movement, whereas soft tissue exhibits damped oscillation
after an impulse. That ringing is the *result* of the impulse, not another
stroke. Without a prominence requirement, each oscillation becomes a keyframe.

On a test video with impulses every 800 ms and ringing at 4 Hz, the output
contained 81 keyframes without prominence filtering and 42 with prominence
0.35, matching roughly 40 impulses. Reconstruction error rose from 0.094
to 0.176 because ringing was deliberately excluded. This is why it is a
selectable profile rather than the default. **Clean stroke signals remain
exactly unchanged:** 42 or 80 keyframes, with identical error.

**Motion signature (scene recognition):** eight measurable scene features:
main direction, motion-center location and spread, number of separate
regions, camera instability, stroke symmetry, and rhythm strength. These
allow the app to recognize similarity to a previously named scene and reuse
parameters that worked there.

**Important distinction:** this does not identify *what* the scene depicts.
Naming an action from its content would require semantic recognition and
a trained model with labeled data. The user supplies the name; the program
transfers it only to similar signatures.

Measured distances: two vertical-motion scenes at different speeds were
0.075 apart; vertical versus horizontal was 0.26–0.32, and versus camera
panning 0.28–0.29. A threshold of 0.15 separated these examples. Above the
threshold, no assignment is made: incorrectly transferred parameters are
worse than no assignment and harder to notice later.

Not yet connected: naming through the UI and applying saved parameters to
generation runs. Region counting currently returns the same value for all
test videos, so it contributes nothing to distinguishing them.

**Learned quality assessment:** user ratings in the measurement report
(usable / borderline / unusable) can train logistic regression over seven
metrics. This deliberately uses plain numpy rather than a neural network,
so weights remain readable and decisions can be inspected. A model is
accepted only if it outperforms the fixed rules in leave-one-out
cross-validation, using at least 12 rated runs and at least 4 per class.
It is stored as `qualitaetsmodell.json` in the configuration directory.
Borderline counts as a failure, favoring an extra user check.

**Script analysis:** divides a loaded script into motion states — stationary,
starting, accelerating, steady, decelerating, and stopping — and displays
a description below it. This catches a gap in other checks: a long flat
section is neither noisy nor arrhythmic, so those checks miss inactivity.
The same classification also drives a coarser chapter timeline
(pause/build/steady/crescendo/winddown) shown alongside it.

**Manual funscript editing:** the playback curve doubles as a point editor
(drag to move, click to add, double-click to delete), with the video
following the point being dragged during a drag. **Script Doctor** runs
Quality Doctor's actions-only checks (timestamps, range, gaps, spikes,
device compatibility, rhythm with reduced confidence) against an imported
file that never went through generation, so has no tracking data — marked
`estimatedFromScriptOnly` so it's never confused with a post-generation run.

**Polarity, O-zones, ring-down:** `SuggestPolarity` compares the first and
second half of a script's mean position and offers to invert (`100 - pos`)
on a mismatch — a sign-convention check, not a tracking-quality one.
`SuggestOZone` proposes a primary marker in the script's last eighth at
its highest-mean window (classical, signal-only), appliable manually or
automatically at generation time. `RingDown` appends damped half-cycles
after a chosen point instead of dropping straight to zero.

**Training history:** past sessions (already logged as JSONL per-session,
see below) are now read back and summarized in the training tab - cycles,
mean peak intensity, how many were interrupted, mean arousal feedback.

**Minimum spacing between actions:** enforced during generation rather
than merely reported. Peaks and valleys are found separately, so minimum
spacing within either list does not enforce spacing between a peak and the
next valley. On noisy signals, up to 45% of actions were less than 100 ms
apart. The point with the smaller deviation from the line joining its
neighbors is removed, preserving extrema. Reconstruction error rose only
from 0.084 to 0.086; clean signals were unaffected.

**Device compatibility:** checks generated files against limits established
in the Funscript community so results remain useful on other hardware and
in other players. These values come from existing projects: 100 ms minimum
action spacing (the Launchcontrol transmission threshold), 900 ms for the
slowest useful full stroke, position range 5–95, and intensity
`500 × |Δpos| / |Δt|` as a speed metric (the funscript-utils definition,
also used in OpenFunscripter, Funscript.io, and XBVR).

**Quality Doctor:** evaluates timestamps, value range, gaps, speed outliers,
rhythm (spectral concentration of the dense, **detrended** signal), tracking
loss, actual motion amplitude, **active time fraction**, and
**reconstruction error**: how faithfully exported actions represent the
measured signal.

---

## Measured performance

All figures below come from actual runs rather than estimates.

| Measurement | Value |
|---|---|
| CSRT tracker | ~100 ms/frame (97.5% of total runtime) |
| Dense optical flow (Farneback) | ~18 ms/frame |
| Video decoding | 0.7% of total runtime |
| Cache hit | 31.7 s → 0.88 s (36× faster) |
| Flow backend versus CSRT | 12 s instead of 51 s per test video |
| grid_lk versus CSRT, easy region | ~15x faster, same or better own-quality score |
| grid_lk versus CSRT, hard/small region | CSRT: real but fragile (own quality dropped 0.95→0.45 on an 800-frame slice); grid_lk: no measured collapse at 4x4 grid density or denser (see rejected KCF/MOSSE below for the failure mode grid_lk avoids) |

grid_lk's own tracking robustness does **not** transfer to Tf/Tj's two-point
distance measurement: measured against a real FunGen reference on the same
clip/ROIs, grid_lk scored near zero (r=0.07-0.08, boundary-pinned lag
search) versus a fresh CSRT run's r=0.52 (real, zero-lag) on the same pair
— own-quality metrics and reference agreement are not the same axis, the
same lesson as the flow-backend/Quality-Doctor gap below. `docs/NEXT.md`
priority 8 has the full clip-by-clip numbers.

**Amplitude fidelity**, with 110 px of true object motion and a realistically
textured background:

| Method | Stationary camera | Panning |
|---|---|---|
| CSRT + feature-based compensation | 112.2 px | 112.8 px |
| Flow backend (old vector correction) | 137.8 px | 145.2 px |
| Flow backend (position correction) | 137.8 px | **100.2 px** |

Camera correction in the flow backend now uses features to correct
positions rather than flow vectors. Panning correlation improved from
0.744 to 0.805.

---

## Third-party code policy

For a project intended for commercial distribution, the source project's
license matters as well as whether code was copied. The following table
records the project's existing license assessment.

| Project | License | Project policy |
|---|---|---|
| Funscript Flow | Apache-2.0 | Code reuse permitted with attribution |
| funscript-utils, launchcontrol | Permissive | Metrics and limits adopted |
| Buttplug (protocol knowledge) | BSD-3 | Protocol facts may be used; code reuse requires attribution |
| **FunGen 1** | **PolyForm Strict 1.0.0** | **Do not reuse** in this project |
| FunGen 2 | Closed binary | No source available to review |

The project excludes FunGen source-code reuse and structurally equivalent
ports because of licensing concerns around derivative works and commercial
use in an MIT-licensed product. Public descriptions of behavior may inform
independent designs. This records the development policy, not a fresh legal
assessment of third-party licenses.

## Code salvaged from fse-generator

Two packages from an earlier project were reviewed individually instead
of merged wholesale:

| Package | Contents | Status |
|---|---|---|
| `motionx` | Iterative RDP returning indices, `Dedup`, motion-state classification | **Connected:** classification drives both script analysis and the chapter timeline |
| `videox` | `ffprobe` wrapper and ffmpeg grayscale reader | Present, **not connected** |

`motionx` uses only the standard library. `videox` requires **ffmpeg and
ffprobe on PATH**, a dependency the current project avoids by decoding
video through Python/OpenCV. Connecting it now would require every user
to install more software without a demonstrated benefit. It becomes useful
if removing Python altogether is the goal; that is an architectural
decision, not simply copying another file.

Explicitly **not** salvaged, based on the original analysis:
`EstimateTranslation` integrates the camera signal rather than the subject
signal with search grid 4 and reaches its limit within a few frames;
`pattern.Periodicity` measures smoothness instead of periodicity (linear
drift scores 1.000); `candidate.Build` therefore systematically favors the
over-smoothed candidate, the same class of error as the zigzag problem.

## Extensibility

Analysis methods are replaceable components (`generator/backends.py`).
Previously, selection was hard-coded inside the pipeline, requiring a
pipeline edit for every new method.

A backend is a function with a fixed contract:

```text
analyze(video_path, roi, options)
  -> (timestamps_ms, positions, frame_size, scene_cuts, stats)
```

Two requirements are essential: positions must use **image coordinates**,
not normalized values, otherwise dynamic-range processing and normalization
would silently be bypassed. Also, `stats["vertical_range"]` must contain
amplitude **in pixels**. Normalization permanently removes that information,
but the Quality Doctor needs it to distinguish real motion from amplified
jitter.

Custom methods are Python files in the plugin directory, registered with
`register(name, func, description)`. `--list-backends` lists available
methods and their origins. Contract violations fail immediately with a
message identifying what is missing; otherwise a plugin returning one row
too few would appear merely to produce an inexplicably poor script.

There is deliberately **no sandbox**: plugins run with the same permissions
as the application.

## Fixed bugs affecting users

**Incomplete Python-module embedding:** four files were embedded but only
two were written to the temporary directory. This was invisible in the
source tree, where all modules sit together. In the packaged executable it
caused **three apparently unrelated failures**: `--backend flow` failed,
the learned quality model was never found, and device checking silently
never ran because its import was wrapped in `try/except`. The entire
Python directory is now embedded with `go:embed *.py`.
`generator_embed_test.go` reads actual imports from the source and verifies
that each module reaches the temporary directory. A manually maintained
list would repeat the original mistake.

**Update check failed silently:** `CheckForUpdate()`'s startup check had
an empty `.catch(() => {})` - any failure (network, GitHub rate-limit, a
non-200 response) vanished with no log and no UI feedback, indistinguishable
from "no update available." Fixed: the swallowed error now at least logs,
and a "Jetzt nach Updates suchen" button in settings shows the actual
result (current version, the new version, or the error) on demand.

**Dropping multiple videos silently discarded all but the first:** a
`drop:videos` (plural) event carried the full dropped-file list, but
nothing ever listened for it - a dead event, so extra files vanished with
no indication anything beyond the first was received. Fixed: the ignored
count now travels with the loaded path in a single `drop:video` event and
shows in the generator's status line, rather than pretending batch
generation (still unbuilt) handled them.

**Keepalive maintained only one channel:** it repeated only the last sent
packet. If that packet controlled suction, vibration was not maintained,
and vice versa, particularly during pauses and Extended-O, when keepalive
is needed most. Both channels' states are now stored and repeated.
Unchanged packets are also suppressed: integer levels mean 100 ramp steps
produce at most 11 distinct packets. The rest were ineffective round trips,
potentially up to 40 per second for two channels updated every 50 ms.

## Known limitations

**The flow backend overestimates amplitude even with a stationary camera:**
137.8 px instead of 110. This is not a camera-correction bug, because no
correction applies in that case; it is a property of the center estimator.
After normalization to 0–100 the effect is smaller than the raw numbers
suggest, and the shape is accurate (correlation 0.941).

**All quality thresholds were calibrated on synthetic videos:** clean sine
motions with known ground truth. Real material is less regular and
systematically scores lower. The measurement report exists so thresholds
can be recalibrated using real runs **and** human ratings.

**The generator produces weaker motion than FunGen — partly explained.**
On the same film, median action jump was 6.5 versus 44.5, jumps greater than
50 points were 0.0% versus 34.7%, and time in the 40–60 middle band was 33.3%
versus 14.2%. Our script jittered around the middle instead of alternating
between extremes.

One cause was identified and fixed: global normalization used only 30 out
of 100 points in half of all 6-second windows. Moving-window dynamic-range
processing raises average motion strength from 10.1 to 26.2.

The remaining cause is **not post-processing**, as measurements now show.
On synthetic video with realistically varying speed (1.2–2.2 Hz), the
pipeline reaches intensity 162.1 versus an ideal 161.4, effectively without
loss. Marked-region size makes no difference either: 112, 111, and 110 px
for 40×40, 70×70, and 120×120 regions. Smoothing attenuates realistic stroke
frequencies by only 0–8%.

The remaining explanation is the measured quantity itself. A single region
measures that image area's displacement, while real material often depends
on the **relative** motion between two bodies, which can be much larger
than either one's absolute displacement. Two-point measurement (`--roi2`)
addresses this. Automatic detection of both regions is not yet good enough
according to measurements (see `find_two_rois`).

The original video is needed to settle this; the exported script alone
cannot recover the original signal.

**Not tested on real hardware:** connection, playback, and training have
only been verified against `device.Mock` and simulated protocol behavior.

**Actual device resolution is unknown:** the 0–10 vibration and 0–5 suction
ranges come from Buttplug's device configuration. They describe Buttplug's
quantization, not necessarily firmware limits. The device tab's raw-value
test is intended to resolve this.

**The learned model has not seen real data:** its mechanics were tested
with synthetic examples, improving accuracy from 50% to 100% in a
constructed case. Real-world performance remains unknown. Fixed rules
remain in use until enough ratings are available.

**Automatic retry has no demonstrated benefit yet:** it is implemented and
tested, but no case in the calibration set improves with retry. All failures
in that set are tracking problems, which retry correctly skips.

---

## Tested and rejected

Recorded so the same unsuccessful approaches are not repeated:

| Idea | Result |
|---|---|
| Divergence as a motion-center estimator (Funscript Flow) | 41 px instead of 110; correlation 0.829 versus 0.904/0.917. Makes the combination worse. |
| Symmetric projection weighting (Funscript Flow) | No measurable difference: 110.5 versus 110.1. |
| Integrated optical flow as a fusion input | Correlation 0.27–0.78; amplitude off by up to 2.4×. Integration accumulates estimation errors. |
| Camera correction using the median flow field | Corrects vectors rather than positions; ineffective for this backend. Replaced with feature-based position correction. |
| Fusion of CSRT and flow backend | Always falls **between** the sources, never beats the best: clean 1.000/0.926 → 0.985; occluded 0.293/0.747 → 0.620. Agreement between two sources is symmetric: it reveals disagreement, not which is right. `fusion.py` is tested but not connected. |
| Minimum cycle count as a periodicity criterion | Penalizes slow but valid motion: two clean cycles would appear unreliable. **Active time fraction** separates cases better: one excursion 0.27, two slow cycles 0.86, continuous motion 1.00. |
| Measuring rhythm globally across a video | Assumes a single continuous rhythm. Two real scripts for the same 77 s film scored 0.203 and 0.039 globally, classifying **both** as noisy, including one from an established tool. Windowed measurement (8 s, median): 0.396 and 0.324. |
| Rhythm measurement without detrending | Linear drift scored 0.289, above the 0.20 rejection threshold, and would pass. A valid sine wave with drift fell from 1.000 to 0.425. Detrending fixes both. |
| Absolute RANSAC match count as a quality metric | Random matches on noise backgrounds increased amplitude to 233 px. The **fraction** separates cases cleanly: 0.24 versus 0.84–0.90. |
| Finer training ramps | Initially rejected for the wrong reason; actual resolution remains unknown. See the raw-value test. |
| CUDA through pip-installed OpenCV | `opencv-python` and `opencv-contrib-python` are built **without CUDA**. Requires a custom build or OpenCL instead. |
| KCF/MOSSE trackers as a faster CSRT replacement | Real speedup (4-14x) and matching quality on easy regions, but **collapse** (near-total lock loss, not gradual noise) on a small/hard 18x16px region: 99%+ frames lost. Unlike the flow-backend tradeoff, a user has no way to judge in advance whether their marked region will hit this. Not shipped; `grid_lk`'s multi-point median (below) reaches similar speed without this failure mode. |
| Two-tracker CSRT parallelization for Tf/Tj (`ThreadPoolExecutor`, then a lower-overhead `threading.Event` handoff) | An isolated microbenchmark showed 1.68x from releasing the GIL, but the real end-to-end pipeline showed no net gain (33-35s either way) with two independently-implemented approaches. OpenCV's own internal parallelism (already ~2.2/4 cores per CSRT call) leaves little headroom Python-level threading can add. Reverted, not shipped. |
| Gentle upscaling (1.15x-1.3x) before tracking a very small/hard ROI | No clean signal: 1.15x nearly tripled the lost-frame rate, 1.3x cut it to a tenth of baseline on the same ROI - opposite of a smooth dose-response. Reads as sub-pixel resampling sensitivity specific to that exact scale factor, not a real "upscaling helps" effect. Not established as beneficial. |
| WebGL/Canvas 2D sharpening of the `<video>` element for low-resolution source clips | The loss happens the moment a frame is pulled out of `<video>` into ANY canvas (`texImage2D` or `drawImage`), before any shader runs - measured via Laplacian variance on identical screenshots (native video 331 vs. plain `drawImage` 5.3). A control test with a static PNG showed no such loss, ruling out generic canvas scaling. Not shipped even as an opt-in: shipping "sharper" while it measurably makes the image less sharp would be a regression, not a feature. Unclear whether this is a general video-decode-pipeline property or specific to this headless/software-decode environment - untested on real hardware. |

---

## Pitfalls

These issues have already cost development time:

- **Noise-background test videos do not work for camera compensation.**
  `goodFeaturesToTrack` finds no stable features, turning the estimate into
  a random walk and making compensation appear broken. Use textured backgrounds.
- **Moving objects in test videos must use world coordinates.** Objects
  drawn at fixed screen positions do not follow camera pans, making correct
  compensation appear wrong.
- **Increment `TRACK_CACHE_VERSION`** when changing `track_roi` or camera
  compensation, otherwise cached results silently come from the old code.
- **Regenerate Wails bindings** with `wails build` after adding a Go method.
  Frontend mocks are generated from `App.js`, so missing bindings surface quickly.
- **Measure rhythm only on the dense signal.** Peak/valley reduction turns
  any signal into a zigzag that appears rhythmic.
- **Build with `-trimpath`.** Otherwise the build machine's path, including
  the username, is embedded in the executable.
- **A CSS `transform` and an SVG `transform="rotate(...)"` presentation
  attribute on the SAME element don't compose** — the CSS one silently
  replaces the attribute instead of adding to it (SVG2/CSS Transforms
  spec). Split them onto nested `<g>`s: outer group carries the
  CSS-animated transform, inner group carries the static
  `transform="rotate(...)"` attribute.
- **Playwright's click/screenshot "stability" check compares
  `getBoundingClientRect()` between two frames for exact equality** — a
  page with ANY continuously running CSS animation nearby (even a
  cosmetic, sub-pixel one that never actually moves the target element)
  can make this never pass, timing out after 30s. Confirm it's cosmetic
  by disabling all animations/transitions (`page.add_style_tag(content=
  "*{animation:none!important;transition:none!important}")`) and
  re-sampling the target's bounding box — if the "drift" disappears,
  use `force=True` on the click (or `animations="disabled"` on
  `screenshot()`) rather than chasing it as a real layout bug.

### Reusable GUI pattern: the training-tab intensity ring

Built for the Training tab's live intensity meter
(`cmd/gui-wails/frontend/src/training.js`'s `renderDualIntensityRing`/
`updateIntensityRing`, styles in `style.css`'s `.tr-ring-*` block) —
worth reaching for again when polishing other dialogs, not applied
elsewhere yet:
- Nested-`<g>` transform separation (see pitfall above) for any SVG
  element that needs both a static rotation and an animated transform.
- One shared full-circle track for two overlapping values (both fills
  go 0–360° from the same start point) instead of splitting the ring
  into a fixed half per value — lets either value dominate visually
  without hardcoding which "side" it owns.
- Overlapping colored regions: normal alpha blending (reduced
  `stroke-opacity` on the top layer), not `mix-blend-mode: screen` —
  screen washes two light/pastel colors toward white as soon as they
  fully overlap (the common "both equal" case), losing the color
  distinction entirely. Keep each gradient in its own color family
  (light tint → saturated hue) rather than sharing one bright stop
  color, for the same reason.
- CSS custom properties set live from JS
  (`el.style.setProperty('--x-amp', ...)`) to drive keyframe animation
  *amplitude* proportional to a live value, instead of swapping CSS
  classes per intensity level.
- `perspective` on the parent + animated `rotateX`/`rotateY` on the
  child for a genuine (not just scaled) 3D tilt; a slowly rotating
  gloss-highlight `<g>` (own `animation: rotate(...)`, separate from any
  other transform on the same element) reads as a specular highlight
  sweeping across a curved surface.
- Deciding what pulses: a channel that should feel local (a vibration)
  animates its own sub-element; a channel that should feel like it
  affects the whole object (suction/contraction) animates the shared
  parent wrapper instead — don't force every channel's motion onto the
  same element just because they share a widget.

---

## Collaboration

`CONTRIBUTING.md` defines the workflow, boundaries, and central rule:
**behavior changes need a regression test verified to fail without the
change**. Documentation-only changes require factual checks.
GitHub runs `tests.yml` on every push and pull request (Go with the race
detector, Python, and frontend tests), and `release.yml` on version tags.

Previously, tests ran **only during releases**. With multiple contributors,
that would reveal problems only during delivery, long after the context
of another contributor's code had been forgotten.

## Tests

After completing the setup in `WIEDERAUFNAHME.md`, run the full suite (Bash):

```bash
go vet ./... && go test ./...
(cd generator && for t in *_test.py; do python3 "$t" || exit 1; done)
for t in cmd/gui-wails/frontend/test/*_test.py; do python3 "$t" || exit 1; done
```

Frontend tests load the real JavaScript in headless Chromium and replace
only the Wails bindings. Mocks are generated automatically from `App.js`
(see `test/_harness.py`).

---

## Why `fusion.py` and `videox` are not connected

Neither currently meets the rule from section 16 of the target concept:
no module without a demonstrably improved processing path. They remain in
the tree with tests because their prerequisites are foreseeable — a third
measurement source for fusion, and (for videox) a need for ffmpeg-based
decode. Removal of the Python dependency is now an active goal via
`trackcv` + `posttrack` (OpenCV decode, not ffmpeg), so connecting
`videox` would add a second external binary without a demonstrated
benefit over the OpenCV path already used by the native CSRT pipeline.

## Project status and next steps

The prioritized operational task list lives exclusively in
[docs/NEXT.md](docs/NEXT.md). Hardware and quality limitations above remain
open until measurement reports resolve them.

The release workflow has run successfully through `v0.2.2`
(September 14, 2026), publishing GUI and CLI binaries for Windows/Linux
plus `checksums.txt`; the source version (`VERSION`/`update.BaseVersion`)
can run ahead of the last published tag between releases. Dozens of PRs
have merged since the original Tf/Tj integration (#2-#4) — see `git log`
or GitHub for the current list; this file's own sections above are kept
current with what's actually shipped, `docs/NEXT.md` with what's still open.

The local AI adapter for the generator is implemented and GUI-wired for
all three planned steps (region proposal, profile suggestion, quality
second opinion) — the existing classical pipeline still measures and
validates in every case; the AI never writes a `.funscript` on its own
path. Still open: no bundled/recommended ONNX region model, and no field
data yet on how useful the profile/quality steps are without someone
running a real local Colibri server against real material. See
`docs/AI_ADAPTER.md` for the architecture, `docs/COLIBRI_SETUP.md` for
server setup, and `docs/LOCAL_MODEL_SETUP.md` for the Owner path across
ONNX / Go profile / Colibri / AIWrite.

## Environment setup

Cloning, dependencies, and build instructions are maintained in
[WIEDERAUFNAHME.md](WIEDERAUFNAHME.md). Go and Wails versions follow
`go.mod`; CI and release builds use that same source for tool versions.
