package funscript

import (
	"fmt"
	"math"
)

// Community device-compatibility limits (same sources as generator/device_profile.py).
const (
	MinActionIntervalMs = 100.0
	SlowestFullStrokeMs = 900.0
	UsablePositionMin   = 5
	UsablePositionMax   = 95
)

// DeviceCompatMetrics mirrors device_profile.evaluate metrics.
type DeviceCompatMetrics struct {
	AvgIntensity              float64
	PeakIntensity             float64
	ActionsTooClose           int
	TooCloseShare             float64
	SlowFullStrokes           int
	PositionSpan              float64
	ActionsOutsideUsableRange int
}

// EvaluateDeviceCompat checks a script against community device limits.
func EvaluateDeviceCompat(actions []Action) (DeviceCompatMetrics, []string) {
	var m DeviceCompatMetrics
	var warnings []string
	if len(actions) < 2 {
		return m, warnings
	}

	n := len(actions) - 1
	dt := make([]float64, n)
	dpos := make([]float64, n)
	intensities := make([]float64, n)
	var weightSum, intensSum float64
	posMin, posMax := float64(actions[0].Pos), float64(actions[0].Pos)

	for i := 0; i < n; i++ {
		dt[i] = float64(actions[i+1].At - actions[i].At)
		dpos[i] = math.Abs(float64(actions[i+1].Pos - actions[i].Pos))
		if dt[i] > 0 {
			intensities[i] = 500.0 * dpos[i] / dt[i]
			intensSum += intensities[i] * dt[i]
			weightSum += dt[i]
		}
		p := float64(actions[i].Pos)
		if p < posMin {
			posMin = p
		}
		if p > posMax {
			posMax = p
		}
		p = float64(actions[i+1].Pos)
		if p < posMin {
			posMin = p
		}
		if p > posMax {
			posMax = p
		}
	}

	if weightSum > 0 {
		m.AvgIntensity = math.Round(intensSum/weightSum*10) / 10
	}
	if len(intensities) > 0 {
		m.PeakIntensity = math.Round(percentileFloat(intensities, 95)*10) / 10
	}

	tooClose := 0
	for _, d := range dt {
		if d > 0 && d < MinActionIntervalMs {
			tooClose++
		}
	}
	m.ActionsTooClose = tooClose
	if n > 0 {
		m.TooCloseShare = float64(tooClose) / float64(n)
	}
	if tooClose > 0 && m.TooCloseShare > 0.05 {
		warnings = append(warnings, fmt.Sprintf(
			"%d Actions (%.0f%%) folgen schneller als %.0fms aufeinander - solche Abschnitte geraten auf dem Gerät aus dem Takt, weil es sie nicht mehr einzeln ausführen kann",
			tooClose, m.TooCloseShare*100, MinActionIntervalMs))
	}

	largeMoves, slowFull := 0, 0
	for i := range dt {
		if dpos[i] >= 50 {
			largeMoves++
			if dt[i] > SlowestFullStrokeMs {
				slowFull++
			}
		}
	}
	m.SlowFullStrokes = slowFull
	if slowFull > 0 && largeMoves > 0 && float64(slowFull)/float64(largeMoves) > 0.25 {
		warnings = append(warnings, fmt.Sprintf(
			"%d große Bewegungen dauern länger als %.0fms - das Gerät fährt sie schneller und steht dann still, im Ergebnis entstehen ungewollte Pausen",
			slowFull, SlowestFullStrokeMs))
	}

	m.PositionSpan = math.Round((posMax-posMin)*10) / 10
	if posMax-posMin < 40 {
		warnings = append(warnings, fmt.Sprintf(
			"Das Skript nutzt nur den Bereich %.0f-%.0f von 100 - auf dem Gerät bleibt die Bewegung entsprechend schwach",
			posMin, posMax))
	}

	outside := 0
	for _, a := range actions {
		if a.Pos < UsablePositionMin || a.Pos > UsablePositionMax {
			outside++
		}
	}
	m.ActionsOutsideUsableRange = outside
	return m, warnings
}

func percentileFloat(values []float64, p float64) float64 {
	if len(values) == 0 {
		return 0
	}
	cp := append([]float64(nil), values...)
	for i := 1; i < len(cp); i++ {
		v := cp[i]
		j := i
		for j > 0 && cp[j-1] > v {
			cp[j] = cp[j-1]
			j--
		}
		cp[j] = v
	}
	if p <= 0 {
		return cp[0]
	}
	if p >= 100 {
		return cp[len(cp)-1]
	}
	rank := (p / 100.0) * float64(len(cp)-1)
	lo := int(math.Floor(rank))
	hi := int(math.Ceil(rank))
	if lo == hi {
		return cp[lo]
	}
	w := rank - float64(lo)
	return cp[lo]*(1-w) + cp[hi]*w
}
