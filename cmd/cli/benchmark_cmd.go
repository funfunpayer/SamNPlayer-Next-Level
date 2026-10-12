package main

import (
	"encoding/json"
	"flag"
	"fmt"
	"os"
	"path/filepath"
	"time"

	"github.com/funfunpayer/SamNPlayer/funscript"
)

// runBenchmark is the Owner/KI compare entry:
//
//	samnplayer benchmark --reference REF.funscript --candidate CAND.funscript
//	  [--video VIDEO] [--json] [--labels-out labels.jsonl] [--max-lag-ms N]
//
// Scores candidate vs FunGen/ref using funscript.ScorePair (Motion Fidelity +
// Quality Doctor) and optionally appends a KI-ready label line.
func runBenchmark(args []string) int {
	fs := flag.NewFlagSet("benchmark", flag.ContinueOnError)
	fs.SetOutput(os.Stderr)
	refPath := fs.String("reference", "", "FunGen / reference .funscript (required)")
	candPath := fs.String("candidate", "", "Candidate .funscript to judge (required)")
	videoPath := fs.String("video", "", "Optional source video path (metadata for KI labels)")
	jsonOut := fs.Bool("json", false, "Emit PairScore JSON on stdout")
	labelsOut := fs.String("labels-out", "", "Append one JSONL label record here (KI training)")
	maxLag := fs.Int("max-lag-ms", funscript.DefaultMaxLagMs, "Lag search window ±ms")
	lagStep := fs.Int("lag-step-ms", funscript.DefaultLagStepMs, "Lag step ms")
	resample := fs.Int("resample-ms", funscript.DefaultResampleStepMs, "Resample step ms")
	fs.Usage = func() {
		fmt.Fprintf(os.Stderr, "Usage: %s benchmark --reference REF.funscript --candidate CAND.funscript [options]\n", os.Args[0])
		fs.PrintDefaults()
	}

	paths, flagArgs := splitCLIArgs(fs, args)
	if err := fs.Parse(flagArgs); err != nil {
		return 2
	}
	// Allow positional: benchmark REF CAND, including mixed forms where one
	// side is already supplied by a named flag.
	if *refPath == "" && len(paths) > 0 {
		*refPath = paths[0]
		paths = paths[1:]
	}
	if *candPath == "" && len(paths) > 0 {
		*candPath = paths[0]
		paths = paths[1:]
	}
	if *refPath == "" || *candPath == "" {
		fs.Usage()
		return 2
	}
	if len(paths) > 0 {
		fmt.Fprintln(os.Stderr, "Unerwartete Positionsargumente:", paths)
		fs.Usage()
		return 2
	}

	ref, err := funscript.Load(*refPath)
	if err != nil {
		fmt.Fprintf(os.Stderr, "Fehler Referenz: %v\n", err)
		return 1
	}
	cand, err := funscript.Load(*candPath)
	if err != nil {
		fmt.Fprintf(os.Stderr, "Fehler Kandidat: %v\n", err)
		return 1
	}

	score := funscript.ScorePair(ref.Actions, cand.Actions, *maxLag, *lagStep, *resample)
	score.Reference = filepath.Clean(*refPath)
	score.Candidate = filepath.Clean(*candPath)
	if *videoPath != "" {
		score.Video = filepath.Clean(*videoPath)
	}

	if *labelsOut != "" {
		if err := appendBenchmarkLabel(*labelsOut, score); err != nil {
			fmt.Fprintf(os.Stderr, "labels-out fehlgeschlagen: %v\n", err)
			return 1
		}
		fmt.Fprintf(os.Stderr, "Label angehängt: %s\n", *labelsOut)
	}

	if *jsonOut {
		enc := json.NewEncoder(os.Stdout)
		enc.SetIndent("", "  ")
		if err := enc.Encode(score); err != nil {
			fmt.Fprintf(os.Stderr, "JSON: %v\n", err)
			return 1
		}
	} else {
		printBenchmarkHuman(score)
	}

	if score.Passed {
		return 0
	}
	return 1
}

func printBenchmarkHuman(score funscript.PairScore) {
	passWord := "NICHT GUT"
	switch score.Label {
	case funscript.LabelGood:
		passWord = "GUT"
	case funscript.LabelReview:
		passWord = "PRÜFEN"
	}
	fmt.Printf("kind=pair_score\n")
	fmt.Printf("label=%s\n", score.Label)
	fmt.Printf("passed=%v\n", score.Passed)
	fmt.Printf("verdict_de=%s\n", passWord)
	fmt.Printf("detail=%s\n", score.Detail)
	if score.Video != "" {
		fmt.Printf("video=%s\n", score.Video)
	}
	if score.Reference != "" {
		fmt.Printf("reference=%s\n", score.Reference)
	}
	if score.Candidate != "" {
		fmt.Printf("candidate=%s\n", score.Candidate)
	}
	fmt.Printf("fidelity_verdict=%s\n", score.Fidelity.Diagnosis.Verdict)
	if score.Fidelity.R != nil {
		fmt.Printf("r=%.4f\n", *score.Fidelity.R)
	} else {
		fmt.Printf("r=n/a\n")
	}
	if score.Fidelity.LagMs != nil {
		fmt.Printf("lag_ms=%d\n", *score.Fidelity.LagMs)
	}
	if score.Fidelity.Orientation != "" {
		fmt.Printf("orientation=%s\n", score.Fidelity.Orientation)
	}
	fmt.Printf("low_confidence=%v\n", score.Fidelity.LowConfidence)
	fmt.Printf("quality_score=%.3f\n", score.Quality.Score)
	fmt.Printf("quality_passed=%v\n", score.Quality.Passed)
	if len(score.Quality.Warnings) > 0 {
		fmt.Printf("quality_warnings=%d\n", len(score.Quality.Warnings))
		for _, w := range score.Quality.Warnings {
			fmt.Printf("  - %s\n", w)
		}
	}
}

// benchmarkLabelRecord is one JSONL line for KI training/eval datasets.
type benchmarkLabelRecord struct {
	Timestamp string              `json:"timestamp"`
	Kind      string              `json:"kind"`
	Label     funscript.PairLabel `json:"label"`
	Passed    bool                `json:"passed"`
	Video     string              `json:"video,omitempty"`
	Reference string              `json:"reference,omitempty"`
	Candidate string              `json:"candidate,omitempty"`
	Score     funscript.PairScore `json:"score"`
}

func appendBenchmarkLabel(path string, score funscript.PairScore) error {
	rec := benchmarkLabelRecord{
		Timestamp: time.Now().UTC().Format(time.RFC3339),
		Kind:      "benchmark_pair_label",
		Label:     score.Label,
		Passed:    score.Passed,
		Video:     score.Video,
		Reference: score.Reference,
		Candidate: score.Candidate,
		Score:     score,
	}
	data, err := json.Marshal(rec)
	if err != nil {
		return err
	}
	if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil && filepath.Dir(path) != "." {
		return err
	}
	f, err := os.OpenFile(path, os.O_APPEND|os.O_CREATE|os.O_WRONLY, 0o644)
	if err != nil {
		return err
	}
	defer f.Close()
	_, err = f.Write(append(data, '\n'))
	return err
}
