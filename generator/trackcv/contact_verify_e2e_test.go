//go:build cgo && opencv

package trackcv

import (
	"path/filepath"
	"testing"
)

// Synthetic OpenCV E2E for ContactVerifyK: a CSRT-survivable clip, teacher
// points on the stroke plus decoys in a quiet corner, then TrackROI with
// rhythm-grid. Without verify both sets steer; with K=1.5 only the stroke
// points survive. Complements the pure-geometry unit tests (nearest window /
// edge 3x3) that do not exercise VideoWriter → CSRT → SceneMap → verify.
func TestTrackROIContactVerifyFiltersQuietTeacherPoints(t *testing.T) {
	path := filepath.Join(t.TempDir(), "contact_verify.avi")
	const (
		seconds   = 12
		amplitude = 80.0
		freq      = 1.2
	)
	writeMovingVideo(t, path, seconds, amplitude, freq)

	roi := Rect{X: 100, Y: 40, W: 120, H: 160}
	var pts []ContactPoint
	for ms := int64(0); ms < int64(seconds)*1000; ms += 500 {
		pts = append(pts,
			ContactPoint{Ms: ms, X: 0.5, Y: 0.5},   // on the oscillating blob
			ContactPoint{Ms: ms, X: 0.02, Y: 0.02}, // quiet corner decoy
		)
	}
	nStroke, nTotal := len(pts)/2, len(pts)

	base := Options{Axis: "y", RhythmGrid: true, ContactPoints: pts}

	off := base
	resOff, err := TrackROI(path, roi, off)
	if err != nil {
		t.Fatalf("TrackROI RhythmGrid (verify off): %v", err)
	}
	if resOff.Stats.ContactPointsUsed != nTotal {
		t.Fatalf("verify off: used %d of %d points, want all", resOff.Stats.ContactPointsUsed, nTotal)
	}

	on := base
	on.ContactVerifyK = 1.5
	resOn, err := TrackROI(path, roi, on)
	if err != nil {
		t.Fatalf("TrackROI RhythmGrid (verify 1.5): %v", err)
	}
	if resOn.Stats.ContactPointsUsed != nStroke {
		t.Fatalf("verify 1.5: used %d points, want %d stroke-only (decoys dropped)",
			resOn.Stats.ContactPointsUsed, nStroke)
	}
	if len(resOn.SceneMap.Windows) == 0 {
		t.Fatal("expected a non-empty SceneMap from the rhythm-grid path")
	}
	if resOn.Stats.TotalFrames < int(testFPS)*seconds/2 {
		t.Fatalf("CSRT produced too few frames: %d", resOn.Stats.TotalFrames)
	}
}
