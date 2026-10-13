//go:build cgo && opencv

package trackcv

import "sort"

// dispGuard flags an implausible single-frame jump out of a SUCCESSFUL
// tracker.Update() (ok=true): in principle OpenCV's CSRT can report
// success while the returned box has actually snapped onto a completely
// different, unrelated location - its own internal correlation filter
// locking onto a different, higher-scoring patch elsewhere in the frame,
// no scene cut or reported loss involved. Investigating a real clip's
// "~1000px long-clip drift" (docs/AGENT_COORD.md, 23 Sep) found two
// single-frame position jumps of 107px and 295px - but debug
// instrumentation traced those to a DIFFERENT mechanism (tracker.Update()
// genuinely, permanently lost the target; the appearance-memory
// reacquire() fallback then matched onto the wrong region because its
// own remembered templates were already poisoned by prior gradual drift
// - see appearance_memory.go's matchesOriginal, the actual fix for that
// clip's jumps). dispGuard never fired on that clip. It stays in as a
// second, independent line of defense against the ok=true-with-a-bad-box
// case specifically, which this investigation didn't rule out in
// general - cheap, and every genuine motion across that same ~6700-frame
// clip stayed under ~50px in a single ~40ms frame, so it has clean room
// to trigger without touching real motion if that case ever does occur.
//
// A fixed pixel threshold would need separate tuning per clip/ROI size -
// too low and it clips real fast motion, too high and it misses a slow
// clip's teleport. Comparing each frame's displacement against a short
// rolling median of its own recent (accepted) history adapts
// automatically instead: real motion raises the local median together
// with the frame that's part of it, so the ratio test stays sane; an
// isolated teleport frame stands far outside whatever the surrounding
// motion actually was, regardless of the clip's general pace.
type dispGuard struct {
	recent []float64
}

const (
	// dispGuardWindow: how many recent accepted per-frame displacements
	// feed the rolling median.
	dispGuardWindow = 20
	// dispGuardRatio: a displacement must exceed this multiple of the
	// recent median to be flagged.
	dispGuardRatio = 8.0
	// dispGuardFloorPx: never flag a displacement below this many px,
	// regardless of ratio - protects near-static passages where the
	// median sits close to 0 (a tiny ratio there would flag ordinary
	// jitter). Comfortably above the largest genuine single-frame motion
	// measured on the real clip above (~50px) and comfortably below the
	// smallest of its two position jumps (107px) - see the package
	// comment for why those turned out not to be dispGuard's case, but
	// they're still the only real-world scale reference available.
	dispGuardFloorPx = 60.0
)

// implausible reports whether d is too large a jump to be genuine tracker
// motion, given the recent accepted displacement history.
func (g *dispGuard) implausible(d float64) bool {
	if d < dispGuardFloorPx || len(g.recent) == 0 {
		return false
	}
	return d > dispGuardRatio*medianOf(g.recent)
}

// accept records a legitimate displacement into the rolling window. Call
// only for a displacement implausible did NOT flag, so a rejected jump
// never skews the baseline it was judged against.
func (g *dispGuard) accept(d float64) {
	g.recent = append(g.recent, d)
	if len(g.recent) > dispGuardWindow {
		g.recent = g.recent[1:]
	}
}

func medianOf(v []float64) float64 {
	s := append([]float64(nil), v...)
	sort.Float64s(s)
	n := len(s)
	if n%2 == 1 {
		return s[n/2]
	}
	return (s[n/2-1] + s[n/2]) / 2
}
