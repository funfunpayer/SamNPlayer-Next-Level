package strokepreview

import (
	"testing"

	"github.com/funfunpayer/SamNPlayer/funscript"
)

func TestPeakOverlapMatchesNearbyUps(t *testing.T) {
	rep := Report{
		UpCount: 2,
		Extrema: []Extrema{
			{AtMs: 1000, Kind: "up"},
			{AtMs: 2000, Kind: "up"},
			{AtMs: 1500, Kind: "down"},
		},
	}
	// Triangle waves peaking near 1000 and 2000.
	ref := []funscript.Action{
		{At: 0, Pos: 20},
		{At: 1000, Pos: 90},
		{At: 1500, Pos: 20},
		{At: 2000, Pos: 90},
		{At: 2500, Pos: 20},
	}
	m, n, recall := PeakOverlap(rep, ref, 300)
	if n < 2 {
		t.Fatalf("expected ref peaks, got %d", n)
	}
	if m < 2 || recall < 0.9 {
		t.Fatalf("matched=%d/%d recall=%.2f", m, n, recall)
	}
}
