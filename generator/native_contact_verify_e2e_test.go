//go:build cgo && opencv

package generator

import (
	"context"
	"encoding/json"
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/funfunpayer/SamNPlayer/funscript"
)

// Full native chain ChatGPT's contact-verify lane left open: synthetic clip →
// contact JSON → GenerateNativeCSRT with --rhythm-grid + --contact-points +
// --contact-verify. Proves CSRT init, LoadContactPoints, and hybrid filtering
// wire through to a loadable funscript (TrackROI filtering asserted in
// trackcv.TestTrackROIContactVerifyFiltersQuietTeacherPoints).
func TestGenerateNativeCSRTContactVerifyEndToEnd(t *testing.T) {
	if !NativeTrackingAvailable() {
		t.Skip("native tracking unavailable")
	}
	dir := t.TempDir()
	video := filepath.Join(dir, "clip.avi")
	const (
		w, h = 320, 240
		fps  = 25.0
		sec  = 8
	)
	writeNativeTestVideo(t, video, w, h, fps, sec)

	contacts := filepath.Join(dir, "contact.json")
	writeContactPointsJSON(t, contacts, sec)

	out := filepath.Join(dir, "out.funscript")
	roi := ROI{X: 160 - 40, Y: 120 - 40, W: 80, H: 80}
	opts := Options{
		Backend:                   "csrt",
		NativePipeline:            true,
		DisableCameraCompensation: false,
		DisableSceneCutDetection:  false,
		SmoothWindow:              11,
		MinPeakDistanceMs:         150,
		MinActionIntervalMs:       100,
		NormPercentile:            2,
		RhythmGrid:                true,
		ContactPointsFile:         contacts,
		ContactVerifyK:            1.5,
	}
	var progress []string
	if err := GenerateNativeCSRT(context.Background(), video, roi, out, opts,
		func(line string) { progress = append(progress, line) }, nil); err != nil {
		t.Fatalf("GenerateNativeCSRT: %v", err)
	}
	script, err := funscript.Load(out)
	if err != nil {
		t.Fatalf("Load: %v", err)
	}
	if len(script.Actions) < 2 {
		t.Fatalf("expected >=2 actions, got %d", len(script.Actions))
	}
	joined := strings.Join(progress, "\n")
	if !strings.Contains(joined, "contact points verified by the engine") {
		t.Fatalf("expected verify progress line, got:\n%s", joined)
	}
	if !strings.Contains(joined, "contact points confirmed by the engine") {
		t.Fatalf("expected confirmed-count progress line, got:\n%s", joined)
	}
}

func writeContactPointsJSON(t *testing.T, path string, seconds int) {
	t.Helper()
	type pt struct {
		TMs    int64   `json:"t_ms"`
		X      float64 `json:"x"`
		Y      float64 `json:"y"`
		Agree  int     `json:"agree"`
		Source string  `json:"source"`
	}
	var points []pt
	for ms := int64(0); ms < int64(seconds)*1000; ms += 500 {
		points = append(points,
			pt{TMs: ms, X: 0.5, Y: 0.5, Agree: 2, Source: "probe"},
			pt{TMs: ms, X: 0.02, Y: 0.02, Agree: 1, Source: "probe"},
		)
	}
	body, err := json.Marshal(map[string]any{"version": 1, "points": points})
	if err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(path, body, 0o644); err != nil {
		t.Fatal(err)
	}
}
