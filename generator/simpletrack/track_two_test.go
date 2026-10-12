package simpletrack

import (
	"context"
	"os/exec"
	"path/filepath"
	"testing"
)

func TestTrackTwoPointsSynthetic(t *testing.T) {
	if _, err := exec.LookPath("ffmpeg"); err != nil {
		t.Skip("ffmpeg not available")
	}
	dir := t.TempDir()
	video := filepath.Join(dir, "two.mp4")
	// Two white boxes oscillating horizontally toward/away from each other.
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
	res, err := TrackTwoPoints(context.Background(), video,
		Rect{X: 40, Y: 100, W: 30, H: 30},
		Rect{X: 220, Y: 100, W: 30, H: 30},
		Options{MaxFrames: 40})
	if err != nil {
		t.Fatal(err)
	}
	if len(res.Positions) < 10 {
		t.Fatalf("too few samples: %d", len(res.Positions))
	}
	if res.Stats.VerticalRange < 5 {
		t.Fatalf("expected distance variation, range=%v", res.Stats.VerticalRange)
	}
	if len(res.LostFlags) != len(res.Positions) {
		t.Fatalf("lost flags len %d != positions %d", len(res.LostFlags), len(res.Positions))
	}
}

// TFTJ step 3 exit gate: a moving partner must change the distance signal
// when tracked; FixedB must freeze the partner box so the distance barely
// moves while only the tip (here: static) is followed.
func TestTrackTwoPoints_fixedVsTrackedPartner(t *testing.T) {
	if _, err := exec.LookPath("ffmpeg"); err != nil {
		t.Skip("ffmpeg not available")
	}
	dir := t.TempDir()
	video := filepath.Join(dir, "partner_moves.mp4")
	// Tip stays put; partner oscillates horizontally (large amplitude).
	args := []string{
		"-v", "error", "-y",
		"-f", "lavfi", "-i", "color=c=black:s=320x240:d=2:r=25",
		"-f", "lavfi", "-i", "color=c=white:s=30x30:d=2:r=25",
		"-f", "lavfi", "-i", "color=c=white:s=30x30:d=2:r=25",
		"-filter_complex",
		"[0][1]overlay=x=40:y=100[tmp];[tmp][2]overlay=x='180+50*sin(2*PI*t)':y=100",
		"-pix_fmt", "yuv420p", video,
	}
	if out, err := exec.Command("ffmpeg", args...).CombinedOutput(); err != nil {
		t.Fatalf("ffmpeg: %v: %s", err, out)
	}
	tip := Rect{X: 40, Y: 100, W: 30, H: 30}
	partner := Rect{X: 180, Y: 100, W: 30, H: 30}

	fixed, err := TrackTwoPoints(context.Background(), video, tip, partner,
		Options{MaxFrames: 50, FixedB: true})
	if err != nil {
		t.Fatalf("fixed: %v", err)
	}
	tracked, err := TrackTwoPoints(context.Background(), video, tip, partner,
		Options{MaxFrames: 50, FixedB: false})
	if err != nil {
		t.Fatalf("tracked: %v", err)
	}
	if fixed.Stats.VerticalRange > 15 {
		t.Fatalf("FixedB should keep almost-constant distance, range=%v", fixed.Stats.VerticalRange)
	}
	if tracked.Stats.VerticalRange < 40 {
		t.Fatalf("tracked partner should move distance a lot, range=%v", tracked.Stats.VerticalRange)
	}
	if tracked.Stats.VerticalRange <= fixed.Stats.VerticalRange*2 {
		t.Fatalf("tracked range %v should clearly exceed fixed %v",
			tracked.Stats.VerticalRange, fixed.Stats.VerticalRange)
	}
}
