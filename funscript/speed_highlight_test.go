package funscript

import "testing"

func TestSpeedHighlightsFindsFastSegment(t *testing.T) {
	// 100 Pos in 50 ms → Intensität = 500*100/50 = 1000 (klar über Default 400).
	actions := []Action{
		{At: 0, Pos: 0},
		{At: 50, Pos: 100},
		{At: 1000, Pos: 100}, // langsam / still
	}
	segs := SpeedHighlights(actions, 0)
	if len(segs) != 1 {
		t.Fatalf("expected 1 segment, got %+v", segs)
	}
	if segs[0].FromMs != 0 || segs[0].ToMs != 50 {
		t.Fatalf("unexpected span: %+v", segs[0])
	}
	if segs[0].Intensity < 999 || segs[0].Intensity > 1001 {
		t.Fatalf("intensity want ~1000, got %.1f", segs[0].Intensity)
	}
}

func TestSpeedHighlightsRespectsThreshold(t *testing.T) {
	actions := []Action{
		{At: 0, Pos: 0},
		{At: 500, Pos: 100}, // Intensität = 500*100/500 = 100
	}
	if segs := SpeedHighlights(actions, 400); len(segs) != 0 {
		t.Fatalf("expected none under threshold, got %+v", segs)
	}
	if segs := SpeedHighlights(actions, 50); len(segs) != 1 {
		t.Fatalf("expected 1 above low threshold, got %+v", segs)
	}
}

func TestSpeedHighlightsEmpty(t *testing.T) {
	if segs := SpeedHighlights(nil, 0); segs != nil {
		t.Fatalf("want nil, got %+v", segs)
	}
}
