package generator

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"testing"
)

func TestSuggestExcludePriorsInsufficientClips(t *testing.T) {
	dir := t.TempDir()
	writePriorClip(t, dir, "clip_a", 1280, 720, [][]int{{10, 500, 200, 120}})
	writePriorClip(t, dir, "clip_b", 1280, 720, [][]int{{20, 480, 180, 140}})
	res, err := SuggestExcludePriors(1920, 1080, dir)
	if err != nil {
		t.Fatal(err)
	}
	if len(res.Suggestions) != 0 {
		t.Fatalf("want 0 suggestions with 2 clips, got %d", len(res.Suggestions))
	}
	if res.ClipsSeen != 2 {
		t.Fatalf("clipsSeen %d", res.ClipsSeen)
	}
	if res.Note == "" {
		t.Fatal("expected note explaining insufficient data")
	}
}

func TestSuggestExcludePriorsHotLowerLeft(t *testing.T) {
	dir := t.TempDir()
	// Lower-left-ish boxes across 3 clips → cell (0,2) on 4×3 grid.
	for _, name := range []string{"clip_a", "clip_b", "clip_c"} {
		writePriorClip(t, dir, name, 1280, 720, [][]int{{40, 520, 220, 140}})
	}
	res, err := SuggestExcludePriors(1280, 720, dir)
	if err != nil {
		t.Fatal(err)
	}
	if res.ClipsSeen != 3 {
		t.Fatalf("clipsSeen %d", res.ClipsSeen)
	}
	if len(res.Suggestions) == 0 {
		t.Fatalf("expected ≥1 suggestion, note=%q", res.Note)
	}
	s := res.Suggestions[0]
	if s.Mark.Kind != "exclude" {
		t.Fatalf("kind %q", s.Mark.Kind)
	}
	if s.Mark.Author != "suggest" {
		t.Fatalf("author %q", s.Mark.Author)
	}
	if !s.Mark.Follow {
		t.Fatal("suggest Ignore should Follow by default")
	}
	if s.ClipCount < L1PriorMinClips {
		t.Fatalf("clipCount %d", s.ClipCount)
	}
	if s.Share < L1PriorMinShare {
		t.Fatalf("share %v", s.Share)
	}
	// Suggested box should sit in lower third of frame.
	midY := s.Mark.Rect.Y + s.Mark.Rect.H/2
	if midY < 720/3 {
		t.Fatalf("expected lower-third suggestion, midY=%d rect=%+v", midY, s.Mark.Rect)
	}
}

func TestSuggestExcludePriorsEmptyDir(t *testing.T) {
	res, err := SuggestExcludePriors(640, 360, t.TempDir())
	if err != nil {
		t.Fatal(err)
	}
	if len(res.Suggestions) != 0 {
		t.Fatalf("got %d", len(res.Suggestions))
	}
}

func TestSuggestExcludePriorsNeedsSize(t *testing.T) {
	if _, err := SuggestExcludePriors(0, 720, t.TempDir()); err == nil {
		t.Fatal("want error for zero size")
	}
}

func TestRectToPriorCellsLowerLeft(t *testing.T) {
	cells := rectToPriorCells([]int{10, 500, 100, 80}, 1280, 720)
	if len(cells) == 0 {
		t.Fatal("no cells")
	}
	// row 2 (lower), col 0 (left) on 4×3 → index 8
	found := false
	for _, c := range cells {
		if c == 2*l1PriorGridCols+0 {
			found = true
		}
	}
	if !found {
		t.Fatalf("want cell 8 among %v", cells)
	}
}

func writePriorClip(t *testing.T, root, name string, w, h int, rects [][]int) {
	t.Helper()
	clip := filepath.Join(root, name)
	if err := os.MkdirAll(clip, 0o755); err != nil {
		t.Fatal(err)
	}
	src, _ := json.Marshal(map[string]any{
		"video": map[string]any{"width": w, "height": h},
	})
	if err := os.WriteFile(filepath.Join(clip, "source.json"), src, 0o644); err != nil {
		t.Fatal(err)
	}
	f, err := os.Create(filepath.Join(clip, "exclude_decisions.jsonl"))
	if err != nil {
		t.Fatal(err)
	}
	enc := json.NewEncoder(f)
	for i, r := range rects {
		if err := enc.Encode(map[string]any{
			"version": 1,
			"role":    "exclude_decision",
			"id":      fmt.Sprintf("m%d", i+1),
			"kind":    "exclude",
			"rect":    r,
			"author":  "user",
			"follow":  true,
		}); err != nil {
			t.Fatal(err)
		}
	}
	if err := f.Close(); err != nil {
		t.Fatal(err)
	}
}
