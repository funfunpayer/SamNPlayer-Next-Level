package funscript

import "fmt"

// PairLabel is the KI-ready gut/nicht-gut classification for one
// reference×candidate comparison (Owner Bench flow + training labels).
type PairLabel string

const (
	LabelGood   PairLabel = "good"   // gut — both Signal Quality and Motion Fidelity clear
	LabelReview PairLabel = "review" // prüfen — usable but timing/quality needs a look
	LabelBad    PairLabel = "bad"    // nicht gut — shape/undefined or hard fail
)

// PairScore is the Owner-facing "is this generated script good?" result.
// Reuses EvaluateMotionFidelity (FunGen/ref compare) + EvaluateScriptQuality
// (Quality Doctor on the candidate). Video is optional metadata for KI
// datasets — scoring itself is script-vs-script.
type PairScore struct {
	Kind        string               `json:"kind"` // always "pair_score"
	Label       PairLabel            `json:"label"`
	Passed      bool                 `json:"passed"` // true only when label == good
	Detail      string               `json:"detail"`
	Video       string               `json:"video,omitempty"`
	Reference   string               `json:"reference,omitempty"`
	Candidate   string               `json:"candidate,omitempty"`
	Fidelity    MotionFidelityResult `json:"fidelity"`
	Quality     ScriptQualityResult  `json:"quality"`
	MinAlignedR float64              `json:"min_aligned_r"`
}

// ScorePair judges a candidate against a FunGen (or other) reference.
// maxLagMs/lagStepMs/resampleStepMs follow BestLagCorrelation defaults when ≤0.
func ScorePair(reference, candidate []Action, maxLagMs, lagStepMs, resampleStepMs int) PairScore {
	if maxLagMs <= 0 {
		maxLagMs = DefaultMaxLagMs
	}
	if lagStepMs <= 0 {
		lagStepMs = DefaultLagStepMs
	}
	if resampleStepMs <= 0 {
		resampleStepMs = DefaultResampleStepMs
	}

	fidelity := EvaluateMotionFidelity(reference, candidate, maxLagMs, lagStepMs, resampleStepMs)
	quality := EvaluateScriptQuality(candidate)

	out := PairScore{
		Kind:        "pair_score",
		Fidelity:    fidelity,
		Quality:     quality,
		MinAlignedR: DefaultMinAlignedR,
	}
	out.Label, out.Passed, out.Detail = judgePair(fidelity, quality)
	return out
}

// judgePair maps existing engine verdicts onto good/review/bad.
// Thresholds stay aligned with DiagnosePhase (DefaultMinAlignedR=0.70) and
// Quality Doctor (passed when score≥0.5 and no hard fail).
func judgePair(fidelity MotionFidelityResult, quality ScriptQualityResult) (PairLabel, bool, string) {
	verdict := fidelity.Diagnosis.Verdict
	r := 0.0
	hasR := fidelity.R != nil
	if hasR {
		r = *fidelity.R
	}

	switch {
	case verdict == PhaseOK && hasR && r >= DefaultMinAlignedR && !fidelity.LowConfidence && quality.Passed:
		return LabelGood, true, "gut: Motion Fidelity ok und Quality Doctor bestanden"
	case verdict == PhaseUndefined || !hasR:
		return LabelBad, false, "nicht gut: keine verwertbare Korrelation zur Referenz"
	case verdict == PhaseShape || (hasR && r < 0.40):
		detail := "nicht gut: Form/Wahrnehmung weicht stark von der Referenz ab"
		if !quality.Passed {
			detail += "; Quality Doctor ebenfalls nicht bestanden"
		}
		return LabelBad, false, detail
	case verdict == PhaseTiming:
		return LabelReview, false, fmt.Sprintf(
			"prüfen: Kurve passt nach Lag-Ausrichtung (r=%.3f) — Timing/Phase prüfen", r)
	case fidelity.LowConfidence:
		return LabelReview, false, fmt.Sprintf(
			"prüfen: Korrelation r=%.3f bei geringer Stichprobe", r)
	case !quality.Passed:
		return LabelReview, false, fmt.Sprintf(
			"prüfen: Referenz-Korrelation r=%.3f ok-ish, aber Quality Doctor nicht bestanden", r)
	default:
		return LabelReview, false, fmt.Sprintf(
			"prüfen: Zwischenfall (verdict=%s r=%.3f quality_passed=%v)", verdict, r, quality.Passed)
	}
}
