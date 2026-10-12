package motionx

import "testing"

func TestChaptersMapsStates(t *testing.T) {
	segs := []Segment{
		{State: Static, StartMs: 0, EndMs: 1000},
		{State: Starting, StartMs: 1000, EndMs: 2000},
		{State: Regular, StartMs: 2000, EndMs: 8000},
		{State: Accelerating, StartMs: 8000, EndMs: 9000},
		{State: Stopping, StartMs: 9000, EndMs: 9500},
	}
	ch := Chapters(segs)
	if len(ch) < 4 {
		t.Fatalf("zu grob zusammengefasst: %+v", ch)
	}
	if ch[0].Kind != "pause" {
		t.Fatalf("erstes Kapitel: %s", ch[0].Kind)
	}
	last := ch[len(ch)-1]
	if last.Kind != "winddown" {
		t.Fatalf("letztes Kapitel: %s", last.Kind)
	}
}

func TestChaptersNil(t *testing.T) {
	if Chapters(nil) != nil {
		t.Fatal("nil in, nil out")
	}
}
