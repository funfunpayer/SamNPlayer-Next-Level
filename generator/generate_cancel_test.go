package generator

import (
	"context"
	"errors"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"testing"
	"time"
)

// TestGenerateWithContextCancelPython runs a fake long-lived "python" and
// cancels after 200ms — must return context.Canceled (review v0.5.1).
func TestGenerateWithContextCancelPython(t *testing.T) {
	if runtime.GOOS == "windows" {
		t.Skip("fake interpreter shell scripts are unix-only")
	}
	dir := t.TempDir()
	withOnlyPath(t, dir)

	// Pass package checks (-c …) immediately; sleep on real generate args.
	// exec so CommandContext kill hits sleep (not a parent bash that leaves
	// sleep holding the stderr pipe open — Scan would hang until sleep ends).
	body := "#!/bin/bash\n" +
		"if [ \"$1\" = \"-c\" ]; then exit 0; fi\n" +
		"echo \"fake generate running\" >&2\n" +
		"exec /bin/sleep 30\n"
	py := filepath.Join(dir, "python3")
	if err := os.WriteFile(py, []byte(body), 0755); err != nil {
		t.Fatal(err)
	}
	_ = os.WriteFile(filepath.Join(dir, "python"), []byte(body), 0755)

	video := filepath.Join(dir, "clip.mp4")
	out := filepath.Join(dir, "out.funscript")
	_ = os.WriteFile(video, []byte("not-a-real-video"), 0644)

	ctx, cancel := context.WithCancel(context.Background())
	done := make(chan error, 1)
	go func() {
		done <- GenerateWithContext(ctx, video, ROI{X: 10, Y: 10, W: 40, H: 40}, out, Options{
			Backend: "csrt",
			// Force Python path even when Go native is eligible.
			PreferPython: true,
		}, nil, nil)
	}()
	time.Sleep(200 * time.Millisecond)
	cancel()
	select {
	case err := <-done:
		if !errors.Is(err, context.Canceled) {
			t.Fatalf("want context.Canceled, got %v", err)
		}
	case <-time.After(5 * time.Second):
		t.Fatal("GenerateWithContext did not return after cancel")
	}
}

// TestGenerateWithContextCancelNativeSimple cancels the videox/NCC path.
func TestGenerateWithContextCancelNativeSimple(t *testing.T) {
	if _, err := exec.LookPath("ffmpeg"); err != nil {
		t.Skip("ffmpeg not available")
	}
	dir := t.TempDir()
	video := filepath.Join(dir, "clip.mp4")
	// ~8s clip so cancel has time to hit mid-track.
	args := []string{
		"-v", "error", "-y",
		"-f", "lavfi", "-i", "testsrc=size=320x240:rate=25:duration=8",
		"-pix_fmt", "yuv420p", video,
	}
	if out, err := exec.Command("ffmpeg", args...).CombinedOutput(); err != nil {
		t.Fatalf("ffmpeg: %v: %s", err, out)
	}
	out := filepath.Join(dir, "out.funscript")
	ctx, cancel := context.WithCancel(context.Background())
	done := make(chan error, 1)
	go func() {
		done <- GenerateWithContext(ctx, video, ROI{X: 120, Y: 80, W: 80, H: 80}, out, Options{
			Backend:           "csrt",
			MaxFrames:         0,
			PreferSimpletrack: !NativeTrackingAvailable(),
		}, nil, nil)
	}()
	time.Sleep(250 * time.Millisecond)
	cancel()
	select {
	case err := <-done:
		if !errors.Is(err, context.Canceled) {
			t.Fatalf("want context.Canceled, got %v", err)
		}
	case <-time.After(15 * time.Second):
		t.Fatal("native GenerateWithContext did not return after cancel")
	}
}
