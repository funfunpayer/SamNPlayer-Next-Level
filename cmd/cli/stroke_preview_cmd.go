package main

import (
	"context"
	"encoding/json"
	"flag"
	"fmt"
	"os"
	"time"

	"github.com/funfunpayer/SamNPlayer/generator/strokepreview"
)

// runStrokePreview: sparse extrema / cut / pan probe (Stage A).
// Usage: samnplayer stroke-preview VIDEO [--json] [--max-seconds N]
func runStrokePreview(args []string) int {
	return runStrokePreviewContext(context.Background(), args)
}

type strokePreviewAnalyzeFunc func(context.Context, string, strokepreview.Options) (strokepreview.Report, error)

func runStrokePreviewContext(ctx context.Context, args []string) int {
	return runStrokePreviewWithContext(ctx, args, strokepreview.Analyze)
}

func runStrokePreviewWithContext(ctx context.Context, args []string, analyze strokePreviewAnalyzeFunc) int {
	fs := flag.NewFlagSet("stroke-preview", flag.ContinueOnError)
	fs.SetOutput(os.Stderr)
	asJSON := fs.Bool("json", false, "emit Report JSON")
	maxSec := fs.Float64("max-seconds", 0, "stop after N seconds (0 = full)")
	maxWidth := fs.Int("max-width", 320, "analysis max width")
	analysisFPS := fs.Float64("fps", 12, "analysis FPS before sparse keep")
	sampleEvery := fs.Int("sample-every", 2, "keep 1 of N analysis frames")
	var paths, flagArgs []string
	paths, flagArgs = splitCLIArgs(fs, args)
	if err := fs.Parse(flagArgs); err != nil {
		return 2
	}
	if len(paths) != 1 {
		fmt.Fprintf(os.Stderr, "Usage: %s stroke-preview VIDEO [--json] [--max-seconds N]\n", os.Args[0])
		return 2
	}
	video := paths[0]

	ctx, cancel := context.WithTimeout(ctx, 10*time.Minute)
	defer cancel()
	rep, err := analyze(ctx, video, strokepreview.Options{
		MaxWidth:    *maxWidth,
		AnalysisFPS: *analysisFPS,
		SampleEvery: *sampleEvery,
		MaxSeconds:  *maxSec,
	})
	if err != nil {
		fmt.Fprintf(os.Stderr, "stroke-preview: %v\n", err)
		return 1
	}
	if *asJSON {
		enc := json.NewEncoder(os.Stdout)
		enc.SetIndent("", "  ")
		_ = enc.Encode(rep)
		return 0
	}
	fmt.Printf("stroke-preview  quality=%s  suggest_audio=%v\n", rep.Quality, rep.SuggestAudio)
	fmt.Printf("  duration≈%dms  samples=%d  wall=%dms  step≈%.0fms\n",
		rep.DurationMs, rep.SampleCount, rep.ElapsedWallMs, rep.SampleStepMs)
	fmt.Printf("  extrema up=%d down=%d  stroke_hz=%.2f  span=%.4f\n",
		rep.UpCount, rep.DownCount, rep.StrokeHz, rep.SignalSpan)
	fmt.Printf("  cuts=%d (%.1f/min)  pan_share=%.2f\n",
		len(rep.Cuts), rep.CutRatePerMin, rep.PanShare)
	if rep.Reason != "" {
		fmt.Printf("  reason: %s\n", rep.Reason)
	}
	show := 8
	if len(rep.Extrema) < show {
		show = len(rep.Extrema)
	}
	for i := 0; i < show; i++ {
		e := rep.Extrema[i]
		fmt.Printf("  %s @ %dms\n", e.Kind, e.AtMs)
	}
	if len(rep.Extrema) > show {
		fmt.Printf("  … %d more extrema\n", len(rep.Extrema)-show)
	}
	return 0
}
