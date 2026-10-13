package funscript

import "testing"

func TestProximityInsideCore(t *testing.T) {
	b := ContactMarkBox{X: 100, Y: 100, W: 40, H: 40}
	if p := proximityToBox(120, 120, b, SpatialContactMargin); p != 1 {
		t.Fatalf("inside core got %v", p)
	}
}

func TestProximityOutsideMargin(t *testing.T) {
	b := ContactMarkBox{X: 100, Y: 100, W: 40, H: 40}
	if p := proximityToBox(10, 10, b, SpatialContactMargin); p != 0 {
		t.Fatalf("far away got %v", p)
	}
}

func TestSpatialContactIntensityUsesTrajectory(t *testing.T) {
	marks := &ContactMarks{
		Primary: &ContactMarkBox{X: 50, Y: 50, W: 20, H: 20, Class: "nipples"},
	}
	tr := &TrajectoryData{
		Width:  200,
		Height: 200,
		Tip: []TrajectoryPoint{
			{AtMs: 0, X: 10, Y: 10},
			{AtMs: 1000, X: 60, Y: 60}, // inside primary
			{AtMs: 2000, X: 10, Y: 10},
		},
	}
	if v := SpatialContactIntensity(marks, tr, 0); v != 0 {
		t.Fatalf("away at 0ms: %v", v)
	}
	if v := SpatialContactIntensity(marks, tr, 1000); v < 0.99 {
		t.Fatalf("inside at 1000ms: %v", v)
	}
	if v := SpatialContactIntensity(marks, tr, 2000); v != 0 {
		t.Fatalf("away at 2000ms: %v", v)
	}
}

func TestSpatialIgnoresTipBoxOnly(t *testing.T) {
	marks := &ContactMarks{
		Tip: &ContactMarkBox{X: 0, Y: 0, W: 50, H: 50},
	}
	tr := &TrajectoryData{Tip: []TrajectoryPoint{{AtMs: 0, X: 10, Y: 10}}}
	if v := SpatialContactIntensity(marks, tr, 0); v != 0 {
		t.Fatalf("tip-only marks must not drive spatial vib: %v", v)
	}
}
