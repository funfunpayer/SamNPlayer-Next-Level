package simpletrack

import (
	"context"
	"os/exec"
	"path/filepath"
	"testing"
)

func TestTrackROISynthetic(t *testing.T) {
	if _, err := exec.LookPath("ffmpeg"); err != nil {
		t.Skip("ffmpeg not available")
	}
	dir := t.TempDir()
	video := filepath.Join(dir, "ball.mp4")
	// White ball bouncing vertically via lavfi overlay — enough contrast for NCC.
	// (drawbox's y=sin(t) is static on some ffmpeg builds; overlay animates.)
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
	res, err := TrackROI(context.Background(), video, Rect{X: 140, Y: 80, W: 40, H: 40}, Options{
		MaxFrames: 40,
		Axis:      "y",
	})
	if err != nil {
		t.Fatal(err)
	}
	if len(res.Positions) < 10 {
		t.Fatalf("too few samples: %d", len(res.Positions))
	}
	if res.Stats.TotalFrames < 10 {
		t.Fatalf("total frames %d", res.Stats.TotalFrames)
	}
	if res.Stats.Confidence <= 0 {
		t.Fatalf("confidence %v", res.Stats.Confidence)
	}
	if res.Stats.VerticalRange < 20 {
		t.Fatalf("expected vertical motion, range=%v", res.Stats.VerticalRange)
	}
}

func TestTrackROICancel(t *testing.T) {
	if _, err := exec.LookPath("ffmpeg"); err != nil {
		t.Skip("ffmpeg not available")
	}
	dir := t.TempDir()
	video := filepath.Join(dir, "long.mp4")
	args := []string{
		"-v", "error", "-y",
		"-f", "lavfi", "-i", "testsrc=size=160x120:rate=25:duration=6",
		"-pix_fmt", "yuv420p", video,
	}
	if out, err := exec.Command("ffmpeg", args...).CombinedOutput(); err != nil {
		t.Fatalf("ffmpeg: %v: %s", err, out)
	}
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	n := 0
	_, err := TrackROI(ctx, video, Rect{X: 40, Y: 30, W: 40, H: 40}, Options{
		Cancel: func() bool {
			n++
			return n > 5
		},
	})
	if err == nil {
		t.Fatal("expected cancel error")
	}
}
