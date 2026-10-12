package funscript

import (
	"fmt"
	"math"
	"sort"
)

// ScriptQualityResult is the Go-side Script Doctor verdict (actions-only).
// Matches generator.ScriptQualityResult / quality_doctor.evaluate without
// dense tracking data — EstimatedFromScriptOnly is always true here.
//
// This is Signal Quality only (docs/SIGNAL_VS_FIDELITY.md), never Motion
// Fidelity against video/reference.
type ScriptQualityResult struct {
	Score                   float64  `json:"score"`
	Passed                  bool     `json:"passed"`
	Warnings                []string `json:"warnings"`
	EstimatedFromScriptOnly bool     `json:"estimatedFromScriptOnly"`
	// Kind is always "signal_quality" so GUI/reports can label correctly
	// next to Motion Fidelity results without guessing.
	Kind string `json:"kind"`
}

// EvaluateScriptQuality runs the actions-only Quality Doctor checks in pure
// Go (no Python). Used by Script Doctor for imported files and as the
// Python-free path in generator.ScriptQuality.
//
// Intentionally does NOT include dense-signal checks (rhythm on raw curve,
// active-time fraction, reconstruction error, tracker-lost fraction) — those
// need generation-time data. Rhythm falls back to the action polyline, same
// as quality_doctor.py when dense_signal is nil (weaker, and marked as such).
func EvaluateScriptQuality(actions []Action) ScriptQualityResult {
	if len(actions) < 2 {
		return ScriptQualityResult{
			Score:                   0,
			Passed:                  false,
			Warnings:                []string{"Weniger als 2 Actions - kein verwertbares Skript"},
			EstimatedFromScriptOnly: true,
			Kind:                    "signal_quality",
		}
	}

	// Flag unsorted input on the original order, then work on a time-sorted
	// copy for the remaining checks (gaps, speed, device compat).
	var warnings []string
	penalty := 0.0
	for i := 1; i < len(actions); i++ {
		if actions[i].At < actions[i-1].At {
			warnings = append(warnings, "Zeitstempel nicht aufsteigend sortiert")
			penalty += 0.3
			break
		}
	}

	sorted := append([]Action(nil), actions...)
	sort.SliceStable(sorted, func(i, j int) bool { return sorted[i].At < sorted[j].At })

	at := make([]float64, len(sorted))
	pos := make([]float64, len(sorted))
	for i, a := range sorted {
		at[i] = float64(a.At)
		pos[i] = float64(a.Pos)
	}

	dup := 0
	for i := 1; i < len(at); i++ {
		if at[i] == at[i-1] {
			dup++
		}
	}
	if dup > 0 {
		warnings = append(warnings, fmt.Sprintf("%d doppelte Zeitstempel", dup))
		penalty += math.Min(0.2, float64(dup)*0.02)
	}

	// Range
	oor := 0
	for _, p := range pos {
		if p < 0 || p > 100 {
			oor++
		}
	}
	if oor > 0 {
		warnings = append(warnings, fmt.Sprintf("%d Positionswerte außerhalb 0-100", oor))
		penalty += math.Min(0.3, float64(oor)*0.05)
	}

	// Gaps
	var dts []float64
	for i := 1; i < len(at); i++ {
		d := at[i] - at[i-1]
		if d > 0 {
			dts = append(dts, d)
		}
	}
	if len(dts) > 0 {
		med := medianFloat(dts)
		thresh := math.Max(med*8, 2000)
		gaps := 0
		for _, d := range dts {
			if d > thresh {
				gaps++
			}
		}
		if gaps > 0 {
			warnings = append(warnings, fmt.Sprintf(
				"%d ungewöhnlich große zeitliche Lücke(n) (>%dms)", gaps, int(thresh)))
			penalty += math.Min(0.25, float64(gaps)*0.08)
		}
	}

	// Speed spikes
	if len(dts) > 4 {
		speeds := make([]float64, 0, len(dts))
		for i := 1; i < len(at); i++ {
			d := at[i] - at[i-1]
			if d <= 0 {
				continue
			}
			speeds = append(speeds, math.Abs(pos[i]-pos[i-1])/math.Max(d, 1))
		}
		if len(speeds) > 4 {
			med := medianFloat(speeds)
			mad := medianAbsDev(speeds, med) + 1e-6
			spikeThresh := math.Max(med+10*mad, 0.5)
			spikes := 0
			for _, s := range speeds {
				if s > spikeThresh {
					spikes++
				}
			}
			if spikes > 0 {
				warnings = append(warnings, fmt.Sprintf(
					"%d extreme Geschwindigkeitsspitze(n) - deutet auf Tracking-Sprung hin", spikes))
				penalty += math.Min(0.3, float64(spikes)*0.05)
			}
		}
	}

	score := math.Max(0, 1.0-penalty)

	_, deviceWarnings := EvaluateDeviceCompat(sorted)
	warnings = append(warnings, deviceWarnings...)
	score -= math.Min(0.2, 0.1*float64(len(deviceWarnings)))
	if score < 0 {
		score = 0
	}

	passed := score >= 0.5 && !hasHardFail(warnings)
	return ScriptQualityResult{
		Score:                   score,
		Passed:                  passed,
		Warnings:                warnings,
		EstimatedFromScriptOnly: true,
		Kind:                    "signal_quality",
	}
}

func hasHardFail(warnings []string) bool {
	// Mirror quality_doctor: out-of-range and unsorted are soft penalties
	// already in score; "passed" is score>=0.5 unless fewer than 2 actions
	// (handled earlier). No extra hard fails beyond score for actions-only.
	_ = warnings
	return false
}

func medianFloat(values []float64) float64 {
	if len(values) == 0 {
		return 0
	}
	cp := append([]float64(nil), values...)
	sort.Float64s(cp)
	mid := len(cp) / 2
	if len(cp)%2 == 0 {
		return (cp[mid-1] + cp[mid]) / 2
	}
	return cp[mid]
}

func medianAbsDev(values []float64, med float64) float64 {
	devs := make([]float64, len(values))
	for i, v := range values {
		devs[i] = math.Abs(v - med)
	}
	return medianFloat(devs)
}
