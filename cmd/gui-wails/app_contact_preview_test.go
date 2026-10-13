package main

import "testing"

func TestPreviewContactVibrationWiresThrough(t *testing.T) {
	a := &App{}
	res := a.PreviewContactVibration(ContactVibPreviewRequest{
		Span:  0.75,
		Curve: "soft",
	})
	if res.Hint == "" || len(res.Sample) < 4 {
		t.Fatalf("got %+v", res)
	}
	if res.PeakVib <= 0 {
		t.Fatalf("expected peak vib > 0, got %v", res.PeakVib)
	}
}
