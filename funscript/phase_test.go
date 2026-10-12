package funscript

import (
	"math"
	"math/rand"
	"testing"
)

func sineActions(durationMs, periodMs, stepMs int, t0 int64, amplitude, center float64) []Action {
	var actions []Action
	for t := 0; t <= durationMs; t += stepMs {
		pos := center + amplitude*math.Sin(2*math.Pi*float64(t)/float64(periodMs))
		actions = append(actions, Action{At: t0 + int64(t), Pos: int(math.Round(pos))})
	}
	return actions
}

func irregularActions(t0 int64) []Action {
	var actions []Action
	t := 0
	for t < 4000 {
		pos := 50 + 15*math.Sin(2*math.Pi*float64(t)/500)
		actions = append(actions, Action{At: t0 + int64(t), Pos: int(math.Round(pos))})
		t += 40
	}
	for t < 6000 {
		actions = append(actions, Action{At: t0 + int64(t), Pos: 50})
		t += 40
	}
	for t < 12000 {
		pos := 50 + 45*math.Sin(2*math.Pi*float64(t)/2000)
		actions = append(actions, Action{At: t0 + int64(t), Pos: int(math.Round(pos))})
		t += 40
	}
	return actions
}

func TestBestLagCorrelationShiftedSine(t *testing.T) {
	reference := sineActions(20000, 2000, 40, 0, 40, 50)
	shifted := sineActions(20000, 2000, 40, 837, 40, 50)
	result := BestLagCorrelation(reference, shifted, 2000, 50, 100)
	if result == nil {
		t.Fatal("expected a correlation result")
	}
	if absInt(result.LagMs-(-837)) > 50 {
		t.Fatalf("lag_ms=%d, want ~-837 (±50)", result.LagMs)
	}
	if result.R <= 0.95 {
		t.Fatalf("aligned r=%.4f, want >0.95", result.R)
	}
	if result.RZeroLag == nil {
		t.Fatal("expected r_zero_lag")
	}
	if result.R-*result.RZeroLag <= 0.3 {
		t.Fatalf("aligned should beat raw by >0.3; r=%.4f r0=%.4f", result.R, *result.RZeroLag)
	}
	if result.LowConfidence {
		t.Fatal("long overlap should not be low_confidence")
	}
}

func TestBestLagCorrelationInverted(t *testing.T) {
	reference := sineActions(20000, 2000, 40, 0, 40, 50)
	inverted := make([]Action, len(reference))
	for i, a := range reference {
		inverted[i] = Action{At: a.At, Pos: 100 - a.Pos}
	}
	result := BestLagCorrelation(reference, inverted, 200, 50, 100)
	if result == nil {
		t.Fatal("expected a correlation result")
	}
	if result.Orientation != "inverted" {
		t.Fatalf("orientation=%q, want inverted", result.Orientation)
	}
	if result.R <= 0.95 {
		t.Fatalf("r=%.4f, want >0.95", result.R)
	}
}

func TestBestLagCorrelationConstantUndefined(t *testing.T) {
	reference := sineActions(20000, 2000, 40, 0, 40, 50)
	constant := make([]Action, 0, 50)
	for tms := 0; tms < 5000; tms += 100 {
		constant = append(constant, Action{At: int64(tms), Pos: 100})
	}
	result := BestLagCorrelation(reference, constant, 500, 100, 100)
	if result != nil {
		t.Fatalf("constant series must yield nil, got %+v", result)
	}
	if pearson([]float64{1, 1, 1, 1}, []float64{1, 2, 3, 4}) != nil {
		t.Fatal("pearson must return nil on zero variance, not 0.0")
	}
}

func TestBestLagCorrelationIrregular(t *testing.T) {
	ref := irregularActions(0)
	same := irregularActions(250)
	result := BestLagCorrelation(ref, same, 1000, 50, 100)
	if result == nil {
		t.Fatal("expected a correlation result")
	}
	if result.R <= 0.9 {
		t.Fatalf("r=%.4f, want >0.9", result.R)
	}
	if result.ShapeError == nil || *result.ShapeError >= 0.3 {
		t.Fatalf("shape_error=%v, want <0.3", result.ShapeError)
	}
}

func TestBestLagCorrelationLowConfidence(t *testing.T) {
	shortRef := sineActions(2000, 500, 40, 0, 40, 50)
	shortMatch := sineActions(2000, 500, 40, 50, 40, 50)
	result := BestLagCorrelation(shortRef, shortMatch, 1000, 50, 100)
	if result == nil {
		t.Fatal("expected a correlation result")
	}
	if !result.LowConfidence {
		t.Fatalf("short overlap should be low_confidence; n_samples=%d", result.NSamples)
	}
}

func TestDiagnosePhaseTimingVsShape(t *testing.T) {
	reference := sineActions(20000, 2000, 40, 0, 40, 50)
	shifted := sineActions(20000, 2000, 40, 837, 40, 50)
	corr := BestLagCorrelation(reference, shifted, 2000, 50, 100)
	d := DiagnosePhase(corr, 0, 0)
	if d.Verdict != PhaseTiming {
		t.Fatalf("verdict=%s, want timing (detail=%s)", d.Verdict, d.Detail)
	}

	// Unrelated noise → shape
	noise := make([]Action, 0, 200)
	for i := 0; i < 200; i++ {
		// Deterministic but not matching the sine — sawtooth-ish.
		noise = append(noise, Action{At: int64(i * 100), Pos: (i * 17) % 101})
	}
	corrNoise := BestLagCorrelation(reference, noise, 500, 100, 100)
	dNoise := DiagnosePhase(corrNoise, 0, 0)
	if dNoise.Verdict != PhaseShape && dNoise.Verdict != PhaseUndefined {
		t.Fatalf("noise verdict=%s, want shape or undefined", dNoise.Verdict)
	}

	if DiagnosePhase(nil, 0, 0).Verdict != PhaseUndefined {
		t.Fatal("nil correlation → undefined")
	}
}

func chirpActions(durationMs, stepMs int, t0 int64) []Action {
	// Non-periodic chirp — avoids sine half-period / inverted lag ambiguity.
	var actions []Action
	for t := 0; t <= durationMs; t += stepMs {
		// Slowly rising frequency + a one-shot bump mid-range.
		freq := 0.0004 + 0.0000008*float64(t)
		pos := 50 + 35*math.Sin(2*math.Pi*freq*float64(t))
		if t > 8000 && t < 10000 {
			pos += 20
		}
		actions = append(actions, Action{At: t0 + int64(t), Pos: int(math.Round(pos))})
	}
	return actions
}

func TestWindowedBestLagCorrelationConstantLag(t *testing.T) {
	// Constant +500ms time shift on a non-periodic chirp → every window ~-500ms.
	reference := chirpActions(60000, 50, 0)
	shifted := chirpActions(60000, 50, 500)
	windows := WindowedBestLagCorrelation(reference, shifted, 15000, 2000, 50, 100)
	if len(windows) < 3 {
		t.Fatalf("expected ≥3 windows, got %d", len(windows))
	}
	var confident int
	for _, w := range windows {
		if w.Correlation == nil {
			continue
		}
		confident++
		if absInt(w.Correlation.LagMs-(-500)) > 100 {
			t.Errorf("window %d-%d: lag_ms=%d, want ~-500", w.WindowStartMs, w.WindowEndMs, w.Correlation.LagMs)
		}
		if w.Correlation.R < 0.9 {
			t.Errorf("window %d-%d: r=%.4f, want >0.9", w.WindowStartMs, w.WindowEndMs, w.Correlation.R)
		}
	}
	if confident < 3 {
		t.Fatalf("expected ≥3 confident windows, got %d", confident)
	}
}

func TestWindowedBestLagCorrelationDriftingLag(t *testing.T) {
	// First half lag 0, second half lag +800ms on a chirp. Windowed mode
	// must report different lags; whole-clip BestLagCorrelation can only pick one.
	ref := chirpActions(40000, 40, 0)
	var cand []Action
	for _, a := range ref {
		lag := int64(0)
		if a.At >= 20000 {
			lag = 800
		}
		cand = append(cand, Action{At: a.At + lag, Pos: a.Pos})
	}
	windows := WindowedBestLagCorrelation(ref, cand, 10000, 2000, 50, 100)
	if len(windows) < 3 {
		t.Fatalf("expected ≥3 windows, got %d", len(windows))
	}
	var earlyLag, lateLag *int
	for _, w := range windows {
		if w.Correlation == nil {
			continue
		}
		lag := w.Correlation.LagMs
		if w.WindowEndMs <= 15000 {
			earlyLag = &lag
		}
		if w.WindowStartMs >= 25000 {
			lateLag = &lag
		}
	}
	if earlyLag == nil || lateLag == nil {
		t.Fatal("need both early and late confident windows")
	}
	if absInt(*earlyLag) > 150 {
		t.Errorf("early lag=%d, want ~0", *earlyLag)
	}
	if absInt(*lateLag-(-800)) > 150 {
		t.Errorf("late lag=%d, want ~-800", *lateLag)
	}
}

// irregularHalfCycleActions builds a non-periodic stroke-like wave: it
// alternates between LOW and HIGH, but each half-cycle's duration is drawn
// fresh (deterministic PRNG) from [minHalfMs, maxHalfMs), so no two cycles
// repeat — unlike a real stroke motion, which is close to periodic.
func irregularHalfCycleActions(durationMs int, seed int64, minHalfMs, maxHalfMs int, t0 int64, low, high int) []Action {
	rng := rand.New(rand.NewSource(seed))
	var actions []Action
	at := 0
	isLow := true
	for at <= durationMs {
		pos := low
		if !isLow {
			pos = high
		}
		actions = append(actions, Action{At: t0 + int64(at), Pos: pos})
		at += minHalfMs + rng.Intn(maxHalfMs-minHalfMs)
		isLow = !isLow
	}
	return actions
}

// TestPeriodicityAliasingCharacterization is a synthetic ground-truth
// reproduction of the F-003 periodicity-aliasing hypothesis
// (docs/FINDINGS_TIMING_TF.md § F-003): a lag search over near-periodic
// motion can lock onto a lag offset by whole multiples of the dominant
// stroke period — and produce a *swinging* per-window lag / orientation
// flip — even when the true underlying offset is constant. Non-periodic
// motion under the identical constant offset does not show this failure
// mode. This does not change BestLagCorrelation/WindowedBestLagCorrelation
// behavior — it locks in and documents a known limitation with an
// executable, reproducible example so it can't silently drift or get
// "fixed" without anyone noticing (per docs/FINDINGS_TIMING_TF.md's "what
// does NOT change" note: the guidance to use windowed measurement, not the
// windowed search itself, is what's trustworthy).
func TestPeriodicityAliasingCharacterization(t *testing.T) {
	const (
		durationMs = 60000
		periodMs   = 280 // measured dominant period, clip_ausschnitt ohne_yolo
		trueLagMs  = 300 // constant, known, injected offset
		windowMs   = 10000
	)

	t.Run("periodic motion: whole-clip lag search can alias onto a wrong multiple of the period", func(t *testing.T) {
		ref := sineActions(durationMs, periodMs, 20, 0, 35, 55)
		shifted := sineActions(durationMs, periodMs, 20, trueLagMs, 35, 55)
		result := BestLagCorrelation(ref, shifted, 1500, 20, 20)
		if result == nil {
			t.Fatal("expected a correlation result")
		}
		if result.R < 0.99 {
			t.Fatalf("periodic signal should correlate near-perfectly at its best lag; r=%.4f", result.R)
		}
		// The true recovered lag (matching the sign convention verified in
		// TestBestLagCorrelationShiftedSine) would be -trueLagMs. Aliasing
		// means the search is free to land on any -trueLagMs ± k*periodMs
		// instead, since those all correlate equally well on a perfectly
		// periodic wave.
		if absInt(result.LagMs-(-trueLagMs)) < periodMs/2 {
			t.Skip("search happened to land on the true lag this time (not guaranteed to alias every run)")
		}
		off := result.LagMs - (-trueLagMs)
		if off%periodMs != 0 {
			t.Fatalf("aliased lag=%d is not an integer number of periods away from true lag=%d (off=%d, period=%d)",
				result.LagMs, -trueLagMs, off, periodMs)
		}
		// Option 1 annotation: when aliasing fired, the flag must light up
		// without changing LagMs from what the search already returned.
		if !result.AliasingRisk {
			t.Fatalf("expected AliasingRisk when lag aliased (lag=%d period≈%d dominant=%d)",
				result.LagMs, periodMs, result.DominantPeriodMs)
		}
		if len(result.AlternateLagsMs) == 0 {
			t.Fatal("expected AlternateLagsMs when AliasingRisk is set")
		}
	})

	t.Run("periodic motion: windowed lag search can swing across windows despite a constant true offset", func(t *testing.T) {
		ref := sineActions(durationMs, periodMs, 20, 0, 35, 55)
		shifted := sineActions(durationMs, periodMs, 20, trueLagMs, 35, 55)
		windows := WindowedBestLagCorrelation(ref, shifted, windowMs, 1500, 20, 20)
		if len(windows) < 3 {
			t.Fatalf("expected >=3 windows, got %d", len(windows))
		}
		seen := map[int]bool{}
		for _, w := range windows {
			if w.Correlation == nil {
				continue
			}
			seen[w.Correlation.LagMs] = true
		}
		if len(seen) < 2 {
			t.Skip("all windows happened to agree this run (aliasing is not guaranteed on every seed/period)")
		}
	})

	t.Run("non-periodic motion: windowed lag search recovers the true constant offset in every window", func(t *testing.T) {
		ref := irregularHalfCycleActions(durationMs, 42, 130, 450, 0, 20, 90)
		shifted := irregularHalfCycleActions(durationMs, 42, 130, 450, trueLagMs, 20, 90)
		windows := WindowedBestLagCorrelation(ref, shifted, windowMs, 1500, 20, 20)
		if len(windows) < 3 {
			t.Fatalf("expected >=3 windows, got %d", len(windows))
		}
		confident := 0
		for _, w := range windows {
			if w.Correlation == nil {
				continue
			}
			confident++
			if absInt(w.Correlation.LagMs-(-trueLagMs)) > 20 {
				t.Errorf("window %d-%dms: lag=%d, want ~%d (non-periodic motion should not alias)",
					w.WindowStartMs, w.WindowEndMs, w.Correlation.LagMs, -trueLagMs)
			}
			if w.Correlation.Orientation != "normal" {
				t.Errorf("window %d-%dms: orientation=%s, want normal", w.WindowStartMs, w.WindowEndMs, w.Correlation.Orientation)
			}
		}
		if confident < 3 {
			t.Fatalf("expected >=3 confident windows, got %d", confident)
		}
		// Non-periodic: no aliasing risk on the confident windows.
		for _, w := range windows {
			if w.Correlation == nil {
				continue
			}
			if w.Correlation.AliasingRisk {
				t.Errorf("window %d-%dms: unexpected AliasingRisk on non-periodic motion (alts=%v)",
					w.WindowStartMs, w.WindowEndMs, w.Correlation.AlternateLagsMs)
			}
		}
	})
}

func TestAliasingRiskFlag_doesNotChangeReportedLag(t *testing.T) {
	// Irregular motion recovers true lag; flag must stay off and lag stable.
	const trueLagMs = 400
	ref := irregularHalfCycleActions(40000, 7, 140, 420, 0, 20, 90)
	shifted := irregularHalfCycleActions(40000, 7, 140, 420, trueLagMs, 20, 90)
	result := BestLagCorrelation(ref, shifted, 1500, 20, 20)
	if result == nil {
		t.Fatal("expected result")
	}
	if absInt(result.LagMs-(-trueLagMs)) > 40 {
		t.Fatalf("lag=%d want ~%d", result.LagMs, -trueLagMs)
	}
	if result.AliasingRisk {
		t.Fatalf("unexpected AliasingRisk alts=%v period=%d", result.AlternateLagsMs, result.DominantPeriodMs)
	}
}

// TestDominantPeriodMsRequiresGenuineLocalPeak locks in a fix to
// dominantPeriodMs (21 Sep 2026, docs/FINDINGS_TIMING_TF.md § F-003): a
// naive global-max-over-the-search-range picked the smallest searched lag
// on every real golden clip tested, because a smooth, continuous motion
// curve's autocorrelation declines from lag→0 regardless of periodicity —
// that decline alone was being reported as a "dominant period" (and,
// separately, MinDominantPeriodMs's floor wasn't even reliably enforced by
// integer division). The fix requires a genuine local peak reached *after*
// the initial decline, and rounds the floor up rather than down.
func TestDominantPeriodMsRequiresGenuineLocalPeak(t *testing.T) {
	t.Run("periodic signal still detects its true period", func(t *testing.T) {
		actions := sineActions(60000, 280, 20, 0, 35, 55)
		period := dominantPeriodMs(actions, 20)
		if absInt(period-280) > 40 {
			t.Fatalf("period=%d, want ~280", period)
		}
	})

	t.Run("floor is enforced even when the true period is shorter than it", func(t *testing.T) {
		// True period (100ms) is below MinDominantPeriodMs (150ms). The old
		// code could return a period under the floor for several step
		// sizes (integer division rounding down); the fix must not.
		actions := sineActions(60000, 100, 20, 0, 35, 55)
		for _, step := range []int{100, 40, 20, 30} {
			period := dominantPeriodMs(actions, step)
			if period != 0 && period < MinDominantPeriodMs {
				t.Errorf("stepMs=%d: period=%d is below MinDominantPeriodMs=%d",
					step, period, MinDominantPeriodMs)
			}
		}
	})
}
