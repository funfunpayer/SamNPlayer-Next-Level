package trackcv

import (
	"math"
	"math/rand"
	"testing"
)

// synthClip builds per-frame cell flow for a 16x9 grid over a 1280x720 frame:
// cell `target` moves with the stroke (velocity of sin at hz, times
// targetSign), every other cell carries small noise, and `distractor` (if
// >=0) oscillates stronger than the target but a quarter period out of phase.
func synthClip(frames int, fps, hz float64, target, distractor int, targetSign float64) (cellV [][]float32, stroke []float64) {
	rng := rand.New(rand.NewSource(1))
	cells := 16 * 9
	cellV = make([][]float32, frames)
	stroke = make([]float64, frames)
	for i := range cellV {
		t := float64(i) / fps
		stroke[i] = 40 * math.Sin(2*math.Pi*hz*t)
		cellV[i] = make([]float32, cells)
		if i == 0 {
			continue
		}
		dv := stroke[i] - stroke[i-1]
		for c := range cellV[i] {
			cellV[i][c] = float32(rng.NormFloat64() * 0.6)
		}
		cellV[i][target] += float32(targetSign * dv)
		if distractor >= 0 {
			cellV[i][distractor] += float32(3 * 40 * (math.Cos(2*math.Pi*hz*t) - math.Cos(2*math.Pi*hz*(t-1/fps))))
		}
	}
	return cellV, stroke
}

func cellCenter(c int) (float64, float64) {
	return (float64(c%16) + 0.5) * 80, (float64(c/16) + 0.5) * 80
}

func seedAtCell(c int) rhythmSeed {
	x, y := cellCenter(c)
	return rhythmSeed{X: x - 15, Y: y - 15, W: 30, H: 30}
}

func testAmplitudeRatio(a, b []float64) float64 {
	var pa, pb float64
	for i := 0; i < min(len(a), len(b)); i++ {
		pa += a[i] * a[i]
		pb += b[i] * b[i]
	}
	if pb <= 1e-12 {
		return math.Inf(1)
	}
	return math.Sqrt(pa / pb)
}

// A drifting box whose own motion only weakly follows the stroke: the grid
// must recover the stroke from the rhythmic cell near the box.
func TestRhythmGridRecoversStrokeDespiteDrift(t *testing.T) {
	const fps, hz, frames = 24.0, 1.1, 24 * 60
	target := 5*16 + 8
	// Far-away (> 3 cells) stronger rhythm must be ignored: it's another body part.
	distractor := 1*16 + 1
	cellV, stroke := synthClip(frames, fps, hz, target, distractor, -1)

	tx, ty := cellCenter(target)
	rng := rand.New(rand.NewSource(2))
	cx, cy, anchor := make([]float64, frames), make([]float64, frames), make([]float64, frames)
	for i := range anchor {
		frac := float64(i) / frames
		drift := 150 * frac // box walks ~2 cells off target
		cx[i], cy[i] = tx-drift, ty
		// The box's own motion follows the stroke less the further it
		// drifts - the real failure mode - plus jitter and baseline drift.
		anchor[i] = ty + drift + 0.3*(1-frac)*stroke[i] + 4*rng.NormFloat64()
	}

	got := rhythmGridPositions(cellV, 16, 9, 1280, 720, cx, cy, anchor,
		seedAtCell(target), nil, fps)
	k := int(2 * fps)
	rGrid := pearson(subtractRollingMean(got, k), stroke)
	rCSRT := pearson(subtractRollingMean(anchor, k), stroke)
	if rGrid < 0.9 {
		t.Errorf("grid curve r=%.3f vs stroke, want >= 0.9 (tracker alone %.3f)", rGrid, rCSRT)
	}
	if rGrid <= rCSRT {
		t.Errorf("grid r=%.3f not better than tracker r=%.3f", rGrid, rCSRT)
	}
}

// Regression for the real heatmap failure: a nearby thigh carries much more
// periodic energy and the CSRT center drifts onto it. The confirmed target
// seed must keep identity instead of following the strongest nearby cell.
func TestRhythmGridKeepsConfirmedTargetWhenTrackerDriftsOntoThigh(t *testing.T) {
	const fps, hz, frames = 24.0, 1.0, 24 * 50
	target := 5*16 + 7
	thigh := target + 1
	cellV, stroke := synthClip(frames, fps, hz, target, thigh, 1)
	tx, ty := cellCenter(target)
	dx, dy := cellCenter(thigh)
	cx, cy, anchor := make([]float64, frames), make([]float64, frames), make([]float64, frames)
	for i := range anchor {
		frac := math.Min(1, float64(i)/(12*fps))
		cx[i] = tx + frac*(dx-tx)
		cy[i] = ty + frac*(dy-ty)
		if i < int(10*fps) {
			anchor[i] = ty + 0.35*stroke[i]
		} else {
			// The drifted tracker follows the quarter-phase thigh motion.
			t := float64(i) / fps
			anchor[i] = dy + 25*math.Cos(2*math.Pi*hz*t)
		}
	}

	got := rhythmGridPositions(cellV, 16, 9, 1280, 720, cx, cy, anchor,
		seedAtCell(target), nil, fps)
	r := pearson(subtractRollingMean(got, int(2*fps)), stroke)
	if r < 0.9 {
		t.Fatalf("confirmed target lost to adjacent thigh: r=%.3f, want >= 0.9", r)
	}
}

func TestRhythmGridRejectsInPhaseAndAntiPhaseHighAmplitudeThigh(t *testing.T) {
	for _, tc := range []struct {
		name string
		sign float64
	}{{"in_phase", 1}, {"anti_phase", -1}} {
		t.Run(tc.name, func(t *testing.T) {
			const fps, hz, frames = 24.0, 1.0, 24 * 40
			target := 5*16 + 7
			thigh := target + 1
			cellV, stroke := synthClip(frames, fps, hz, target, -1, 1)
			for i := 1; i < frames; i++ {
				dv := stroke[i] - stroke[i-1]
				cellV[i][thigh] += float32(tc.sign * 4 * dv)
			}
			tx, ty := cellCenter(target)
			dx, dy := cellCenter(thigh)
			cx, cy, anchor := make([]float64, frames), make([]float64, frames), make([]float64, frames)
			for i := range anchor {
				frac := math.Min(1, float64(i)/(10*fps))
				cx[i], cy[i] = tx+frac*(dx-tx), ty+frac*(dy-ty)
				anchor[i] = ty + 0.3*stroke[i]
			}
			// Deliberately wide enough to include both cells. Identity must come
			// from the box center, not the stronger rhythm score.
			wideSeed := rhythmSeed{X: tx - 60, Y: ty - 40, W: 120, H: 80}

			got := rhythmGridPositions(cellV, 16, 9, 1280, 720, cx, cy, anchor,
				wideSeed, nil, fps)
			k := int(2 * fps)
			gotAC := subtractRollingMean(got, k)
			strokeAC := subtractRollingMean(stroke, k)
			if r := pearson(gotAC, strokeAC); r < 0.9 {
				t.Fatalf("target rhythm lost: r=%.3f, want >= 0.9", r)
			}
			if ratio := testAmplitudeRatio(gotAC, strokeAC); ratio > 2.0 {
				t.Fatalf("thigh amplitude took over: ratio=%.2f, want <= 2.0", ratio)
			}
		})
	}
}

func TestRhythmGridReseedsConfirmedIdentityAfterSceneCut(t *testing.T) {
	const fps, hz, secondHz, frames = 24.0, 1.1, 1.7, 24 * 40
	firstTarget := 5*16 + 7
	secondTarget := 2*16 + 12
	firstFlow, stroke := synthClip(frames, fps, hz, firstTarget, -1, 1)
	secondFlow, secondStroke := synthClip(frames, fps, secondHz, secondTarget, -1, -1)
	cut := frames / 2
	fx, fy := cellCenter(firstTarget)
	sx, sy := cellCenter(secondTarget)
	cx, cy, anchor := make([]float64, frames), make([]float64, frames), make([]float64, frames)
	for i := 0; i < frames; i++ {
		if i < cut {
			cx[i], cy[i] = fx, fy
			anchor[i] = fy + 0.3*stroke[i]
			continue
		}
		firstFlow[i] = secondFlow[i]
		cx[i], cy[i] = sx, sy
		anchor[i] = sy + 0.3*secondStroke[i]
	}

	got := rhythmGridPositions(firstFlow, 16, 9, 1280, 720, cx, cy, anchor,
		seedAtCell(firstTarget), []int{cut}, fps)
	if math.Abs(got[cut]-anchor[cut]) > 1e-9 {
		t.Fatalf("cut was not re-anchored: got %.3f want %.3f", got[cut], anchor[cut])
	}
	fallbackEnd := cut + int(rhythmWindowSec*fps/2)
	for i := cut; i < fallbackEnd; i++ {
		if math.Abs(got[i]-anchor[i]) > 1e-8 {
			t.Fatalf("frame %d after cut rewrote tracker fallback: got %.6f want %.6f",
				i, got[i], anchor[i])
		}
	}
	for _, part := range []struct {
		name      string
		a, b      int
		reference []float64
	}{
		{"immediately_before", cut - int(4*fps), cut, stroke},
		{"immediately_after", cut, cut + int(4*fps), secondStroke},
		{"late_after", cut + int(5*fps), frames, secondStroke},
	} {
		r := pearson(
			subtractRollingMean(got[part.a:part.b], int(2*fps)),
			part.reference[part.a:part.b],
		)
		if r < 0.9 {
			t.Fatalf("%s cut: target correlation %.3f, want >= 0.9", part.name, r)
		}
	}
}

// No rhythm anywhere (static scene): output falls back to the tracker.
func TestRhythmGridFallsBackToTracker(t *testing.T) {
	const fps, frames = 24.0, 24 * 20
	cellV := make([][]float32, frames)
	cx, cy, anchor := make([]float64, frames), make([]float64, frames), make([]float64, frames)
	for i := range cellV {
		cellV[i] = make([]float32, 16*9)
		cx[i], cy[i] = 640, 360
		anchor[i] = 360 + 10*math.Sin(float64(i)/5)
	}
	got := rhythmGridPositions(cellV, 16, 9, 1280, 720, cx, cy, anchor,
		rhythmSeed{X: 600, Y: 320, W: 80, H: 80}, nil, fps)
	for i := range got {
		if math.Abs(got[i]-anchor[i]) > 1e-9 {
			t.Fatalf("frame %d: got %v, want tracker %v", i, got[i], anchor[i])
		}
	}
}

func TestRhythmGridBadInputReturnsTracker(t *testing.T) {
	anchor := []float64{1, 2, 3}
	got := rhythmGridPositions(make([][]float32, 2), 16, 9, 1280, 720,
		anchor, anchor, anchor, rhythmSeed{}, nil, 24)
	if len(got) != 3 || got[2] != 3 {
		t.Fatalf("got %v, want copy of tracker positions", got)
	}
	got[0] = 99
	if anchor[0] != 1 {
		t.Fatal("result aliases the tracker slice")
	}
}

func TestRhythmGridRows(t *testing.T) {
	for _, c := range []struct{ w, h, want int }{{1280, 720, 9}, {1920, 1080, 9}, {1080, 1920, 28}, {640, 480, 12}, {0, 0, 9}} {
		if got := rhythmGridRows(c.w, c.h); got != c.want {
			t.Errorf("%dx%d: got %d rows, want %d", c.w, c.h, got, c.want)
		}
	}
}

// Where the box's own motion carries no stroke at all, the tracker's sign is
// a coin toss per chunk; orientation must then come from the curve already
// written, or the stroke flips back and forth.
func TestRhythmGridKeepsOrientationWhereTrackerIsUninformative(t *testing.T) {
	const fps, hz, frames = 24.0, 1.1, 24 * 90
	target := 5*16 + 8
	cellV, stroke := synthClip(frames, fps, hz, target, -1, -1)
	tx, ty := cellCenter(target)
	rng := rand.New(rand.NewSource(3))
	cx, cy, anchor := make([]float64, frames), make([]float64, frames), make([]float64, frames)
	for i := range anchor {
		cx[i], cy[i] = tx, ty
		follow := 0.3
		if i > frames/3 && i < 2*frames/3 {
			follow = 0 // middle third: box sits on something that doesn't stroke
		}
		anchor[i] = ty + follow*stroke[i] + 4*rng.NormFloat64()
	}
	got := rhythmGridPositions(cellV, 16, 9, 1280, 720, cx, cy, anchor,
		seedAtCell(target), nil, fps)
	k := int(2 * fps)
	if r := pearson(subtractRollingMean(got, k), stroke); r < 0.95 {
		t.Errorf("grid curve r=%.3f vs stroke, want >= 0.95 (orientation flips where the tracker is uninformative)", r)
	}
}

// P1: scoreWindows + chooseAndStitch must produce the same curve as the
// combined rhythmGridPositions path (bit-identical on synthetic data).
func TestRhythmGridSplitBitIdentical(t *testing.T) {
	const fps, hz, frames = 24.0, 1.1, 24 * 60
	target := 5*16 + 8
	cellV, stroke := synthClip(frames, fps, hz, target, -1, -1)
	tx, ty := cellCenter(target)
	rng := rand.New(rand.NewSource(4))
	cx, cy, anchor := make([]float64, frames), make([]float64, frames), make([]float64, frames)
	for i := range anchor {
		cx[i], cy[i] = tx, ty
		anchor[i] = ty + 0.4*stroke[i] + 3*rng.NormFloat64()
	}
	seed := seedAtCell(target)
	viaWrapper := rhythmGridPositions(cellV, 16, 9, 1280, 720, cx, cy, anchor, seed, nil, fps)
	viaMap, m := rhythmGridPositionsWithMap(cellV, 16, 9, 1280, 720, cx, cy, anchor, seed, nil, fps)
	if len(viaWrapper) != len(viaMap) {
		t.Fatalf("length mismatch: wrapper %d map %d", len(viaWrapper), len(viaMap))
	}
	for i := range viaWrapper {
		if viaWrapper[i] != viaMap[i] {
			t.Fatalf("frame %d: wrapper %v != map path %v (not bit-identical)", i, viaWrapper[i], viaMap[i])
		}
	}
	if m.Cols != 16 || m.Rows != 9 || m.Width != 1280 || m.Height != 720 {
		t.Fatalf("map shape: cols=%d rows=%d %dx%d", m.Cols, m.Rows, m.Width, m.Height)
	}
	if len(m.Windows) == 0 {
		t.Fatal("expected scored windows")
	}
	for _, w := range m.Windows {
		if len(w.Score) != 16*9 {
			t.Fatalf("window score len %d, want %d", len(w.Score), 16*9)
		}
		if w.TempoHz < rhythmTempoLoHz || w.TempoHz > rhythmTempoHiHz {
			t.Errorf("tempo %.3f outside stroke band", w.TempoHz)
		}
		// Full-run path should pick a cell near the target.
		if w.ChosenCell < 0 {
			continue
		}
		if w.SignRule != "tracker" && w.SignRule != "continuity" {
			t.Errorf("unexpected SignRule %q", w.SignRule)
		}
	}
}

func TestNormalizeScores(t *testing.T) {
	s := normalizeScores([]float64{0, 1, 0.5})
	if s[1] != 255 || s[2] != 128 {
		t.Fatalf("got %v, want max=255 mid≈128", s)
	}
	z := normalizeScores([]float64{0, 0, 0})
	for _, v := range z {
		if v != 0 {
			t.Fatalf("zero scores should stay 0, got %v", z)
		}
	}
}

func TestScoreWindowsQuickScanNoBox(t *testing.T) {
	const fps, frames = 24.0, 24 * 30
	target := 5*16 + 8
	cellV, _ := synthClip(frames, fps, 1.0, target, -1, 1)
	m := scoreWindows(cellV, 16, 9, 1280, 720, nil, nil, fps)
	if m.Cols != 16 || m.Rows != 9 {
		t.Fatalf("shape %dx%d", m.Cols, m.Rows)
	}
	if len(m.Windows) == 0 {
		t.Fatal("expected windows")
	}
	for _, w := range m.Windows {
		if w.ChosenCell != -1 {
			t.Errorf("quick-scan ChosenCell should stay -1, got %d", w.ChosenCell)
		}
		if len(w.Score) != 144 {
			t.Fatalf("score len %d", len(w.Score))
		}
	}
}

// P3: exclude mark skips a high-scoring thigh cell so the search lands on the tip.
func TestRhythmGridExcludeSkipsHighScoringThigh(t *testing.T) {
	const fps, hz, frames = 24.0, 1.0, 24 * 40
	target := 5*16 + 7
	thigh := target + 1
	cellV, _ := synthClip(frames, fps, hz, target, thigh, 1)
	tx, ty := cellCenter(thigh) // box sits on the thigh (closer than tip)
	cx, cy, anchor := make([]float64, frames), make([]float64, frames), make([]float64, frames)
	for i := range anchor {
		cx[i], cy[i] = tx, ty
		anchor[i] = ty
	}
	// No seed → radius search around the box; thigh wins by distance.
	emptySeed := rhythmSeed{}
	_, without := rhythmGridPositionsWithMapMarks(cellV, 16, 9, 1280, 720, cx, cy, anchor,
		emptySeed, nil, fps, nil)
	if len(without.Windows) == 0 {
		t.Fatal("expected windows")
	}
	pickedThigh := false
	for _, w := range without.Windows {
		if w.ChosenCell == thigh {
			pickedThigh = true
			break
		}
	}
	if !pickedThigh {
		t.Fatalf("precondition: without exclude expected thigh cell %d among chosen, got %+v",
			thigh, chosenCells(without))
	}

	thx, thy := cellCenter(thigh)
	exclude := []SceneMark{{
		Kind: "exclude", ID: "thigh",
		Rect: Rect{X: int(thx) - 40, Y: int(thy) - 40, W: 80, H: 80},
	}}
	_, withEx := rhythmGridPositionsWithMapMarks(cellV, 16, 9, 1280, 720, cx, cy, anchor,
		emptySeed, nil, fps, exclude)
	for _, w := range withEx.Windows {
		if w.ChosenCell == thigh {
			t.Fatalf("exclude must skip thigh cell %d, window marks=%v chosen=%d",
				thigh, w.Marks, w.ChosenCell)
		}
		if len(w.Marks) != 1 || w.Marks[0] != "thigh" {
			t.Errorf("expected Marks=[thigh], got %v", w.Marks)
		}
	}
	pickedTarget := false
	for _, w := range withEx.Windows {
		if w.ChosenCell == target {
			pickedTarget = true
			break
		}
	}
	if !pickedTarget {
		t.Fatalf("with exclude expected tip cell %d among chosen, got %+v",
			target, chosenCells(withEx))
	}
}

// Follow Path (not static Rect) must drive exclude — Owner: marks move with subject.
func TestRhythmGridExcludeUsesFollowPath(t *testing.T) {
	const fps, hz, frames = 24.0, 1.0, 24 * 40
	target := 5*16 + 7
	thigh := target + 1
	cellV, _ := synthClip(frames, fps, hz, target, thigh, 1)
	tx, ty := cellCenter(thigh)
	cx, cy, anchor := make([]float64, frames), make([]float64, frames), make([]float64, frames)
	for i := range anchor {
		cx[i], cy[i] = tx, ty
		anchor[i] = ty
	}
	emptySeed := rhythmSeed{}
	thx, thy := cellCenter(thigh)
	// Static Rect is inert (far corner); Path lands on the thigh at every sample.
	exclude := []SceneMark{{
		Kind: "exclude", ID: "thigh", Follow: true,
		Rect: Rect{X: 0, Y: 0, W: 20, H: 20},
		Path: []MarkSample{
			{Ms: 0, Rect: Rect{X: int(thx) - 40, Y: int(thy) - 40, W: 80, H: 80}},
			{Ms: 20000, Rect: Rect{X: int(thx) - 40, Y: int(thy) - 40, W: 80, H: 80}},
		},
	}}
	_, withEx := rhythmGridPositionsWithMapMarks(cellV, 16, 9, 1280, 720, cx, cy, anchor,
		emptySeed, nil, fps, exclude)
	for _, w := range withEx.Windows {
		if w.ChosenCell == thigh {
			t.Fatalf("Follow Path exclude must skip thigh cell %d, marks=%v chosen=%d",
				thigh, w.Marks, w.ChosenCell)
		}
	}
}

// P3: source hint prefers the hinted cell when its score is ≥ 0.5× best outside.
func TestRhythmGridSourceHintPreference(t *testing.T) {
	const fps, hz, frames = 24.0, 1.0, 24 * 40
	near := 5*16 + 7   // closer to box, weaker rhythm
	hinted := near + 2 // farther, stronger rhythm (distractor amp)
	cellV, _ := synthClip(frames, fps, hz, near, hinted, 1)
	// Boost the "near" cell as the seed target signal; synthClip already
	// puts stronger motion on distractor (hinted).
	nx, ny := cellCenter(near)
	cx, cy, anchor := make([]float64, frames), make([]float64, frames), make([]float64, frames)
	for i := range anchor {
		cx[i], cy[i] = nx, ny
		anchor[i] = ny
	}
	emptySeed := rhythmSeed{}
	hx, hy := cellCenter(hinted)
	source := []SceneMark{{
		Kind: "source", ID: "stroke",
		Rect: Rect{X: int(hx) - 40, Y: int(hy) - 40, W: 80, H: 80},
	}}
	_, withSrc := rhythmGridPositionsWithMapMarks(cellV, 16, 9, 1280, 720, cx, cy, anchor,
		emptySeed, nil, fps, source)
	pickedHint := false
	for _, w := range withSrc.Windows {
		if w.ChosenCell == hinted {
			pickedHint = true
		}
		if len(w.Marks) != 1 || w.Marks[0] != "stroke" {
			t.Errorf("expected Marks=[stroke], got %v", w.Marks)
		}
	}
	if !pickedHint {
		t.Fatalf("source hint should prefer hinted cell %d, got %+v",
			hinted, chosenCells(withSrc))
	}

	// Without the hint, distance-to-box picks the nearer cell.
	_, noSrc := rhythmGridPositionsWithMapMarks(cellV, 16, 9, 1280, 720, cx, cy, anchor,
		emptySeed, nil, fps, nil)
	pickedNear := false
	for _, w := range noSrc.Windows {
		if w.ChosenCell == near {
			pickedNear = true
			break
		}
	}
	if !pickedNear {
		t.Fatalf("precondition: without source expected near cell %d, got %+v",
			near, chosenCells(noSrc))
	}
}

// P3: nil SceneMarks keep the pre-M3 curve bit-identical.
func TestRhythmGridMarksNilBitIdentical(t *testing.T) {
	const fps, hz, frames = 24.0, 1.1, 24 * 40
	target := 5*16 + 8
	cellV, stroke := synthClip(frames, fps, hz, target, -1, -1)
	tx, ty := cellCenter(target)
	cx, cy, anchor := make([]float64, frames), make([]float64, frames), make([]float64, frames)
	for i := range anchor {
		cx[i], cy[i] = tx, ty
		anchor[i] = ty + 0.4*stroke[i]
	}
	seed := seedAtCell(target)
	a := rhythmGridPositions(cellV, 16, 9, 1280, 720, cx, cy, anchor, seed, nil, fps)
	b := rhythmGridPositionsMarks(cellV, 16, 9, 1280, 720, cx, cy, anchor, seed, nil, fps, nil)
	if len(a) != len(b) {
		t.Fatalf("len %d vs %d", len(a), len(b))
	}
	for i := range a {
		if a[i] != b[i] {
			t.Fatalf("frame %d: %v != %v", i, a[i], b[i])
		}
	}
}

func chosenCells(m SceneMap) []int {
	var out []int
	for _, w := range m.Windows {
		out = append(out, w.ChosenCell)
	}
	return out
}

// Real-clip failure (IdLock): the seed cell nearest the ROI centre carries the
// stroke at first, then goes quiet while the stroke is two cells away (still
// inside the search radius). The lock must be released instead of emitting
// noise for the rest of the shot; the #248 thigh tests above keep a 3-4x
// adjacent distractor from triggering the same release.
func TestRhythmGridReleasesLockWhenLockedCellGoesQuiet(t *testing.T) {
	const fps, hz, frames = 24.0, 1.1, 24 * 80
	seedCell := 5*16 + 7
	moved := seedCell + 2
	cellV, stroke := synthClip(frames, fps, hz, seedCell, -1, 1)
	quiet := int(20 * fps)
	for i := quiet; i < frames; i++ {
		dv := float32(stroke[i] - stroke[i-1])
		cellV[i][seedCell] -= dv // seed cell falls back to noise
		cellV[i][moved] += dv
	}
	sx, sy := cellCenter(seedCell)
	rng := rand.New(rand.NewSource(5))
	cx, cy, anchor := make([]float64, frames), make([]float64, frames), make([]float64, frames)
	for i := range anchor {
		cx[i], cy[i] = sx, sy
		anchor[i] = sy + 0.3*stroke[i] + 4*rng.NormFloat64()
	}
	got := rhythmGridPositions(cellV, 16, 9, 1280, 720, cx, cy, anchor,
		seedAtCell(seedCell), nil, fps)
	late := int(40 * fps)
	if r := pearson(subtractRollingMean(got[late:], int(2*fps)), stroke[late:]); r < 0.9 {
		t.Fatalf("locked cell went quiet but lock was kept: r=%.3f after %ds, want >= 0.9",
			r, late/int(fps))
	}
}
