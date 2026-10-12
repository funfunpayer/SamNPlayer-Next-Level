// Package trackutil holds small tracking helpers shared by trackcv and
// simpletrack (no OpenCV / ffmpeg dependency).
package trackutil

// Coast keeps a box alive for a few frames after a tracker miss by advancing
// it with the last observed center velocity (ByteTrack-style low-conf coast).
// After MaxFrames consecutive misses it reports include=false so callers can
// exclude the partner from fusion (F-006). With only one prior observation
// (no velocity yet) it holds the last box still.
type Coast struct {
	MaxFrames int // 0 → DefaultCoastFrames

	lost           int
	havePos        bool
	haveVel        bool
	prevCX, prevCY float64
	vx, vy         float64
	x, y, w, h     int
}

// DefaultCoastFrames ≈ ¼ s at 30 fps — bridge brief occlusion without inventing
// a long false path.
const DefaultCoastFrames = 8

// Reset clears velocity state (e.g. after appearance reacquire re-anchors).
func (c *Coast) Reset() {
	c.lost = 0
	c.havePos = false
	c.haveVel = false
	c.vx, c.vy = 0, 0
}

// ObserveOK records a successful tracker update.
func (c *Coast) ObserveOK(x, y, w, h int) {
	cx := float64(x) + float64(w)/2
	cy := float64(y) + float64(h)/2
	if c.havePos {
		c.vx = cx - c.prevCX
		c.vy = cy - c.prevCY
		c.haveVel = true
	}
	c.prevCX, c.prevCY = cx, cy
	c.havePos = true
	c.x, c.y, c.w, c.h = x, y, w, h
	c.lost = 0
}

// OnLost advances the coasted box. include is false when the coast budget is
// exhausted or there is no prior position.
func (c *Coast) OnLost() (x, y, w, h int, include bool) {
	max := c.MaxFrames
	if max <= 0 {
		max = DefaultCoastFrames
	}
	c.lost++
	if !c.havePos || c.lost > max || c.w <= 0 || c.h <= 0 {
		return 0, 0, 0, 0, false
	}
	if c.haveVel {
		c.x += int(mathRound(c.vx))
		c.y += int(mathRound(c.vy))
		c.prevCX += c.vx
		c.prevCY += c.vy
	}
	return c.x, c.y, c.w, c.h, true
}

// LostStreak is consecutive misses since the last ObserveOK.
func (c *Coast) LostStreak() int { return c.lost }

func mathRound(v float64) float64 {
	if v < 0 {
		return float64(int(v - 0.5))
	}
	return float64(int(v + 0.5))
}
