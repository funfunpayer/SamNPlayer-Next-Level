package main

import (
	"net/http"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

// Reproduces: after the first SetPlaybackVideo, swapping to another file left
// <video src> on the same http://127.0.0.1:port/video string, so the WebView
// kept showing the first film. VideoFileURL must change when the path changes.
func TestVideoFileURLChangesWhenPlaybackVideoSwapped(t *testing.T) {
	dir := t.TempDir()
	aPath := filepath.Join(dir, "a.mp4")
	bPath := filepath.Join(dir, "b.mp4")
	for _, p := range []string{aPath, bPath} {
		if err := os.WriteFile(p, []byte("fake"), 0o644); err != nil {
			t.Fatal(err)
		}
	}

	a := NewApp()
	urlA, err := a.SetPlaybackVideo(aPath)
	if err != nil {
		t.Fatalf("set A: %v", err)
	}
	if !strings.Contains(urlA, "/video?v=") {
		t.Fatalf("expected cache-bust query in %q", urlA)
	}

	urlB, err := a.SetPlaybackVideo(bPath)
	if err != nil {
		t.Fatalf("set B: %v", err)
	}
	if urlA == urlB {
		t.Fatalf("VideoFileURL must change on swap; both %q", urlA)
	}
	if a.VideoFileURL() != urlB {
		t.Fatalf("VideoFileURL()=%q want %q", a.VideoFileURL(), urlB)
	}

	// Serving must still work with the query string (ServeMux ignores it).
	resp, err := http.Get(urlB)
	if err != nil {
		t.Fatalf("GET swapped url: %v", err)
	}
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		t.Fatalf("GET status %d", resp.StatusCode)
	}

	a.ClearPlaybackVideo()
	if got := a.VideoFileURL(); got != "" {
		t.Fatalf("cleared VideoFileURL = %q, want empty", got)
	}
}

func TestSetPlaybackVideoSamePathKeepsURL(t *testing.T) {
	dir := t.TempDir()
	path := filepath.Join(dir, "same.mp4")
	if err := os.WriteFile(path, []byte("x"), 0o644); err != nil {
		t.Fatal(err)
	}
	a := NewApp()
	u1, err := a.SetPlaybackVideo(path)
	if err != nil {
		t.Fatal(err)
	}
	u2, err := a.SetPlaybackVideo(path)
	if err != nil {
		t.Fatal(err)
	}
	if u1 != u2 {
		t.Fatalf("same path should keep URL: %q vs %q", u1, u2)
	}
}
