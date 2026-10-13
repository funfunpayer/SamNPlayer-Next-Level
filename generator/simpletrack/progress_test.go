package simpletrack

import "testing"

func TestProgressReporterThrottles(t *testing.T) {
	var calls [][2]int
	p := newProgressReporter(func(done, total int) {
		calls = append(calls, [2]int{done, total})
	}, 1000)
	for i := 0; i <= 1000; i++ {
		p.report(i)
	}
	if len(calls) < 10 || len(calls) > 120 {
		n := 10
		if len(calls) < n {
			n = len(calls)
		}
		t.Fatalf("expected ~100 reports, got %d: %v", len(calls), calls[:n])
	}
	if calls[0][0] != 0 || calls[0][1] != 1000 {
		t.Fatalf("first call = %v", calls[0])
	}
	last := calls[len(calls)-1]
	if last[0] != 1000 {
		t.Fatalf("last done = %v want 1000", last)
	}
}

func TestProgressReporterNilSafe(t *testing.T) {
	var p *progressReporter
	p.report(1) // must not panic
	p = newProgressReporter(nil, 10)
	p.report(1)
}

func TestProgressTotalFramesAfterSeek(t *testing.T) {
	if got := progressTotalFrames(3000, 50, 30, 0); got != 1500 {
		t.Fatalf("remaining frames after seek = %d, want 1500", got)
	}
	if got := progressTotalFrames(3000, 50, 30, 200); got != 200 {
		t.Fatalf("MaxFrames after seek = %d, want 200", got)
	}
	if got := progressTotalFrames(3000, 0, 30, 0); got != 3000 {
		t.Fatalf("zero seek changed total: %d", got)
	}
	if got := progressTotalFrames(300, 20, 30, 100); got != 0 {
		t.Fatalf("known exhausted total = %d, want 0", got)
	}
	if got := progressTotalFrames(0, 20, 30, 100); got != 100 {
		t.Fatalf("unknown total with MaxFrames = %d, want 100", got)
	}
}
