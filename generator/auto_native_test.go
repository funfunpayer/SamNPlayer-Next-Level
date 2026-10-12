package generator

import (
	"context"
	"encoding/json"
	"os"
	"os/exec"
	"path/filepath"
	"testing"
)

// Default generate options must take the product tracker path without PreferPython.
// With -tags opencv → Go CSRT. Without OpenCV → Python CSRT (hard requirement).
func TestGenerateWithContextAutoNativeDefault(t *testing.T) {
	if _, err := exec.LookPath("ffmpeg"); err != nil {
		t.Skip("ffmpeg not available")
	}
	dir := t.TempDir()
	video := filepath.Join(dir, "ball.mp4")
	args := []string{
		"-v", "error", "-y",
		"-f", "lavfi", "-i", "color=c=black:s=320x240:d=2:r=25",
		"-f", "lavfi", "-i", "color=c=white:s=40x40:d=2:r=25",
		"-filter_complex", "[0][1]overlay=x=140:y='80+40*sin(2*PI*t)'",
		"-pix_fmt", "yuv420p", video,
	}
	if out, err := exec.Command("ffmpeg", args...).CombinedOutput(); err != nil {
		t.Fatalf("ffmpeg: %v: %s", err, out)
	}
	out := filepath.Join(dir, "out.funscript")
	err := GenerateWithContext(context.Background(), video, ROI{X: 140, Y: 80, W: 40, H: 40}, out, Options{
		Backend:             "csrt",
		AutoRetry:           true, // default GUI
		SmoothWindow:        11,
		MinPeakDistanceMs:   150,
		MinActionIntervalMs: 100,
		MaxFrames:           40,
	}, nil, nil)
	if err != nil {
		if !NativeTrackingAvailable() && CheckDependencies() != nil {
			t.Skipf("no Go CSRT and no Python CSRT in this environment: %v", err)
		}
		t.Fatalf("GenerateWithContext: %v", err)
	}
	data, err := os.ReadFile(out)
	if err != nil {
		t.Fatal(err)
	}
	var doc struct {
		Metadata map[string]any `json:"metadata"`
	}
	if err := json.Unmarshal(data, &doc); err != nil {
		t.Fatal(err)
	}
	native, _ := doc.Metadata["native_pipeline"].(map[string]any)
	if NativeTrackingAvailable() {
		if native == nil {
			t.Fatalf("OpenCV build must use Go CSRT native_pipeline, got %+v", doc.Metadata)
		}
		return
	}
	if native != nil {
		if tr, _ := native["tracking"].(string); tr == "simpletrack" {
			t.Fatal("product Generate must not use simpletrack without PreferSimpletrack")
		}
	}
}
