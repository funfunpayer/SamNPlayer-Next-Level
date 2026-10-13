package funscript

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func TestCompareDatasetPairsAndReport(t *testing.T) {
	dir := t.TempDir()
	// Shared stroke shape: reference + hub variant (slight lag via shifted times).
	ref := `{"actions":[{"at":0,"pos":20},{"at":500,"pos":80},{"at":1000,"pos":20},{"at":1500,"pos":80},{"at":2000,"pos":20}]}`
	hub := `{"actions":[{"at":100,"pos":20},{"at":600,"pos":80},{"at":1100,"pos":20},{"at":1600,"pos":80},{"at":2100,"pos":20}]}`
	if err := os.WriteFile(filepath.Join(dir, "clip_a.funscript"), []byte(ref), 0644); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(dir, "clip_a__hub.funscript"), []byte(hub), 0644); err != nil {
		t.Fatal(err)
	}
	result, err := CompareDataset(dir, 1000)
	if err != nil {
		t.Fatal(err)
	}
	if len(result.Rows) != 1 {
		t.Fatalf("rows=%d excluded=%v skipped=%v", len(result.Rows), result.Excluded, result.Skipped)
	}
	if result.Rows[0].Kind != "hub" {
		t.Fatalf("kind=%s", result.Rows[0].Kind)
	}
	if result.Rows[0].R < 0.9 {
		t.Fatalf("expected high correlation, got r=%.3f lag=%d", result.Rows[0].R, result.Rows[0].LagMs)
	}
	report := FormatCompareReport(result)
	if !strings.Contains(report, "mean r (hub") {
		t.Fatalf("report missing summary:\n%s", report)
	}
}

func TestCompareDatasetSkipsFungenBinary(t *testing.T) {
	dir := t.TempDir()
	if err := os.WriteFile(filepath.Join(dir, "proj.fungen"), []byte("FGPROJ"), 0644); err != nil {
		t.Fatal(err)
	}
	result, err := CompareDataset(dir, 500)
	if err != nil {
		t.Fatal(err)
	}
	if len(result.Skipped) == 0 {
		t.Fatal("expected .fungen skip")
	}
}
