package generator

import (
	"strings"
	"testing"
)

func TestNativePipelineEligible(t *testing.T) {
	roi := ROI{X: 10, Y: 10, W: 40, H: 40}
	ok := Options{Backend: "csrt", DisableCache: true}
	// Eligible is option-only: OpenCV optional (Windows uses simpletrack).
	if !NativePipelineEligible(ok, roi) {
		t.Fatal("expected eligible for plain CSRT options")
	}
	// Default GUI has AutoRetry on — must still be eligible (retry is in-Go).
	if !NativePipelineEligible(Options{Backend: "csrt", AutoRetry: true}, roi) {
		t.Fatal("AutoRetry must not block the Go path")
	}
	if NativePipelineEligible(Options{Backend: "flow"}, roi) {
		t.Fatal("flow must not be eligible")
	}
	// Tf/Tj two-point is now Go-native (CSRT / simpletrack TrackTwoPoints).
	if !NativePipelineEligible(Options{Backend: "csrt", ROI2: ROI{W: 10, H: 10}, Profile: "tf"}, roi) {
		t.Fatal("roi2 / Tf/Tj must be eligible on the Go path")
	}
	if NativePipelineEligible(Options{Backend: "csrt", PerSceneROI: true}, roi) {
		t.Fatal("per-scene must not be eligible")
	}
	if NativePipelineEligible(Options{Backend: "csrt", AIQualityOpinion: true}, roi) {
		t.Fatal("AI opinion must not be eligible")
	}
	if !NativePipelineEligible(Options{Backend: "csrt", ROI2: ROI{W: 10, H: 10},
		ExtraTargets: []NamedROI{{X: 1, Y: 1, W: 5, H: 5}}}, roi) {
		t.Fatal("extra Tf/Tj targets + ROI2 must stay on the Go path")
	}
	if NativePipelineEligible(Options{Backend: "csrt", MaskROIs: []ROI{{X: 1, Y: 1, W: 5, H: 5}}}, roi) {
		t.Fatal("soft masks without RhythmGrid must force Python path")
	}
	// M3: soft masks + RhythmGrid on single-ROI stay on the Go path.
	if !NativePipelineEligible(Options{Backend: "csrt", RhythmGrid: true,
		MaskROIs: []ROI{{X: 1, Y: 1, W: 5, H: 5}}}, roi) {
		t.Fatal("soft masks + RhythmGrid must stay eligible on the Go path")
	}
	// Two-point + masks still force Python even with RhythmGrid (grid is single-ROI).
	if NativePipelineEligible(Options{Backend: "csrt", RhythmGrid: true, Profile: "tf",
		ROI2: ROI{W: 10, H: 10}, MaskROIs: []ROI{{X: 1, Y: 1, W: 5, H: 5}}}, roi) {
		t.Fatal("soft masks + Tf/Tj must still force Python")
	}
	// Audio check is post-hoc in Go — must not force the Python path.
	if !NativePipelineEligible(Options{Backend: "csrt", AudioCheck: true}, roi) {
		t.Fatal("AudioCheck must stay eligible on the Go path")
	}
	if NativePipelineEligible(Options{Backend: "csrt"}, ROI{}) {
		t.Fatal("empty ROI must not be eligible")
	}
	if !SimpleTrackingAvailable() {
		t.Fatal("simpletrack must always be available")
	}
}

func TestNativePipelineSkipReason(t *testing.T) {
	roi := ROI{X: 10, Y: 10, W: 40, H: 40}
	if got := NativePipelineSkipReason(Options{Backend: "csrt"}, roi); got != "" {
		t.Fatalf("eligible options must have empty skip reason, got %q", got)
	}
	got := NativePipelineSkipReason(Options{Backend: "csrt", PerSceneROI: true}, roi)
	if !strings.Contains(got, "Re-find region") {
		t.Fatalf("PerSceneROI skip reason should name Re-find region, got %q", got)
	}
	got = NativePipelineSkipReason(Options{Backend: "flow"}, roi)
	if !strings.Contains(got, "flow") {
		t.Fatalf("flow skip reason should name backend, got %q", got)
	}
	got = NativePipelineSkipReason(Options{Backend: "csrt"}, ROI{})
	if !strings.Contains(got, "empty") {
		t.Fatalf("empty ROI skip reason, got %q", got)
	}
}
