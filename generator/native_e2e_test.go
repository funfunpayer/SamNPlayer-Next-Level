//go:build cgo && opencv

package generator

import (
	"context"
	"math"
	"math/rand"
	"path/filepath"
	"testing"

	"github.com/funfunpayer/SamNPlayer/funscript"
	"github.com/funfunpayer/SamNPlayer/generator/trackcv"
)

// End-to-end: synthetic video → GenerateNativeCSRT → loadable funscript.
func TestGenerateNativeCSRTEndToEnd(t *testing.T) {
	if !NativeTrackingAvailable() {
		t.Skip("native tracking unavailable")
	}
	dir := t.TempDir()
	video := filepath.Join(dir, "clip.avi")
	const (
		w, h = 320, 240
		fps  = 25.0
		sec  = 3
	)
	writeNativeTestVideo(t, video, w, h, fps, sec)

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
	}
	if err := GenerateNativeCSRT(context.Background(), video, roi, out, opts, nil, nil); err != nil {
		t.Fatalf("GenerateNativeCSRT: %v", err)
	}
	script, err := funscript.Load(out)
	if err != nil {
		t.Fatalf("Load: %v", err)
	}
	if len(script.Actions) < 2 {
		t.Fatalf("expected >=2 actions, got %d", len(script.Actions))
	}
	if script.Metadata.Creator == "" || script.Metadata.Duration <= 0 {
		t.Fatalf("unexpected metadata: %+v", script.Metadata)
	}
	if script.Metadata.QualityScore == nil {
		t.Fatalf("expected native dense Quality Doctor score in metadata")
	}
}

func writeNativeTestVideo(t *testing.T, path string, width, height int, fps float64, seconds int) {
	t.Helper()
	buf := make([]byte, width*height*3)
	for i := 0; i < len(buf); i += 3 {
		buf[i], buf[i+1], buf[i+2] = 40, 40, 40
	}
	rng := rand.New(rand.NewSource(11))
	for i := 0; i < 140; i++ {
		x0 := rng.Intn(width)
		y0 := rng.Intn(height)
		bw := 6 + rng.Intn(12)
		bh := 6 + rng.Intn(12)
		b := byte(60 + rng.Intn(130))
		g := byte(60 + rng.Intn(130))
		r := byte(60 + rng.Intn(130))
		for y := y0; y < y0+bh && y < height; y++ {
			for x := x0; x < x0+bw && x < width; x++ {
				idx := (y*width + x) * 3
				buf[idx], buf[idx+1], buf[idx+2] = b, g, r
			}
		}
	}
	vw := trackcv.OpenVideoWriter(path, fps, width, height)
	defer vw.Close()
	frames := int(fps * float64(seconds))
	amp := 80.0
	for i := 0; i < frames; i++ {
		frame := append([]byte(nil), buf...)
		tt := float64(i) / fps
		cy := int(120 + (amp/2)*math.Sin(2*math.Pi*1.0*tt))
		cx, radius := 160, 35
		r2 := radius * radius
		for y := cy - radius; y <= cy+radius; y++ {
			if y < 0 || y >= height {
				continue
			}
			for x := cx - radius; x <= cx+radius; x++ {
				if x < 0 || x >= width {
					continue
				}
				dx, dy := x-cx, y-cy
				if dx*dx+dy*dy <= r2 {
					idx := (y*width + x) * 3
					frame[idx], frame[idx+1], frame[idx+2] = 250, 250, 250
				}
			}
		}
		vw.WriteBGR(frame, width, height, width*3)
	}
}
