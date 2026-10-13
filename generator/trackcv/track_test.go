//go:build cgo && opencv

package trackcv

// Regressionstest für TrackROI, nach demselben Muster wie die
// Python-Backend-Tests (*_backend_test.py): ein synthetisches Testvideo
// mit bekannter Ground Truth (Amplitude, Schwenk, Szenenschnitt), dagegen
// gemessen statt nur angenommen. Erzeugt sein eigenes Testvideo über
// VideoWriter (kein ffmpeg/Python nötig) - bewusst unabhängig vom
// Python-seitigen Testsuite, so wie "Go"/"Python (Generator)" auch in CI
// getrennte Jobs sind.
//
// Die eigentliche Sprachparität (Go liefert dieselben Werte wie Python für
// denselben Tracking-Lauf) ist NICHT Teil dieses automatisierten Tests -
// das wurde einmalig manuell gemessen (r=0.9996, siehe Paketkommentar in
// track.go und docs/NEXT.md) und ist keine laufende CI-Prüfung, weil sie
// eine Python-Laufzeit im Go-Testjob voraussetzen würde.

import (
	"math"
	"math/rand"
	"os"
	"path/filepath"
	"testing"
)

const (
	testW, testH, testFPS = 320, 240, 25.0
)

// textureBackground füllt buf mit einem grauen Hintergrund plus zufälligen
// farbigen Rechtecken - genug Textur für goodFeaturesToTrack/CSRT, ohne
// Anspruch auf Pixel-Parität mit den Python-Testvideos (eigenständiger
// Go-Test mit eigener Ground Truth, siehe Paketkommentar oben).
func textureBackground(seed int64) []byte {
	buf := make([]byte, testW*testH*3)
	for i := 0; i < len(buf); i += 3 {
		buf[i], buf[i+1], buf[i+2] = 40, 40, 40
	}
	rng := rand.New(rand.NewSource(seed))
	for i := 0; i < 140; i++ {
		x0 := rng.Intn(testW)
		y0 := rng.Intn(testH)
		w := 6 + rng.Intn(12)
		h := 6 + rng.Intn(12)
		b := byte(60 + rng.Intn(130))
		g := byte(60 + rng.Intn(130))
		r := byte(60 + rng.Intn(130))
		fillRect(buf, testW, testH, x0, y0, x0+w, y0+h, b, g, r)
	}
	return buf
}

func fillRect(buf []byte, width, height, x0, y0, x1, y1 int, b, g, r byte) {
	x0, y0 = clampInt(x0, 0, width), clampInt(y0, 0, height)
	x1, y1 = clampInt(x1, 0, width), clampInt(y1, 0, height)
	for y := y0; y < y1; y++ {
		row := y * width * 3
		for x := x0; x < x1; x++ {
			idx := row + x*3
			buf[idx], buf[idx+1], buf[idx+2] = b, g, r
		}
	}
}

func fillCircle(buf []byte, width, height, cx, cy, radius int, b, g, r byte) {
	r2 := radius * radius
	for y := cy - radius; y <= cy+radius; y++ {
		if y < 0 || y >= height {
			continue
		}
		dy := y - cy
		for x := cx - radius; x <= cx+radius; x++ {
			if x < 0 || x >= width {
				continue
			}
			dx := x - cx
			if dx*dx+dy*dy <= r2 {
				idx := (y*width + x) * 3
				buf[idx], buf[idx+1], buf[idx+2] = b, g, r
			}
		}
	}
}

func clampInt(v, lo, hi int) int {
	if v < lo {
		return lo
	}
	if v > hi {
		return hi
	}
	return v
}

func writeMovingVideo(t *testing.T, path string, seconds int, amplitude float64, freq float64) {
	t.Helper()
	bg := textureBackground(11)
	w := OpenVideoWriter(path, testFPS, testW, testH)
	defer w.Close()
	frames := int(testFPS * float64(seconds))
	for i := 0; i < frames; i++ {
		frame := append([]byte(nil), bg...)
		tt := float64(i) / testFPS
		y := int(120 + (amplitude/2)*math.Sin(2*math.Pi*freq*tt))
		fillCircle(frame, testW, testH, 160, y, 35, 250, 250, 250)
		w.WriteBGR(frame, testW, testH, testW*3)
	}
}

// writeMovingVideoWithPan wie writeMovingVideo, aber die "Kamera" schwenkt
// (das Ausschnittsfenster wandert über einen größeren Weltausschnitt) -
// dieselbe Idee wie camera_compensation_test.py's write_moving_with_pan().
func writeMovingVideoWithPan(t *testing.T, path string, seconds int, amplitude float64) {
	t.Helper()
	const pad = 60
	bigW, bigH := testW+2*pad, testH+2*pad
	big := make([]byte, bigW*bigH*3)
	for i := 0; i < len(big); i += 3 {
		big[i], big[i+1], big[i+2] = 40, 40, 40
	}
	rng := rand.New(rand.NewSource(11))
	for i := 0; i < 260; i++ {
		x0 := rng.Intn(bigW)
		y0 := rng.Intn(bigH)
		w := 6 + rng.Intn(12)
		h := 6 + rng.Intn(12)
		fillRect(big, bigW, bigH, x0, y0, x0+w, y0+h,
			byte(60+rng.Intn(130)), byte(60+rng.Intn(130)), byte(60+rng.Intn(130)))
	}

	w := OpenVideoWriter(path, testFPS, testW, testH)
	defer w.Close()
	frames := int(testFPS * float64(seconds))
	for i := 0; i < frames; i++ {
		tt := float64(i) / testFPS
		offX := int(60 + 30*math.Sin(2*math.Pi*tt*0.11))
		offY := int(60 + 20*math.Sin(2*math.Pi*tt*0.08))

		frame := make([]byte, testW*testH*3)
		for y := 0; y < testH; y++ {
			srcRow := (offY + y) * bigW * 3
			dstRow := y * testW * 3
			copy(frame[dstRow:dstRow+testW*3], big[srcRow+offX*3:srcRow+offX*3+testW*3])
		}

		worldY := 60 + 120 + (amplitude/2)*math.Sin(2*math.Pi*tt)
		cy := int(worldY) - offY
		fillCircle(frame, testW, testH, 160, cy, 35, 250, 250, 250)
		w.WriteBGR(frame, testW, testH, testW*3)
	}
}

func writeHardCutVideo(t *testing.T, path string, seconds int, cutAt int, amplitude float64) {
	t.Helper()
	bg1 := textureBackground(11)
	bg2 := textureBackground(99)
	w := OpenVideoWriter(path, testFPS, testW, testH)
	defer w.Close()
	frames := int(testFPS * float64(seconds))
	for i := 0; i < frames; i++ {
		bg := bg1
		if i >= cutAt {
			bg = bg2
		}
		frame := append([]byte(nil), bg...)
		if i >= cutAt {
			for j := 0; j < len(frame); j++ {
				frame[j] = byte(float64(frame[j])*0.4 + 20*0.6)
			}
		}
		tt := float64(i) / testFPS
		y := int(120 + (amplitude/2)*math.Sin(2*math.Pi*tt))
		fillCircle(frame, testW, testH, 160, y, 35, 250, 250, 250)
		w.WriteBGR(frame, testW, testH, testW*3)
	}
}

func ptpFloat(v []float64) float64 { return ptp(v) }

func TestTrackROIBasicAmplitude(t *testing.T) {
	path := filepath.Join(t.TempDir(), "moving.mp4")
	const amplitude = 80.0
	writeMovingVideo(t, path, 8, amplitude, 1.0)

	res, err := TrackROI(path, Rect{X: 100, Y: 40, W: 120, H: 160}, Options{
		CameraCompensation: false,
		SceneCutDetection:  false,
		AppearanceMemory:   false,
		Axis:               "y",
	})
	if err != nil {
		t.Fatalf("TrackROI failed: %v", err)
	}
	if len(res.TimestampsMs) != len(res.Positions) {
		t.Fatalf("timestamps/positions length mismatch: %d vs %d", len(res.TimestampsMs), len(res.Positions))
	}
	if res.Width != testW || res.Height != testH {
		t.Fatalf("unexpected frame size: %dx%d", res.Width, res.Height)
	}
	got := ptpFloat(res.Positions)
	if math.Abs(got-amplitude) > 25 {
		t.Errorf("amplitude off: got %.1f, want ~%.1f", got, amplitude)
	}
	if res.Stats.TrackerLostFrames > res.Stats.TotalFrames/20 {
		t.Errorf("too many lost frames on easy material: %d/%d",
			res.Stats.TrackerLostFrames, res.Stats.TotalFrames)
	}
}

func TestTrackROICameraCompensation(t *testing.T) {
	path := filepath.Join(t.TempDir(), "panning.mp4")
	const amplitude = 80.0
	writeMovingVideoWithPan(t, path, 10, amplitude)

	uncompensated, err := TrackROI(path, Rect{X: 100, Y: 40, W: 120, H: 160}, Options{
		CameraCompensation: false, SceneCutDetection: false, Axis: "y",
	})
	if err != nil {
		t.Fatalf("TrackROI (uncompensated) failed: %v", err)
	}
	compensated, err := TrackROI(path, Rect{X: 100, Y: 40, W: 120, H: 160}, Options{
		CameraCompensation: true, SceneCutDetection: false, Axis: "y",
	})
	if err != nil {
		t.Fatalf("TrackROI (compensated) failed: %v", err)
	}

	uncompErr := math.Abs(ptpFloat(uncompensated.Positions) - amplitude)
	compErr := math.Abs(ptpFloat(compensated.Positions) - amplitude)
	if compErr >= uncompErr {
		t.Errorf("camera compensation did not improve amplitude accuracy: "+
			"uncompensated off by %.1f, compensated off by %.1f", uncompErr, compErr)
	}
}

func TestTrackROISceneCutDetection(t *testing.T) {
	path := filepath.Join(t.TempDir(), "cut.mp4")
	writeHardCutVideo(t, path, 8, 100, 80.0)

	res, err := TrackROI(path, Rect{X: 100, Y: 40, W: 120, H: 160}, Options{
		CameraCompensation: false, SceneCutDetection: true, AppearanceMemory: true, Axis: "y",
	})
	if err != nil {
		t.Fatalf("TrackROI failed: %v", err)
	}
	if len(res.SceneCuts) == 0 {
		t.Fatal("expected at least one detected scene cut")
	}
	// Nach dem Schnitt (Frame >110, etwas Puffer für die Wiederanverankerung)
	// soll weiterhin Bewegung sichtbar sein statt an einer falschen Stelle
	// hängen zu bleiben.
	tailStart := 110
	if tailStart >= len(res.Positions) {
		t.Fatalf("test video too short for tail check: %d frames", len(res.Positions))
	}
	tail := res.Positions[tailStart:]
	if ptpFloat(tail) < 24 {
		t.Errorf("motion not visible after scene cut recovery: ptp=%.1f", ptpFloat(tail))
	}
}

func TestTrackROIAxisSelection(t *testing.T) {
	path := filepath.Join(t.TempDir(), "vertical.mp4")
	writeMovingVideo(t, path, 8, 80.0, 1.0)

	resX, err := TrackROI(path, Rect{X: 100, Y: 40, W: 120, H: 160}, Options{Axis: "x"})
	if err != nil {
		t.Fatalf("TrackROI (axis=x) failed: %v", err)
	}
	resY, err := TrackROI(path, Rect{X: 100, Y: 40, W: 120, H: 160}, Options{Axis: "y"})
	if err != nil {
		t.Fatalf("TrackROI (axis=y) failed: %v", err)
	}
	if ptpFloat(resX.Positions) >= ptpFloat(resY.Positions) {
		t.Errorf("horizontal axis should show little motion for purely vertical movement: "+
			"x=%.1f y=%.1f", ptpFloat(resX.Positions), ptpFloat(resY.Positions))
	}

	resAuto, err := TrackROI(path, Rect{X: 100, Y: 40, W: 120, H: 160}, Options{Axis: "auto"})
	if err != nil {
		t.Fatalf("TrackROI (axis=auto) failed: %v", err)
	}
	if math.Abs(ptpFloat(resAuto.Positions)-ptpFloat(resY.Positions)) > 1e-9 {
		t.Errorf("axis=auto should pick the y axis here, got ptp=%.1f (y=%.1f)",
			ptpFloat(resAuto.Positions), ptpFloat(resY.Positions))
	}
}

func TestTrackROIMaxFrames(t *testing.T) {
	path := filepath.Join(t.TempDir(), "moving.mp4")
	writeMovingVideo(t, path, 8, 80.0, 1.0)

	res, err := TrackROI(path, Rect{X: 100, Y: 40, W: 120, H: 160}, Options{MaxFrames: 40, Axis: "y"})
	if err != nil {
		t.Fatalf("TrackROI failed: %v", err)
	}
	if res.Stats.TotalFrames > 40 {
		t.Errorf("MaxFrames not respected: got %d frames", res.Stats.TotalFrames)
	}
}

func TestMain_videoWriterProducesReadableFile(t *testing.T) {
	path := filepath.Join(t.TempDir(), "smoke.mp4")
	writeMovingVideo(t, path, 1, 20.0, 1.0)
	if fi, err := os.Stat(path); err != nil || fi.Size() == 0 {
		t.Fatalf("expected a non-empty video file, err=%v", err)
	}
}

// RhythmGrid only swaps the stroke signal source: the curve must still follow
// the real motion (ground-truth sine) and the CSRT trajectory must be
// unchanged.
func TestTrackROIRhythmGrid(t *testing.T) {
	path := filepath.Join(t.TempDir(), "moving.mp4")
	const amplitude, freq = 80.0, 1.2
	writeMovingVideo(t, path, 12, amplitude, freq)
	roi := Rect{X: 100, Y: 40, W: 120, H: 160}
	base := Options{Axis: "y", CaptureTrajectory: true}
	plain, err := TrackROI(path, roi, base)
	if err != nil {
		t.Fatalf("TrackROI: %v", err)
	}
	withGrid := base
	withGrid.RhythmGrid = true
	grid, err := TrackROI(path, roi, withGrid)
	if err != nil {
		t.Fatalf("TrackROI RhythmGrid: %v", err)
	}
	if len(grid.Positions) != len(grid.TimestampsMs) {
		t.Fatalf("positions/timestamps: %d vs %d", len(grid.Positions), len(grid.TimestampsMs))
	}
	for i := range plain.TrajectoryA {
		if plain.TrajectoryA[i] != grid.TrajectoryA[i] {
			t.Fatalf("frame %d: trajectory changed %v -> %v", i, plain.TrajectoryA[i], grid.TrajectoryA[i])
		}
	}
	truth := make([]float64, len(grid.Positions))
	for i := range truth {
		truth[i] = math.Sin(2 * math.Pi * freq * float64(i) / testFPS)
	}
	k := int(2 * testFPS)
	if r := pearson(subtractRollingMean(grid.Positions, k), truth); r < 0.9 {
		t.Errorf("rhythm-grid curve r=%.3f vs ground truth, want >= 0.9", r)
	}
}
