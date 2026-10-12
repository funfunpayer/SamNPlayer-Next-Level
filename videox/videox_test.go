package videox

import (
	"context"
	"io"
	"os/exec"
	"path/filepath"
	"testing"
	"time"
)

// makeTestVideo renders a short synthetic clip with ffmpeg.
func makeTestVideo(t *testing.T, extraArgs ...string) string {
	t.Helper()
	if _, err := exec.LookPath("ffmpeg"); err != nil {
		t.Skip("ffmpeg not available")
	}
	path := filepath.Join(t.TempDir(), "test.mp4")
	args := []string{
		"-v", "error", "-y",
		"-f", "lavfi", "-i", "testsrc=size=320x240:rate=25:duration=2",
	}
	args = append(args, extraArgs...)
	args = append(args, "-pix_fmt", "yuv420p", path)
	if out, err := exec.Command("ffmpeg", args...).CombinedOutput(); err != nil {
		t.Fatalf("ffmpeg: %v: %s", err, out)
	}
	return path
}

func TestProbe(t *testing.T) {
	path := makeTestVideo(t)
	info, err := Probe(context.Background(), path)
	if err != nil {
		t.Fatal(err)
	}
	if info.Width != 320 || info.Height != 240 {
		t.Fatalf("unexpected geometry %dx%d", info.Width, info.Height)
	}
	if info.FPS < 24.9 || info.FPS > 25.1 {
		t.Fatalf("unexpected fps %.3f", info.FPS)
	}
	if info.Duration < 1900*time.Millisecond || info.Duration > 2100*time.Millisecond {
		t.Fatalf("unexpected duration %v", info.Duration)
	}
	if info.VFR {
		t.Fatal("constant frame rate clip flagged as VFR")
	}
	if info.Codec == "" || info.PixFmt == "" {
		t.Fatalf("missing codec/pixfmt: %+v", info)
	}
}

func TestProbeMissingFile(t *testing.T) {
	if _, err := Probe(context.Background(), "/nonexistent/file.mp4"); err == nil {
		t.Fatal("expected an error for a missing file")
	}
}

func TestGrayReaderScalesAndCounts(t *testing.T) {
	path := makeTestVideo(t)
	info, err := Probe(context.Background(), path)
	if err != nil {
		t.Fatal(err)
	}

	r, err := NewGrayReader(context.Background(), path, info, GrayReaderOptions{
		FPS:        10,
		MaxWidth:   160,
		AutoRotate: true,
	})
	if err != nil {
		t.Fatal(err)
	}

	if r.Width != 160 || r.Height != 120 {
		t.Fatalf("scaling failed: %dx%d", r.Width, r.Height)
	}

	n := 0
	for {
		f, err := r.Next(n)
		if err == io.EOF {
			break
		}
		if err != nil {
			t.Fatalf("frame %d: %v (%s)", n, err, r.Stderr())
		}
		if len(f.Pixels) != r.Width*r.Height {
			t.Fatalf("frame %d has %d bytes", n, len(f.Pixels))
		}
		n++
	}
	if err := r.Close(); err != nil {
		t.Fatalf("close: %v", err)
	}

	// 2 seconds at 10 fps, allowing for boundary rounding.
	if n < 18 || n > 22 {
		t.Fatalf("expected about 20 frames, got %d", n)
	}
}

// TestGrayReaderEarlyClose covers the deadlock in the original implementation,
// which called Wait without terminating ffmpeg.
func TestGrayReaderEarlyClose(t *testing.T) {
	path := makeTestVideo(t)
	info, err := Probe(context.Background(), path)
	if err != nil {
		t.Fatal(err)
	}
	r, err := NewGrayReader(context.Background(), path, info, GrayReaderOptions{MaxWidth: 320})
	if err != nil {
		t.Fatal(err)
	}
	if _, err := r.Next(0); err != nil {
		t.Fatalf("first frame: %v", err)
	}

	done := make(chan error, 1)
	go func() { done <- r.Close() }()

	select {
	case err := <-done:
		if err != nil {
			t.Fatalf("early close reported an error: %v", err)
		}
	case <-time.After(10 * time.Second):
		t.Fatal("Close deadlocked")
	}
}

func TestGrayReaderCloseIsIdempotent(t *testing.T) {
	path := makeTestVideo(t)
	info, _ := Probe(context.Background(), path)
	r, err := NewGrayReader(context.Background(), path, info, GrayReaderOptions{})
	if err != nil {
		t.Fatal(err)
	}
	_ = r.Close()
	if err := r.Close(); err != nil {
		t.Fatalf("second close: %v", err)
	}
}

func TestFitWidth(t *testing.T) {
	cases := []struct{ w, h, max, wantW, wantH int }{
		{1920, 1080, 640, 640, 360},
		{640, 480, 0, 640, 480},
		{320, 240, 640, 320, 240},
		{1080, 1920, 640, 640, 1138},
	}
	for _, c := range cases {
		gotW, gotH := fitWidth(c.w, c.h, c.max)
		if gotW != c.wantW || gotH != c.wantH {
			t.Fatalf("fitWidth(%d,%d,%d) = %d,%d want %d,%d",
				c.w, c.h, c.max, gotW, gotH, c.wantW, c.wantH)
		}
	}
}
