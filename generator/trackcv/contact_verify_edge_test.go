//go:build cgo && opencv

package trackcv

import "testing"

// Regression coverage for ContactVerifyK's two subtle geometry/time choices.
// These tests intentionally exercise only verification: the OpenCV TrackROI
// path is covered separately by the package's native video tests.
func TestVerifyContactPointsUsesNearestOverlappingWindow(t *testing.T) {
	// Real rhythm windows overlap (8 s wide, 2 s step). At 5.1 s the
	// 2-10 s window (centre 6 s) is nearer than 0-8 s (centre 4 s).
	weak := make([]uint8, 16)
	weak[0] = 100
	weak[15] = 120 // not enough for k=1.5
	strong := make([]uint8, 16)
	strong[0] = 100
	strong[15] = 200
	m := SceneMap{Cols: 4, Rows: 4, Windows: []MapWindow{
		{StartMs: 0, EndMs: 8000, ChosenCell: 0, Score: weak},
		{StartMs: 2000, EndMs: 10000, ChosenCell: 0, Score: strong},
	}}
	p := ContactPoint{Ms: 5100, X: 0.99, Y: 0.99}
	got := verifyContactPoints([]ContactPoint{p}, m, 1.5)
	if len(got) != 1 || got[0].Ms != p.Ms {
		t.Fatalf("nearest overlapping window should keep point at %d ms, got %+v", p.Ms, got)
	}
}

func TestVerifyContactPointsClampsThreeByThreeAtImageEdges(t *testing.T) {
	score := make([]uint8, 16)
	score[5] = 100  // engine's own cell
	score[0] = 180  // top-left contact evidence
	score[15] = 180 // bottom-right contact evidence
	m := SceneMap{Cols: 4, Rows: 4, Windows: []MapWindow{{
		StartMs:    0,
		EndMs:      8000,
		ChosenCell: 5,
		Score:      score,
	}}}
	pts := []ContactPoint{
		{Ms: 1000, X: 0, Y: 0},
		{Ms: 2000, X: 1, Y: 1},
	}
	got := verifyContactPoints(pts, m, 1.5)
	if len(got) != 2 {
		t.Fatalf("edge 3x3 neighbourhoods should be clamped and keep both points, got %+v", got)
	}
}
