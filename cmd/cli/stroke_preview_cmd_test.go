package main

import "testing"

func TestStrokePreviewRejectsExtraVideoPaths(t *testing.T) {
	if got := runStrokePreview([]string{"old.mp4", "new.mp4"}); got != 2 {
		t.Fatalf("runStrokePreview with two videos returned %d, want usage error 2", got)
	}
}
