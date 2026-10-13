package generator

import (
	"context"
	"errors"
	"fmt"
	"time"

	"github.com/funfunpayer/SamNPlayer/funscript"
	"github.com/funfunpayer/SamNPlayer/generator/posttrack"
	"github.com/funfunpayer/SamNPlayer/generator/simpletrack"
	"github.com/funfunpayer/SamNPlayer/logging"
)

// SimpleTrackingAvailable is always true: tracking uses videox/ffmpeg (no
// cgo/OpenCV). Runtime still needs ffmpeg on PATH — same as videox tests.
func SimpleTrackingAvailable() bool { return true }

// NativeGoGenerateAvailable reports whether any Python-free generate path
// can run for CSRT-style single-ROI options (OpenCV CSRT and/or simpletrack).
func NativeGoGenerateAvailable() bool {
	return NativeTrackingAvailable() || SimpleTrackingAvailable()
}

// GenerateNativeSimple runs simpletrack (videox NCC) + posttrack without
// Python/OpenCV. Used automatically when NativePipeline is eligible and
// CSRT/OpenCV is not linked — notably Windows release builds. Supports
// single-ROI and Tf/Tj two-point (ROI2).
func GenerateNativeSimple(ctx context.Context, videoPath string, roi ROI, outputPath string, opts Options, onProgress func(line string), onPercent func(pct int)) error {
	if ctx == nil {
		ctx = context.Background()
	}
	opts = withTipROI(opts, roi)
	if !nativeOptionsEligible(opts, roi) {
		return fmt.Errorf("generator: native simple pipeline not eligible for these options")
	}
	if err := ctx.Err(); err != nil {
		return err
	}

	progress := func(line string) {
		logging.Info("generator/native-simple: " + line)
		if onProgress != nil {
			onProgress(line)
		}
	}
	twoPoint := distancePartnersActive(opts)
	if opts.ROI2.W > 0 && opts.ROI2.H > 0 && !twoPoint {
		progress("storing contact mark (feel) — tip CSRT writes the stroke (not tip↔partner distance)")
	}
	multi := twoPoint && len(opts.ExtraTargets) > 0
	if multi {
		progress("Go multi-partner simpletrack (NCC) — PreferSimpletrack / lab path")
	} else if twoPoint {
		progress("Go two-point simpletrack (NCC) — PreferSimpletrack / lab path")
	} else {
		progress("Go simpletrack (NCC over ffmpeg) — PreferSimpletrack / lab path")
	}

	start := time.Now()
	axis := opts.Axis
	if axis == "" {
		axis = "auto"
	}
	stOpts := simpletrack.Options{
		MaxFrames:         opts.MaxFrames,
		StartTimeSec:      opts.StartTimeSec,
		Axis:              axis,
		Cancel:            func() bool { return ctx.Err() != nil },
		FixedB:            opts.ROI2Fixed,
		OnProgress:        percentFromProgress(onPercent),
		CaptureTrajectory: opts.CaptureTrajectory,
	}

	var tr simpletrack.Result
	var err error
	if twoPoint {
		partners := []simpletrack.Partner{{
			ROI:   simpletrack.Rect{X: opts.ROI2.X, Y: opts.ROI2.Y, W: opts.ROI2.W, H: opts.ROI2.H},
			Fixed: opts.ROI2Fixed,
		}}
		for _, t := range opts.ExtraTargets {
			if t.W <= 0 || t.H <= 0 {
				continue
			}
			partners = append(partners, simpletrack.Partner{
				ROI:   simpletrack.Rect{X: t.X, Y: t.Y, W: t.W, H: t.H},
				Fixed: t.Fixed,
			})
		}
		tr, err = simpletrack.TrackMultiPoints(ctx, videoPath,
			simpletrack.Rect{X: roi.X, Y: roi.Y, W: roi.W, H: roi.H},
			partners, stOpts)
	} else {
		tr, err = simpletrack.TrackROI(ctx, videoPath, simpletrack.Rect{X: roi.X, Y: roi.Y, W: roi.W, H: roi.H}, stOpts)
	}
	if err != nil {
		if errors.Is(err, simpletrack.ErrCanceled) || errors.Is(ctx.Err(), context.Canceled) {
			return context.Canceled
		}
		if errors.Is(ctx.Err(), context.DeadlineExceeded) {
			return context.DeadlineExceeded
		}
		return fmt.Errorf("generator/native-simple: tracking: %w", err)
	}
	if onPercent != nil {
		onPercent(100)
	}
	progress(fmt.Sprintf("%d Frames getrackt (%dx%d) in %s",
		len(tr.TimestampsMs), tr.Width, tr.Height, time.Since(start).Round(time.Millisecond)))

	ntr := nativeTrackResult{
		TimestampsMs: tr.TimestampsMs,
		Positions:    tr.Positions,
		Width:        tr.Width,
		Height:       tr.Height,
		LostFrames:   tr.Stats.TrackerLostFrames,
		TotalFrames:  tr.Stats.TotalFrames,
		VertRange:    tr.Stats.VerticalRange,
		ValidFrames:  tr.Stats.ValidFrames,
		Confidence:   tr.Stats.Confidence,
		Reason:       tr.Stats.Reason,
		LostFlags:    tr.LostFlags,
		TrajectoryA:  convertSimpletrackPoints(tr.TrajectoryA),
		TrajectoryB:  convertSimpletrackPoints(tr.TrajectoryB),
	}
	backend := "ncc"
	if multi {
		backend = "multi_point_ncc"
	} else if twoPoint {
		backend = "two_point_ncc"
	}
	return finishNativeGenerate(ctx, videoPath, outputPath, opts, ntr, "simpletrack", backend, progress, onPercent, start)
}

// convertSimpletrackPoints maps simpletrack.Point onto the shared
// NativePoint used by native.go (mirrors convertTrackcvPoints).
func convertSimpletrackPoints(pts []simpletrack.Point) []NativePoint {
	if len(pts) == 0 {
		return nil
	}
	out := make([]NativePoint, len(pts))
	for i, p := range pts {
		out[i] = NativePoint{X: p.X, Y: p.Y}
	}
	return out
}

// finishNativeGenerate shared posttrack + quality + write for CSRT and simple.
func finishNativeGenerate(
	ctx context.Context,
	videoPath string,
	outputPath string,
	opts Options,
	tr nativeTrackResult,
	tracking, backend string,
	progress func(string),
	onPercent func(int),
	start time.Time,
) error {
	ts := make([]float64, len(tr.TimestampsMs))
	for i, v := range tr.TimestampsMs {
		ts[i] = float64(v)
	}

	buildPost := func(smooth, peakDist int, adaptive, normPct float64) (posttrack.Options, error) {
		postOpts := posttrack.DefaultOptions()
		postOpts.Invert = opts.Invert
		if smooth > 0 {
			postOpts.SmoothWindow = smooth
		}
		if peakDist > 0 {
			postOpts.MinPeakDistanceMs = peakDist
		}
		postOpts.RDPTolerance = opts.RDPTolerance
		if normPct > 0 {
			postOpts.NormPercentile = normPct
		} else if normPct < 0 {
			postOpts.NormPercentile = 0
		}
		postOpts.AdaptiveError = adaptive
		if opts.MinActionIntervalMs > 0 {
			postOpts.MinIntervalMs = opts.MinActionIntervalMs
		} else if opts.MinActionIntervalMs < 0 {
			postOpts.MinIntervalMs = 0
		}
		postOpts.DynamicRangeMs = opts.DynamicRangeMs
		postOpts.PeakProminence = opts.PeakProminence
		postOpts.DetrendWindowMs = opts.DetrendWindowMs
		postOpts.BandpassLowHz = opts.BandpassLowHz
		postOpts.BandpassHighHz = opts.BandpassHighHz
		if opts.Profile == "weich" {
			if postOpts.PeakProminence == 0 {
				postOpts.PeakProminence = 0.35
			}
			if peakDist == 0 {
				postOpts.MinPeakDistanceMs = 200
			}
			if postOpts.DynamicRangeMs == 0 {
				postOpts.DynamicRangeMs = 3000
			}
		}
		if opts.Profile == "autotune" {
			// Multi-filter recipe inspired by FunGen Ultimate Autotune /
			// Funscript-Flow — classical track still writes the script.
			if postOpts.DetrendWindowMs == 0 {
				postOpts.DetrendWindowMs = 3000
			}
			if postOpts.BandpassLowHz == 0 && postOpts.BandpassHighHz == 0 {
				postOpts.BandpassLowHz = 0.5
				postOpts.BandpassHighHz = 4.0
			}
			if postOpts.DynamicRangeMs == 0 {
				postOpts.DynamicRangeMs = 3000
			}
			if postOpts.PeakProminence == 0 {
				postOpts.PeakProminence = 0.2
			}
		}
		return postOpts, nil
	}

	smooth := opts.SmoothWindow
	peakDist := opts.MinPeakDistanceMs
	adaptive := opts.AdaptiveKeyframeError
	normPct := opts.NormPercentile

	runSignal := func(smooth, peakDist int, adaptive, normPct float64) ([]funscript.Action, posttrack.Result, funscript.ScriptQualityResult, error) {
		postOpts, _ := buildPost(smooth, peakDist, adaptive, normPct)
		result, err := posttrack.PositionsToActions(ts, tr.Positions, postOpts)
		if err != nil {
			return nil, posttrack.Result{}, funscript.ScriptQualityResult{}, err
		}
		actions := result.Actions
		maxSpeed := opts.MaxSpeed
		if maxSpeed <= 0 && opts.Profile == "autotune" {
			maxSpeed = 400
		}
		if maxSpeed > 0 {
			actions, _ = posttrack.LimitSpeed(actions, maxSpeed)
		}
		if funscript.IsDistanceProfile(opts.Profile) {
			actions = posttrack.ClampActionsPos(actions, 20, 90)
		}
		var lostFrac *float64
		if tr.TotalFrames > 0 {
			f := float64(tr.LostFrames) / float64(tr.TotalFrames)
			lostFrac = &f
		}
		var motionFrac *float64
		if tr.Height > 0 && tr.VertRange > 0 {
			f := tr.VertRange / float64(tr.Height)
			motionFrac = &f
		}
		var durationMs *float64
		if len(actions) > 0 {
			d := float64(actions[len(actions)-1].At)
			durationMs = &d
		}
		quality := funscript.EvaluateDenseQuality(actions, funscript.DenseQualityInput{
			DenseAt:             result.DenseAt,
			DensePos:            result.DensePos,
			TrackerLostFraction: lostFrac,
			MotionRangeFraction: motionFrac,
			VideoDurationMs:     durationMs,
			ValidFrames:         tr.ValidFrames,
			Confidence:          tr.Confidence,
			Reason:              tr.Reason,
		})
		return actions, result, quality, nil
	}

	actions, _, quality, err := runSignal(smooth, peakDist, adaptive, normPct)
	if err != nil {
		return fmt.Errorf("generator/native: signal path: %w", err)
	}
	if opts.MaxSpeed > 0 {
		progress(fmt.Sprintf("Geschwindigkeitsbegrenzung aktiv (%.0f Einheiten/s)", opts.MaxSpeed))
	}

	// Auto-Retry: vary signal params only (same as Python). Tracking issues
	// are not retriable here.
	if opts.AutoRetry && !quality.Passed {
		lostHeavy := tr.TotalFrames > 0 && float64(tr.LostFrames)/float64(tr.TotalFrames) > 0.5
		motionTiny := tr.Height > 0 && tr.VertRange/float64(tr.Height) < 0.03
		if lostHeavy || motionTiny {
			progress("Auto-Retry übersprungen: Problem liegt im Tracking, nicht in der Signalverarbeitung")
		} else {
			type cand struct {
				smooth, peakDist int
				adaptive, norm   float64
			}
			baseSmooth := smooth
			if baseSmooth <= 0 {
				baseSmooth = 11
			}
			basePeak := peakDist
			if basePeak <= 0 {
				basePeak = 150
			}
			candidates := []cand{
				{max(5, baseSmooth-4), basePeak, adaptive, normPct},
				{baseSmooth + 10, basePeak, adaptive, normPct},
				{baseSmooth + 20, max(250, basePeak*2), adaptive, normPct},
				{baseSmooth, basePeak, adaptive, 0},
			}
			progress(fmt.Sprintf("Quality check failed (score %.2f) — trying %d alternative signal settings",
				quality.Score, len(candidates)))
			bestActions, bestQuality := actions, quality
			for _, c := range candidates {
				if err := ctx.Err(); err != nil {
					return err
				}
				acts, _, q, err := runSignal(c.smooth, c.peakDist, c.adaptive, c.norm)
				if err != nil {
					continue
				}
				if q.Score > bestQuality.Score {
					bestActions, bestQuality = acts, q
				}
				if q.Passed {
					break
				}
			}
			actions, quality = bestActions, bestQuality
			progress(fmt.Sprintf("Bestes Ergebnis nach Auto-Retry: Score %.2f passed=%v", quality.Score, quality.Passed))
		}
	}

	progress(fmt.Sprintf("%d Keyframes aus %d Frames", len(actions), len(tr.TimestampsMs)))

	if opts.MaxOutputMs > 0 {
		before := len(actions)
		actions = truncateActionsMs(actions, opts.MaxOutputMs)
		if len(actions) < before {
			progress(fmt.Sprintf("License/trial cap: truncated to %dms (%d → %d actions)",
				opts.MaxOutputMs, before, len(actions)))
		}
	}

	if err := ctx.Err(); err != nil {
		return err
	}

	progress(fmt.Sprintf("Quality Doctor (dense): score=%.2f passed=%v", quality.Score, quality.Passed))

	var audioMeta *funscript.AudioCheck
	if opts.AudioCheck {
		progress("Audio tempo check (Go, post-hoc)…")
		audioMeta = CheckAudioTempoContext(ctx, videoPath, actions)
		if err := ctx.Err(); err != nil {
			return err
		}
		if audioMeta == nil {
			progress("Audio tempo check unavailable (no ffmpeg / no audio track)")
		} else {
			switch {
			case audioMeta.ScriptHz != nil && audioMeta.AudioHz != nil:
				progress(fmt.Sprintf("Audio tempo check: script %.2fHz, audio %.2fHz",
					*audioMeta.ScriptHz, *audioMeta.AudioHz))
			case audioMeta.AudioHz != nil:
				progress(fmt.Sprintf("Audio tempo check: audio %.2fHz (script tempo not estimated)",
					*audioMeta.AudioHz))
			case audioMeta.ScriptHz != nil:
				progress(fmt.Sprintf("Audio tempo check: script %.2fHz (audio tempo not estimated)",
					*audioMeta.ScriptHz))
			}
			AppendQualityAudioHint(quality, audioMeta)
			for _, w := range audioMeta.Warnings {
				progress("warning: " + w)
			}
		}
	}

	if err := writeNativeFunscriptNamed(outputPath, videoPath, actions, opts, tr, quality, audioMeta, tracking, backend); err != nil {
		return err
	}
	progress(fmt.Sprintf("wrote %s (total %s)", outputPath, time.Since(start).Round(time.Millisecond)))
	if onPercent != nil {
		onPercent(100)
	}
	return nil
}

// truncateActionsMs keeps actions with At <= maxMs (inclusive), ensuring ≥2 points.
func truncateActionsMs(actions []funscript.Action, maxMs int64) []funscript.Action {
	if maxMs <= 0 || len(actions) < 2 {
		return actions
	}
	out := make([]funscript.Action, 0, len(actions))
	for _, a := range actions {
		if a.At <= maxMs {
			out = append(out, a)
		}
	}
	if len(out) >= 2 {
		return out
	}
	// Keep first two even if beyond cap (degenerate short script).
	return actions[:2]
}
