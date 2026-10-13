package generator

import (
	"context"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"

	"github.com/funfunpayer/SamNPlayer/generator/strokepreview"
)

func TestApplyStrokePreviewSynthetic(t *testing.T) {
	if _, err := exec.LookPath("ffmpeg"); err != nil {
		t.Skip("ffmpeg not available")
	}
	dir := t.TempDir()
	video := filepath.Join(dir, "bounce.mp4")
	args := []string{
		"-v", "error", "-y",
		"-f", "lavfi", "-i", "color=c=black:s=320x240:d=3:r=24",
		"-f", "lavfi", "-i", "color=c=white:s=40x40:d=3:r=24",
		"-filter_complex", "[0][1]overlay=x=140:y='100+80*sin(2*PI*t)'",
		"-pix_fmt", "yuv420p", video,
	}
	if out, err := exec.Command("ffmpeg", args...).CombinedOutput(); err != nil {
		t.Fatalf("ffmpeg: %v: %s", err, out)
	}
	var lines []string
	opts := applyStrokePreview(context.Background(), video, Options{}, func(s string) {
		lines = append(lines, s)
	})
	if opts.StrokePreviewHint == nil {
		t.Fatalf("expected hint, lines=%v", lines)
	}
	joined := strings.Join(lines, "\n")
	if !strings.Contains(joined, "STROKE_PREVIEW") {
		t.Fatalf("no progress lines: %s", joined)
	}
	if opts.MinPeakDistanceMs == 0 && opts.StrokePreviewHint["quality"] == "ok" {
		// Peak bias is advisory-only now — still expect a tip in the hint map.
		if _, ok := opts.StrokePreviewHint["suggested_min_peak_distance_ms"]; !ok {
			t.Fatal("ok preview should suggest peak distance in hint metadata")
		}
	}
}

func TestApplyStrokePreviewSkip(t *testing.T) {
	opts := applyStrokePreview(context.Background(), "/nope.mp4", Options{SkipStrokePreview: true}, nil)
	if opts.StrokePreviewHint != nil {
		t.Fatal("skip must not run")
	}
}

func TestApplyStrokePreviewSteersCutRateEnablesPerSceneROI(t *testing.T) {
	h := strokepreview.Hint{CutRatePerMin: 5.0, PanShare: 0.1}
	var lines []string
	opts := applyStrokePreviewSteers(Options{
		StrokePreviewHint: map[string]any{"stage": "A"},
	}, h, func(s string) { lines = append(lines, s) })
	joined := strings.Join(lines, "\n")
	if NativeTrackingAvailable() {
		// Go CSRT builds must not steer into Python (#338).
		if opts.PerSceneROI {
			t.Fatal("Go CSRT build must not enable PerSceneROI from stroke preview")
		}
		if !strings.Contains(joined, "staying on Go CSRT") {
			t.Fatalf("missing Go CSRT stay progress: %s", joined)
		}
		return
	}
	if !opts.PerSceneROI {
		t.Fatal("expected PerSceneROI enabled for high cut rate (no Go CSRT)")
	}
	if !strings.Contains(joined, "enabling “Re-find region after each cut”") {
		t.Fatalf("missing enable progress: %s", joined)
	}
	if opts.StrokePreviewHint["stage"] != "B" {
		t.Fatalf("stage=%v want B", opts.StrokePreviewHint["stage"])
	}
	if opts.StrokePreviewHint["steered_per_scene_roi"] != true {
		t.Fatal("expected steered_per_scene_roi metadata")
	}
}

func TestApplyStrokePreviewSteersPanEnablesCamera(t *testing.T) {
	h := strokepreview.Hint{CutRatePerMin: 0, PanShare: 0.55}
	var lines []string
	opts := applyStrokePreviewSteers(Options{
		DisableCameraCompensation: true,
		StrokePreviewHint:         map[string]any{"stage": "A"},
	}, h, func(s string) { lines = append(lines, s) })
	if opts.DisableCameraCompensation {
		t.Fatal("expected camera compensation enabled for high pan share")
	}
	joined := strings.Join(lines, "\n")
	if !strings.Contains(joined, "enabling camera motion compensation") {
		t.Fatalf("missing enable progress: %s", joined)
	}
	if opts.StrokePreviewHint["steered_camera_compensation"] != true {
		t.Fatal("expected steered_camera_compensation metadata")
	}
}

func TestApplyStrokePreviewSteersNoopWhenAlreadyOn(t *testing.T) {
	h := strokepreview.Hint{CutRatePerMin: 6, PanShare: 0.5}
	opts := applyStrokePreviewSteers(Options{
		PerSceneROI:               true,
		DisableCameraCompensation: false,
		StrokePreviewHint:         map[string]any{"stage": "A"},
	}, h, nil)
	if opts.StrokePreviewHint["stage"] != "A" {
		t.Fatalf("no new steers → stage should stay A, got %v", opts.StrokePreviewHint["stage"])
	}
}

func TestSoftenPythonOnlyOptsClearsPerSceneWhenGoCSRT(t *testing.T) {
	roi := ROI{X: 10, Y: 10, W: 40, H: 40}
	var lines []string
	out := softenPythonOnlyOptsForGoCSRT(Options{Backend: "csrt", PerSceneROI: true}, roi, func(s string) {
		lines = append(lines, s)
	})
	if NativeTrackingAvailable() {
		if out.PerSceneROI {
			t.Fatal("Go CSRT builds must soft-clear PerSceneROI (#338)")
		}
		if !strings.Contains(strings.Join(lines, "\n"), "keeping Go CSRT") {
			t.Fatalf("expected keep-Go progress, got %v", lines)
		}
		return
	}
	if !out.PerSceneROI {
		t.Fatal("without linked Go CSRT, PerSceneROI must stay for Python path")
	}
}
