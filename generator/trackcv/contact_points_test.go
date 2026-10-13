package trackcv

import (
	"math"
	"math/rand"
	"testing"
)

func frameTimes(frames int, fps float64) []int {
	ts := make([]int, frames)
	for i := range ts {
		ts[i] = int(float64(i) * 1000 / fps)
	}
	return ts
}

func TestApplyContactPointsGateAndHold(t *testing.T) {
	const frames = 100 // 25 fps -> 4 s
	ts := frameTimes(frames, 25)
	cx, cy := make([]float64, frames), make([]float64, frames)
	for i := range cx {
		cx[i], cy[i] = 100, 100
	}
	if x, y := applyContactPoints(cx, cy, ts, nil, 0, 1280, 720, 16); &x[0] != &cx[0] || &y[0] != &cy[0] {
		t.Fatal("no points must return the input slices unchanged")
	}
	pts := []ContactPoint{
		{Ms: 3000, X: 110.0 / 1280, Y: 120.0 / 720}, // inside the 3-cell radius: ignored
		{Ms: 0, X: 0.9, Y: 0.9},                     // far away: used
	}
	x, y := applyContactPoints(cx, cy, ts, pts, 0, 1280, 720, 16)
	for i := range x {
		switch {
		case ts[i] <= 1000:
			if x[i] != 0.9*1280 || y[i] != 0.9*720 {
				t.Fatalf("frame %d (%d ms): want far contact point, got %v,%v", i, ts[i], x[i], y[i])
			}
		default:
			// 1..2 s: outside the hold of the 0 ms point, nearest is 3000 ms
			// but outside hold until 2 s; from 2 s the near point wins and
			// is inside the radius. Either way the box centre stays.
			if x[i] != 100 || y[i] != 100 {
				t.Fatalf("frame %d (%d ms): want box centre, got %v,%v", i, ts[i], x[i], y[i])
			}
		}
	}
	if cx[0] != 100 || cy[0] != 100 {
		t.Fatal("input slices must not be modified")
	}
	// Explicit hold: 2.5 s keeps the far point up to 2.5 s ... unless the
	// 3 s point is nearer in time (from 1.5 s on).
	x, _ = applyContactPoints(cx, cy, ts, pts, 2500, 1280, 720, 16)
	if x[int(1.4*25)] != 0.9*1280 || x[int(1.6*25)] != 100 {
		t.Fatalf("hold/nearest: got %v at 1.4 s, %v at 1.6 s", x[int(1.4*25)], x[int(1.6*25)])
	}
}

// The CSRT box (and the user ROI) sit 9 cells away from the only rhythmic
// cell - the multi-person failure, where the box stays on a head. Without
// contact points the grid cannot reach the stroke (search radius 3 cells);
// with teacher points on the stroke it must lock onto it.
func TestRhythmGridContactPointsReachFarStroke(t *testing.T) {
	const fps, hz, frames = 24.0, 1.1, 24 * 60
	boxCell := 2*16 + 2
	target := 6*16 + 11
	cellV, stroke := synthClip(frames, fps, hz, target, -1, 1)
	bx, by := cellCenter(boxCell)
	tx, ty := cellCenter(target)
	rng := rand.New(rand.NewSource(7))
	cx, cy, anchor := make([]float64, frames), make([]float64, frames), make([]float64, frames)
	for i := range anchor {
		cx[i], cy[i] = bx, by
		// The whole body rocks a little with the stroke: weak sign info.
		anchor[i] = by + 0.3*stroke[i] + 4*rng.NormFloat64()
	}
	ts := frameTimes(frames, fps)
	late := int(20 * fps)

	without := rhythmGridPositions(cellV, 16, 9, 1280, 720, cx, cy, anchor, seedAtCell(boxCell), nil, fps)
	if r := math.Abs(pearson(subtractRollingMean(without[late:], int(2*fps)), stroke[late:])); r > 0.5 {
		t.Fatalf("setup: grid reached the far stroke without contact points (|r|=%.3f)", r)
	}

	var pts []ContactPoint
	for ms := 0; ms < frames*1000/int(fps); ms += 500 {
		pts = append(pts, ContactPoint{Ms: int64(ms), X: tx / 1280, Y: ty / 720})
	}
	sx, sy := applyContactPoints(cx, cy, ts, pts, 0, 1280, 720, 16)
	with := rhythmGridPositions(cellV, 16, 9, 1280, 720, sx, sy, anchor, seedAtCell(boxCell), nil, fps)
	if r := pearson(subtractRollingMean(with[late:], int(2*fps)), stroke[late:]); r < 0.9 {
		t.Fatalf("contact points on the stroke: r=%.3f after %ds, want >= 0.9", r, late/int(fps))
	}
}

// Points inside the search radius must leave the curve bit-identical: the
// gate that keeps both goldens unchanged.
func TestRhythmGridContactPointsNearBoxBitIdentical(t *testing.T) {
	const fps, hz, frames = 24.0, 1.1, 24 * 30
	target := 5*16 + 7
	cellV, stroke := synthClip(frames, fps, hz, target, -1, 1)
	bx, by := cellCenter(target)
	cx, cy, anchor := make([]float64, frames), make([]float64, frames), make([]float64, frames)
	for i := range anchor {
		cx[i], cy[i] = bx, by
		anchor[i] = by + 0.5*stroke[i]
	}
	var pts []ContactPoint
	for ms := 0; ms < frames*1000/int(fps); ms += 500 {
		pts = append(pts, ContactPoint{Ms: int64(ms), X: (bx + 2*80) / 1280, Y: (by + 80) / 720})
	}
	sx, sy := applyContactPoints(cx, cy, frameTimes(frames, fps), pts, 0, 1280, 720, 16)
	a := rhythmGridPositions(cellV, 16, 9, 1280, 720, cx, cy, anchor, seedAtCell(target), nil, fps)
	b := rhythmGridPositions(cellV, 16, 9, 1280, 720, sx, sy, anchor, seedAtCell(target), nil, fps)
	for i := range a {
		if a[i] != b[i] {
			t.Fatalf("frame %d: %v != %v - points inside the radius changed the curve", i, a[i], b[i])
		}
	}
}

func TestVerifyContactPointsKeepsOnlyWhereEngineMeasuresMore(t *testing.T) {
	// 4x3 grid, one window 0-8 s; the engine chose cell 0 (score 100).
	score := make([]uint8, 12)
	score[0] = 100
	score[6] = 200 // col 2, row 1: twice the chosen cell
	score[11] = 120
	m := SceneMap{Cols: 4, Rows: 3, Windows: []MapWindow{
		{StartMs: 0, EndMs: 8000, ChosenCell: 0, Score: score},
		{StartMs: 20000, EndMs: 28000, ChosenCell: -1, Score: score}, // no own cell: nothing to verify
	}}
	pts := []ContactPoint{
		{Ms: 1000, X: 0.6, Y: 0.5},   // cell 2,1 -> 200 >= 1.5*100: keep
		{Ms: 2000, X: 0.95, Y: 0.95}, // cell 3,2 -> 3x3 max 200 (neighbour 2,1): keep
		{Ms: 3000, X: 0.05, Y: 0.9},  // cell 0,2 -> 3x3 max 100: drop
		{Ms: 24000, X: 0.6, Y: 0.5},  // window without chosen cell: drop
	}
	got := verifyContactPoints(pts, m, 1.5)
	if len(got) != 2 || got[0].Ms != 1000 || got[1].Ms != 2000 {
		t.Fatalf("k=1.5 kept %+v", got)
	}
	if got := verifyContactPoints(pts, m, 2.5); len(got) != 0 {
		t.Fatalf("k=2.5 kept %+v", got)
	}
	if got := verifyContactPoints(pts, SceneMap{}, 1.5); got != nil {
		t.Fatalf("empty map kept %+v", got)
	}
}

func TestVerifyContactPointsAtHonoursStartOffset(t *testing.T) {
	// Run started 60 s into the video: the map's windows count from 0, the
	// contact points are absolute. Window 0-8 s (relative) = 60-68 s video:
	// chosen cell 0 is weak, cell 6 strong. Window 20-28 s: cell 6 weak.
	strong := make([]uint8, 12)
	strong[0], strong[6] = 100, 200
	weak := make([]uint8, 12)
	weak[0], weak[6] = 200, 100
	m := SceneMap{Cols: 4, Rows: 3, Windows: []MapWindow{
		{StartMs: 0, EndMs: 8000, ChosenCell: 0, Score: strong},
		{StartMs: 20000, EndMs: 28000, ChosenCell: 0, Score: weak},
	}}
	pts := []ContactPoint{{Ms: 64000, X: 0.6, Y: 0.5}} // video 64 s = relative 4 s
	if got := verifyContactPointsAt(pts, m, 1.5, 60000); len(got) != 1 {
		t.Fatalf("with offset: kept %+v, want the point (strong window)", got)
	}
	// Without the offset the point lands on the weak window and is dropped.
	if got := verifyContactPoints(pts, m, 1.5); len(got) != 0 {
		t.Fatalf("without offset: kept %+v", got)
	}
}
