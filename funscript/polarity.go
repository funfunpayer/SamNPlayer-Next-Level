package funscript

import "fmt"

// PolarityHint is a suggestion only: whether the first half of the script
// sits unusually low or high. FunGen and this project often disagree on
// which direction is "up"; flipping is a sign convention, not tracking
// quality. The user must confirm — nothing is auto-applied.
type PolarityHint struct {
	SuggestInvert bool    `json:"suggestInvert"`
	FirstHalfMean float64 `json:"firstHalfMean"`
	Reason        string  `json:"reason"`
}

// SuggestPolarity looks at the first half of the timeline (not the first
// half of the point list — irregular keyframe spacing would bias that).
func SuggestPolarity(actions []Action) PolarityHint {
	if len(actions) < 4 {
		return PolarityHint{Reason: "zu wenige Punkte für eine Richtungsschätzung"}
	}
	start, end := actions[0].At, actions[len(actions)-1].At
	if end <= start {
		return PolarityHint{Reason: "Skript hat keine Dauer"}
	}
	mid := start + (end-start)/2
	var sum float64
	var n int
	for _, a := range actions {
		if a.At <= mid {
			sum += float64(a.Pos)
			n++
		}
	}
	if n == 0 {
		return PolarityHint{Reason: "keine Punkte in der ersten Hälfte"}
	}
	mean := sum / float64(n)
	hint := PolarityHint{FirstHalfMean: mean}
	switch {
	case mean < 40:
		hint.SuggestInvert = true
		hint.Reason = fmt.Sprintf(
			"erste Hälfte sitzt im Mittel bei %.0f — Richtung umkehren, falls 100 „drin/oben“ sein soll",
			mean)
	case mean > 60:
		hint.SuggestInvert = false
		hint.Reason = fmt.Sprintf(
			"erste Hälfte sitzt im Mittel bei %.0f — aktuelle Richtung wirkt konsistent",
			mean)
	default:
		hint.Reason = fmt.Sprintf(
			"erste Hälfte sitzt im Mittel bei %.0f — keine klare Richtung, nichts vorschlagen",
			mean)
	}
	return hint
}
