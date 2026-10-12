package main

import "testing"

func TestPreviewPostprocessWiresThrough(t *testing.T) {
	a := &App{}
	res, err := a.PreviewPostprocess(PostprocessPreviewRequest{
		SmoothWindow:      11,
		MinPeakDistanceMs: 150,
		PeakProminence:    0.2,
	})
	if err != nil {
		t.Fatal(err)
	}
	if res.KeyframeCount < 2 {
		t.Fatalf("expected keyframes, got %d", res.KeyframeCount)
	}
	if res.Hint == "" {
		t.Fatal("empty hint")
	}
}
