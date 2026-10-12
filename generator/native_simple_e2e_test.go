package generator

import (
	"context"
	"encoding/json"
	"os"
	"os/exec"
	"path/filepath"
	"testing"
)

// End-to-end without OpenCV: ffmpeg clip → GenerateNativeSimple → funscript.
func TestGenerateNativeSimpleEndToEnd(t *testing.T) {
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
	opts := Options{
		Backend:             "csrt",
		NativePipeline:      true,
		SmoothWindow:        11,
		MinPeakDistanceMs:   150,
		MinActionIntervalMs: 100,
		MaxFrames:           40,
	}
	if err := GenerateNativeSimple(context.Background(), video, ROI{X: 140, Y: 80, W: 40, H: 40}, out, opts, nil, nil); err != nil {
		t.Fatalf("GenerateNativeSimple: %v", err)
	}
	data, err := os.ReadFile(out)
	if err != nil {
		t.Fatal(err)
	}
	var doc struct {
		Actions  []map[string]any `json:"actions"`
		Metadata map[string]any   `json:"metadata"`
	}
	if err := json.Unmarshal(data, &doc); err != nil {
		t.Fatal(err)
	}
	if len(doc.Actions) < 2 {
		t.Fatalf("expected >=2 actions, got %d", len(doc.Actions))
	}
	native, _ := doc.Metadata["native_pipeline"].(map[string]any)
	if native == nil {
		t.Fatalf("missing native_pipeline metadata: %+v", doc.Metadata)
	}
	if native["tracking"] != "simpletrack" {
		t.Fatalf("tracking=%v", native["tracking"])
	}
}
