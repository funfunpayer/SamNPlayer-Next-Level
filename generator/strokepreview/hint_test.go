package strokepreview

import "testing"

func TestToHintSuggestsPeakDistance(t *testing.T) {
	r := Report{Quality: "ok", StrokeHz: 1.5, ElapsedWallMs: 100, Reason: "clear"}
	h := r.ToHint()
	if h.SuggestedMinPeakDistanceMs < 120 || h.SuggestedMinPeakDistanceMs > 450 {
		t.Fatalf("peak ms=%d", h.SuggestedMinPeakDistanceMs)
	}
	if h.ProgressLine() == "" {
		t.Fatal("empty progress line")
	}
	m := h.MetadataMap()
	if m["stage"] != "A" {
		t.Fatalf("%v", m)
	}
}

func TestToHintWeakSuggestsAudio(t *testing.T) {
	r := Report{Quality: "weak", SuggestAudio: true, Reason: "no motion"}
	h := r.ToHint()
	if !h.SuggestAudioCheck {
		t.Fatal("expected audio hint")
	}
}
