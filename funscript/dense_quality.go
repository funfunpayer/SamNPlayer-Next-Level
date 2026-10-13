package funscript

import (
	"fmt"
	"math"
	"sort"
)

const rhythmWindowMs = 8000.0

// DenseQualityInput carries generation-time signals that Script Doctor
// (actions-only) cannot see. Port of quality_doctor.evaluate dense branches.
type DenseQualityInput struct {
	DenseAt             []float64
	DensePos            []float64
	TrackerLostFraction *float64
	MotionRangeFraction *float64
	VideoDurationMs     *float64
	SceneRanges         [][2]int // inclusive start, exclusive end in dense indices
	// Observation contract (FINDINGS F-004) — recorded in metrics/metadata.
	ValidFrames int
	Confidence  float64
	Reason      string
}

// EvaluateDenseQuality runs actions-only Script Doctor checks plus dense
// Signal Quality (rhythm, active fraction, reconstruction, tracker-lost,
// motion amplitude). EstimatedFromScriptOnly is false when dense_at/pos
// are provided. Kind remains "signal_quality".
func EvaluateDenseQuality(actions []Action, in DenseQualityInput) ScriptQualityResult {
	base := EvaluateScriptQuality(actions)
	if len(actions) < 2 {
		return base
	}

	hasDense := len(in.DenseAt) > 2 && len(in.DenseAt) == len(in.DensePos)
	warnings := append([]string(nil), base.Warnings...)
	score := base.Score
	hardFail := false
	var concentration *float64
	var activeFraction *float64
	var reconstructionError *float64

	sorted := append([]Action(nil), actions...)
	sort.SliceStable(sorted, func(i, j int) bool { return sorted[i].At < sorted[j].At })
	at := make([]float64, len(sorted))
	pos := make([]float64, len(sorted))
	for i, a := range sorted {
		at[i] = float64(a.At)
		pos[i] = float64(a.Pos)
	}

	// --- Rhythm / spectral concentration (dense preferred) ---
	srcAt, srcPos := at, pos
	if hasDense {
		srcAt, srcPos = in.DenseAt, in.DensePos
	}
	srcDt := positiveDiffs(srcAt)

	if hasDense && len(in.SceneRanges) > 1 {
		var perScene []float64
		for _, rg := range in.SceneRanges {
			start, end := rg[0], rg[1]
			if end > len(srcPos) {
				end = len(srcPos)
			}
			if end-start < 16 {
				continue
			}
			segAt, segPos := srcAt[start:end], srcPos[start:end]
			segDt := positiveDiffs(segAt)
			if len(segDt) == 0 {
				continue
			}
			step := math.Max(medianFloat(segDt), 10)
			grid := arange(segAt[0], segAt[len(segAt)-1], step)
			if len(grid) <= 8 {
				continue
			}
			sig := detrend(interpSeries(grid, segAt, segPos))
			if c := spectralConcentration(sig); c != nil {
				perScene = append(perScene, *c)
			}
		}
		if len(perScene) > 0 {
			c := medianFloat(perScene)
			concentration = &c
			if c < 0.25 {
				warnings = append(warnings, fmt.Sprintf(
					"Signal wirkt verrauscht statt rhythmisch (spektrale Konzentration %.0f%%, Median über %d Szenen)",
					c*100, len(perScene)))
				score -= 0.45
			}
		}
	} else if len(srcPos) > 8 && len(srcDt) > 0 {
		uniformDt := math.Max(medianFloat(srcDt), 10)
		tUniform := arange(srcAt[0], srcAt[len(srcAt)-1], uniformDt)
		if len(tUniform) > 8 {
			posUniform := interpSeries(tUniform, srcAt, srcPos)
			if windowed := windowedConcentration(tUniform, posUniform); windowed != nil {
				concentration = windowed
				if *windowed < 0.25 {
					warnings = append(warnings, fmt.Sprintf(
						"Signal wirkt verrauscht statt rhythmisch (spektrale Konzentration %.0f%%)",
						*windowed*100))
					score -= 0.45
				}
			} else if c := spectralConcentration(detrend(posUniform)); c != nil {
				concentration = c
				if *c < 0.25 {
					warnings = append(warnings, fmt.Sprintf(
						"Signal wirkt verrauscht statt rhythmisch (spektrale Konzentration %.0f%%)",
						*c*100))
					score -= 0.45
				}
			}
		}
	}

	// Keyframe density vs duration
	if in.VideoDurationMs != nil && *in.VideoDurationMs > 0 {
		apm := float64(len(actions)) / (*in.VideoDurationMs / 60000.0)
		if apm < 4 {
			warnings = append(warnings, fmt.Sprintf(
				"Sehr wenige Keyframes (%.1f/min) - Bewegung evtl. nicht erkannt", apm))
			score -= 0.15
		}
	}

	// Tracker lost
	if in.TrackerLostFraction != nil && *in.TrackerLostFraction > 0.05 {
		f := *in.TrackerLostFraction
		warnings = append(warnings, fmt.Sprintf(
			"Tracker hat das Objekt in %.0f%% der Frames verloren - dort wurde die letzte bekannte Position fortgeschrieben",
			f*100))
		score -= math.Min(0.4, f*0.6)
		if f > 0.5 {
			hardFail = true
		}
	}

	// Active time fraction
	if hasDense {
		denseValues := detrend(append([]float64(nil), in.DensePos...))
		amplitude := ptp(denseValues)
		if len(denseValues) > 50 && amplitude > 1e-9 {
			window := maxInt(10, len(denseValues)/40)
			step := maxInt(1, window/5)
			var active int
			var samples int
			for i := 0; i < len(denseValues); i += step {
				lo := maxInt(0, i-window)
				hi := minInt(len(denseValues), i+window)
				if ptp(denseValues[lo:hi]) > amplitude*0.15 {
					active++
				}
				samples++
			}
			if samples > 0 {
				af := float64(active) / float64(samples)
				activeFraction = &af
				if af < 0.35 {
					warnings = append(warnings, fmt.Sprintf(
						"Bewegung findet nur in %.0f%% der Laufzeit statt - der Rest ist praktisch unbewegt",
						af*100))
					score -= 0.3
				} else if af < 0.6 {
					warnings = append(warnings, fmt.Sprintf(
						"In %.0f%% der Laufzeit passiert kaum etwas", (1-af)*100))
					score -= 0.1
				}
			}
		}
	}

	// Reconstruction error
	if hasDense && len(actions) >= 2 {
		reconstructed := interpSeries(in.DenseAt, at, pos)
		span := ptp(in.DensePos)
		if span > 1e-6 {
			var sumSq float64
			for i := range reconstructed {
				d := reconstructed[i] - in.DensePos[i]
				sumSq += d * d
			}
			re := math.Sqrt(sumSq/float64(len(reconstructed))) / span
			reconstructionError = &re
			if re > 0.20 {
				warnings = append(warnings, fmt.Sprintf(
					"Das Skript gibt den gemessenen Verlauf nur grob wieder (mittlerer Fehler %.0f%% der Auslenkung) - zu stark geglättet oder zu wenige Keyframes",
					re*100))
				score -= 0.3
			} else if re > 0.12 {
				warnings = append(warnings, fmt.Sprintf(
					"Das Skript weicht spürbar vom gemessenen Verlauf ab (%.0f%% der Auslenkung)",
					re*100))
				score -= 0.1
			}
		}
	}

	// Motion amplitude in image coordinates
	if in.MotionRangeFraction != nil {
		f := *in.MotionRangeFraction
		if f < 0.03 {
			warnings = append(warnings, fmt.Sprintf(
				"Kaum echte Bewegung: die verfolgte Region bewegt sich nur über %.1f%% der Bildhöhe. Die Kurve entsteht fast nur durch Hochskalieren von Trackerzittern",
				f*100))
			score -= 0.5
			hardFail = true
		} else if f < 0.08 {
			warnings = append(warnings, fmt.Sprintf(
				"Geringe Bewegungsamplitude (%.1f%% der Bildhöhe) - Ergebnis vor Gebrauch prüfen",
				f*100))
			score -= 0.15
		}
	}

	if concentration != nil && *concentration < 0.20 {
		hardFail = true
	}
	if in.Reason == "tracker_lost_heavy" {
		hardFail = true
	}

	score = math.Max(0, math.Min(1, score))
	// Round like Python quality_doctor (2 decimals).
	score = math.Round(score*100) / 100
	passed := score >= 0.5 && !hardFail

	_ = reconstructionError
	_ = activeFraction

	return ScriptQualityResult{
		Score:                   score,
		Passed:                  passed,
		Warnings:                warnings,
		EstimatedFromScriptOnly: !hasDense,
		Kind:                    "signal_quality",
	}
}

func positiveDiffs(xs []float64) []float64 {
	var out []float64
	for i := 1; i < len(xs); i++ {
		d := xs[i] - xs[i-1]
		if d > 0 {
			out = append(out, d)
		}
	}
	return out
}

func arange(start, end, step float64) []float64 {
	if step <= 0 || end <= start {
		return nil
	}
	n := int((end-start)/step) + 1
	out := make([]float64, 0, n)
	for t := start; t < end; t += step {
		out = append(out, t)
	}
	return out
}

func interpSeries(grid, xs, ys []float64) []float64 {
	out := make([]float64, len(grid))
	for i, t := range grid {
		out[i] = interpClamp(xs, ys, t)
	}
	return out
}

func detrend(values []float64) []float64 {
	n := len(values)
	if n == 0 {
		return values
	}
	out := make([]float64, n)
	if n < 3 {
		m := mean(values)
		for i, v := range values {
			out[i] = v - m
		}
		return out
	}
	// Least-squares linear fit (polyfit deg 1).
	var sumX, sumY, sumXX, sumXY float64
	for i, y := range values {
		x := float64(i)
		sumX += x
		sumY += y
		sumXX += x * x
		sumXY += x * y
	}
	nf := float64(n)
	den := nf*sumXX - sumX*sumX
	var slope, intercept float64
	if math.Abs(den) < 1e-12 {
		intercept = sumY / nf
	} else {
		slope = (nf*sumXY - sumX*sumY) / den
		intercept = (sumY - slope*sumX) / nf
	}
	for i, y := range values {
		out[i] = y - (slope*float64(i) + intercept)
	}
	return out
}

func spectralConcentration(values []float64) *float64 {
	signal := detrend(values)
	spectrum := rfftAbs(signal)
	if len(spectrum) <= 4 {
		return nil
	}
	ac := spectrum[1:] // drop DC
	var sum float64
	for _, v := range ac {
		sum += v
	}
	if sum <= 0 || len(ac) <= 3 {
		return nil
	}
	sorted := append([]float64(nil), ac...)
	sort.Float64s(sorted)
	top3 := sorted[len(sorted)-1] + sorted[len(sorted)-2] + sorted[len(sorted)-3]
	c := top3 / sum
	return &c
}

func windowedConcentration(grid, values []float64) *float64 {
	if len(grid) < 16 {
		return nil
	}
	step := 40.0
	if len(grid) > 1 {
		diffs := positiveDiffs(grid)
		if len(diffs) > 0 {
			step = medianFloat(diffs)
		}
	}
	size := int(rhythmWindowMs / math.Max(step, 1.0))
	if size < 16 || len(values) < size*2 {
		return spectralConcentration(values)
	}
	var scores []float64
	stride := maxInt(1, size/2)
	for start := 0; start+size <= len(values); start += stride {
		if s := spectralConcentration(values[start : start+size]); s != nil {
			scores = append(scores, *s)
		}
	}
	if len(scores) == 0 {
		return spectralConcentration(values)
	}
	m := medianFloat(scores)
	return &m
}

// rfftAbs is a real DFT magnitude spectrum (numpy.fft.rfft compatible bins).
// O(n²) is fine: Quality Doctor runs once per generation.
func rfftAbs(x []float64) []float64 {
	n := len(x)
	out := make([]float64, n/2+1)
	for k := 0; k <= n/2; k++ {
		var re, im float64
		for t, v := range x {
			ang := -2 * math.Pi * float64(k) * float64(t) / float64(n)
			re += v * math.Cos(ang)
			im += v * math.Sin(ang)
		}
		out[k] = math.Hypot(re, im)
	}
	return out
}

func ptp(v []float64) float64 {
	if len(v) == 0 {
		return 0
	}
	lo, hi := v[0], v[0]
	for _, x := range v {
		if x < lo {
			lo = x
		}
		if x > hi {
			hi = x
		}
	}
	return hi - lo
}

func maxInt(a, b int) int {
	if a > b {
		return a
	}
	return b
}

func minInt(a, b int) int {
	if a < b {
		return a
	}
	return b
}
