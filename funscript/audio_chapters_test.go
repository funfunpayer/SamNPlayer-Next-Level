package funscript

import "testing"

func TestChaptersFromAudioSegments(t *testing.T) {
	segs := []AudioSegmentHint{
		{Label: "holding", StartMs: 0, EndMs: 1500, SpeechHold: true},
		{Label: "gentle", StartMs: 1500, EndMs: 4000},
		{Label: "intense", StartMs: 4000, EndMs: 8000},
		{Label: "climax", StartMs: 8000, EndMs: 10000},
		{Label: "gentle", StartMs: 50, EndMs: 50}, // empty — skip
	}
	got := ChaptersFromAudioSegments(segs)
	if len(got) != 4 {
		t.Fatalf("want 4 chapters, got %d: %+v", len(got), got)
	}
	if got[0].Name != "Hold (speech)" || got[0].StartTime != 0 || got[0].EndTime != 1500 {
		t.Fatalf("holding chapter: %+v", got[0])
	}
	if got[1].Name != "Gentle" || got[2].Name != "Intense" || got[3].Name != "Climax" {
		t.Fatalf("label names: %+v", got)
	}
}

func TestChaptersFromAudioSegmentsNil(t *testing.T) {
	if ChaptersFromAudioSegments(nil) != nil {
		t.Fatal("nil in → nil out")
	}
}
