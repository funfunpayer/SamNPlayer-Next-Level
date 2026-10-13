package simpletrack

import (
	"context"
	"os/exec"
	"path/filepath"
	"reflect"
	"testing"
)

// TestTrackROICaptureTrajectory locks in two claims from the MT-Debug
// additive capture hook: (1) CaptureTrajectory=false leaves the signal
// path byte-identical to before the flag existed, and (2) when on,
// TrajectoryA lines up 1:1 with TimestampsMs/Positions in the same
// video-pixel space as Width/Height.
func TestTrackROICaptureTrajectory(t *testing.T) {
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
	roi := Rect{X: 140, Y: 80, W: 40, H: 40}

	off, err := TrackROI(context.Background(), video, roi, Options{MaxFrames: 40, Axis: "y"})
	if err != nil {
		t.Fatal(err)
	}
	if off.TrajectoryA != nil {
		t.Fatalf("CaptureTrajectory=false must leave TrajectoryA nil, got %d points", len(off.TrajectoryA))
	}

	on, err := TrackROI(context.Background(), video, roi, Options{MaxFrames: 40, Axis: "y", CaptureTrajectory: true})
	if err != nil {
		t.Fatal(err)
	}
	if !reflect.DeepEqual(off.Positions, on.Positions) {
		t.Fatalf("CaptureTrajectory changed Positions: off=%v on=%v", off.Positions, on.Positions)
	}
	if !reflect.DeepEqual(off.TimestampsMs, on.TimestampsMs) {
		t.Fatalf("CaptureTrajectory changed TimestampsMs")
	}
	if off.Stats != on.Stats {
		t.Fatalf("CaptureTrajectory changed Stats: off=%+v on=%+v", off.Stats, on.Stats)
	}
	if len(on.TrajectoryA) != len(on.Positions) {
		t.Fatalf("TrajectoryA len %d != Positions len %d", len(on.TrajectoryA), len(on.Positions))
	}
	for i, p := range on.TrajectoryA {
		if p.X < 0 || p.X > float64(on.Width) || p.Y < 0 || p.Y > float64(on.Height) {
			t.Fatalf("TrajectoryA[%d]=%+v out of video bounds %dx%d", i, p, on.Width, on.Height)
		}
	}
}

// TestTrackTwoPointsCaptureTrajectory: same claims as above, for the
// tip+partner path — TrajectoryB must also be populated.
func TestTrackTwoPointsCaptureTrajectory(t *testing.T) {
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
	roiA := Rect{X: 40, Y: 100, W: 30, H: 30}
	roiB := Rect{X: 220, Y: 100, W: 30, H: 30}

	off, err := TrackTwoPoints(context.Background(), video, roiA, roiB, Options{MaxFrames: 40})
	if err != nil {
		t.Fatal(err)
	}
	if off.TrajectoryA != nil || off.TrajectoryB != nil {
		t.Fatalf("CaptureTrajectory=false must leave TrajectoryA/B nil")
	}

	on, err := TrackTwoPoints(context.Background(), video, roiA, roiB, Options{MaxFrames: 40, CaptureTrajectory: true})
	if err != nil {
		t.Fatal(err)
	}
	if !reflect.DeepEqual(off.Positions, on.Positions) {
		t.Fatalf("CaptureTrajectory changed Positions")
	}
	if !reflect.DeepEqual(off.LostFlags, on.LostFlags) {
		t.Fatalf("CaptureTrajectory changed LostFlags")
	}
	if len(on.TrajectoryA) != len(on.Positions) || len(on.TrajectoryB) != len(on.Positions) {
		t.Fatalf("TrajectoryA/B len mismatch: A=%d B=%d Positions=%d",
			len(on.TrajectoryA), len(on.TrajectoryB), len(on.Positions))
	}
}
