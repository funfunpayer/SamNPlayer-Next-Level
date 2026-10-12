package funscript

import (
	"fmt"
	"sort"
)

// OZoneSuggestion is a candidate range for a primary O-marker / Extended-O
// band. It is a proposal: the user confirms, moves, or discards it.
type OZoneSuggestion struct {
	StartMs int64   `json:"startMs"`
	EndMs   int64   `json:"endMs"`
	Reason  string  `json:"reason"`
	OK      bool    `json:"ok"`
	Mean    float64 `json:"mean,omitempty"` // mittlere Position im Fenster - Grundlage für SuggestSecondaryOZones' Intensitätsschätzung
}

// SuggestOZone picks a window in the last 8–15% of the script that has the
// highest mean position among sliding windows of ~8% duration. That matches
// "Ende + anhaltend hohe Intensität" without claiming semantic recognition.
func SuggestOZone(actions []Action) OZoneSuggestion {
	if len(actions) < 4 {
		return OZoneSuggestion{Reason: "zu wenige Punkte"}
	}
	start, end := actions[0].At, actions[len(actions)-1].At
	dur := end - start
	if dur < 4000 {
		return OZoneSuggestion{Reason: "Skript kürzer als 4s — kein O-Vorschlag"}
	}
	tailStart := start + int64(float64(dur)*0.85)
	win := int64(float64(dur) * 0.08)
	if win < 1500 {
		win = 1500
	}
	if tailStart+win > end {
		tailStart = end - win
	}
	bestStart := tailStart
	bestMean := -1.0
	step := win / 4
	if step < 200 {
		step = 200
	}
	for w0 := tailStart; w0+win <= end; w0 += step {
		mean := windowMeanPos(actions, w0, w0+win)
		if mean > bestMean {
			bestMean = mean
			bestStart = w0
		}
	}
	if bestMean < 35 {
		return OZoneSuggestion{
			Reason: fmt.Sprintf("letztes Achtel bleibt flach (Mittel %.0f) — kein O-Vorschlag", bestMean),
		}
	}
	return OZoneSuggestion{
		StartMs: bestStart,
		EndMs:   bestStart + win,
		OK:      true,
		Mean:    bestMean,
		Reason: fmt.Sprintf(
			"letztes Achtel, Fenster mit höchster mittlerer Position (%.0f)",
			bestMean),
	}
}

// Grenzen für SuggestSecondaryOZones' Kandidatenfilter: eine frühere
// Erhebung zählt nur als "sekundär", wenn sie sich klar von der eigenen
// Umgebung abhebt (secondaryMinMargin über dem Median aller Fenster vor dem
// Hauptmarker - ein durchgehend erhöhtes, aber FLACHES Vorspiel ist keine
// Erhebung, egal wie hoch die absolute Position liegt) UND deutlich unter
// der Stärke des Hauptmarkers bleibt (secondaryMeanCeilingFrac - sonst wäre
// es ein zweiter Hauptmarker, kein schwächerer).
const (
	secondaryMinMargin       = 10.0
	secondaryMeanCeilingFrac = 0.85
)

func median(values []float64) float64 {
	if len(values) == 0 {
		return 0
	}
	sorted := append([]float64(nil), values...)
	sort.Float64s(sorted)
	mid := len(sorted) / 2
	if len(sorted)%2 == 0 {
		return (sorted[mid-1] + sorted[mid]) / 2
	}
	return sorted[mid]
}

// SuggestSecondaryOZones proposes up to maxCount additional, WEAKER O-marker
// candidates earlier in the script than the primary window - the pattern
// the user specified (Chat, 14. September 2026): "one primary marker
// shortly before climax, plus optionally one or two secondary markers
// earlier in the scene at lower intensity, not as strong". Classical,
// signal-only, same family as SuggestOZone: a local peak in mean position,
// scored lower than the primary and never overlapping it or each other.
// Returns nil (not an error) when primary itself has no zone, or there is
// no room/no qualifying candidate before it - "no secondary markers" is
// the normal, expected result for most scripts, not a failure.
func SuggestSecondaryOZones(actions []Action, primary OZoneSuggestion, maxCount int) []OZoneSuggestion {
	if !primary.OK || maxCount <= 0 || len(actions) < 4 {
		return nil
	}
	start := actions[0].At
	win := primary.EndMs - primary.StartMs
	if win <= 0 || primary.StartMs-start < win {
		return nil // kein Platz für ein gleich großes Fenster vor dem Hauptmarker
	}
	primaryMean := primary.Mean
	if primaryMean <= 0 {
		primaryMean = windowMeanPos(actions, primary.StartMs, primary.EndMs)
	}
	if primaryMean <= 0 {
		return nil
	}

	step := win / 4
	if step < 200 {
		step = 200
	}

	type window struct {
		start, end int64
		mean       float64
	}
	var windows []window
	var means []float64
	for w0 := start; w0+win <= primary.StartMs; w0 += step {
		mean := windowMeanPos(actions, w0, w0+win)
		windows = append(windows, window{w0, w0 + win, mean})
		means = append(means, mean)
	}
	if len(windows) == 0 {
		return nil
	}
	baseline := median(means)

	type candidate = window
	var pool []candidate
	for _, w := range windows {
		if w.mean > baseline+secondaryMinMargin && w.mean < primaryMean*secondaryMeanCeilingFrac {
			pool = append(pool, w)
		}
	}
	sort.Slice(pool, func(i, j int) bool { return pool[i].mean > pool[j].mean })

	var out []OZoneSuggestion
	for _, c := range pool {
		if len(out) >= maxCount {
			break
		}
		overlaps := false
		for _, o := range out {
			if c.start < o.EndMs && o.StartMs < c.end {
				overlaps = true
				break
			}
		}
		if overlaps {
			continue
		}
		out = append(out, OZoneSuggestion{
			StartMs: c.start, EndMs: c.end, OK: true, Mean: c.mean,
			Reason: fmt.Sprintf(
				"frühere, schwächere Erhebung (Mittel %.0f, Hauptmarker %.0f)",
				c.mean, primaryMean),
		})
	}
	// Chronologisch, nicht nach Stärke - liest sich natürlicher, sobald die
	// Auswahl getroffen ist.
	sort.Slice(out, func(i, j int) bool { return out[i].StartMs < out[j].StartMs })
	return out
}

func windowMeanPos(actions []Action, from, to int64) float64 {
	var sum float64
	var n int
	for _, a := range actions {
		if a.At >= from && a.At <= to {
			sum += float64(a.Pos)
			n++
		}
	}
	if n == 0 {
		return 0
	}
	return sum / float64(n)
}
