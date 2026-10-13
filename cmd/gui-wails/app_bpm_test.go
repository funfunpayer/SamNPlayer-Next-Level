package main

import (
	"context"
	"testing"
)

func TestBpmFromHzFoldsIntoInputRange(t *testing.T) {
	cases := map[float64]float64{0: 0, 2: 120, 0.25: 30, 0.2: 24, 5: 150, 1.5: 90}
	for hz, want := range cases {
		if got := bpmFromHz(hz); got != want {
			t.Errorf("bpmFromHz(%v) = %v, want %v", hz, got, want)
		}
	}
}

func TestEstimatePlaybackAudioBPM(t *testing.T) {
	t.Setenv("XDG_CONFIG_HOME", t.TempDir())
	old := estimateAudioTempoFn
	t.Cleanup(func() { estimateAudioTempoFn = old })
	estimateAudioTempoFn = func(_ context.Context, path string) (float64, error) {
		if path != "/v.mp4" {
			t.Fatalf("path %q", path)
		}
		return 2.05, nil
	}
	a := NewApp()
	if _, err := a.EstimatePlaybackAudioBPM(); err == nil {
		t.Fatal("no video loaded must be an error")
	}
	a.stateMu.Lock()
	a.setVideoPathLocked("/v.mp4")
	a.stateMu.Unlock()
	if bpm, err := a.EstimatePlaybackAudioBPM(); err != nil || bpm != 123 {
		t.Fatalf("got %v %v, want 123", bpm, err)
	}
}
