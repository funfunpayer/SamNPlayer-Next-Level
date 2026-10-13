//go:build cgo && opencv

package trackcv

import (
	"math"

	"github.com/funfunpayer/SamNPlayer/generator/trackutil"
)

func (o Options) appearanceMemoryEnabled() bool {
	return o.AppearanceMemory
}

// recoverOrCoast: on a successful update, remember + reset coast. On miss,
// try appearance reacquire (same partner/tip ID slot — re-init tracker), else
// leave ok=false for the caller to coast or exclude.
//
// Hardening (parity with TrackROI's long-clip drift defenses):
//   - dispGuard treats an ok=true but implausible single-frame jump as a loss
//     so reacquire/coast can recover instead of accepting a teleport.
//   - periodic remember() is gated by matchesOriginal so a slow CSRT drift
//     cannot poison the recovery bank (then reacquire onto the wrong patch).
//   - reacquire itself rejects candidates that fail matchesOriginal when a
//     trusted templates[0] exists (see appearanceMemory.reacquire).
func recoverOrCoast(
	cap *VideoCapture,
	gray *Gray,
	tr *Tracker,
	mem *appearanceMemory,
	coast *trackutil.Coast,
	guard *dispGuard,
	newBox Rect,
	ok bool,
	prev Rect,
	frameIdx int,
) (okOut bool, box Rect, tracker *Tracker) {
	tracker = tr
	if ok {
		d := math.Hypot(
			float64(newBox.X+newBox.W/2-prev.X-prev.W/2),
			float64(newBox.Y+newBox.H/2-prev.Y-prev.H/2),
		)
		if guard != nil && guard.implausible(d) {
			ok = false
		} else {
			if guard != nil {
				guard.accept(d)
			}
			box = newBox
			coast.ObserveOK(box.X, box.Y, box.W, box.H)
			if mem != nil && frameIdx%rememberEveryNFrames == 0 && gray != nil {
				if score, known := mem.matchesOriginal(gray, box); !known || score >= mem.minScore {
					mem.remember(gray, box)
				}
			}
			return true, box, tracker
		}
	}
	if mem != nil && gray != nil {
		if found, reacquired := mem.reacquire(gray); reacquired {
			if tracker != nil {
				tracker.Close()
			}
			tracker = NewTracker()
			tracker.Init(cap, found)
			coast.Reset()
			coast.ObserveOK(found.X, found.Y, found.W, found.H)
			if guard != nil {
				// Fresh motion baseline after a deliberate re-anchor.
				*guard = dispGuard{}
			}
			return true, found, tracker
		}
	}
	return false, prev, tracker
}

// seedAppearanceMemory stores the frame-0 crop as templates[0] — the
// user-confirmed tip/partner region — before any CSRT update can drift.
func seedAppearanceMemory(mem *appearanceMemory, gray *Gray, box Rect) {
	if mem == nil || gray == nil {
		return
	}
	mem.remember(gray, box)
}
