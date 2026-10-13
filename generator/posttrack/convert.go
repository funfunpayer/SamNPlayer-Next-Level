package posttrack

import (
	"fmt"
	"math"

	"github.com/funfunpayer/SamNPlayer/funscript"
)

// Options controls PositionsToActions — mirrors positions_to_funscript kwargs.
type Options struct {
	Invert            bool
	SmoothWindow      int
	MinPeakDistanceMs int
	RDPTolerance      float64
	NormPercentile    float64
	SceneRanges       [][2]int // inclusive start, exclusive end in sample indices
	AdaptiveError     float64
	MinIntervalMs     float64
	DynamicRangeMs    float64
	PeakProminence    float64 // fraction of position span; 0 = off
	// DetrendWindowMs: rolling-mean high-pass before normalize (0 = off).
	// Typical: 2000–4000 ms — kills slow camera/lighting drift.
	DetrendWindowMs float64
	// BandpassLowHz / BandpassHighHz: device-safe stroke band (0 = edge off).
	// Typical FunGen/Flow band: 0.5–4 Hz.
	BandpassLowHz  float64
	BandpassHighHz float64
}

// DefaultOptions matches generate_funscript.positions_to_funscript defaults.
func DefaultOptions() Options {
	return Options{
		SmoothWindow:      11,
		MinPeakDistanceMs: 150,
		NormPercentile:    2.0,
		MinIntervalMs:     100.0,
	}
}

// Result is the keyframe actions plus the dense normalised curve (Quality
// Doctor needs the dense signal; keyframes alone look rhythmic even when the
// tracker wandered).
type Result struct {
	Actions  []funscript.Action
	DenseAt  []float64
	DensePos []float64
}

// PositionsToActions converts a raw tracking curve into funscript actions.
// Port of generate_funscript.positions_to_funscript — same stages, same order.
func PositionsToActions(timestampsMs []float64, yPositions []float64, opts Options) (Result, error) {
	n := len(yPositions)
	if n == 0 || len(timestampsMs) != n {
		return Result{}, fmt.Errorf("posttrack: timestamps and positions length mismatch")
	}

	smoothWindow := opts.SmoothWindow
	if smoothWindow <= 0 {
		smoothWindow = 11
	}
	if n < smoothWindow {
		// Python: smooth_window = n - 1 if n % 2 == 0 else n
		if n%2 == 0 {
			smoothWindow = n - 1
		} else {
			smoothWindow = n
		}
	}

	var smoothed []float64
	if smoothWindow < 5 {
		smoothed = append([]float64(nil), yPositions...)
	} else {
		if smoothWindow%2 == 0 {
			smoothWindow++
		}
		smoothed = savgol(yPositions, smoothWindow, 3)
	}

	stepMs := medianDiff(timestampsMs)
	sampleHz := 1000.0 / math.Max(stepMs, 1.0)

	// Drift removal then stroke-band filter (FunGen / Funscript-Flow inspired).
	// Order: savgol → detrend → bandpass → dynamic-range → percentile normalize.
	if opts.DetrendWindowMs > 0 && n > 4 {
		smoothed = RollingDetrend(smoothed, opts.DetrendWindowMs, stepMs)
	}
	if (opts.BandpassLowHz > 0 || opts.BandpassHighHz > 0) && n > 8 {
		smoothed = Bandpass(smoothed, sampleHz, opts.BandpassLowHz, opts.BandpassHighHz)
	}

	if opts.DynamicRangeMs > 0 && len(timestampsMs) > 4 {
		window := int(opts.DynamicRangeMs / math.Max(stepMs, 1.0))
		smoothed = dynamicRangeNormalize(smoothed, window, 5.0, 0.12)
	}

	pos := make([]float64, n)
	if len(opts.SceneRanges) > 0 {
		for i := range pos {
			pos[i] = smoothed[i]
		}
		for _, rg := range opts.SceneRanges {
			start, end := rg[0], rg[1]
			if end > n {
				end = n
			}
			if end-start < 3 {
				continue
			}
			seg := smoothed[start:end]
			var sLo, sHi float64
			if opts.NormPercentile > 0 {
				sLo = percentile(seg, opts.NormPercentile)
				sHi = percentile(seg, 100.0-opts.NormPercentile)
			} else {
				sLo, sHi = minMax(seg)
			}
			if sHi-sLo < 1e-6 {
				sLo, sHi = minMax(seg)
			}
			if sHi-sLo < 1e-6 {
				for i := start; i < end; i++ {
					pos[i] = 0.5
				}
			} else {
				for i := start; i < end; i++ {
					pos[i] = clip01((smoothed[i] - sLo) / (sHi - sLo))
				}
			}
		}
		for i := range pos {
			pos[i] *= 100.0
		}
	} else {
		var lo, hi float64
		if opts.NormPercentile > 0 {
			lo = percentile(smoothed, opts.NormPercentile)
			hi = percentile(smoothed, 100.0-opts.NormPercentile)
		} else {
			lo, hi = minMax(smoothed)
		}
		if hi-lo < 1e-6 {
			lo, hi = minMax(smoothed)
		}
		if hi-lo < 1e-6 {
			return Result{}, fmt.Errorf("posttrack: no discernible motion in the tracked region — check tip ROI size/placement (avoid near-full-frame boxes); Autotune filters need visible stroke motion")
		}
		for i := range smoothed {
			pos[i] = clip01((smoothed[i]-lo)/(hi-lo)) * 100.0
		}
	}

	if !opts.Invert {
		for i := range pos {
			pos[i] = 100.0 - pos[i]
		}
	}

	fpsEstimate := 30.0
	if n > 1 {
		fpsEstimate = 1000.0 / medianDiff(timestampsMs)
	}
	minDistanceFrames := int(float64(opts.MinPeakDistanceMs) / 1000.0 * fpsEstimate)
	if minDistanceFrames < 1 {
		minDistanceFrames = 1
	}

	prominence := 0.0
	if opts.PeakProminence > 0 {
		span := ptp(pos)
		if span > 1e-9 {
			prominence = span * opts.PeakProminence
		}
	}

	peakIdx := findPeaks(pos, minDistanceFrames, prominence)
	neg := make([]float64, n)
	for i, v := range pos {
		neg[i] = -v
	}
	valleyIdx := findPeaks(neg, minDistanceFrames, prominence)

	keySet := map[int]bool{0: true, n - 1: true}
	for _, i := range peakIdx {
		keySet[i] = true
	}
	for _, i := range valleyIdx {
		keySet[i] = true
	}
	keyframeIdx := make([]int, 0, len(keySet))
	for i := range keySet {
		keyframeIdx = append(keyframeIdx, i)
	}
	keyframeIdx = uniqueSorted(keyframeIdx)

	if opts.AdaptiveError > 0 {
		keyframeIdx = refineKeyframes(timestampsMs, pos, keyframeIdx, opts.AdaptiveError, 2.0)
	}

	// MinIntervalMs <= 0 disables the filter (Python: 0 = abschalten).
	if opts.MinIntervalMs > 0 {
		keyframeIdx = enforceMinInterval(timestampsMs, pos, keyframeIdx, opts.MinIntervalMs)
	}

	if opts.RDPTolerance > 0 && len(keyframeIdx) > 2 {
		keyframeIdx = rdpSimplify(timestampsMs, pos, keyframeIdx, opts.RDPTolerance)
	}

	actions := make([]funscript.Action, len(keyframeIdx))
	for i, idx := range keyframeIdx {
		actions[i] = funscript.Action{
			At:  int64(timestampsMs[idx]),
			Pos: int(math.Round(pos[idx])),
		}
	}

	denseAt := append([]float64(nil), timestampsMs...)
	densePos := append([]float64(nil), pos...)
	return Result{Actions: actions, DenseAt: denseAt, DensePos: densePos}, nil
}

// LimitSpeed caps per-action position change — port of limit_speed().
func LimitSpeed(actions []funscript.Action, maxUnitsPerSecond float64) ([]funscript.Action, int) {
	if maxUnitsPerSecond <= 0 || len(actions) < 2 {
		return append([]funscript.Action(nil), actions...), 0
	}
	limited := make([]funscript.Action, 0, len(actions))
	limited = append(limited, actions[0])
	changed := 0
	for _, action := range actions[1:] {
		dtMs := action.At - limited[len(limited)-1].At
		if dtMs <= 0 {
			limited = append(limited, action)
			continue
		}
		maxDelta := maxUnitsPerSecond * float64(dtMs) / 1000.0
		delta := float64(action.Pos - limited[len(limited)-1].Pos)
		if abs(delta) > maxDelta {
			sign := 1.0
			if delta < 0 {
				sign = -1
			}
			newPos := float64(limited[len(limited)-1].Pos) + sign*maxDelta
			if newPos < 0 {
				newPos = 0
			}
			if newPos > 100 {
				newPos = 100
			}
			limited = append(limited, funscript.Action{At: action.At, Pos: int(math.Round(newPos))})
			changed++
		} else {
			limited = append(limited, action)
		}
	}
	return limited, changed
}

// ClampActionsPos clamps positions into [lo, hi] for tf/tj profiles.
func ClampActionsPos(actions []funscript.Action, lo, hi int) []funscript.Action {
	out := make([]funscript.Action, len(actions))
	for i, a := range actions {
		pos := a.Pos
		if pos < lo {
			pos = lo
		} else if pos > hi {
			pos = hi
		}
		out[i] = funscript.Action{At: a.At, Pos: pos}
	}
	return out
}
