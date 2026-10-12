package generator

import (
	"context"
	"os"
	"os/exec"
	"path/filepath"
	"strconv"
	"strings"
	"testing"
	"time"
)

func makeSeekTestClip(t *testing.T, path string, durationSec float64) {
	t.Helper()
	if _, err := exec.LookPath("ffmpeg"); err != nil {
		t.Skip("ffmpeg not on PATH")
	}
	cmd := exec.Command("ffmpeg", "-y", "-nostdin", "-v", "error",
		"-f", "lavfi", "-i", "color=c=blue:s=320x180:d="+strconv.FormatFloat(durationSec, 'f', 3, 64),
		"-pix_fmt", "yuv420p", path)
	if out, err := cmd.CombinedOutput(); err != nil {
		t.Fatalf("ffmpeg make clip: %v\n%s", err, out)
	}
}

func TestDumpFrameAtZeroAndMid(t *testing.T) {
	dir := t.TempDir()
	clip := filepath.Join(dir, "clip.mp4")
	makeSeekTestClip(t, clip, 3)

	ctx, cancel := context.WithTimeout(context.Background(), 15*time.Second)
	defer cancel()

	out0 := filepath.Join(dir, "f0.png")
	w, h, err := DumpFrameAt(ctx, clip, out0, 0)
	if err != nil {
		t.Fatalf("DumpFrameAt(0): %v", err)
	}
	if w != 320 || h != 180 {
		t.Fatalf("DumpFrameAt(0) size = %dx%d, want 320x180", w, h)
	}
	if st, err := os.Stat(out0); err != nil || st.Size() < 32 {
		t.Fatalf("DumpFrameAt(0) wrote unusable file: %v size=%v", err, st)
	}

	out1 := filepath.Join(dir, "f1.png")
	w, h, err = DumpFrameAt(ctx, clip, out1, 1.5)
	if err != nil {
		t.Fatalf("DumpFrameAt(1.5): %v", err)
	}
	if w != 320 || h != 180 {
		t.Fatalf("DumpFrameAt(1.5) size = %dx%d, want 320x180", w, h)
	}
}

func TestDumpFrameAtPastEndFailsCleanly(t *testing.T) {
	dir := t.TempDir()
	clip := filepath.Join(dir, "clip.mp4")
	makeSeekTestClip(t, clip, 1)

	ctx, cancel := context.WithTimeout(context.Background(), 15*time.Second)
	defer cancel()

	out := filepath.Join(dir, "past.png")
	_, _, err := DumpFrameAt(ctx, clip, out, 30)
	if err == nil {
		t.Fatal("DumpFrameAt past end: expected error")
	}
	// Must not surface the opaque "unknown frame size: … / EOF" pair alone —
	// empty-frame or frame-failed wording is actionable for Create seek.
	msg := err.Error()
	if !strings.Contains(msg, "empty frame") && !strings.Contains(msg, "frame at") {
		t.Fatalf("unexpected error wording: %v", err)
	}
}
