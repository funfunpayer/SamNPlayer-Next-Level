# Spec: Video-to-Motion-Script Generator

**Status:** draft 1. **Type:** behaviour-only build specification.

This document says *what* the feature must do, not *how* any existing product does it. It contains no code, names, file formats or assets from another application. All algorithms below are described as independent requirements, and an implementer may choose any approach that passes the tests in §8.

---

## 1. Purpose

Given a video in which something moves back and forth (oscillating motion), produce a **motion script**: an ordered list of time-stamped positions on a 0–100 scale that follows that motion. A media player can play the script in sync with the video, for example to animate an on-screen indicator or drive a linear actuator.

### 1.1 In scope
- Analysing a local video file and generating a script automatically.
- Letting the user choose *what* to follow: a region of the frame, or an automatically detected object.
- Cleaning the raw motion into a script with few, meaningful points.
- Previewing, saving and re-loading the script.
- Progress reporting and cancellation.

### 1.2 Out of scope (may be added later)
- Controlling physical hardware directly.
- Live or streaming sources.
- Training AI models.

---

## 2. Glossary

| Term | Meaning |
|---|---|
| **Frame** | One decoded picture of the video, with its presentation timestamp in milliseconds. |
| **Region of interest (ROI)** | A rectangle in frame coordinates that contains the motion to follow. |
| **Raw signal** | One number per analysed frame describing where the followed motion is along the chosen axis. |
| **Position** | An integer 0–100. 0 = one end of travel, 100 = the other end. |
| **Point** | A pair (time in ms, position). |
| **Script** | A list of points sorted by time, plus metadata. |
| **Extremum** | A local turning point (peak or valley) of the signal. |
| **Detector** | An optional pluggable AI model that returns labelled boxes for a frame. |

---

## 3. Functional requirements

### 3.1 Opening a video
- **FR-1.** The user can choose a local video file. The system reads its duration, frame rate, resolution and the timestamp of each frame.
- **FR-2.** If the file cannot be opened or has no video stream, the system shows a clear error and changes nothing else (see §6).
- **FR-3.** Variable-frame-rate video must be supported. Times always come from frame timestamps, never from `frame index × nominal fps`.

### 3.2 Choosing what to follow
- **FR-4. Manual mode.** The user draws a rectangle (the ROI) on a paused frame, and that frame becomes the start frame. The user also chooses the **axis** (vertical, the default, or horizontal) and whether the output is **inverted**.
- **FR-5. Automatic mode.** If a detector is available, the system runs it on the start frame and proposes the most confident box of the configured target label as the ROI. The user may accept it or edit it.
- **FR-6.** Generation can be limited to a **time range** (start to end). The default is the whole video.

### 3.3 Analysis
- **FR-7.** Decode frames in order across the selected range, at the **analysis rate**. The default is every frame. The user may set a cap such as 30 analysed frames per second. Skipped frames are not used.
- **FR-8.** Before analysis, frames may be downscaled so the long side is at most **analysis width** (default 640 px). The ROI is scaled by the same factor, and outputs are reported in original coordinates.
- **FR-9. Following the ROI.** For each analysed frame, estimate how the content inside the ROI moved along the chosen axis relative to the previous analysed frame, and update the ROI's position to follow the content.
  - At least one model-free method is required, for example dense optical flow or template matching inside the ROI.
  - A detector-assisted method is optional. If present, the detector re-locates the ROI periodically, every **N** frames (default 15), and whenever tracking confidence drops below a threshold.
- **FR-10.** Every analysed frame produces one raw-signal sample `(t_ms, value, confidence 0–1)`. When the target is lost, confidence is 0 and the value is absent.
- **FR-11.** Raw samples are cached per video and settings, so post-processing can be re-run (§3.4) without decoding again.

### 3.4 Turning the signal into a script
Each step below is a separate, individually testable stage, applied in this order.

- **FR-12. Gap handling.**
  - Gaps no longer than **max-gap** (default 500 ms) are filled by linear interpolation.
  - Longer gaps split the signal into segments, and no points are produced inside a long gap.
- **FR-13. Outlier rejection.** Drop a sample if it differs from the median of its ±k neighbours (default k = 3) by more than **outlier factor × local MAD**; the default factor is 5. Treat the dropped sample as a gap.
- **FR-14. Smoothing.** Apply a zero-phase low-pass smoothing, meaning it causes no time shift. The default window is 120 ms, and a window of 0 disables smoothing.
- **FR-15. Detrend and normalise.** For each segment:
  - Remove slow drift: subtract a rolling baseline with a window of **4 s**.
  - Map the result to 0–100 using the 2nd and 98th percentiles of the segment as 0 and 100, clamping values outside.
  - If **inverted**, use `100 − p`.
  - If the segment's travel is smaller than **min amplitude** (default 2 % of ROI height or width), output a constant 50 for that segment.
- **FR-16. Extrema extraction.** Keep only turning points.
  - A peak or valley counts only if its prominence is at least **min stroke** position units (default 10).
  - Consecutive kept points must be at least **min interval** apart (default 60 ms). If two are closer, keep the more extreme one.
- **FR-17. Speed limit (optional).** If the speed between two points, `|Δpos| / Δt` in units per second, exceeds **max speed** (default off, typical value 500 /s), reduce the movement toward the midpoint until it fits. Times never change.
- **FR-18. Simplification (optional).** Optionally remove points whose removal changes the curve by less than **tolerance** position units (default 2). First and last points are always kept.
- **FR-19.** Output positions are integers 0–100 and output times are integer milliseconds. Times are strictly increasing, with no duplicates.

### 3.5 Result, preview and editing
- **FR-20.** Show the script as a graph aligned with the video timeline. The playhead moves with playback.
- **FR-21.** During playback, show the current interpolated position, a linear interpolation between neighbouring points.
- **FR-22.** Changing any post-processing setting (FR-12 to FR-18) updates the result from the cache within 1 s for a 1-hour video on a typical desktop. No new decode is needed.
- **FR-23.** The user can save the script. The default location is next to the video, with the same base name and the script extension. Overwriting an existing file asks for confirmation.
- **FR-24.** Opening a video automatically loads a script with the matching base name if one exists.

### 3.6 Progress and control
- **FR-25.** Generation runs in the background. The interface stays responsive and shows the percentage done, the time remaining and the current stage.
- **FR-26.** The user can cancel at any time. Cancelling stops work within 1 s, keeps the previous script unchanged and writes no partial file.
- **FR-27.** Only one generation runs per video at a time. Starting a second one asks the user whether to restart.

### 3.7 Hardware acceleration
- **FR-28.** Video decoding and the detector should use the GPU when available and fall back to the CPU automatically. The fallback is logged once, and the user is not interrupted.
- **FR-29.** Results must be identical, within the §8 tolerances, regardless of GPU or CPU.

---

## 4. Data

### 4.1 Inputs
| Input | Source | Required |
|---|---|---|
| Video file | User selection; any container and codec the media library can decode | yes |
| ROI, axis, inverted, time range | User, or the detector proposal | yes (has defaults) |
| Settings (§4.3) | Settings store; defaults below | no |
| Detector model | A file the integrator supplies: a model they trained or are licensed to use. None is shipped with this spec. | no |

### 4.2 Detector contract (optional component)
- **Input:** one RGB image with values from 0 to 1. The model declares its square input size, for example 640. Images are letterboxed to keep the aspect ratio.
- **Output:** a list of `(label, confidence 0–1, box x, y, w, h)` in original frame coordinates, after non-maximum suppression with IoU 0.45.
- **Target label:** configurable. Only detections with confidence of at least 0.35 are used.
- **Missing or failing detector:** the feature still works in manual mode and the automatic-mode button is disabled with an explanation.

### 4.3 Settings and defaults
| Setting | Default | Valid range |
|---|---|---|
| analysis width | 640 px | 160–3840 |
| analysis rate cap | off (every frame) | 5–240 fps |
| detector refresh N | 15 frames | 1–600 |
| max-gap | 500 ms | 0–5000 |
| outlier factor / k | 5 / 3 | 2–20 / 1–15 |
| smoothing window | 120 ms | 0–1000 |
| baseline window | 4000 ms | 500–30000, or off |
| min amplitude | 2 % | 0–50 % |
| min stroke | 10 | 0–100 |
| min interval | 60 ms | 0–1000 |
| max speed | off | 50–2000 units/s |
| simplify tolerance | 2 | 0–20, or off |

Out-of-range values are rejected at input with a message. They are never silently clamped.

### 4.4 Output script (neutral default schema)
The script is a UTF-8 JSON document:

```json
{
  "format": "motion-script",
  "version": 1,
  "source": { "file": "clip.mp4", "duration_ms": 123456 },
  "generator": { "settings": { "...": "values from 4.3" }, "range_ms": [0, 123456] },
  "inverted": false,
  "points": [ { "t": 0, "p": 50 }, { "t": 420, "p": 93 } ]
}
```

If the host application already has a script format, write that format instead, mapping `t` to its time field and `p` to its position field. The neutral schema is the fallback.

### 4.5 Cache
- **Key:** file size + modification time + a hash of the first and last 1 MB of the video + ROI + axis + analysis settings (FR-7 to FR-9).
- **Value:** the raw samples (FR-10).
- **Lifetime:** the cache is safe to delete at any time.

---

## 5. Pipeline overview

```
video ─► decode (GPU→CPU fallback) ─► downscale ─► follow ROI (± detector) ─► raw samples ─► [cache]
                                                                                     │
  points ◄─ simplify ◄─ speed limit ◄─ extrema ◄─ detrend+normalise ◄─ smooth ◄─ outliers ◄─ gaps
     │
     ├─► timeline graph + live position
     └─► save file
```

---

## 6. Edge cases and error handling

| # | Situation | Required behaviour |
|---|---|---|
| E1 | File missing, unreadable or not a video | Error "Could not open video: <reason>"; nothing changes |
| E2 | Video has audio only | Error "No video stream found" |
| E3 | Corrupt frames in the middle | Skip them as gaps (FR-12), log the count, continue. More than 20 % corrupt → finish, but warn the user. |
| E4 | Variable frame rate or timestamp jumps | Use real timestamps. A backwards timestamp is dropped and logged. |
| E5 | ROI is empty, smaller than 8×8 px, or outside the frame | Refuse to start; explain the minimum size |
| E6 | Target lost (confidence 0) for longer than max-gap | Split into segments; no points inside the gap; re-acquire using the detector if available |
| E7 | No motion (static scene) | Script with two points at position 50 (start, end) and an info message "No motion found" |
| E8 | Motion too fast for the analysis rate (aliasing) | If the detected stroke rate is above `0.4 × analysis fps`, warn "Analysis rate too low for this motion" |
| E9 | Video shorter than 1 s, or range shorter than 1 s | Allowed; produces a valid script, possibly just start and end points |
| E10 | Very long video (more than 3 h) | Works with bounded memory: raw samples are streamed to the cache, and the RAM limit for the stage is 500 MB |
| E11 | Detector file missing, corrupt or incompatible | Automatic mode disabled with the reason; manual mode unaffected |
| E12 | GPU initialisation fails mid-run | Switch to CPU from the current frame and continue; log once |
| E13 | Disk full or permission denied on save | Error with the path; the in-memory script is kept and the user can choose another location |
| E14 | Script file already exists | Ask before overwriting (FR-23) |
| E15 | User cancels | See FR-26 |
| E16 | Settings out of range | See §4.3; field highlighted, start disabled |
| E17 | Rotated video (rotation metadata) | Frames are shown and analysed upright; the axis refers to the upright image |
| E18 | Loading a script file that is invalid | Error "Script file invalid: <reason>"; the video still plays |

---

## 7. Non-functional requirements
- **N1. Speed.** Analysis of 1080p30 video at 640 px analysis width runs at least 2× real-time on a mid-range CPU, using the model-free method.
- **N2. Determinism.** The same input and settings give a byte-identical script.
- **N3. Responsiveness.** The interface never blocks for more than 100 ms during generation.
- **N4. Observability.** Each stage logs its start, end, duration and counts (samples, gaps, points).
- **N5. Independence.** Each post-processing stage (FR-12 to FR-18) is a pure function of `(samples, settings)` and can be unit-tested without video.

---

## 8. Test cases

Synthetic test videos are generated by the test suite. A **moving bar** video is a white 40×40 square on a black 320×240 background, whose vertical centre follows a known function `y(t)`.

### 8.1 End to end
| ID | Given | When | Then |
|---|---|---|---|
| T1 | Moving bar, `y` = sine at 1 Hz, amplitude 80 px, 10 s, 30 fps, constant fps | Generate, manual ROI around the square, default settings | 20 ± 1 points (10 peaks + 10 valleys). Peak times within ±40 ms of true peaks. Peaks ≥ 90, valleys ≤ 10. |
| T2 | Same as T1 | Generate with inverted = true | Every position equals `100 − T1 position` (±1) at the same times |
| T3 | Same as T1 but the square moves horizontally | Axis = horizontal | Same result as T1 |
| T4 | Same as T1 at 60 fps, variable frame rate (random frame drops of up to 30 %) | Generate | Point times within ±40 ms of true extrema; no time error builds up |
| T5 | Static square, 5 s | Generate | Exactly 2 points, both p = 50; "No motion found" reported (E7) |
| T6 | Sine 1 Hz with a 2 s black gap in the middle (square removed) | Generate, max-gap 500 ms | No points inside the gap; points on both sides correct as in T1 |
| T7 | Sine 1 Hz with a 300 ms gap | Generate | Gap filled; point count as in T1 |
| T8 | Sine 4 Hz at 10 fps | Generate | Aliasing warning shown (E8) |
| T9 | Sine 1 Hz plus a slow upward drift of 60 px over 10 s | Generate | Peaks ≥ 85 and valleys ≤ 15 across the whole clip (drift removed) |
| T10 | T1 video | Generate on range 2 s–5 s | All points within [2000, 5000] ms; 6 ± 1 points |
| T11 | T1 video, cancel at 50 % | Cancel | Stops within 1 s; previous script unchanged; no file written |
| T12 | T1 video, generate twice | Compare | Byte-identical scripts (N2) |

### 8.2 Post-processing units (no video needed)
| ID | Input samples | Settings | Expected |
|---|---|---|---|
| U1 | Sine with one spike sample 10× the amplitude | default | Spike removed; no extra extremum |
| U2 | Samples with gaps of 200 ms and 900 ms | max-gap 500 | First gap filled; second splits into 2 segments |
| U3 | Small ripple (amplitude 3) on a large sine (amplitude 100) | min stroke 10 | Ripple produces no points |
| U4 | Two extrema 30 ms apart | min interval 60 | The more extreme one is kept |
| U5 | Points 0→100 in 100 ms | max speed 500 | Movement reduced to ≤ 50 units, centred on the midpoint; times unchanged |
| U6 | Straight line of 100 points from 0 to 100 | simplify tol 2 | Only first and last remain |
| U7 | Constant signal | default | Constant 50 |
| U8 | Any output | – | Times strictly increasing integers; positions integers 0–100 |
| U9 | Smoothing on a sine | window 120 ms | Peak times shift by ≤ 1 sample (zero-phase) |

### 8.3 Errors and files
| ID | Given | Expected |
|---|---|---|
| X1 | Non-existent path | E1 message; no crash |
| X2 | Text file renamed to `.mp4` | E1 message |
| X3 | Audio-only file | E2 message |
| X4 | ROI 4×4 px | Start refused (E5) |
| X5 | Setting outside range (min stroke = 150) | Rejected (E16) |
| X6 | Save to a read-only folder | E13 message; script kept in memory |
| X7 | Save, then reopen the video | Script auto-loaded (FR-24); same points |
| X8 | Load a malformed script JSON | E18 message; video plays |
| X9 | Detector missing | Automatic mode disabled with reason; T1 still passes in manual mode |
| X10 | Video with 90° rotation metadata | Vertical motion in the upright image gives a T1-like result |

### 8.4 Pass criteria
Every test above passes on the CPU-only path. Tests marked for a GPU are optional and must give the same results within the stated tolerances.

---

## 9. Provenance note
This specification was written from externally observable behaviour and general domain knowledge. It deliberately does not reproduce any other product's source code, file formats, identifiers, models or assets. Implementers must supply their own detector models (if any) under a licence they hold.

---

## 10. Status in SamNPlayer Next (D-158)

The Generator already did most of this before the spec existed; what was missing came in as opt-in switches. With every switch off, the output is byte-identical to before (test "spec switches: all off").

**Already in place (0.7 port)**

| Spec | Where |
|---|---|
| FR-1/3 open, real timestamps | `vision/FrameReader` (PTS in µs) |
| FR-4 box, axis, invert | `GenerateRequest.box`, `axis`, `signal.invert` |
| FR-5 detector proposal | Find tip, ONNX detector (`vision/onnx_detector`) |
| FR-9 following the box | CSRT tracker + appearance memory, camera compensation, scene cuts (`engine/track_box`) |
| FR-14–16, FR-18 | Savitzky–Golay, detrend, percentile normalisation, peaks with prominence, minimum interval, RDP (`engine/post_track`) |
| FR-17 speed limit | `LimitSpeed` (from the previous point, see below) |
| FR-23 overwrite guard, FR-25–27 run, cancel, one at a time | `GenerateRunner` |

**New, opt-in**

| Spec | Switch | Where |
|---|---|---|
| FR-6 end time | `GenerateRequest.end_ms`, `TrackOptions.end_ms` | `track_box.cpp` |
| FR-7 analysis rate cap | `max_analysis_fps` | `track_box.cpp` |
| FR-10 lost samples | `TrackResult::lost` (always filled) | `track_box.cpp` |
| FR-12 gaps | *Bridge short tracking gaps* → `max_gap_ms` 500 | `PlanGaps` |
| FR-13 outliers | *Ignore single-frame jumps* → `outlier_mad_factor` 5 | `FindOutliers` |
| FR-11 cache | *Reuse the tracking* → `config_dir()/cache/track` | `track_cache.cpp` |
| E8 aliasing | always on (a note only) | `AliasingWarning` |

End time and analysis rate have no control on the Generator page yet. The comparison controller's manifest reaches them (`end_ms`, `max_analysis_fps`).

**Default recipe "Recommended" (D-160, 27 Oct)**

The Generator's default is the profile `recommended`: Autotune's recipe with FR-13 outliers (5 × MAD), FR-17 speed limit 500 units/s and FR-16 minimum interval 60 ms (100 ms on a stroker). FR-14's 120 ms smoothing, the 4 s baseline and FR-12 gap handling measured worse on synthetic tracks with known strokes and on the Owner's clip, so they are not in it (numbers in D-160). Normal, Soft and Autotune are unchanged.

**Kept from 0.7 on purpose (differs from this spec)**

| Spec | SamNPlayer | Why |
|---|---|---|
| E7/T5/U7: no motion gives 2 points at 50 | An error: "no discernible motion … check the box" | A flat script hides a wrong box (LESSONS GD-11) |
| FR-17/U5: speed limit centred on the midpoint | Moves from the previous point | 0.7 parity (D-135) |
| FR-15 min amplitude, FR-16 min stroke in units | Prominence as a fraction of the span (`peak_prominence`) | Same effect, 0.7's unit |

**Not covered yet**

| Spec | Why |
|---|---|
| T4 as a real variable-frame-rate file | OpenCV's writer cannot mux variable frame rate; the test drops 30 % of the track instead |
| X3 audio-only file, X10 rotation metadata | The test cannot mux such files without FFmpeg's command-line tool |
| E5/X4 minimum box size | Not checked in the engine |
| FR-28/29 GPU decode | Decoding is OpenCV's FFmpeg backend on the CPU |
