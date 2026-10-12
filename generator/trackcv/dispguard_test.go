//go:build cgo && opencv

package trackcv

import "testing"

// Pure unit tests against real-clip scale numbers (docs/AGENT_COORD.md, 23
// Sep "CSRT long-clip drift"): median per-frame displacement ~1.5px,
// largest genuine single-frame motion ~41.5px across a ~6700-frame clip.
// 107px/295px are that clip's own two position jumps - dispGuard's
// package comment explains those turned out to be a different mechanism
// (bad reacquire, not this guard's case), so these are used here only as
// realistic "clearly too large" reference values, not as jumps this guard
// is claimed to have caught on that clip.

func TestDispGuardFlagsImplausibleJumpsAtRealClipScale(t *testing.T) {
	var g dispGuard
	// Feed a calm baseline like the clip's typical frame-to-frame motion.
	for _, d := range []float64{1.2, 1.5, 1.4, 1.6, 1.3, 1.5, 1.4, 1.5, 1.6, 1.4} {
		if g.implausible(d) {
			t.Fatalf("baseline displacement %.1f wrongly flagged", d)
		}
		g.accept(d)
	}
	for _, d := range []float64{106.8, 294.5} {
		if !g.implausible(d) {
			t.Errorf("real-clip-scale jump %.1fpx was not flagged", d)
		}
	}
}

func TestDispGuardAllowsLargestGenuineRealClipMotion(t *testing.T) {
	var g dispGuard
	for _, d := range []float64{1.2, 1.5, 1.4, 1.6, 1.3, 1.5, 1.4, 1.5, 1.6, 1.4} {
		g.accept(d)
	}
	// The largest single-frame motion actually measured across the whole
	// real clip (see docs/AGENT_COORD.md) - must never be flagged.
	if g.implausible(41.5) {
		t.Error("largest genuine real-clip single-frame motion (41.5px) was wrongly flagged")
	}
}

func TestDispGuardIgnoresJitterBelowFloorRegardlessOfRatio(t *testing.T) {
	var g dispGuard
	// Near-static baseline (median close to 0) - a naive ratio-only test
	// would flag almost any nonzero jitter here.
	for i := 0; i < dispGuardWindow; i++ {
		g.accept(0.1)
	}
	if g.implausible(dispGuardFloorPx - 1) {
		t.Error("displacement just below the absolute floor was flagged despite a near-zero baseline")
	}
}

func TestDispGuardRatioScalesWithFastMotion(t *testing.T) {
	var g dispGuard
	// A genuinely fast passage: several consecutive frames all show
	// elevated (but consistent, non-isolated) motion - the rolling
	// median rises with it, so a similarly-sized frame in the middle of
	// that passage must not be flagged as an outlier.
	for i := 0; i < dispGuardWindow; i++ {
		g.accept(30)
	}
	if g.implausible(35) {
		t.Error("consistent fast motion was flagged as an outlier jump")
	}
}

func TestDispGuardAcceptTrimsWindow(t *testing.T) {
	var g dispGuard
	for i := 0; i < dispGuardWindow*3; i++ {
		g.accept(1.0)
	}
	if len(g.recent) != dispGuardWindow {
		t.Errorf("rolling window not trimmed: len=%d want=%d", len(g.recent), dispGuardWindow)
	}
}

func TestDispGuardColdStartNeverFlags(t *testing.T) {
	var g dispGuard
	if g.implausible(500) {
		t.Error("a guard with no history yet must not flag (nothing to compare against)")
	}
}
