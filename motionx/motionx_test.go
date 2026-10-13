package motionx

import (
	"math"
	"testing"
)

func sine(n int, fps, freq float64) []Point {
	out := make([]Point, n)
	for i := range out {
		t := float64(i) / fps
		out[i] = Point{
			TMs: t * 1000,
			Pos: (math.Sin(2*math.Pi*freq*t) + 1) / 2 * 100,
		}
	}
	return out
}

func TestSimplifyKeepsEndpoints(t *testing.T) {
	pts := sine(200, 30, 1)
	idx := Simplify(pts, 1.0)
	if len(idx) < 2 {
		t.Fatalf("expected at least two points, got %d", len(idx))
	}
	if idx[0] != 0 || idx[len(idx)-1] != len(pts)-1 {
		t.Fatalf("endpoints not preserved: %v .. %v", idx[0], idx[len(idx)-1])
	}
}

func TestSimplifyRespectsTolerance(t *testing.T) {
	pts := sine(300, 30, 1.2)
	kept := SimplifyPoints(pts, 2.0)

	if len(kept) >= len(pts) {
		t.Fatalf("no reduction happened: %d -> %d", len(pts), len(kept))
	}

	// Every original sample must be reconstructable within tolerance.
	for _, p := range pts {
		got := interpolate(kept, p.TMs)
		if d := math.Abs(got - p.Pos); d > 2.0+1e-6 {
			t.Fatalf("deviation %.3f at t=%.1f exceeds tolerance", d, p.TMs)
		}
	}
}

func TestSimplifyStraightLineCollapses(t *testing.T) {
	pts := make([]Point, 500)
	for i := range pts {
		pts[i] = Point{TMs: float64(i) * 10, Pos: float64(i) * 0.1}
	}
	kept := SimplifyPoints(pts, 0.5)
	if len(kept) != 2 {
		t.Fatalf("straight line should collapse to 2 points, got %d", len(kept))
	}
}

func TestSimplifyDeepCurveDoesNotRecurse(t *testing.T) {
	// Sawtooth forces a split at nearly every sample.
	pts := make([]Point, 20000)
	for i := range pts {
		p := 0.0
		if i%2 == 1 {
			p = 100
		}
		pts[i] = Point{TMs: float64(i) * 10, Pos: p}
	}
	if got := len(Simplify(pts, 0.5)); got < 1000 {
		t.Fatalf("unexpected reduction on sawtooth: %d", got)
	}
}

func TestDedup(t *testing.T) {
	pts := []Point{
		{TMs: 0, Pos: 10},
		{TMs: 0.4, Pos: 20},
		{TMs: 1.2, Pos: 30},
		{TMs: 2, Pos: 40},
	}
	got := Dedup(pts)
	if len(got) != 3 {
		t.Fatalf("expected 3 points, got %d", len(got))
	}
	if got[0].Pos != 10 || got[1].Pos != 30 {
		t.Fatalf("wrong survivors: %+v", got)
	}
}

func TestClassifyStaticCurve(t *testing.T) {
	pts := make([]Point, 300)
	for i := range pts {
		pts[i] = Point{TMs: float64(i) * 33, Pos: 50}
	}
	segs := Classify(pts, ClassifyOptions{})
	if len(segs) != 1 || segs[0].State != Static {
		t.Fatalf("expected a single static segment, got %+v", segs)
	}
}

func TestClassifyStartStop(t *testing.T) {
	var pts []Point
	tMs := 0.0
	// 1s still, 2s of motion, 1s still.
	for i := 0; i < 30; i++ {
		pts = append(pts, Point{TMs: tMs, Pos: 20})
		tMs += 33
	}
	moving := sine(60, 30, 1)
	for _, p := range moving {
		pts = append(pts, Point{TMs: tMs, Pos: p.Pos})
		tMs += 33
	}
	for i := 0; i < 30; i++ {
		pts = append(pts, Point{TMs: tMs, Pos: 50})
		tMs += 33
	}

	segs := Classify(pts, ClassifyOptions{})
	if len(segs) < 3 {
		t.Fatalf("expected at least three segments, got %d: %+v", len(segs), segs)
	}
	if segs[0].State != Static {
		t.Fatalf("expected leading static segment, got %s", segs[0].State)
	}
	if segs[len(segs)-1].State != Static {
		t.Fatalf("expected trailing static segment, got %s", segs[len(segs)-1].State)
	}
	sawMotion := false
	for _, s := range segs {
		if s.State == Regular || s.State == Accelerating || s.State == Decelerating {
			sawMotion = true
		}
	}
	if !sawMotion {
		t.Fatal("no motion state detected in the moving section")
	}
}

func TestClassifySegmentsAreContiguousAndLongEnough(t *testing.T) {
	pts := sine(600, 30, 0.8)
	opt := ClassifyOptions{MinSegmentMs: 250}
	segs := Classify(pts, opt)
	if len(segs) == 0 {
		t.Fatal("no segments")
	}
	for i, s := range segs {
		if s.EndMs < s.StartMs {
			t.Fatalf("segment %d is inverted: %+v", i, s)
		}
		if len(segs) > 1 && s.Duration() < opt.MinSegmentMs {
			t.Fatalf("segment %d too short after merge: %.1fms", i, s.Duration())
		}
	}
	if segs[0].StartMs != pts[0].TMs {
		t.Fatalf("first segment does not start at t0")
	}
	if segs[len(segs)-1].EndMs != pts[len(pts)-1].TMs {
		t.Fatalf("last segment does not end at tN")
	}
}

func interpolate(pts []Point, tMs float64) float64 {
	if len(pts) == 0 {
		return 0
	}
	if tMs <= pts[0].TMs {
		return pts[0].Pos
	}
	for i := 1; i < len(pts); i++ {
		if tMs <= pts[i].TMs {
			dt := pts[i].TMs - pts[i-1].TMs
			if dt <= 0 {
				return pts[i].Pos
			}
			f := (tMs - pts[i-1].TMs) / dt
			return pts[i-1].Pos + (pts[i].Pos-pts[i-1].Pos)*f
		}
	}
	return pts[len(pts)-1].Pos
}
