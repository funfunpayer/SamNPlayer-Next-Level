package main

import (
	"context"
	"errors"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"github.com/funfunpayer/SamNPlayer/videox"
)

func TestProbeCreateVideoHeader(t *testing.T) {
	clip := filepath.Join(t.TempDir(), "My Clip.mp4")
	if err := os.WriteFile(clip, []byte("x"), 0o644); err != nil {
		t.Fatal(err)
	}
	prevProbe, prevPoster := createProbeFn, createPosterFn
	t.Cleanup(func() { createProbeFn, createPosterFn = prevProbe, prevPoster })
	createProbeFn = func(context.Context, string) (videox.Info, error) {
		return videox.Info{Width: 1080, Height: 1920, Rotation: 90, FPS: 29.97, Duration: 1936 * time.Second, Codec: "h264"}, nil
	}
	var posterAt int64
	createPosterFn = func(_ context.Context, _ string, atMs int64, out string) error {
		posterAt = atMs
		return os.WriteFile(out, []byte{0xff, 0xd8, 0xff}, 0o644)
	}
	a := NewApp()
	info, err := a.ProbeCreateVideo(clip)
	if err != nil {
		t.Fatal(err)
	}
	if info.Name != "My Clip.mp4" || info.Width != 1920 || info.Height != 1080 || info.FPS != 29.97 || info.DurationSec != 1936 {
		t.Fatalf("header facts wrong (rotation applied?): %+v", info)
	}
	if posterAt != 30000 {
		t.Fatalf("poster at %d ms, want 10%% capped at 30 s", posterAt)
	}
	if !strings.HasPrefix(info.PosterURL, "data:image/jpeg;base64,") {
		t.Fatalf("poster missing: %q", info.PosterURL)
	}

	// A failed poster keeps the facts; a failed probe is an error.
	createPosterFn = func(context.Context, string, int64, string) error { return errors.New("no frame") }
	info, err = a.ProbeCreateVideo(clip)
	if err != nil || info.PosterURL != "" || info.Width != 1920 {
		t.Fatalf("poster failure must not hide facts: %+v %v", info, err)
	}
	createProbeFn = func(context.Context, string) (videox.Info, error) { return videox.Info{}, errors.New("not a video") }
	if _, err := a.ProbeCreateVideo(clip); err == nil {
		t.Fatal("probe failure must be reported")
	}
	if _, err := a.ProbeCreateVideo(filepath.Join(t.TempDir(), "missing.mp4")); err == nil {
		t.Fatal("missing file must be refused")
	}
}
