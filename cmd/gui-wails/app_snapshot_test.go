package main

import (
	"context"
	"os"
	"path/filepath"
	"testing"
)

func TestSaveVideoSnapshot(t *testing.T) {
	t.Setenv("XDG_CONFIG_HOME", t.TempDir())
	a := NewApp()
	if _, err := a.SaveVideoSnapshot(1000); err == nil {
		t.Fatal("no video: want error")
	}
	dir := t.TempDir()
	video := filepath.Join(dir, "My Clip.mp4")
	if err := os.WriteFile(video, []byte("x"), 0o644); err != nil {
		t.Fatal(err)
	}
	a.stateMu.Lock()
	a.setVideoPathLocked(video)
	a.stateMu.Unlock()

	oldSave, oldCmd := snapshotSavePathFn, snapshotCommandFn
	t.Cleanup(func() { snapshotSavePathFn, snapshotCommandFn = oldSave, oldCmd })
	var suggested string
	snapshotSavePathFn = func(_ *App, s string) (string, error) {
		suggested = s
		return filepath.Join(dir, "frame"), nil // no extension on purpose
	}
	var gotAt int64
	snapshotCommandFn = func(_ context.Context, v string, atMs int64, out string) error {
		gotAt = atMs
		return os.WriteFile(out, []byte("png"), 0o644)
	}
	out, err := a.SaveVideoSnapshot(83_500)
	if err != nil {
		t.Fatal(err)
	}
	if suggested != "My Clip_01-23.png" || gotAt != 83_500 || filepath.Ext(out) != ".png" {
		t.Fatalf("suggested %q at %d out %q", suggested, gotAt, out)
	}
	snapshotSavePathFn = func(*App, string) (string, error) { return "", nil }
	if out, err := a.SaveVideoSnapshot(0); out != "" || err != nil {
		t.Fatalf("cancel: %q %v", out, err)
	}
}
