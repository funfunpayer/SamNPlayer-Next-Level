// Speed-Highlights im Community-/OFS-Sinn:
// Intensität = 500 × |Δpos| / |Δt_ms| (funscript-utils, OFS, Funscript.io, XBVR).
package funscript

import "math"

// DefaultMaxIntensity ist die Schwelle für „zu schnell“-Markierungen auf der
// Kurve, wenn der Aufrufer keinen Wert übergibt. 400 ≈ sehr zügiger Hub
// (100 = voller Hub pro Sekunde); bewusst konservativ für die Anzeige.
const DefaultMaxIntensity = 400.0

// SpeedSegment ist ein Actions-Abschnitt, dessen Community-Intensität die
// Schwelle überschreitet — OFS-ähnliches Max-Speed-Highlight.
type SpeedSegment struct {
	FromMs    int64   `json:"fromMs"`
	ToMs      int64   `json:"toMs"`
	Intensity float64 `json:"intensity"`
}

// SpeedHighlights liefert Segmente mit Intensität > maxIntensity.
// maxIntensity <= 0 nutzt DefaultMaxIntensity.
func SpeedHighlights(actions []Action, maxIntensity float64) []SpeedSegment {
	if maxIntensity <= 0 {
		maxIntensity = DefaultMaxIntensity
	}
	if len(actions) < 2 {
		return nil
	}
	out := make([]SpeedSegment, 0)
	for i := 0; i < len(actions)-1; i++ {
		dt := float64(actions[i+1].At - actions[i].At)
		if dt <= 0 {
			continue
		}
		dpos := math.Abs(float64(actions[i+1].Pos - actions[i].Pos))
		intensity := 500.0 * dpos / dt
		if intensity > maxIntensity {
			out = append(out, SpeedSegment{
				FromMs:    actions[i].At,
				ToMs:      actions[i+1].At,
				Intensity: math.Round(intensity*10) / 10,
			})
		}
	}
	return out
}
