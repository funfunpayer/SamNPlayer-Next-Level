package funscript

import (
	"testing"
)

func TestScorePairIdenticalIsGood(t *testing.T) {
	ref := sineActions(20000, 2000, 40, 0, 40, 50)
	score := ScorePair(ref, ref, 0, 0, 0)
	if score.Kind != "pair_score" {
		t.Fatalf("kind=%q", score.Kind)
	}
	if score.Label != LabelGood || !score.Passed {
		t.Fatalf("want good/passed, got label=%s passed=%v detail=%s fidelity=%+v quality=%+v",
			score.Label, score.Passed, score.Detail, score.Fidelity, score.Quality)
	}
	if score.Fidelity.R == nil || *score.Fidelity.R < 0.99 {
		t.Fatalf("identical scripts should have r≈1, got %+v", score.Fidelity.R)
	}
}

func TestScorePairShiftedIsReviewTiming(t *testing.T) {
	ref := sineActions(20000, 2000, 40, 0, 40, 50)
	shifted := sineActions(20000, 2000, 40, 800, 40, 50)
	score := ScorePair(ref, shifted, 2000, 50, 100)
	if score.Label != LabelReview {
		t.Fatalf("want review for pure lag, got %s (%s) fidelity=%+v",
			score.Label, score.Detail, score.Fidelity.Diagnosis)
	}
	if score.Passed {
		t.Fatal("timing mismatch must not pass")
	}
	if score.Fidelity.Diagnosis.Verdict != PhaseTiming {
		t.Fatalf("verdict=%s, want timing", score.Fidelity.Diagnosis.Verdict)
	}
}

func TestScorePairUnrelatedIsBad(t *testing.T) {
	ref := sineActions(20000, 2000, 40, 0, 40, 50)
	// Flat-ish near-constant candidate → undefined/weak correlation.
	cand := make([]Action, 0, 50)
	for i := 0; i < 50; i++ {
		cand = append(cand, Action{At: int64(i * 400), Pos: 50})
	}
	score := ScorePair(ref, cand, 1000, 100, 100)
	if score.Label != LabelBad {
		t.Fatalf("want bad for constant candidate, got %s (%s)", score.Label, score.Detail)
	}
	if score.Passed {
		t.Fatal("bad pair must not pass")
	}
}

func TestScorePairQualityFailForcesReview(t *testing.T) {
	ref := sineActions(20000, 2000, 40, 0, 40, 50)
	// Same shape but unsorted / tiny span that Quality Doctor rejects.
	cand := []Action{
		{At: 100, Pos: 20},
		{At: 50, Pos: 80}, // not ascending
		{At: 200, Pos: 20},
	}
	score := ScorePair(ref[:3], cand, 1000, 100, 100)
	// Very short overlap may be bad/undefined; if it somehow correlates,
	// quality failure still blocks "good".
	if score.Label == LabelGood || score.Passed {
		t.Fatalf("broken candidate must not be good: %+v", score)
	}
}
