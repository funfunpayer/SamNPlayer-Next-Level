package videox

import (
	"context"
	"os"
	"os/exec"
	"path/filepath"
	"testing"
)

func TestResolveToolPrefersEnv(t *testing.T) {
	ResetToolCache()
	dir := t.TempDir()
	fake := filepath.Join(dir, "ffmpeg")
	if err := os.WriteFile(fake, []byte("#!/bin/sh\n"), 0o755); err != nil {
		t.Fatal(err)
	}
	t.Setenv("SAMNPLAYER_FFMPEG", fake)
	p, err := FFmpeg()
	if err != nil {
		t.Fatal(err)
	}
	if p != fake {
		t.Fatalf("got %q want %q", p, fake)
	}
}

func TestResolveToolSiblingDir(t *testing.T) {
	// Soft check: Available() should succeed in CI where apt ffmpeg exists.
	ResetToolCache()
	t.Setenv("SAMNPLAYER_FFMPEG", "")
	if !Available() {
		if _, err := exec.LookPath("ffmpeg"); err != nil {
			t.Skip("ffmpeg not on PATH")
		}
		t.Fatal("Available should be true when PATH has ffmpeg")
	}
}

func TestCommandFFmpeg(t *testing.T) {
	ResetToolCache()
	if !Available() {
		t.Skip("ffmpeg not available")
	}
	cmd, err := CommandContext(context.Background(), "-version")
	if err != nil {
		t.Fatal(err)
	}
	out, err := cmd.CombinedOutput()
	if err != nil {
		t.Fatalf("%v: %s", err, out)
	}
	if len(out) < 10 {
		t.Fatalf("unexpected -version output: %q", out)
	}
}
