package main

import (
	"fmt"

	"github.com/funfunpayer/SamNPlayer/funscript"
)

// GetScriptActions liefert die vollen, nicht resampleten Punkte des
// aktuell geladenen Skripts - anders als GetScriptCurve, das für die
// Anzeige auf maxPoints herunterrechnet. Grundlage für den Kurven-Editor:
// bearbeitet werden muss der echte Punkt, nicht ein Anzeige-Kompromiss.
func (a *App) GetScriptActions() ([]funscript.Action, error) {
	script := a.loadedScript()
	if script == nil {
		return nil, fmt.Errorf("no script loaded")
	}
	return script.Actions, nil
}

// GetSpeedHighlights liefert OFS-/Community-Stil „zu schnell“-Segmente
// (Intensität = 500×|Δpos|/|Δt|). maxIntensity <= 0 nutzt den Default.
func (a *App) GetSpeedHighlights(maxIntensity float64) ([]funscript.SpeedSegment, error) {
	script := a.loadedScript()
	if script == nil {
		return nil, fmt.Errorf("no script loaded")
	}
	return funscript.SpeedHighlights(script.Actions, maxIntensity), nil
}

// SaveScriptActions schreibt eine im Editor geänderte Punktliste zurück und
// lädt das Skript danach über denselben Pfad wie LoadFunscript neu ein - so
// bleibt a.currentScript exakt das, was auf der Platte steht (geklemmte
// Positionswerte, sortierte Reihenfolge), statt dass Speicher- und
// Dateizustand leicht auseinanderlaufen.
func (a *App) SaveScriptActions(actions []funscript.Action) error {
	return a.SaveScriptAxisActions(string(funscript.AxisGeneral), actions)
}
