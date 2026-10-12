//go:build cgo && opencv

package main

import (
	"context"
	"math"
	"math/rand"
	"path/filepath"
	"testing"
	"time"

	"github.com/funfunpayer/SamNPlayer/generator"
	"github.com/funfunpayer/SamNPlayer/generator/trackcv"
)

// FREE-04 with the real generator (OpenCV CI job): a synthetic clip with a
// bouncing bright disc → Everyday CSRT generate → Load in the app → Save as
// Emotion Script → reload → Play on the mock device to the end. Synthetic
// footage only (no real video in the repository).
func TestFreeChainGenerateSavePlay(t *testing.T) {
	if !generator.NativeTrackingAvailable() {
		t.Skip("native tracking unavailable")
	}
	t.Setenv("XDG_CONFIG_HOME", t.TempDir())
	dir := t.TempDir()
	video := filepath.Join(dir, "clip.avi")
	writeBouncingDiscVideo(t, video, 320, 240, 25, 4)

	out := filepath.Join(dir, "clip.funscript")
	opts := generator.Options{
		Backend:             "csrt",
		NativePipeline:      true,
		SmoothWindow:        11,
		MinPeakDistanceMs:   150,
		MinActionIntervalMs: 100,
		NormPercentile:      2,
	}
	roi := generator.ROI{X: 120, Y: 80, W: 80, H: 80}
	if err := generator.GenerateNativeCSRT(context.Background(), video, roi, out, opts, nil, nil); err != nil {
		t.Fatalf("generate: %v", err)
	}

	a := NewApp()
	info, err := a.LoadFunscript(out)
	if err != nil {
		t.Fatalf("load generated script: %v", err)
	}
	if info.ActionCount < 4 {
		t.Fatalf("generated script too short: %+v", info)
	}
	samnPath, err := a.SaveLoadedAsSamn()
	if err != nil {
		t.Fatalf("save as Emotion Script: %v", err)
	}
	b := NewApp()
	if _, err := b.LoadFunscript(samnPath); err != nil {
		t.Fatalf("reload Emotion Script: %v", err)
	}
	rec := recordEvents(t)
	frames := playToEnd(t, b, rec, 20*time.Second)
	active := 0
	for _, f := range frames {
		if v, _ := f["vibration"].(float64); v > 0.05 {
			active++
		}
	}
	if len(frames) < 20 || active == 0 {
		t.Fatalf("generated script did not drive the device: %d frames, %d active", len(frames), active)
	}
}

// writeBouncingDiscVideo: textured background plus a bright disc moving
// up and down at 1 Hz, the same pattern as the generator's own e2e test.
func writeBouncingDiscVideo(t *testing.T, path string, width, height int, fps float64, seconds int) {
	t.Helper()
	bg := make([]byte, width*height*3)
	for i := 0; i < len(bg); i += 3 {
		bg[i], bg[i+1], bg[i+2] = 40, 40, 40
	}
	rng := rand.New(rand.NewSource(11))
	for i := 0; i < 140; i++ {
		x0, y0 := rng.Intn(width), rng.Intn(height)
		bw, bh := 6+rng.Intn(12), 6+rng.Intn(12)
		c := [3]byte{byte(60 + rng.Intn(130)), byte(60 + rng.Intn(130)), byte(60 + rng.Intn(130))}
		for y := y0; y < y0+bh && y < height; y++ {
			for x := x0; x < x0+bw && x < width; x++ {
				idx := (y*width + x) * 3
				bg[idx], bg[idx+1], bg[idx+2] = c[0], c[1], c[2]
			}
		}
	}
	vw := trackcv.OpenVideoWriter(path, fps, width, height)
	defer vw.Close()
	const radius = 35
	for i := 0; i < int(fps*float64(seconds)); i++ {
		frame := append([]byte(nil), bg...)
		cy := int(float64(height)/2 + 40*math.Sin(2*math.Pi*float64(i)/fps))
		cx := width / 2
		for y := cy - radius; y <= cy+radius; y++ {
			for x := cx - radius; x <= cx+radius; x++ {
				if y < 0 || y >= height || x < 0 || x >= width {
					continue
				}
				if dx, dy := x-cx, y-cy; dx*dx+dy*dy <= radius*radius {
					idx := (y*width + x) * 3
					frame[idx], frame[idx+1], frame[idx+2] = 250, 250, 250
				}
			}
		}
		vw.WriteBGR(frame, width, height, width*3)
	}
}
