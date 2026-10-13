package generator

import (
	"context"
	"encoding/json"
	"os"
	"os/exec"
	"path/filepath"
	"testing"
)

func TestGenerateNativeTwoPointSimple(t *testing.T) {
	if _, err := exec.LookPath("ffmpeg"); err != nil {
		t.Skip("ffmpeg not available")
	}
	dir := t.TempDir()
	video := filepath.Join(dir, "two.mp4")
	args := []string{
		"-v", "error", "-y",
		"-f", "lavfi", "-i", "color=c=black:s=320x240:d=2:r=25",
		"-f", "lavfi", "-i", "color=c=white:s=30x30:d=2:r=25",
		"-f", "lavfi", "-i", "color=c=white:s=30x30:d=2:r=25",
		"-filter_complex",
		"[0][1]overlay=x='40+30*sin(2*PI*t)':y=100[tmp];[tmp][2]overlay=x='220-30*sin(2*PI*t)':y=100",
		"-pix_fmt", "yuv420p", video,
	}
	if out, err := exec.Command("ffmpeg", args...).CombinedOutput(); err != nil {
		t.Fatalf("ffmpeg: %v: %s", err, out)
	}
	out := filepath.Join(dir, "out.funscript")
	err := GenerateWithContext(context.Background(), video,
		ROI{X: 40, Y: 100, W: 30, H: 30}, out,
		Options{
			Backend:             "csrt",
			Profile:             "tf",
			ROI2:                ROI{X: 220, Y: 100, W: 30, H: 30},
			ContactVibration:    true,
			SmoothWindow:        11,
			MinPeakDistanceMs:   150,
			MinActionIntervalMs: 100,
			MaxFrames:           40,
			PreferSimpletrack:   !NativeTrackingAvailable(),
		}, nil, nil)
	if err != nil {
		t.Fatalf("GenerateWithContext two-point: %v", err)
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
	if native == nil {
		t.Fatalf("expected native_pipeline, got %+v", doc.Metadata)
	}
	backend, _ := native["backend"].(string)
	if backend != "two_point_ncc" && backend != "two_point" {
		t.Fatalf("backend=%q", backend)
	}
}
