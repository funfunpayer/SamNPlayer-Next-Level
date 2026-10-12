package strokepreview

import (
	"context"
	"fmt"
	"io"
	"math"
	"time"

	"github.com/funfunpayer/SamNPlayer/generator/posttrack"
	"github.com/funfunpayer/SamNPlayer/videox"
)

// Options controls the sparse motion probe.
type Options struct {
	// MaxWidth downscales analysis frames (0 → 320). Speed over fidelity.
	MaxWidth int
	// SampleEvery keeps 1 of N decoded frames after fps filter (0 → 2).
	SampleEvery int
	// AnalysisFPS resamples before sampling (0 → 12). Sparse by design.
	AnalysisFPS float64
	// MinPeakDistanceMs minimum gap between extrema (0 → 200).
	MinPeakDistanceMs int
	// MaxSeconds stops after this many seconds of source (0 → full).
	MaxSeconds float64
	// CutMADFactor: frame mean-abs-diff above median*factor → cut flag (0 → 4).
	CutMADFactor float64
	// PanShiftPx: mean horizontal shift above this (at MaxWidth) → pan (0 → 2.5).
	PanShiftPx float64
}

// Extrema is one peak (up endpoint) or valley (logical down point).
type Extrema struct {
	AtMs  int     `json:"at_ms"`
	Kind  string  `json:"kind"` // "up" | "down"
	Value float64 `json:"value"`
}

// CutEvent is a hard scene-cut candidate from a MAD spike.
type CutEvent struct {
	AtMs int     `json:"at_ms"`
	MAD  float64 `json:"mad"`
}

// Report is the Stage-A preview — timing + flags, not a funscript.
type Report struct {
	DurationMs    int        `json:"duration_ms"`
	SampleCount   int        `json:"sample_count"`
	AnalysisFPS   float64    `json:"analysis_fps"`
	SampleStepMs  float64    `json:"sample_step_ms"`
	Width         int        `json:"width"`
	Height        int        `json:"height"`
	Extrema       []Extrema  `json:"extrema"`
	UpCount       int        `json:"up_count"`
	DownCount     int        `json:"down_count"`
	StrokeHz      float64    `json:"stroke_hz"` // from median up→up interval; 0 if unknown
	Cuts          []CutEvent `json:"cuts"`
	CutRatePerMin float64    `json:"cut_rate_per_min"`
	PanFrames     int        `json:"pan_frames"`
	PanShare      float64    `json:"pan_share"` // 0..1 of samples flagged as pan-like
	SignalSpan    float64    `json:"signal_span"`
	Quality       string     `json:"quality"` // "ok" | "weak" | "unstable"
	SuggestAudio  bool       `json:"suggest_audio_check"`
	Reason        string     `json:"reason,omitempty"`
	ElapsedWallMs int64      `json:"elapsed_wall_ms"`
}

func (o *Options) normalize() {
	if o.MaxWidth <= 0 {
		o.MaxWidth = 320
	}
	if o.SampleEvery <= 0 {
		o.SampleEvery = 2
	}
	if o.AnalysisFPS <= 0 {
		o.AnalysisFPS = 12
	}
	if o.MinPeakDistanceMs <= 0 {
		o.MinPeakDistanceMs = 200
	}
	if o.CutMADFactor <= 0 {
		o.CutMADFactor = 4
	}
	if o.PanShiftPx <= 0 {
		o.PanShiftPx = 2.5
	}
}

// Analyze runs the sparse motion probe on videoPath.
func Analyze(ctx context.Context, videoPath string, opts Options) (Report, error) {
	if ctx == nil {
		ctx = context.Background()
	}
	opts.normalize()
	wall0 := time.Now()

	info, err := videox.Probe(ctx, videoPath)
	if err != nil {
		return Report{}, fmt.Errorf("strokepreview: probe: %w", err)
	}

	reader, err := videox.NewGrayReader(ctx, videoPath, info, videox.GrayReaderOptions{
		FPS:        opts.AnalysisFPS,
		MaxWidth:   opts.MaxWidth,
		AutoRotate: true,
	})
	if err != nil {
		return Report{}, fmt.Errorf("strokepreview: open: %w", err)
	}
	defer reader.Close()

	stepMs := 1000.0 / opts.AnalysisFPS * float64(opts.SampleEvery)
	maxSamples := 0
	if opts.MaxSeconds > 0 {
		maxSamples = int(opts.MaxSeconds*1000/stepMs) + 2
	}

	var (
		prev       []byte
		signal     []float64
		mads       []float64
		timesMs    []int
		panFlags   []bool
		sampleIdx  int
		frameIndex int
	)

	for {
		if err := ctx.Err(); err != nil {
			return Report{}, err
		}
		fr, err := reader.Next(frameIndex)
		if err == io.EOF {
			break
		}
		if err != nil {
			return Report{}, fmt.Errorf("strokepreview: read: %w", err)
		}
		frameIndex++
		if (frameIndex-1)%opts.SampleEvery != 0 {
			continue
		}
		atMs := int(math.Round(float64(sampleIdx) * stepMs))
		if prev == nil {
			prev = append([]byte(nil), fr.Pixels...)
			sampleIdx++
			continue
		}
		mad, cy, hx := frameStats(prev, fr.Pixels, fr.Width, fr.Height)
		if math.IsNaN(cy) {
			// No real motion between the two samples (stroke turning
			// point): hold the last centroid instead of inventing a
			// jump to mid-frame, which doubled/tripled StrokeHz.
			cy = 0.5
			if n := len(signal); n > 0 {
				cy = signal[n-1]
			}
		}
		signal = append(signal, cy)
		mads = append(mads, mad)
		timesMs = append(timesMs, atMs)
		panFlags = append(panFlags, math.Abs(hx) >= opts.PanShiftPx)
		copy(prev, fr.Pixels)
		sampleIdx++
		if maxSamples > 0 && sampleIdx >= maxSamples {
			break
		}
	}

	rep := Report{
		DurationMs:    int(math.Round(float64(sampleIdx) * stepMs)),
		SampleCount:   len(signal),
		AnalysisFPS:   opts.AnalysisFPS,
		SampleStepMs:  stepMs,
		Width:         reader.Width,
		Height:        reader.Height,
		ElapsedWallMs: time.Since(wall0).Milliseconds(),
	}
	if len(signal) < 5 {
		rep.Quality = "weak"
		rep.SuggestAudio = true
		rep.Reason = "too few motion samples"
		return rep, nil
	}

	// Light high-pass so slow lighting/camera drift does not invent extrema.
	detrended := posttrack.RollingDetrend(signal, 2500, stepMs)
	span := peakToPeak(detrended)
	rep.SignalSpan = span

	minDist := int(math.Round(float64(opts.MinPeakDistanceMs) / stepMs))
	if minDist < 1 {
		minDist = 1
	}
	prom := span * 0.12
	if prom < 1e-6 {
		prom = 0
	}
	upIdx := posttrack.FindPeaks(detrended, minDist, prom)
	neg := make([]float64, len(detrended))
	for i, v := range detrended {
		neg[i] = -v
	}
	downIdx := posttrack.FindPeaks(neg, minDist, prom)

	for _, i := range upIdx {
		rep.Extrema = append(rep.Extrema, Extrema{AtMs: timesMs[i], Kind: "up", Value: detrended[i]})
	}
	for _, i := range downIdx {
		rep.Extrema = append(rep.Extrema, Extrema{AtMs: timesMs[i], Kind: "down", Value: detrended[i]})
	}
	rep.UpCount = len(upIdx)
	rep.DownCount = len(downIdx)
	rep.StrokeHz = strokeHzFromPeaks(timesMs, upIdx)

	medMAD := median(mads)
	cutThr := medMAD * opts.CutMADFactor
	if cutThr < 8 {
		cutThr = 8 // floor so quiet clips do not flag every bobble
	}
	for i, mad := range mads {
		if mad >= cutThr {
			rep.Cuts = append(rep.Cuts, CutEvent{AtMs: timesMs[i], MAD: mad})
		}
	}
	mins := float64(rep.DurationMs) / 60000.0
	if mins > 0 {
		rep.CutRatePerMin = float64(len(rep.Cuts)) / mins
	}
	for _, p := range panFlags {
		if p {
			rep.PanFrames++
		}
	}
	if len(panFlags) > 0 {
		rep.PanShare = float64(rep.PanFrames) / float64(len(panFlags))
	}

	rep.Quality, rep.SuggestAudio, rep.Reason = classify(rep)
	return rep, nil
}

// centroidNoiseFloor: per-pixel abs diffs below this are codec noise and do
// not steer the motion centroid (they still count toward MAD / pan).
const centroidNoiseFloor = 8.0

// frameStats returns mean abs diff, motion-energy centroid y (0=top..1=bottom),
// and a crude horizontal shift proxy (right energy − left energy of abs diff).
// centroidY is NaN when no pixel moved above centroidNoiseFloor — the caller
// holds the previous value then.
func frameStats(prev, cur []byte, w, h int) (mad, centroidY, horizShift float64) {
	if len(prev) != len(cur) || w*h != len(cur) || w < 4 || h < 4 {
		return 0, 0.5, 0
	}
	x0, x1 := w/4, 3*w/4 // center band — ignore borders (letterbox / UI)
	var sumDiff, sumWY, sumC float64
	var left, right float64
	mid := w / 2
	n := 0
	for y := 0; y < h; y++ {
		row := y * w
		for x := x0; x < x1; x++ {
			d := math.Abs(float64(cur[row+x]) - float64(prev[row+x]))
			sumDiff += d
			if d >= centroidNoiseFloor {
				sumC += d
				sumWY += d * float64(y)
			}
			if x < mid {
				left += d
			} else {
				right += d
			}
			n++
		}
	}
	if n == 0 {
		return 0, 0.5, 0
	}
	mad = sumDiff / float64(n)
	if sumC > 1e-6 {
		centroidY = (sumWY / sumC) / float64(h-1)
	} else {
		centroidY = math.NaN()
	}
	// Normalize horizontal imbalance to a soft “pixel-like” shift scale.
	horizShift = (right - left) / float64(n) * float64(w) / 40.0
	return mad, centroidY, horizShift
}

func peakToPeak(y []float64) float64 {
	if len(y) == 0 {
		return 0
	}
	lo, hi := y[0], y[0]
	for _, v := range y[1:] {
		if v < lo {
			lo = v
		}
		if v > hi {
			hi = v
		}
	}
	return hi - lo
}

func median(y []float64) float64 {
	if len(y) == 0 {
		return 0
	}
	cp := append([]float64(nil), y...)
	// insertion sort — n is small (sparse)
	for i := 1; i < len(cp); i++ {
		v := cp[i]
		j := i
		for j > 0 && cp[j-1] > v {
			cp[j] = cp[j-1]
			j--
		}
		cp[j] = v
	}
	m := len(cp) / 2
	if len(cp)%2 == 0 {
		return (cp[m-1] + cp[m]) / 2
	}
	return cp[m]
}

func strokeHzFromPeaks(timesMs []int, peaks []int) float64 {
	if len(peaks) < 2 {
		return 0
	}
	intervals := make([]float64, 0, len(peaks)-1)
	for i := 1; i < len(peaks); i++ {
		dt := float64(timesMs[peaks[i]] - timesMs[peaks[i-1]])
		if dt >= 150 && dt <= 4000 {
			intervals = append(intervals, dt)
		}
	}
	if len(intervals) == 0 {
		return 0
	}
	med := median(intervals)
	if med <= 0 {
		return 0
	}
	return 1000.0 / med
}

func classify(r Report) (quality string, suggestAudio bool, reason string) {
	switch {
	case r.SampleCount < 8:
		return "weak", true, "too few samples"
	case r.SignalSpan < 0.02:
		return "weak", true, "almost no vertical motion energy"
	case r.CutRatePerMin > 8:
		return "unstable", true, "high cut rate — re-anchor / scene handling"
	case r.PanShare > 0.35 && r.UpCount < 3:
		return "unstable", true, "camera-like horizontal energy without clear stroke"
	case r.StrokeHz <= 0 || r.UpCount+r.DownCount < 4:
		return "weak", true, "no clear up/down extrema"
	case r.StrokeHz < 0.2 || r.StrokeHz > 4.0:
		return "unstable", true, "implausible stroke tempo from extrema"
	default:
		return "ok", false, "clear extrema; audio check optional"
	}
}
