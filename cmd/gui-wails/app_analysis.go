package main

import (
	"fmt"
	"sort"

	"github.com/funfunpayer/SamNPlayer/generator"
	"github.com/funfunpayer/SamNPlayer/motionx"
)

// Dieser Tab-Baustein nutzt motionx aus dem übernommenen fse-Salvage. Das
// Paket ist bewusst abhängigkeitsfrei (nur Standardbibliothek) und arbeitet
// auf Point{TMs, Pos}, ließ sich also ohne Adapter anschließen.
//
// Warum die Zustandsklassifikation überhaupt nützlich ist: bisher sagt der
// Quality Doctor, ob ein Skript brauchbar aussieht, aber nicht, WORAUS es
// besteht. Ein Skript, das zur Hälfte still steht, kann eine tadellose
// spektrale Konzentration haben - die stillen Abschnitte fallen dabei
// einfach nicht auf.

// ScriptSegment ist ein klassifizierter Abschnitt für die Anzeige.
type ScriptSegment struct {
	State     string  `json:"state"`
	StartMs   float64 `json:"startMs"`
	EndMs     float64 `json:"endMs"`
	MeanSpeed float64 `json:"meanSpeed"`
}

// ScriptAnalysis fasst zusammen, woraus ein Skript besteht.
type ScriptAnalysis struct {
	Segments     []ScriptSegment    `json:"segments"`
	ShareByState map[string]float64 `json:"shareByState"`
	TotalMs      float64            `json:"totalMs"`
	Summary      string             `json:"summary"`
}

// AnalyzeScript zerlegt das geladene Skript in Bewegungszustände.
func (a *App) AnalyzeScript() (ScriptAnalysis, error) {
	script := a.loadedScript()
	if script == nil || len(script.Actions) < 4 {
		return ScriptAnalysis{}, fmt.Errorf("no script loaded (or too few points)")
	}

	points := make([]motionx.Point, 0, len(script.Actions))
	for _, action := range script.Actions {
		points = append(points, motionx.Point{
			TMs: float64(action.At),
			Pos: float64(action.Pos),
		})
	}

	segments := motionx.Classify(points, motionx.ClassifyOptions{
		// StaticSpeed in Positionseinheiten pro SEKUNDE - im Original des
		// Pakets waren die Schwellen pro Sample und änderten ihre Bedeutung
		// still, sobald sich die Abtastrate änderte.
		StaticSpeed:  4,
		ChangeRatio:  0.25,
		MinSegmentMs: 250,
		SmoothRadius: 2,
	})

	analysis := ScriptAnalysis{
		ShareByState: map[string]float64{},
		TotalMs:      points[len(points)-1].TMs - points[0].TMs,
	}
	for _, segment := range segments {
		analysis.Segments = append(analysis.Segments, ScriptSegment{
			State:     string(segment.State),
			StartMs:   segment.StartMs,
			EndMs:     segment.EndMs,
			MeanSpeed: segment.MeanSpeed,
		})
		analysis.ShareByState[string(segment.State)] += segment.Duration()
	}
	if analysis.TotalMs > 0 {
		for state, duration := range analysis.ShareByState {
			analysis.ShareByState[state] = duration / analysis.TotalMs
		}
	}
	analysis.Summary = summarizeStates(analysis.ShareByState, analysis.TotalMs)
	return analysis, nil
}

// summarizeStates writes one plain-English line for the Playback analysis box.
func summarizeStates(shares map[string]float64, totalMs float64) string {
	if len(shares) == 0 {
		return "No segments detected."
	}
	type entry struct {
		state string
		share float64
	}
	entries := make([]entry, 0, len(shares))
	for state, share := range shares {
		entries = append(entries, entry{state, share})
	}
	sort.Slice(entries, func(i, j int) bool { return entries[i].share > entries[j].share })

	labels := map[string]string{
		"static":       "still",
		"starting":     "starting",
		"accelerating": "accelerating",
		"regular":      "steady",
		"decelerating": "slowing",
		"stopping":     "stopping",
	}

	parts := make([]string, 0, 3)
	for _, e := range entries {
		if e.share < 0.02 {
			continue
		}
		label := labels[e.state]
		if label == "" {
			label = e.state
		}
		parts = append(parts, fmt.Sprintf("%s %.0f%%", label, e.share*100))
		if len(parts) == 4 {
			break
		}
	}

	text := fmt.Sprintf("%.0f seconds: ", totalMs/1000)
	for i, part := range parts {
		if i > 0 {
			text += ", "
		}
		text += part
	}
	// Flat stretches are quiet to Quality Doctor — surface them here.
	if shares["static"] > 0.3 {
		text += fmt.Sprintf(" — more than %.0f%% still; long stretches without motion",
			shares["static"]*100)
	}
	return text
}

// ScriptQuality wendet Quality Doctor auf das gerade geladene Skript an,
// ohne dass dafür eine Generierung/ein Video nötig ist ("Script Doctor" für
// importierte Dateien). Das ist Signal Quality
// (docs/SIGNAL_VS_FIDELITY.md), kein Motion-Fidelity-Vergleich gegen Video.
// Pure Go (generator.ScriptQuality) — kein Python.
func (a *App) ScriptQuality() (generator.ScriptQualityResult, error) {
	if a.loadedScriptPath() == "" {
		return generator.ScriptQualityResult{}, fmt.Errorf("no script loaded")
	}
	return generator.ScriptQuality(a.loadedScriptPath())
}
