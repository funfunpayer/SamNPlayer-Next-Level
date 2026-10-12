package posttrack

import (
	"fmt"
	"math"

	"github.com/funfunpayer/SamNPlayer/funscript"
)

// PreviewRequest holds Expert-tuning knobs for a live postprocess estimate.
// Values mirror GUI Expert fields; PeakProminence 0 = off (no prominence gate).
type PreviewRequest struct {
	SmoothWindow      int
	MinPeakDistanceMs int
	PeakProminence    float64
	RDPTolerance      float64
	AdaptiveError     float64
	MaxSpeed          float64
	// DurationMs of the synthetic probe clip (default 10s).
	DurationMs float64
}

// PreviewResult summarises what the knobs would do on a fixed synthetic probe.
// Not a substitute for tracking — only live feedback for smooth / prominence /
// peak spacing / RDP (DeepFunGen-style postprocess displays).
type PreviewResult struct {
	KeyframeCount int     `json:"keyframeCount"`
	PeakCount     int     `json:"peakCount"`
	ValleyCount   int     `json:"valleyCount"`
	MeanHz        float64 `json:"meanHz"`
	Hint          string  `json:"hint"`
	// Sample is a downsampled keyframe polyline (atMs, pos 0–100) for Expert
	// live SVG — synthetic probe only, never the user's clip.
	Sample []PreviewPoint `json:"sample,omitempty"`
	// RawSample is the dense probe before postprocess (muted underlay).
	RawSample []PreviewPoint `json:"rawSample,omitempty"`
	// Peaks are peak markers on the dense postprocessed curve (for SVG dots).
	Peaks []PreviewPoint `json:"peaks,omitempty"`
}

// PreviewPoint is one keyframe on the Expert postprocess probe curve.
type PreviewPoint struct {
	AtMs int64 `json:"atMs"`
	Pos  int   `json:"pos"`
}

// PreviewStats runs PositionsToActions on a fixed multi-stroke probe signal
// so the GUI can show live-ish keyframe/peak feedback when Expert knobs change.
func PreviewStats(req PreviewRequest) (PreviewResult, error) {
	opts := DefaultOptions()
	if req.SmoothWindow > 0 {
		opts.SmoothWindow = req.SmoothWindow
	}
	if req.MinPeakDistanceMs > 0 {
		opts.MinPeakDistanceMs = req.MinPeakDistanceMs
	}
	opts.PeakProminence = req.PeakProminence
	opts.RDPTolerance = req.RDPTolerance
	opts.AdaptiveError = req.AdaptiveError

	dur := req.DurationMs
	if dur <= 0 {
		dur = 10000
	}
	ts, pos := syntheticProbe(dur, 30.0)
	rawSample := downsampleProbeSeries(ts, pos, 96)
	res, err := PositionsToActions(ts, pos, opts)
	if err != nil {
		return PreviewResult{}, err
	}

	actions := res.Actions
	if req.MaxSpeed > 0 {
		actions, _ = LimitSpeed(actions, req.MaxSpeed)
	}

	// Peak/valley counts from dense normalised curve (same prominence gate).
	stepMs := medianDiff(ts)
	fps := 1000.0 / math.Max(stepMs, 1.0)
	minDist := int(float64(opts.MinPeakDistanceMs) / 1000.0 * fps)
	if minDist < 1 {
		minDist = 1
	}
	prom := 0.0
	if opts.PeakProminence > 0 {
		span := ptp(res.DensePos)
		if span > 1e-9 {
			prom = span * opts.PeakProminence
		}
	}
	peakIdx := findPeaks(res.DensePos, minDist, prom)
	neg := make([]float64, len(res.DensePos))
	for i, v := range res.DensePos {
		neg[i] = -v
	}
	valleys := findPeaks(neg, minDist, prom)
	peakPts := peakMarkers(res.DenseAt, res.DensePos, peakIdx, 48)

	meanHz := 0.0
	if len(actions) >= 2 {
		spanMs := float64(actions[len(actions)-1].At - actions[0].At)
		if spanMs > 0 {
			// Half-cycles ≈ peaks+valleys; report stroke Hz ≈ keyframe rate / 2.
			meanHz = (float64(len(actions)-1) / 2.0) / (spanMs / 1000.0)
		}
	}

	hint := previewHint(req, len(actions), len(peakIdx))
	return PreviewResult{
		KeyframeCount: len(actions),
		PeakCount:     len(peakIdx),
		ValleyCount:   len(valleys),
		MeanHz:        meanHz,
		Hint:          hint,
		Sample:        downsamplePreviewActions(actions, 96),
		RawSample:     rawSample,
		Peaks:         peakPts,
	}, nil
}

// downsampleProbeSeries maps the raw synthetic probe (px-ish) into 0–100 SVG
// points before postprocess — muted underlay for Expert dual-curve view.
func downsampleProbeSeries(ts, pos []float64, maxN int) []PreviewPoint {
	if len(ts) == 0 || len(pos) == 0 || len(ts) != len(pos) {
		return nil
	}
	minP, maxP := pos[0], pos[0]
	for _, v := range pos[1:] {
		if v < minP {
			minP = v
		}
		if v > maxP {
			maxP = v
		}
	}
	span := maxP - minP
	if span < 1e-9 {
		span = 1
	}
	norm := make([]funscript.Action, len(pos))
	for i := range pos {
		n := (pos[i] - minP) / span * 100
		norm[i] = funscript.Action{At: int64(math.Round(ts[i])), Pos: int(math.Round(n))}
	}
	return downsamplePreviewActions(norm, maxN)
}

// peakMarkers converts dense peak indices into SVG points (pos 0–100).
func peakMarkers(denseAt, densePos []float64, idxs []int, maxN int) []PreviewPoint {
	if len(idxs) == 0 || len(denseAt) == 0 || len(densePos) != len(denseAt) {
		return nil
	}
	minP, maxP := densePos[0], densePos[0]
	for _, v := range densePos[1:] {
		if v < minP {
			minP = v
		}
		if v > maxP {
			maxP = v
		}
	}
	span := maxP - minP
	if span < 1e-9 {
		span = 1
	}
	if maxN < 1 {
		maxN = 1
	}
	step := 1
	if len(idxs) > maxN {
		step = (len(idxs) + maxN - 1) / maxN
	}
	out := make([]PreviewPoint, 0, maxN)
	for i := 0; i < len(idxs); i += step {
		idx := idxs[i]
		if idx < 0 || idx >= len(densePos) {
			continue
		}
		n := (densePos[idx] - minP) / span * 100
		out = append(out, PreviewPoint{
			AtMs: int64(math.Round(denseAt[idx])),
			Pos:  int(math.Round(n)),
		})
		if len(out) >= maxN {
			break
		}
	}
	return out
}

// downsamplePreviewActions keeps endpoints and evenly spaced mid points so the
// Expert SVG stays light while still reflecting knob changes.
func downsamplePreviewActions(actions []funscript.Action, maxN int) []PreviewPoint {
	if len(actions) == 0 {
		return nil
	}
	if maxN < 2 {
		maxN = 2
	}
	if len(actions) <= maxN {
		out := make([]PreviewPoint, len(actions))
		for i, a := range actions {
			out[i] = PreviewPoint{AtMs: a.At, Pos: a.Pos}
		}
		return out
	}
	out := make([]PreviewPoint, 0, maxN)
	last := len(actions) - 1
	for i := 0; i < maxN; i++ {
		idx := i * last / (maxN - 1)
		a := actions[idx]
		out = append(out, PreviewPoint{AtMs: a.At, Pos: a.Pos})
	}
	return out
}

func previewHint(req PreviewRequest, keyframes, peaks int) string {
	bits := []string{
		fmt.Sprintf("Probe: %d keyframes, %d peaks", keyframes, peaks),
	}
	if req.PeakProminence <= 0 {
		bits = append(bits, "prominence off (0 = profile default on Create)")
	} else if req.PeakProminence >= 0.4 {
		bits = append(bits, "high prominence — fewer soft strokes kept")
	} else if req.PeakProminence <= 0.15 {
		bits = append(bits, "low prominence — keeps small wiggles")
	}
	if req.SmoothWindow >= 21 {
		bits = append(bits, "wide smooth — calmer, may lag")
	} else if req.SmoothWindow > 0 && req.SmoothWindow <= 5 {
		bits = append(bits, "narrow smooth — more noise")
	}
	if req.MinPeakDistanceMs >= 300 {
		bits = append(bits, "wide peak spacing — slower tempo")
	}
	if req.RDPTolerance > 0 {
		bits = append(bits, fmt.Sprintf("RDP %.1f thins the curve", req.RDPTolerance))
	}
	if req.MaxSpeed > 0 {
		bits = append(bits, fmt.Sprintf("max speed %.0f", req.MaxSpeed))
	}
	out := bits[0]
	for i := 1; i < len(bits); i++ {
		out += " · " + bits[i]
	}
	return out
}

// syntheticProbe builds a noisy multi-Hz stroke-like signal (px-ish amplitude)
// so prominence and smooth window have a visible effect.
func syntheticProbe(durationMs, fps float64) (timestamps []float64, positions []float64) {
	if fps < 1 {
		fps = 30
	}
	n := int(durationMs/1000.0*fps) + 1
	if n < 32 {
		n = 32
	}
	timestamps = make([]float64, n)
	positions = make([]float64, n)
	for i := 0; i < n; i++ {
		t := float64(i) / fps
		timestamps[i] = t * 1000.0
		// Main ~1.2 Hz stroke + softer ~2.4 Hz overtone + tiny jitter.
		main := 40.0 * math.Sin(2*math.Pi*1.2*t)
		soft := 8.0 * math.Sin(2*math.Pi*2.4*t+0.3)
		jitter := 1.5 * math.Sin(2*math.Pi*7.0*t+1.1)
		positions[i] = 100.0 + main + soft + jitter
	}
	return timestamps, positions
}
