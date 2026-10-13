//go:build cgo && opencv

package trackcv

import "testing"

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
