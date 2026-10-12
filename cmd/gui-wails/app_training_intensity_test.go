package main

import (
	"testing"

	"github.com/funfunpayer/SamNPlayer/player"
)

// resolveTrainingScript is what both StartTraining and TrainingScriptPreview
// go through - a script's curve levels must come back scaled by
// intensityFactor (see TrainingRequest.IntensityFactor's own doc comment),
// while an unknown name still errors exactly like loadAnyTrainingScript
// alone would.
func TestResolveTrainingScriptAppliesIntensityFactor(t *testing.T) {
	t.Setenv("XDG_CONFIG_HOME", t.TempDir())

	neutral, err := resolveTrainingScript("vibration-wave-suction-focus", 1)
	if err != nil {
		t.Fatalf("resolveTrainingScript(neutral): %v", err)
	}
	scaled, err := resolveTrainingScript("vibration-wave-suction-focus", 0.5)
	if err != nil {
		t.Fatalf("resolveTrainingScript(0.5): %v", err)
	}

	wantPeak := neutral.Phases[0].Vibration.PeakLevel * 0.5
	if got := scaled.Phases[0].Vibration.PeakLevel; got != wantPeak {
		t.Errorf("PeakLevel not scaled: got %v, want %v", got, wantPeak)
	}
	if got, want := scaled.Phases[0].Vibration.RampUpMs, neutral.Phases[0].Vibration.RampUpMs; got != want {
		t.Errorf("timing must stay untouched by an intensity factor: got %d, want %d", got, want)
	}
}

func TestResolveTrainingScriptNeutralFactorLeavesScriptUnchanged(t *testing.T) {
	t.Setenv("XDG_CONFIG_HOME", t.TempDir())

	for _, factor := range []float64{0, -1, 1} {
		script, err := resolveTrainingScript("vibration-wave-suction-focus", factor)
		if err != nil {
			t.Fatalf("resolveTrainingScript(%v): %v", factor, err)
		}
		builtin, _ := loadAnyTrainingScript("vibration-wave-suction-focus")
		if got, want := script.Phases[0].Vibration.PeakLevel, builtin.Phases[0].Vibration.PeakLevel; got != want {
			t.Errorf("factor=%v should be a no-op: got PeakLevel=%v, want %v", factor, got, want)
		}
	}
}

func TestResolveTrainingScriptUnknownNameErrors(t *testing.T) {
	t.Setenv("XDG_CONFIG_HOME", t.TempDir())

	if _, err := resolveTrainingScript("does-not-exist", 1); err == nil {
		t.Error("an unknown script name should still error, factor or not")
	}
}

// The plan preview must show the SAME curve that's about to run - a user
// picking "Gentle" (or getting an automatic history nudge) and then seeing
// an unscaled preview would be misled about what they're starting.
func TestTrainingScriptPreviewReflectsIntensityFactor(t *testing.T) {
	t.Setenv("XDG_CONFIG_HOME", t.TempDir())
	a := NewApp()

	neutral, err := a.TrainingScriptPreview("vibration-wave-suction-focus", 1)
	if err != nil {
		t.Fatalf("TrainingScriptPreview(neutral): %v", err)
	}
	scaled, err := a.TrainingScriptPreview("vibration-wave-suction-focus", 0.5)
	if err != nil {
		t.Fatalf("TrainingScriptPreview(0.5): %v", err)
	}
	if len(neutral.Vibration) == 0 || len(scaled.Vibration) == 0 {
		t.Fatal("expected non-empty vibration curves in both previews")
	}
	// Peak point (second point of the first repeat, see buildScriptPreview)
	// must be roughly halved, not identical to the neutral preview.
	neutralPeak := neutral.Vibration[1].Level
	scaledPeak := scaled.Vibration[1].Level
	if scaledPeak != neutralPeak*0.5 {
		t.Errorf("preview peak not scaled: neutral=%v scaled=%v (want %v)", neutralPeak, scaledPeak, neutralPeak*0.5)
	}
}

// Die Vorschau zeigt beim Plateau-Script, was der Player fährt: die zweite
// Wiederholung beginnt auf dem Plateau, nicht bei 0.
func TestScriptPreviewPlateauStartsFromFloor(t *testing.T) {
	script, ok := player.BuiltinTrainingScript("plateau")
	if !ok {
		t.Fatal("plateau fehlt")
	}
	pts := buildScriptPreview(script).Vibration
	floor := script.Phases[0].Vibration.EndLevel
	// 4 Punkte pro Wiederholung: Start, Höhepunkt, Halteende, Ende.
	if len(pts) < 8 || pts[0].Level != 0 || pts[4].Level != floor {
		t.Fatalf("Start 1. Wdh. %v (soll 0), Start 2. Wdh. %v (soll %v)", pts[0].Level, pts[4].Level, floor)
	}
}
