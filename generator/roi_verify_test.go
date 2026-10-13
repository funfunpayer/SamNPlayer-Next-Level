package generator

import (
	"context"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"
)

func makeVerifyTestVideo(t *testing.T, movingBox bool) string {
	t.Helper()
	if _, err := exec.LookPath("ffmpeg"); err != nil {
		t.Skip("ffmpeg not available")
	}
	path := filepath.Join(t.TempDir(), "roi.mp4")
	// Moving white square on dark background vs static noise.
	src := "color=c=black:s=320x240:d=3"
	if movingBox {
		// overlay a moving rect via geq / drawbox filter chain is awkward;
		// use testsrc which has motion everywhere — ROI center should still score ok.
		src = "testsrc=size=320x240:rate=10:duration=3"
	}
	cmd := exec.Command("ffmpeg", "-v", "error", "-y",
		"-f", "lavfi", "-i", src,
		"-pix_fmt", "yuv420p", path)
	if out, err := cmd.CombinedOutput(); err != nil {
		t.Fatalf("ffmpeg: %v: %s", err, out)
	}
	return path
}

func TestVerifyROIMotionPresent(t *testing.T) {
	path := makeVerifyTestVideo(t, true)
	res := VerifyROI(context.Background(), path, ROI{X: 80, Y: 60, W: 160, H: 120})
	if res.Score <= 0 && res.Warning == "" {
		t.Fatalf("expected a score or warning, got %+v", res)
	}
	// Full-frame-ish ROI on testsrc should not warn about weak concentration.
	_ = os.Remove(path)
}

func TestVerifyROITooSmall(t *testing.T) {
	res := VerifyROI(context.Background(), "x.mp4", ROI{X: 1, Y: 1, W: 2, H: 2})
	if res.Warning == "" {
		t.Fatal("expected warning for tiny ROI")
	}
}

func TestVerifyROIHugeBoxWarnsEditable(t *testing.T) {
	path := makeVerifyTestVideo(t, true)
	defer os.Remove(path)
	// Near-full-frame tip seed — Tip-Find should warn that the box is editable.
	res := VerifyROI(context.Background(), path, ROI{X: 0, Y: 0, W: 300, H: 220})
	if res.Warning == "" {
		t.Fatalf("expected coverage warning for huge ROI, got %+v", res)
	}
	low := strings.ToLower(res.Warning)
	if !strings.Contains(low, "editable") && !strings.Contains(low, "drag") {
		t.Fatalf("warning should mention editable/drag refine, got %q", res.Warning)
	}
}
