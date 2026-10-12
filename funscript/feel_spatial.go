package funscript

import "math"

// SpatialContactMargin expands each contact box by this fraction of its
// size (0.25 = 25% padding) so grazing still counts as contact.
const SpatialContactMargin = 0.25

// tipAtMs returns tip (x,y) nearest to atMs from trajectory, or ok=false.
func tipAtMs(tr *TrajectoryData, atMs int64) (x, y float64, ok bool) {
	if tr == nil || len(tr.Tip) == 0 {
		return 0, 0, false
	}
	best := tr.Tip[0]
	bestDist := abs64(tr.Tip[0].AtMs - atMs)
	for i := 1; i < len(tr.Tip); i++ {
		d := abs64(tr.Tip[i].AtMs - atMs)
		if d < bestDist {
			bestDist = d
			best = tr.Tip[i]
		}
	}
	// Reject samples more than 250ms away (sparse / lost track).
	if bestDist > 250 {
		return 0, 0, false
	}
	return best.X, best.Y, true
}

func abs64(v int64) int64 {
	if v < 0 {
		return -v
	}
	return v
}

// pointInExpandedBox reports whether (x,y) is inside box grown by margin.
func pointInExpandedBox(x, y float64, b ContactMarkBox, margin float64) bool {
	if b.W <= 0 || b.H <= 0 {
		return false
	}
	padX := float64(b.W) * margin
	padY := float64(b.H) * margin
	left := float64(b.X) - padX
	top := float64(b.Y) - padY
	right := float64(b.X+b.W) + padX
	bottom := float64(b.Y+b.H) + padY
	return x >= left && x <= right && y >= top && y <= bottom
}

// proximityToBox returns 1 when inside the raw box, tapering to 0 at the
// expanded margin edge; 0 when outside the margin.
func proximityToBox(x, y float64, b ContactMarkBox, margin float64) float64 {
	if b.W <= 0 || b.H <= 0 {
		return 0
	}
	if !pointInExpandedBox(x, y, b, margin) {
		return 0
	}
	// Distance outside the core box (0 inside).
	cx0, cy0 := float64(b.X), float64(b.Y)
	cx1, cy1 := float64(b.X+b.W), float64(b.Y+b.H)
	dx, dy := 0.0, 0.0
	if x < cx0 {
		dx = cx0 - x
	} else if x > cx1 {
		dx = x - cx1
	}
	if y < cy0 {
		dy = cy0 - y
	} else if y > cy1 {
		dy = y - cy1
	}
	if dx == 0 && dy == 0 {
		return 1
	}
	padX := float64(b.W) * margin
	padY := float64(b.H) * margin
	if padX <= 0 || padY <= 0 {
		return 1
	}
	// Chebyshev-ish normalize into [0,1] falloff.
	nx := dx / padX
	ny := dy / padY
	t := math.Max(nx, ny)
	if t >= 1 {
		return 0
	}
	return 1 - t
}

// SpatialContactIntensity: tip trajectory vs marked contact areas (not tip
// box). 0 when marks/trajectory missing. Feel-decouple Stage A — does not
// change the stroke curve.
func SpatialContactIntensity(marks *ContactMarks, tr *TrajectoryData, atMs int64) float64 {
	if marks == nil || !marksHasContactAreas(marks) || tr == nil {
		return 0
	}
	x, y, ok := tipAtMs(tr, atMs)
	if !ok {
		return 0
	}
	best := 0.0
	if marks.Primary != nil {
		if p := proximityToBox(x, y, *marks.Primary, SpatialContactMargin); p > best {
			best = p
		}
	}
	for _, e := range marks.Extras {
		if p := proximityToBox(x, y, e, SpatialContactMargin); p > best {
			best = p
		}
	}
	return best
}

func marksHasContactAreas(c *ContactMarks) bool {
	if c == nil {
		return false
	}
	if c.Primary != nil && c.Primary.W > 0 && c.Primary.H > 0 {
		return true
	}
	for _, e := range c.Extras {
		if e.W > 0 && e.H > 0 {
			return true
		}
	}
	return false
}
