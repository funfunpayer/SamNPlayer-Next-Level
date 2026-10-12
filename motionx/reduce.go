// Package motionx holds signal-level helpers that operate on a plain
// (time, position) series, so they can be dropped into any pipeline without
// dragging along a foreign type hierarchy.
//
// Salvaged from fse-generator v1.3 (internal/export, internal/state).
package motionx

// Point is one sample of a motion curve.
// TMs is milliseconds since start; Pos is the position in 0..100.
type Point struct {
	TMs float64
	Pos float64
}

// Simplify runs Ramer-Douglas-Peucker and returns the indices to keep,
// in ascending order. The first and last points are always kept.
//
// The deviation measure is the vertical distance to the straight line drawn
// between the segment endpoints in time. That is the correct variant for
// funscripts: perpendicular distance in a mixed ms/position space would depend
// on the arbitrary ratio between the two axes' units.
//
// maxError is in position units. Around 0.5 keeps the curve visually
// identical; 2.0 to 3.0 is a noticeable but usually acceptable reduction.
//
// Difference to the original: iterative instead of recursive, so a long dense
// curve cannot blow the stack, and it returns indices rather than a copied
// slice so callers keep their own sample metadata.
func Simplify(pts []Point, maxError float64) []int {
	if len(pts) <= 2 {
		return allIndices(len(pts))
	}
	if maxError <= 0 {
		maxError = 0.5
	}

	keep := make([]bool, len(pts))
	keep[0] = true
	keep[len(pts)-1] = true

	type span struct{ a, b int }
	stack := []span{{0, len(pts) - 1}}

	for len(stack) > 0 {
		s := stack[len(stack)-1]
		stack = stack[:len(stack)-1]
		if s.b-s.a <= 1 {
			continue
		}

		p0, p1 := pts[s.a], pts[s.b]
		dt := p1.TMs - p0.TMs

		maxDev := 0.0
		maxIdx := -1
		for i := s.a + 1; i < s.b; i++ {
			var interp float64
			if dt <= 0 {
				// Degenerate span (equal or non-monotonic timestamps):
				// fall back to comparing against the start position.
				interp = p0.Pos
			} else {
				f := (pts[i].TMs - p0.TMs) / dt
				interp = p0.Pos + (p1.Pos-p0.Pos)*f
			}
			dev := pts[i].Pos - interp
			if dev < 0 {
				dev = -dev
			}
			if dev > maxDev {
				maxDev = dev
				maxIdx = i
			}
		}

		if maxIdx >= 0 && maxDev > maxError {
			keep[maxIdx] = true
			stack = append(stack, span{s.a, maxIdx}, span{maxIdx, s.b})
		}
	}

	out := make([]int, 0, len(pts))
	for i, k := range keep {
		if k {
			out = append(out, i)
		}
	}
	return out
}

// SimplifyPoints is the convenience form of Simplify.
func SimplifyPoints(pts []Point, maxError float64) []Point {
	idx := Simplify(pts, maxError)
	out := make([]Point, 0, len(idx))
	for _, i := range idx {
		out = append(out, pts[i])
	}
	return out
}

// Dedup removes points that land on the same rounded millisecond, keeping the
// first of each group. Run it before Simplify; duplicate timestamps produce
// zero-length spans and are rejected by most players.
func Dedup(pts []Point) []Point {
	if len(pts) < 2 {
		return append([]Point(nil), pts...)
	}
	out := make([]Point, 0, len(pts))
	out = append(out, pts[0])
	last := int64(pts[0].TMs + 0.5)
	for _, p := range pts[1:] {
		ms := int64(p.TMs + 0.5)
		if ms == last {
			continue
		}
		out = append(out, p)
		last = ms
	}
	return out
}

func allIndices(n int) []int {
	out := make([]int, n)
	for i := range out {
		out[i] = i
	}
	return out
}
