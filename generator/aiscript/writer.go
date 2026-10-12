// Package aiscript is the opt-in AI draft script path (Owner 26 Sep 2026).
//
// Everyday Create still uses classical CSRT. This package becomes Available
// when a local imitation library has ≥1 exported classical sample (S2
// experimental) or, later, when an ONNX draft model is configured.
//
// See docs/AI_SCRIPT_WRITER.md.
package aiscript

import (
	"fmt"
	"path/filepath"
)

// Status reports whether a local AI draft writer can run.
type Status struct {
	Available   bool   `json:"available"`
	Reason      string `json:"reason"`
	ModelPath   string `json:"modelPath,omitempty"`
	Stage       string `json:"stage"` // e.g. "S1", "S2-imitation"
	SampleCount int    `json:"sampleCount,omitempty"`
}

// DraftRequest is the input for an experimental AI stroke draft.
type DraftRequest struct {
	VideoPath string `json:"videoPath"`
	ModelPath string `json:"modelPath,omitempty"`
	// ImitationDir is where S1 Export classical run wrote JSON samples.
	ImitationDir string `json:"imitationDir,omitempty"`
	// DurationMs optional target length; 0 = keep source sample span.
	DurationMs int64 `json:"durationMs,omitempty"`
	// TipROI optional seed box in pixel coords (x,y,w,h). Zero = unused.
	TipX float64 `json:"tipX,omitempty"`
	TipY float64 `json:"tipY,omitempty"`
	TipW float64 `json:"tipW,omitempty"`
	TipH float64 `json:"tipH,omitempty"`
}

// DraftResult is a proposed stroke only — caller must run Quality Doctor
// and require an explicit Keep in the GUI before replacing a script.
type DraftResult struct {
	Actions  []Action `json:"actions"`
	Warnings []string `json:"warnings,omitempty"`
	Engine   string   `json:"engine"`
	Notes    string   `json:"notes,omitempty"`
	// QD verdict on the draft (actions-only); Keep still required.
	QDPassed   *bool    `json:"qdPassed,omitempty"`
	QDScore    *float64 `json:"qdScore,omitempty"`
	QDWarnings []string `json:"qdWarnings,omitempty"`
	SourcePath string   `json:"sourcePath,omitempty"`
}

// Action mirrors funscript time/pos without importing funscript here
// (keeps the package leaf-light for early stages).
type Action struct {
	At  int64 `json:"at"`
	Pos int   `json:"pos"`
}

// StatusFor returns availability for a model path alone (no imitation dir).
// Prefer StatusWithLibrary when the GUI knows the S1 export folder.
func StatusFor(modelPath string) Status {
	return StatusWithLibrary(modelPath, "")
}

// StatusWithLibrary reports availability from imitation samples and/or model.
func StatusWithLibrary(modelPath, imitationDir string) Status {
	n := 0
	if imitationDir != "" {
		n = CountValidSamples(imitationDir)
	}
	if n > 0 {
		return Status{
			Available:   true,
			Reason:      fmt.Sprintf("Imitation library ready (%d sample(s)). Experimental draft stretches the best duration + tip-aspect match — Everyday Create stays CSRT. Keep required after Quality Doctor.", n),
			ModelPath:   modelPath,
			Stage:       "S2-imitation",
			SampleCount: n,
		}
	}
	if modelPath == "" {
		return Status{
			Available: false,
			Reason:    "No AI draft model yet. Export classical good runs (Advanced → Export classical run) to build a local imitation library, then draft becomes available. Everyday Create stays CSRT.",
			Stage:     "S1",
		}
	}
	// Future ONNX path: refuse until inference ships so we never pretend.
	return Status{
		Available: false,
		Reason:    "AI draft model path set, but ONNX draft inference is not implemented yet. Export classical runs to use the imitation library (S2-imitation) instead. Everyday CSRT unchanged.",
		ModelPath: modelPath,
		Stage:     "S1",
	}
}

// Draft runs the experimental writer. Uses the local imitation library when
// samples exist; otherwise fails closed.
func Draft(req DraftRequest) (DraftResult, error) {
	st := StatusWithLibrary(req.ModelPath, req.ImitationDir)
	if !st.Available {
		return DraftResult{}, fmt.Errorf("aiscript: %s", st.Reason)
	}
	if st.Stage == "S2-imitation" {
		return draftFromImitation(req)
	}
	return DraftResult{}, fmt.Errorf("aiscript: draft engine %q not implemented", st.Stage)
}

func draftFromImitation(req DraftRequest) (DraftResult, error) {
	dir := req.ImitationDir
	if dir == "" {
		dir = DefaultImitationDir()
	}
	samples, err := LoadImitationSamples(dir)
	if err != nil {
		return DraftResult{}, err
	}
	if len(samples) == 0 {
		return DraftResult{}, fmt.Errorf("aiscript: no imitation samples under %s — export a classical run first", dir)
	}
	best, err := PickBestSample(samples, req.DurationMs, req.TipW, req.TipH)
	if err != nil {
		return DraftResult{}, err
	}
	actions := StretchActions(best.Sample.Actions, req.DurationMs)
	if len(actions) < 2 {
		return DraftResult{}, fmt.Errorf("aiscript: stretched draft too short")
	}
	notes := fmt.Sprintf("Stretched from %s (source %dms → target %dms). Experimental imitation draft — not Everyday CSRT.",
		filepath.Base(best.Path), best.Sample.DurationMs, req.DurationMs)
	if req.DurationMs <= 0 {
		notes = fmt.Sprintf("Copied from %s (no target duration). Experimental imitation draft — not Everyday CSRT.",
			filepath.Base(best.Path))
	}
	if TipAspect(req.TipW, req.TipH) > 0 && TipAspect(best.Sample.TipW, best.Sample.TipH) > 0 {
		notes += fmt.Sprintf(" Tip aspect matched (request %.2f vs sample %.2f).",
			TipAspect(req.TipW, req.TipH), TipAspect(best.Sample.TipW, best.Sample.TipH))
	}
	warn := []string{
		"Experimental: draft is a duration + tip-aspect classical imitation, not a vision model.",
		"Review Quality Doctor, then Keep draft or Discard. Everyday Create path unchanged.",
	}
	return DraftResult{
		Actions:    actions,
		Warnings:   warn,
		Engine:     "imitation-stretch",
		Notes:      notes,
		SourcePath: best.Path,
	}, nil
}
