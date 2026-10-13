package funscript

import (
	"math"
	"sort"
)

// Phase-Analyzer defaults — mirrored from generator/fungen_compare.py so
// Go and Python agree on the same lag search when diagnosing a clip.
const (
	DefaultMaxLagMs       = 1000
	DefaultLagStepMs      = 100
	DefaultResampleStepMs = 100
	MinOverlapSamples     = 10
	LowConfidenceSamples  = 30
	DefaultTimingGainMin  = 0.30  // aligned r must beat raw by this much → "timing"
	DefaultMinAlignedR    = 0.70  // below this after alignment → shape/perception
	DefaultWindowMs       = 30000 // 30s windows for windowed mode

	// Aliasing annotation (F-003 option 1): flag near-tied lags at ±k×period
	// without changing the reported best lag. See docs/FINDINGS_TIMING_TF.md.
	DefaultAliasingEpsilon = 0.02 // alternate r must be within this of best.R
	MinDominantPeriodMs    = 150
	MaxDominantPeriodMs    = 2000
)

// LagCorrelation is the result of BestLagCorrelation — same fields as
// fungen_compare.best_lag_correlation(). Nil pointer fields mean
// "undefined" (never 0.0 for a missing value).
type LagCorrelation struct {
	R             float64  `json:"r"`
	LagMs         int      `json:"lag_ms"`
	Orientation   string   `json:"orientation"` // "normal" or "inverted"
	NSamples      int      `json:"n_samples"`
	ShapeError    *float64 `json:"shape_error"`
	RZeroLag      *float64 `json:"r_zero_lag"`
	LowConfidence bool     `json:"low_confidence"`
	// AliasingRisk is set when another lag near best±k×DominantPeriodMs
	// scores within DefaultAliasingEpsilon of R. LagMs / R are unchanged —
	// additive diagnostic only (TFTJ / F-003 option 1).
	AliasingRisk     bool  `json:"aliasing_risk,omitempty"`
	DominantPeriodMs int   `json:"dominant_period_ms,omitempty"`
	AlternateLagsMs  []int `json:"alternate_lags_ms,omitempty"`
}

// WindowResult is one absolute-time window from WindowedBestLagCorrelation.
// Correlation is nil when the window has too few actions or Pearson is
// undefined (constant series) — never silently dropped, so quiet sections
// show up as undefined rather than missing rows.
type WindowResult struct {
	WindowStartMs int64           `json:"window_start_ms"`
	WindowEndMs   int64           `json:"window_end_ms"`
	Correlation   *LagCorrelation `json:"correlation"`
	Reason        string          `json:"reason,omitempty"` // "too_few_actions_in_window" | "undefined_correlation"
}

// PhaseVerdict is the diagnose step from docs/ENGINE.md (timing vs shape):
// after best-lag, decide whether to fix timing or perception/shape.
type PhaseVerdict string

const (
	PhaseUndefined PhaseVerdict = "undefined" // constant / too-short overlap
	PhaseTiming    PhaseVerdict = "timing"    // shape OK, phase wrong
	PhaseShape     PhaseVerdict = "shape"     // aligned correlation still weak
	PhaseOK        PhaseVerdict = "ok"        // raw already matches well
)

// PhaseDiagnosis pairs the lag search with the research-doc decision tree.
type PhaseDiagnosis struct {
	Correlation *LagCorrelation `json:"correlation"`
	Verdict     PhaseVerdict    `json:"verdict"`
	Detail      string          `json:"detail"`
}

// MotionFidelityResult is a labeled Motion Fidelity report
// (docs/SIGNAL_VS_FIDELITY.md) — reference vs candidate via BestLagCorrelation.
// Distinct from ScriptQualityResult (Signal Quality only).
type MotionFidelityResult struct {
	Kind             string         `json:"kind"` // always "motion_fidelity"
	Diagnosis        PhaseDiagnosis `json:"diagnosis"`
	R                *float64       `json:"r"`
	RZeroLag         *float64       `json:"r_zero_lag"`
	LagMs            *int           `json:"lag_ms"`
	Orientation      string         `json:"orientation,omitempty"`
	LowConfidence    bool           `json:"low_confidence"`
	AliasingRisk     bool           `json:"aliasing_risk,omitempty"`
	DominantPeriodMs int            `json:"dominant_period_ms,omitempty"`
	AlternateLagsMs  []int          `json:"alternate_lags_ms,omitempty"`
}

// EvaluateMotionFidelity compares reference A against candidate B.
// Defaults for lag search match BestLagCorrelation / fungen_compare.
func EvaluateMotionFidelity(reference, candidate []Action, maxLagMs, lagStepMs, resampleStepMs int) MotionFidelityResult {
	corr := BestLagCorrelation(reference, candidate, maxLagMs, lagStepMs, resampleStepMs)
	diag := DiagnosePhase(corr, 0, 0)
	out := MotionFidelityResult{
		Kind:      "motion_fidelity",
		Diagnosis: diag,
	}
	if corr != nil {
		r := corr.R
		out.R = &r
		out.RZeroLag = corr.RZeroLag
		lag := corr.LagMs
		out.LagMs = &lag
		out.Orientation = corr.Orientation
		out.LowConfidence = corr.LowConfidence
		out.AliasingRisk = corr.AliasingRisk
		out.DominantPeriodMs = corr.DominantPeriodMs
		out.AlternateLagsMs = append([]int(nil), corr.AlternateLagsMs...)
	}
	return out
}

// BestLagCorrelation searches lag and orientation for the best match of
// two action lists. Port of generator/fungen_compare.best_lag_correlation:
// shared absolute timeline, ±maxLagMs search, normal + inverted (100-pos).
// Returns nil when no usable overlap exists or Pearson is undefined
// (constant series) for every candidate lag.
func BestLagCorrelation(a, b []Action, maxLagMs, lagStepMs, resampleStepMs int) *LagCorrelation {
	if maxLagMs < 0 {
		maxLagMs = DefaultMaxLagMs
	}
	if lagStepMs <= 0 {
		lagStepMs = DefaultLagStepMs
	}
	if resampleStepMs <= 0 {
		resampleStepMs = DefaultResampleStepMs
	}
	if len(a) < 2 || len(b) < 2 {
		return nil
	}

	aSorted := sortActions(a)
	bSorted := sortActions(b)
	aT0, aT1 := aSorted[0].At, aSorted[len(aSorted)-1].At
	bT0, bT1 := bSorted[0].At, bSorted[len(bSorted)-1].At

	var best *LagCorrelation
	var zeroLagR *float64
	// Best r per lag (either orientation) — used only for aliasing annotation.
	lagScores := map[int]float64{}

	for lagMs := -maxLagMs; lagMs <= maxLagMs; lagMs += lagStepMs {
		overlapStart := max64(aT0, bT0+int64(lagMs))
		overlapEnd := min64(aT1, bT1+int64(lagMs))
		if overlapEnd <= overlapStart {
			continue
		}
		nSamples := int((overlapEnd-overlapStart)/int64(resampleStepMs)) + 1
		if nSamples < MinOverlapSamples {
			continue
		}

		seriesA := resampleAbsolute(aSorted, overlapStart, overlapEnd, resampleStepMs)
		shiftedB := make([]Action, len(bSorted))
		for i, act := range bSorted {
			shiftedB[i] = Action{At: act.At + int64(lagMs), Pos: act.Pos}
		}
		seriesB := resampleAbsolute(shiftedB, overlapStart, overlapEnd, resampleStepMs)

		rNormal := pearson(seriesA, seriesB)
		var rInverted *float64
		invertedB := invertSeries(seriesB)
		if rNormal != nil {
			rInverted = pearson(seriesA, invertedB)
		}

		candidates := []struct {
			r           *float64
			orientation string
			sb          []float64
		}{
			{rNormal, "normal", seriesB},
			{rInverted, "inverted", invertedB},
		}
		for _, c := range candidates {
			if c.r == nil {
				continue
			}
			if prev, ok := lagScores[lagMs]; !ok || *c.r > prev {
				lagScores[lagMs] = *c.r
			}
			if best == nil || *c.r > best.R {
				se := shapeNormalizedError(seriesA, c.sb)
				best = &LagCorrelation{
					R:           *c.r,
					LagMs:       lagMs,
					Orientation: c.orientation,
					NSamples:    len(seriesA),
					ShapeError:  se,
				}
			}
		}
		if lagMs == 0 && rNormal != nil {
			v := *rNormal
			zeroLagR = &v
		}
	}

	if best == nil {
		return nil
	}
	best.RZeroLag = zeroLagR
	best.LowConfidence = best.NSamples < LowConfidenceSamples
	annotateAliasingRisk(best, lagScores, aSorted, resampleStepMs, maxLagMs, lagStepMs)
	return best
}

// annotateAliasingRisk sets AliasingRisk / AlternateLagsMs when another
// searched lag near best±k×period scores almost as well. Never changes
// LagMs or R (F-003 option 1).
func annotateAliasingRisk(best *LagCorrelation, lagScores map[int]float64, ref []Action, resampleStepMs, maxLagMs, lagStepMs int) {
	if best == nil || len(lagScores) < 2 {
		return
	}
	period := dominantPeriodMs(ref, resampleStepMs)
	if period < MinDominantPeriodMs {
		return
	}
	best.DominantPeriodMs = period
	tolerance := lagStepMs
	if tolerance < 1 {
		tolerance = DefaultLagStepMs
	}
	var alts []int
	for k := 1; k*period <= maxLagMs*2; k++ {
		for _, sign := range []int{1, -1} {
			target := best.LagMs + sign*k*period
			if target < -maxLagMs || target > maxLagMs {
				continue
			}
			for lag, r := range lagScores {
				if lag == best.LagMs {
					continue
				}
				if absInt(lag-target) > tolerance {
					continue
				}
				if best.R-r <= DefaultAliasingEpsilon {
					alts = append(alts, lag)
				}
			}
		}
	}
	if len(alts) == 0 {
		return
	}
	sort.Ints(alts)
	// Dedupe
	uniq := alts[:0]
	prev := math.MinInt32
	for _, lag := range alts {
		if lag == prev {
			continue
		}
		uniq = append(uniq, lag)
		prev = lag
	}
	best.AlternateLagsMs = uniq
	best.AliasingRisk = true
}

// dominantPeriodMs estimates the dominant stroke period via normalized
// autocorrelation of the resampled absolute position series. Returns 0
// when no clear peak exists.
func dominantPeriodMs(actions []Action, stepMs int) int {
	if len(actions) < 4 || stepMs <= 0 {
		return 0
	}
	sorted := sortActions(actions)
	t0, t1 := sorted[0].At, sorted[len(sorted)-1].At
	if t1-t0 < int64(MinDominantPeriodMs*2) {
		return 0
	}
	series := resampleAbsolute(sorted, t0, t1, stepMs)
	n := len(series)
	if n < 16 {
		return 0
	}
	mean := 0.0
	for _, v := range series {
		mean += v
	}
	mean /= float64(n)
	var denom float64
	centered := make([]float64, n)
	for i, v := range series {
		centered[i] = v - mean
		denom += centered[i] * centered[i]
	}
	if denom < 1e-9 {
		return 0
	}
	minLag := MinDominantPeriodMs / stepMs
	if minLag < 1 {
		minLag = 1
	}
	// Round the floor UP, not down: integer division alone can let minLag
	// fall short of MinDominantPeriodMs (e.g. 150/40 = 3 → 120ms, not
	// 150ms), silently searching below the documented floor.
	if minLag*stepMs < MinDominantPeriodMs {
		minLag++
	}
	maxLag := MaxDominantPeriodMs / stepMs
	if maxLag >= n/2 {
		maxLag = n/2 - 1
	}
	if maxLag <= minLag {
		return 0
	}
	acs := make([]float64, maxLag-minLag+1)
	for idx, lag := 0, minLag; lag <= maxLag; idx, lag = idx+1, lag+1 {
		var num float64
		for i := 0; i+lag < n; i++ {
			num += centered[i] * centered[i+lag]
		}
		acs[idx] = num / denom
	}
	// A real, continuous motion curve is smooth: nearby samples correlate
	// strongly regardless of periodicity, so autocorrelation naturally
	// declines from the smallest lag onward. A naive global-max over the
	// whole range picks that smallest lag every time — a number that looks
	// like a "period" but reflects only smoothness, not periodicity (see
	// docs/FINDINGS_TIMING_TF.md § F-003, 21 Sep correction). Only accept a
	// genuine LOCAL peak reached *after* that initial decline; a curve that
	// declines the whole way through has no confident period here.
	bestLag, bestAC := 0, 0.0
	pastInitialDecline := false
	for idx, lag := 0, minLag; lag <= maxLag; idx, lag = idx+1, lag+1 {
		if idx > 0 && acs[idx] > acs[idx-1] {
			pastInitialDecline = true
		}
		isLocalMax := idx > 0 && idx < len(acs)-1 && acs[idx] > acs[idx-1] && acs[idx] >= acs[idx+1]
		if pastInitialDecline && isLocalMax && acs[idx] > bestAC {
			bestAC = acs[idx]
			bestLag = lag
		}
	}
	// Require a clear periodic peak (not flat noise).
	if bestAC < 0.25 || bestLag == 0 {
		return 0
	}
	return bestLag * stepMs
}

func absInt(x int) int {
	if x < 0 {
		return -x
	}
	return x
}

// WindowedBestLagCorrelation splits series a's absolute timeline into
// fixed-size windows and runs BestLagCorrelation independently per window
// against b (padded by maxLagMs so edge lags are not starved). Port of
// generator/fungen_compare_windowed.windowed_correlation. windowMs ≤ 0
// uses DefaultWindowMs (30s).
func WindowedBestLagCorrelation(a, b []Action, windowMs, maxLagMs, lagStepMs, resampleStepMs int) []WindowResult {
	if windowMs <= 0 {
		windowMs = DefaultWindowMs
	}
	if maxLagMs < 0 {
		maxLagMs = DefaultMaxLagMs
	}
	if len(a) < 2 {
		return nil
	}
	aSorted := sortActions(a)
	bSorted := sortActions(b)
	t0, t1 := aSorted[0].At, aSorted[len(aSorted)-1].At

	var results []WindowResult
	start := t0
	for start < t1 {
		end := start + int64(windowMs)
		if end > t1 {
			end = t1
		}
		aSlice := filterActionsInRange(aSorted, start, end)
		pad := int64(maxLagMs)
		bSlice := filterActionsInRange(bSorted, start-pad, end+pad)

		row := WindowResult{WindowStartMs: start, WindowEndMs: end}
		if len(aSlice) < 2 || len(bSlice) < 2 {
			row.Reason = "too_few_actions_in_window"
		} else {
			best := BestLagCorrelation(aSlice, bSlice, maxLagMs, lagStepMs, resampleStepMs)
			if best == nil {
				row.Reason = "undefined_correlation"
			} else {
				row.Correlation = best
			}
		}
		results = append(results, row)
		start = end
	}
	return results
}

func filterActionsInRange(actions []Action, tStart, tEnd int64) []Action {
	out := make([]Action, 0, len(actions))
	for _, a := range actions {
		if a.At >= tStart && a.At <= tEnd {
			out = append(out, a)
		}
	}
	return out
}

// DiagnosePhase applies the research-doc flow: if aligned correlation is
// much better than raw → timing; if aligned stays weak → shape/perception;
// otherwise OK. timingGainMin / minAlignedR ≤ 0 use the package defaults.
func DiagnosePhase(corr *LagCorrelation, timingGainMin, minAlignedR float64) PhaseDiagnosis {
	if timingGainMin <= 0 {
		timingGainMin = DefaultTimingGainMin
	}
	if minAlignedR <= 0 {
		minAlignedR = DefaultMinAlignedR
	}
	if corr == nil {
		return PhaseDiagnosis{
			Verdict: PhaseUndefined,
			Detail:  "keine verwertbare Überlappung oder konstante Serie (Pearson undefiniert)",
		}
	}
	d := PhaseDiagnosis{Correlation: corr}
	if corr.R < minAlignedR {
		d.Verdict = PhaseShape
		d.Detail = "aligned correlation bleibt schwach — Perception/Normalisierung/ROI prüfen"
		return d
	}
	raw := 0.0
	hasRaw := corr.RZeroLag != nil
	if hasRaw {
		raw = *corr.RZeroLag
	}
	if hasRaw && corr.R-raw >= timingGainMin {
		d.Verdict = PhaseTiming
		d.Detail = "Kurve passt nach Lag-Ausrichtung — primär Timing/Phase reparieren"
		return d
	}
	d.Verdict = PhaseOK
	d.Detail = "Rohkorrelation und Alignierung sind beide brauchbar"
	return d
}

func sortActions(in []Action) []Action {
	out := append([]Action(nil), in...)
	sort.SliceStable(out, func(i, j int) bool { return out[i].At < out[j].At })
	return out
}

func resampleAbsolute(actions []Action, tStart, tEnd int64, stepMs int) []float64 {
	times := make([]float64, len(actions))
	positions := make([]float64, len(actions))
	for i, a := range actions {
		times[i] = float64(a.At)
		positions[i] = float64(a.Pos)
	}
	n := int((tEnd-tStart)/int64(stepMs)) + 1
	out := make([]float64, 0, n)
	for t := tStart; t <= tEnd; t += int64(stepMs) {
		out = append(out, interpClamp(times, positions, float64(t)))
	}
	return out
}

// interpClamp matches numpy.interp defaults: linear between knots, clamp
// outside the series' own time range (no extrapolation).
func interpClamp(xs, ys []float64, x float64) float64 {
	if len(xs) == 0 {
		return 0
	}
	if x <= xs[0] {
		return ys[0]
	}
	if x >= xs[len(xs)-1] {
		return ys[len(ys)-1]
	}
	// Binary search for rightmost xs[i] <= x
	i := sort.Search(len(xs), func(i int) bool { return xs[i] > x })
	// xs[i-1] <= x < xs[i]
	x0, x1 := xs[i-1], xs[i]
	y0, y1 := ys[i-1], ys[i]
	if x1 == x0 {
		return y0
	}
	w := (x - x0) / (x1 - x0)
	return y0 + w*(y1-y0)
}

func invertSeries(s []float64) []float64 {
	out := make([]float64, len(s))
	for i, v := range s {
		out[i] = 100.0 - v
	}
	return out
}

// pearson returns Pearson r, or nil if either series has ~zero variance
// (matches fungen_compare.pearson — never 0.0 for undefined).
func pearson(a, b []float64) *float64 {
	if len(a) < 2 || len(a) != len(b) {
		return nil
	}
	meanA, meanB := mean(a), mean(b)
	var sumA, sumB, sumAB float64
	for i := range a {
		da := a[i] - meanA
		db := b[i] - meanB
		sumA += da * da
		sumB += db * db
		sumAB += da * db
	}
	// Population std (ddof=0), same as numpy.std / corrcoef default.
	n := float64(len(a))
	stdA := math.Sqrt(sumA / n)
	stdB := math.Sqrt(sumB / n)
	if stdA < 1e-9 || stdB < 1e-9 {
		return nil
	}
	r := sumAB / (n * stdA * stdB)
	return &r
}

func shapeNormalizedError(a, b []float64) *float64 {
	if len(a) < 2 || len(a) != len(b) {
		return nil
	}
	meanA, meanB := mean(a), mean(b)
	stdA, stdB := popStd(a, meanA), popStd(b, meanB)
	if stdA < 1e-9 || stdB < 1e-9 {
		return nil
	}
	var sum float64
	for i := range a {
		za := (a[i] - meanA) / stdA
		zb := (b[i] - meanB) / stdB
		sum += math.Abs(za - zb)
	}
	v := sum / float64(len(a))
	return &v
}

func mean(xs []float64) float64 {
	var s float64
	for _, v := range xs {
		s += v
	}
	return s / float64(len(xs))
}

func popStd(xs []float64, m float64) float64 {
	var s float64
	for _, v := range xs {
		d := v - m
		s += d * d
	}
	return math.Sqrt(s / float64(len(xs)))
}

func max64(a, b int64) int64 {
	if a > b {
		return a
	}
	return b
}

func min64(a, b int64) int64 {
	if a < b {
		return a
	}
	return b
}
