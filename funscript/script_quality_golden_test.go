package funscript

import (
	"encoding/json"
	"math"
	"os"
	"path/filepath"
	"testing"
)

// TestEvaluateScriptQualityMatchesPythonGoldens locks actions-only Script
// Doctor scores against quality_doctor.evaluate() outputs committed in
// testdata/script_quality_goldens.json. Prevents Go/Python threshold drift
// (review PR #86 finding 3).
func TestEvaluateScriptQualityMatchesPythonGoldens(t *testing.T) {
	path := filepath.Join("testdata", "script_quality_goldens.json")
	data, err := os.ReadFile(path)
	if err != nil {
		t.Fatalf("read goldens: %v", err)
	}
	var goldens map[string]struct {
		Actions  []Action `json:"actions"`
		Score    float64  `json:"score"`
		Passed   bool     `json:"passed"`
		Warnings []string `json:"warnings"`
	}
	if err := json.Unmarshal(data, &goldens); err != nil {
		t.Fatalf("parse goldens: %v", err)
	}
	if len(goldens) == 0 {
		t.Fatal("empty goldens")
	}
	for name, g := range goldens {
		t.Run(name, func(t *testing.T) {
			got := EvaluateScriptQuality(g.Actions)
			if got.Passed != g.Passed {
				t.Errorf("passed: got %v want %v", got.Passed, g.Passed)
			}
			if math.Abs(got.Score-g.Score) > 0.001 {
				t.Errorf("score: got %.3f want %.3f", got.Score, g.Score)
			}
			if len(got.Warnings) != len(g.Warnings) {
				t.Fatalf("warnings: got %v want %v", got.Warnings, g.Warnings)
			}
			for i := range g.Warnings {
				if got.Warnings[i] != g.Warnings[i] {
					t.Errorf("warning[%d]: got %q want %q", i, got.Warnings[i], g.Warnings[i])
				}
			}
		})
	}
}
