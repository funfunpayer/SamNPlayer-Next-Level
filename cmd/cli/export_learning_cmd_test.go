package main

import (
	"os"
	"path/filepath"
	"testing"
)

func TestExportLearningDeleteRejectsPathWithoutDeleting(t *testing.T) {
	root := t.TempDir()
	learningDir := filepath.Join(root, "scene_map_learning")
	if err := os.MkdirAll(learningDir, 0o755); err != nil {
		t.Fatal(err)
	}
	marker := filepath.Join(learningDir, "keep.txt")
	if err := os.WriteFile(marker, []byte("keep"), 0o644); err != nil {
		t.Fatal(err)
	}

	if got := runExportLearning([]string{"project.samn", "--delete", "--output-dir", root}); got != 2 {
		t.Fatalf("runExportLearning returned %d, want usage error 2", got)
	}
	if _, err := os.Stat(marker); err != nil {
		t.Fatalf("delete mode mutated data after invalid arguments: %v", err)
	}
}
