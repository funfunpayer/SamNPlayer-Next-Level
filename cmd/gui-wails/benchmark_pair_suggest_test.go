package main

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func touchFunscript(t *testing.T, path string) {
	t.Helper()
	if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(path, []byte(`{"actions":[{"at":0,"pos":50},{"at":1000,"pos":60}]}`), 0o644); err != nil {
		t.Fatal(err)
	}
}

func TestSuggestBenchmarkPairBesideVideoHubPair(t *testing.T) {
	dir := t.TempDir()
	video := filepath.Join(dir, "hub_easy.mp4")
	if err := os.WriteFile(video, []byte("fake"), 0o644); err != nil {
		t.Fatal(err)
	}
	ref := filepath.Join(dir, "hub_easy.funscript")
	cand := filepath.Join(dir, "hub_easy__hub.funscript")
	touchFunscript(t, ref)
	touchFunscript(t, cand)

	got, err := suggestBenchmarkPairBesideVideo(video)
	if err != nil {
		t.Fatal(err)
	}
	if got.Reference != ref {
		t.Fatalf("reference: want %s got %s", ref, got.Reference)
	}
	if got.Candidate != cand {
		t.Fatalf("candidate: want %s got %s", cand, got.Candidate)
	}
	if !strings.Contains(got.Note, "Paired") {
		t.Fatalf("note: %q", got.Note)
	}
}

func TestSuggestBenchmarkPairBesideVideoGoldenSubdirs(t *testing.T) {
	dir := t.TempDir()
	video := filepath.Join(dir, "clip_ausschnitt.mp4")
	if err := os.WriteFile(video, []byte("fake"), 0o644); err != nil {
		t.Fatal(err)
	}
	ref := filepath.Join(dir, "mit_yolo", "clip_ausschnitt.funscript")
	cand := filepath.Join(dir, "mit_yolo", "clip_ausschnitt__hub.funscript")
	touchFunscript(t, ref)
	touchFunscript(t, cand)
	// second ref — should still pick a hub pair
	touchFunscript(t, filepath.Join(dir, "ohne_yolo", "clip_ausschnitt_ohne_yolo.funscript"))
	touchFunscript(t, filepath.Join(dir, "ohne_yolo", "clip_ausschnitt_ohne_yolo__hub.funscript"))

	got, err := suggestBenchmarkPairBesideVideo(video)
	if err != nil {
		t.Fatal(err)
	}
	if got.Reference == "" || got.Candidate == "" {
		t.Fatalf("expected both paths, got %+v", got)
	}
	if !isEverydayAxisFunscript(filepath.Base(got.Candidate)) {
		t.Fatalf("candidate should be axis/hub: %s", got.Candidate)
	}
	if isEverydayAxisFunscript(filepath.Base(got.Reference)) {
		t.Fatalf("reference should not be axis: %s", got.Reference)
	}
	if len(got.References) < 2 {
		t.Fatalf("expected multiple refs, got %v", got.References)
	}
}

func TestSuggestBenchmarkPairBesideVideoOnlyStemIsCandidate(t *testing.T) {
	dir := t.TempDir()
	video := filepath.Join(dir, "hub_easy.mp4")
	if err := os.WriteFile(video, []byte("fake"), 0o644); err != nil {
		t.Fatal(err)
	}
	only := filepath.Join(dir, "hub_easy.funscript")
	touchFunscript(t, only)

	got, err := suggestBenchmarkPairBesideVideo(video)
	if err != nil {
		t.Fatal(err)
	}
	if got.Candidate != only {
		t.Fatalf("candidate: want %s got %s", only, got.Candidate)
	}
	if got.Reference != "" {
		t.Fatalf("reference should stay empty, got %s", got.Reference)
	}
	if !strings.Contains(got.Note, "candidate") {
		t.Fatalf("note: %q", got.Note)
	}
}

func TestSuggestBenchmarkPairBesideVideoNone(t *testing.T) {
	dir := t.TempDir()
	video := filepath.Join(dir, "hub_easy.mp4")
	if err := os.WriteFile(video, []byte("fake"), 0o644); err != nil {
		t.Fatal(err)
	}
	got, err := suggestBenchmarkPairBesideVideo(video)
	if err != nil {
		t.Fatal(err)
	}
	if got.Reference != "" || got.Candidate != "" {
		t.Fatalf("expected empty pair, got %+v", got)
	}
	if !strings.Contains(got.Note, "No related") {
		t.Fatalf("note: %q", got.Note)
	}
}

func TestSuggestBenchmarkPairBesideVideoRequiresPath(t *testing.T) {
	if _, err := suggestBenchmarkPairBesideVideo(""); err == nil {
		t.Fatal("expected error for empty path")
	}
}
