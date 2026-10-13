package main

import (
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func TestTrainingVideoServedSeparately(t *testing.T) {
	t.Setenv("XDG_CONFIG_HOME", t.TempDir())
	a := NewApp()
	if a.TrainingVideoURL() != "" {
		t.Fatal("no training video at start")
	}
	if _, err := a.SetTrainingVideo(filepath.Join(t.TempDir(), "missing.mp4")); err == nil {
		t.Fatal("missing file must be refused")
	}
	clip := filepath.Join(t.TempDir(), "film.mp4")
	if err := os.WriteFile(clip, []byte("FILMDATA"), 0o644); err != nil {
		t.Fatal(err)
	}
	url, err := a.SetTrainingVideo(clip)
	if err != nil {
		t.Fatal(err)
	}
	if !strings.Contains(url, "/training-video?v=") {
		t.Fatalf("unexpected URL %q", url)
	}
	if a.VideoFileURL() != "" {
		t.Fatal("Training film must not become the Player video")
	}

	rec := httptest.NewRecorder()
	a.serveTrainingVideo(rec, httptest.NewRequest(http.MethodGet, "/training-video?v=1", nil))
	if rec.Code != http.StatusOK || rec.Body.String() != "FILMDATA" {
		t.Fatalf("serve: %d %q", rec.Code, rec.Body.String())
	}
	if ct := rec.Header().Get("Content-Type"); ct != "video/mp4" {
		t.Fatalf("content type %q", ct)
	}
	// Range requests (seeking) work like for the Player video.
	rec = httptest.NewRecorder()
	req := httptest.NewRequest(http.MethodGet, "/training-video?v=1", nil)
	req.Header.Set("Range", "bytes=4-7")
	a.serveTrainingVideo(rec, req)
	if rec.Code != http.StatusPartialContent || rec.Body.String() != "DATA" {
		t.Fatalf("range: %d %q", rec.Code, rec.Body.String())
	}

	a.ClearTrainingVideo()
	if a.TrainingVideoURL() != "" {
		t.Fatal("clear must drop the URL")
	}
	rec = httptest.NewRecorder()
	a.serveTrainingVideo(rec, httptest.NewRequest(http.MethodGet, "/training-video", nil))
	if rec.Code != http.StatusNotFound {
		t.Fatalf("after clear: %d", rec.Code)
	}
}
