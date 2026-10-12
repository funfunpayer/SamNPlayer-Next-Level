//go:build cgo && opencv

package trackcv

import (
	"math"
	"path/filepath"
	"testing"

	"github.com/funfunpayer/SamNPlayer/generator/trackutil"
)

// Hard-tip golden: a poisoned later template must not beat the frame-0 tip
// seed. reacquire used to pick the global best MatchTemplate score across
// the whole bank; with a distractor crop in the bank it hard-tipped onto
// the wrong patch. With matchesOriginal gating, that candidate is rejected
// and (when no other candidate survives) reacquire returns false so the
// caller can coast instead.
func TestReacquireRejectsPoisonedDistractorVersusTipSeed(t *testing.T) {
	const tipCX, tipCY, tipR = 80, 120, 32
	const distCX, distCY = 240, 120
	tipROI := Rect{X: tipCX - tipR, Y: tipCY - tipR, W: 2 * tipR, H: 2 * tipR}
	distROI := Rect{X: distCX - 30, Y: distCY - 30, W: 60, H: 60}

	both := grayFrameFromVideo(t, func(buf []byte) {
		paintTipPattern(buf, tipCX, tipCY, tipR)
		paintDistractorPattern(buf, distCX, distCY)
	})
	defer both.Close()

	m := newAppearanceMemory()
	defer m.close()
	seedAppearanceMemory(m, both, tipROI)
	// Simulate ungated drift remember() of the wrong region (pre-hardening).
	m.remember(both, distROI)
	if len(m.templates) < 2 {
		t.Fatalf("need tip seed + distractor templates, got %d", len(m.templates))
	}

	// Tip gone; only distractor remains — classic hard-tip failure frame.
	onlyDist := grayFrameFromVideo(t, func(buf []byte) {
		paintDistractorPattern(buf, distCX, distCY)
	})
	defer onlyDist.Close()

	if found, ok := m.reacquire(onlyDist); ok {
		t.Fatalf("reacquire locked onto distractor at %+v — want reject so coast can bridge", found)
	}
}

// When the tip is still visible, reacquire must recover near the tip seed
// even if a distractor template also sits in the bank.
func TestReacquirePrefersTipWhenVisibleDespiteDistractorTemplate(t *testing.T) {
	const tipCX, tipCY, tipR = 80, 120, 32
	const distCX, distCY = 240, 120
	tipROI := Rect{X: tipCX - tipR, Y: tipCY - tipR, W: 2 * tipR, H: 2 * tipR}
	distROI := Rect{X: distCX - 30, Y: distCY - 30, W: 60, H: 60}

	both := grayFrameFromVideo(t, func(buf []byte) {
		paintTipPattern(buf, tipCX, tipCY, tipR)
		paintDistractorPattern(buf, distCX, distCY)
	})
	defer both.Close()

	m := newAppearanceMemory()
	defer m.close()
	seedAppearanceMemory(m, both, tipROI)
	m.remember(both, distROI)

	found, ok := m.reacquire(both)
	if !ok {
		t.Fatal("expected reacquire to find the visible tip")
	}
	fcx, fcy := found.X+found.W/2, found.Y+found.H/2
	if absInt(fcx-tipCX) > 24 || absInt(fcy-tipCY) > 24 {
		t.Fatalf("reacquired center (%d,%d) far from tip (%d,%d)", fcx, fcy, tipCX, tipCY)
	}
}

func absInt(v int) int {
	if v < 0 {
		return -v
	}
	return v
}

// High-contrast tip: bright disk with a dark plus — distinctive vs stripes.
func paintTipPattern(buf []byte, cx, cy, radius int) {
	fillCircle(buf, testW, testH, cx, cy, radius, 250, 250, 250)
	fillRect(buf, testW, testH, cx-radius+4, cy-3, cx+radius-4, cy+3, 20, 20, 20)
	fillRect(buf, testW, testH, cx-3, cy-radius+4, cx+3, cy+radius-4, 20, 20, 20)
}

// Distractor: horizontal stripes — matchesOriginal vs tip-plus must fail.
func paintDistractorPattern(buf []byte, cx, cy int) {
	fillRect(buf, testW, testH, cx-30, cy-30, cx+30, cy+30, 60, 60, 60)
	for y := cy - 28; y < cy+28; y += 8 {
		fillRect(buf, testW, testH, cx-28, y, cx+28, y+4, 220, 40, 40)
	}
}

// Tip+partner path with AppearanceMemory: synthetic moving tip must keep
// usable frames (regression vs. ungated remember / missing frame-0 seed).
func TestTrackTwoPointsHardTipAppearanceMemoryStable(t *testing.T) {
	path := filepath.Join(t.TempDir(), "tip_partner.mp4")
	writeTwoMovingVideo(t, path, 3)

	tip := Rect{X: 160 - 30, Y: 80 - 30, W: 60, H: 60}
	partner := Rect{X: 160 - 25, Y: 160 - 25, W: 50, H: 50}
	result, err := TrackTwoPoints(path, tip, partner, Options{AppearanceMemory: true})
	if err != nil {
		t.Fatalf("TrackTwoPoints: %v", err)
	}
	if result.Stats.ValidFrames < result.Stats.TotalFrames/2 {
		t.Errorf("hard-tip appearance path lost too many frames: valid=%d total=%d reason=%q",
			result.Stats.ValidFrames, result.Stats.TotalFrames, result.Stats.Reason)
	}
	if result.Stats.TotalFrames < 10 {
		t.Fatalf("too few frames tracked: %d", result.Stats.TotalFrames)
	}
}

// recoverOrCoast: ensure Reset+ObserveOK path from a good reacquire clears
// coast lost streak (contract used by tip coast-vs-hard-tip goldens).
func TestRecoverOrCoastResetsCoastOnReacquire(t *testing.T) {
	const cx, cy, radius = 160, 120, 35
	roi := Rect{X: cx - radius, Y: cy - radius, W: 2 * radius, H: 2 * radius}
	path := filepath.Join(t.TempDir(), "reacq.mp4")
	writeStaticCircleVideo(t, path, cx, cy, radius, 4)

	cap := OpenVideo(path)
	defer cap.Close()
	if !cap.IsOpened() || !cap.Read() {
		t.Fatal("open synthetic video")
	}
	gray0 := cap.ToGray()
	defer gray0.Close()

	mem := newAppearanceMemory()
	defer mem.close()
	seedAppearanceMemory(mem, gray0, roi)

	tr := NewTracker()
	tr.Init(cap, roi)
	defer func() {
		if tr != nil {
			tr.Close()
		}
	}()

	var coast trackutil.Coast
	var guard dispGuard
	coast.ObserveOK(roi.X, roi.Y, roi.W, roi.H)
	coast.OnLost()
	if coast.LostStreak() != 1 {
		t.Fatalf("setup lost streak %d", coast.LostStreak())
	}

	if !cap.Read() {
		t.Fatal("need second frame")
	}
	gray := cap.ToGray()
	defer gray.Close()

	ok, box, tr2 := recoverOrCoast(cap, gray, tr, mem, &coast, &guard, Rect{}, false, roi, 1)
	tr = tr2
	if !ok {
		t.Fatal("expected appearance reacquire on visible tip")
	}
	if coast.LostStreak() != 0 {
		t.Fatalf("reacquire must Reset coast lost streak, got %d", coast.LostStreak())
	}
	bcx, bcy := box.X+box.W/2, box.Y+box.H/2
	if absInt(bcx-cx) > 25 || absInt(bcy-cy) > 25 {
		t.Fatalf("reacquired box center (%d,%d) far from tip (%d,%d)", bcx, bcy, cx, cy)
	}
}

func writeStaticCircleVideo(t *testing.T, path string, cx, cy, radius, seconds int) {
	t.Helper()
	w := OpenVideoWriter(path, testFPS, testW, testH)
	defer w.Close()
	n := int(float64(seconds) * testFPS)
	for i := 0; i < n; i++ {
		buf := textureBackground(21)
		paintTipPattern(buf, cx, cy, radius)
		w.WriteBGR(buf, testW, testH, testW*3)
	}
}

// Tip oscillates vertically; partner stays lower (Tf/Tj-like distance signal).
func writeTwoMovingVideo(t *testing.T, path string, seconds int) {
	t.Helper()
	w := OpenVideoWriter(path, testFPS, testW, testH)
	defer w.Close()
	n := int(float64(seconds) * testFPS)
	for i := 0; i < n; i++ {
		buf := textureBackground(31)
		phase := float64(i) / testFPS
		tipY := 80 + int(35*math.Sin(phase*2*math.Pi*1.0))
		paintTipPattern(buf, 160, tipY, 28)
		fillCircle(buf, testW, testH, 160, 170, 24, 200, 180, 40)
		w.WriteBGR(buf, testW, testH, testW*3)
	}
}
