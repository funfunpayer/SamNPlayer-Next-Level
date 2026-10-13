//go:build !suite

package main

import (
	"errors"
	"testing"
)

// Free build: training/learning bindings refuse with errSuiteOnly and
// report suite=false, so the GUI hides them.
func TestCustomerBuildRefusesStudioTools(t *testing.T) {
	t.Setenv("XDG_CONFIG_HOME", t.TempDir())
	a := NewApp()
	if a.IsSuiteBuild() {
		t.Fatal("default build must be the Free edition")
	}
	if k := guiAssetKind(); k != "gui" {
		t.Fatalf("Free build must update from the Free asset, got %q", k)
	}
	if err := a.RunRoiModelTraining(10, "cpu"); !errors.Is(err, errSuiteOnly) {
		t.Fatalf("AI Train must be refused: %v", err)
	}
	if err := a.RunGoldenClipBenchmark("m.json"); !errors.Is(err, errSuiteOnly) {
		t.Fatalf("benchmark must be refused: %v", err)
	}
	if _, err := a.TrainQualityModel(); !errors.Is(err, errSuiteOnly) {
		t.Fatalf("quality model training must be refused: %v", err)
	}
	if _, err := a.ExportSceneMapLearning("x.samn"); !errors.Is(err, errSuiteOnly) {
		t.Fatalf("learning export must be refused: %v", err)
	}
	if a.CheckRoiTrainingAvailable() || a.CancelRoiTraining() || a.CancelGoldenClipBenchmark() {
		t.Fatal("availability/cancel must be false in the Free build")
	}
}
