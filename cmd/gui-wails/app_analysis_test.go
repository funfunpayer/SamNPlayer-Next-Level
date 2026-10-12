package main

import (
	"strings"
	"testing"

	"github.com/funfunpayer/SamNPlayer/funscript"
)

// Der eigentliche Nutzen der Zustandsklassifikation: ein Skript, das über
// weite Strecken still steht, fällt den übrigen Prüfungen NICHT auf - eine
// flache Strecke ist weder verrauscht noch unrhythmisch. Hier wird es
// sichtbar.
func TestAnalyzeScriptFindsStaticStretches(t *testing.T) {
	a := NewApp()
	var actions []funscript.Action
	// 10 Sekunden Bewegung, dann 10 Sekunden Stillstand.
	for i := 0; i < 40; i++ {
		pos := 10
		if i%2 == 0 {
			pos = 90
		}
		actions = append(actions, funscript.Action{At: int64(i * 250), Pos: pos})
	}
	for i := 0; i < 40; i++ {
		actions = append(actions, funscript.Action{At: int64(10000 + i*250), Pos: 50})
	}
	a.currentScript = &funscript.Script{Actions: actions}

	analysis, err := a.AnalyzeScript()
	if err != nil {
		t.Fatalf("AnalyzeScript: %v", err)
	}
	if len(analysis.Segments) == 0 {
		t.Fatal("keine Abschnitte erkannt")
	}
	if analysis.ShareByState["static"] < 0.25 {
		t.Errorf("Stillstand nicht erkannt: %v", analysis.ShareByState)
	}
	if !strings.Contains(analysis.Summary, "still") {
		t.Errorf("summary does not mention still stretches: %q", analysis.Summary)
	}
	total := 0.0
	for _, share := range analysis.ShareByState {
		total += share
	}
	if total < 0.95 || total > 1.05 {
		t.Errorf("Anteile summieren sich auf %.2f statt 1.0", total)
	}
}

func TestAnalyzeScriptWithoutScript(t *testing.T) {
	a := NewApp()
	if _, err := a.AnalyzeScript(); err == nil {
		t.Error("ohne geladenes Skript muss ein Fehler kommen")
	}
}

// Ein durchgehend gleichmäßiges Skript darf NICHT als Stillstand gelten -
// sonst wäre die Klassifikation wertlos.
func TestAnalyzeScriptSteadyMotion(t *testing.T) {
	a := NewApp()
	var actions []funscript.Action
	for i := 0; i < 80; i++ {
		pos := 10
		if i%2 == 0 {
			pos = 90
		}
		actions = append(actions, funscript.Action{At: int64(i * 250), Pos: pos})
	}
	a.currentScript = &funscript.Script{Actions: actions}

	analysis, err := a.AnalyzeScript()
	if err != nil {
		t.Fatalf("AnalyzeScript: %v", err)
	}
	if analysis.ShareByState["static"] > 0.1 {
		t.Errorf("gleichmäßige Bewegung fälschlich als Stillstand: %v", analysis.ShareByState)
	}
}
