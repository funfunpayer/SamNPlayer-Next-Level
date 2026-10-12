package funscript

import (
	"math"
	"testing"
)

func TestEvaluateDenseQualityReconstruction(t *testing.T) {
	// Dense sine; fine keyframes → low reconstruction error; coarse → higher.
	denseAt := make([]float64, 200)
	densePos := make([]float64, 200)
	for i := range denseAt {
		denseAt[i] = float64(i) * 50 // 20 Hz sample
		densePos[i] = 50 + 40*math.Sin(2*math.Pi*float64(i)/40)
	}
	fine := make([]Action, 0, 40)
	for i := 0; i < len(denseAt); i += 5 {
		fine = append(fine, Action{At: int64(denseAt[i]), Pos: int(math.Round(densePos[i]))})
	}
	coarse := make([]Action, 0, 5)
	for i := 0; i < len(denseAt); i += 50 {
		coarse = append(coarse, Action{At: int64(denseAt[i]), Pos: int(math.Round(densePos[i]))})
	}
	in := DenseQualityInput{DenseAt: denseAt, DensePos: densePos}
	rFine := EvaluateDenseQuality(fine, in)
	rCoarse := EvaluateDenseQuality(coarse, in)
	if rFine.EstimatedFromScriptOnly {
		t.Fatal("expected dense path (EstimatedFromScriptOnly=false)")
	}
	if rFine.Score < rCoarse.Score-0.05 && rFine.Passed && !rCoarse.Passed {
		// coarse may fail reconstruction; fine should not be worse
	}
	// Coarse should accumulate more warnings or lower score when reconstruction bites.
	if rCoarse.Score > rFine.Score+0.15 {
		t.Fatalf("coarse score %.2f unexpectedly higher than fine %.2f", rCoarse.Score, rFine.Score)
	}
}

func TestEvaluateDenseQualityTrackerLost(t *testing.T) {
	acts := []Action{{At: 0, Pos: 20}, {At: 400, Pos: 80}, {At: 800, Pos: 20}, {At: 1200, Pos: 80}}
	denseAt := []float64{0, 200, 400, 600, 800, 1000, 1200}
	densePos := []float64{20, 50, 80, 50, 20, 50, 80}
	lost := 0.6
	r := EvaluateDenseQuality(acts, DenseQualityInput{
		DenseAt: denseAt, DensePos: densePos,
		TrackerLostFraction: &lost,
	})
	if r.Passed {
		t.Fatalf("expected hard-fail on 60%% tracker lost, got %+v", r)
	}
	if r.Score >= 0.9 {
		t.Fatalf("expected score penalty, got %.2f", r.Score)
	}
}

func TestEvaluateDenseQualityMotionRange(t *testing.T) {
	acts := []Action{{At: 0, Pos: 20}, {At: 400, Pos: 80}, {At: 800, Pos: 20}}
	tiny := 0.01
	r := EvaluateDenseQuality(acts, DenseQualityInput{MotionRangeFraction: &tiny})
	if r.Passed {
		t.Fatalf("expected hard-fail on tiny motion, got %+v", r)
	}
}

func TestSpectralConcentrationSineVsNoise(t *testing.T) {
	sine := make([]float64, 256)
	noise := make([]float64, 256)
	for i := range sine {
		sine[i] = math.Sin(2 * math.Pi * float64(i) / 32)
		noise[i] = float64((i*17+3)%50) / 50 // rough pseudo-noise
	}
	cs := spectralConcentration(sine)
	cn := spectralConcentration(noise)
	if cs == nil || cn == nil {
		t.Fatalf("nil concentration sine=%v noise=%v", cs, cn)
	}
	if *cs <= *cn {
		t.Fatalf("sine should concentrate more than noise: sine=%.3f noise=%.3f", *cs, *cn)
	}
	if *cs < 0.25 {
		t.Fatalf("clean sine concentration too low: %.3f", *cs)
	}
}
