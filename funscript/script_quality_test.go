package funscript

import (
	"os"
	"path/filepath"
	"testing"
)

func TestEvaluateDeviceCompatTooClose(t *testing.T) {
	actions := []Action{
		{At: 0, Pos: 0},
		{At: 50, Pos: 50},
		{At: 100, Pos: 0},
		{At: 150, Pos: 50},
		{At: 200, Pos: 0},
		{At: 1000, Pos: 50},
	}
	_, warnings := EvaluateDeviceCompat(actions)
	if len(warnings) == 0 {
		t.Fatal("expected too-close warning")
	}
}

func TestEvaluateDeviceCompatClean(t *testing.T) {
	actions := []Action{
		{At: 0, Pos: 10},
		{At: 400, Pos: 90},
		{At: 800, Pos: 10},
		{At: 1200, Pos: 90},
	}
	m, warnings := EvaluateDeviceCompat(actions)
	if len(warnings) != 0 {
		t.Fatalf("unexpected warnings: %v", warnings)
	}
	if m.PositionSpan < 70 {
		t.Fatalf("span %.1f", m.PositionSpan)
	}
}

func TestEvaluateScriptQualityRejectsEmpty(t *testing.T) {
	r := EvaluateScriptQuality([]Action{{At: 0, Pos: 50}})
	if r.Passed || r.Score != 0 {
		t.Fatalf("got %+v", r)
	}
	if !r.EstimatedFromScriptOnly {
		t.Fatal("must mark estimatedFromScriptOnly")
	}
}

func TestEvaluateScriptQualityFlagsOutOfRange(t *testing.T) {
	r := EvaluateScriptQuality([]Action{
		{At: 0, Pos: -10},
		{At: 500, Pos: 150},
		{At: 1000, Pos: 50},
	})
	found := false
	for _, w := range r.Warnings {
		if contains(w, "außerhalb 0-100") {
			found = true
		}
	}
	if !found {
		t.Fatalf("expected out-of-range warning, got %v", r.Warnings)
	}
}

func TestEvaluateScriptQualityKind(t *testing.T) {
	r := EvaluateScriptQuality([]Action{{At: 0, Pos: 20}, {At: 400, Pos: 80}})
	if r.Kind != "signal_quality" {
		t.Fatalf("kind=%q", r.Kind)
	}
}

func TestEvaluateMotionFidelityKind(t *testing.T) {
	ref := sineActions(8000, 2000, 40, 0, 40, 50)
	cand := sineActions(8000, 2000, 40, 200, 40, 50)
	mf := EvaluateMotionFidelity(ref, cand, 1000, 50, 100)
	if mf.Kind != "motion_fidelity" {
		t.Fatalf("kind=%q", mf.Kind)
	}
	if mf.R == nil || *mf.R < 0.9 {
		t.Fatalf("expected high motion fidelity, got %+v", mf)
	}
	if mf.Diagnosis.Verdict == "" {
		t.Fatal("missing verdict")
	}
}

func TestEvaluateScriptQualityFlagsUnsorted(t *testing.T) {
	// Deliberately out of time order — classic splice error. Must be
	// flagged on the original order, not after sorting away the defect.
	actions := []Action{
		{At: 0, Pos: 20},
		{At: 800, Pos: 90},
		{At: 400, Pos: 50}, // goes backwards
		{At: 1200, Pos: 20},
	}
	r := EvaluateScriptQuality(actions)
	found := false
	for _, w := range r.Warnings {
		if contains(w, "nicht aufsteigend sortiert") {
			found = true
		}
	}
	if !found {
		t.Fatalf("expected unsorted warning, got score=%.3f warnings=%v", r.Score, r.Warnings)
	}
	if r.Score >= 1.0 {
		t.Fatalf("unsorted input must not score a perfect 1.0, got %.3f", r.Score)
	}
}

func TestEvaluateScriptQualityAcceptsCleanStroke(t *testing.T) {
	actions := make([]Action, 0, 20)
	for i := 0; i < 20; i++ {
		pos := 20
		if i%2 == 1 {
			pos = 90
		}
		actions = append(actions, Action{At: int64(i * 400), Pos: pos})
	}
	r := EvaluateScriptQuality(actions)
	if !r.Passed {
		t.Fatalf("clean stroke should pass, got score=%.2f warnings=%v", r.Score, r.Warnings)
	}
}

func TestScriptQualityViaTempFile(t *testing.T) {
	dir := t.TempDir()
	path := filepath.Join(dir, "t.funscript")
	content := `{"actions":[{"at":0,"pos":20},{"at":400,"pos":90},{"at":800,"pos":20},{"at":1200,"pos":90}]}`
	if err := os.WriteFile(path, []byte(content), 0644); err != nil {
		t.Fatal(err)
	}
	script, err := Load(path)
	if err != nil {
		t.Fatal(err)
	}
	r := EvaluateScriptQuality(script.Actions)
	if !r.EstimatedFromScriptOnly {
		t.Fatal("flag missing")
	}
	if r.Score <= 0 {
		t.Fatalf("score %.2f", r.Score)
	}
}

func contains(s, sub string) bool {
	return len(s) >= len(sub) && (s == sub || len(sub) == 0 ||
		(func() bool {
			for i := 0; i+len(sub) <= len(s); i++ {
				if s[i:i+len(sub)] == sub {
					return true
				}
			}
			return false
		})())
}
