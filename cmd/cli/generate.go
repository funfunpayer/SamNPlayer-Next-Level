package main

import (
	"context"
	"flag"
	"fmt"
	"os"
	"path/filepath"
	"strconv"
	"strings"

	"github.com/funfunpayer/SamNPlayer/funscript"
	"github.com/funfunpayer/SamNPlayer/generator"
	"github.com/funfunpayer/SamNPlayer/sam"
)

// runGenerate is the headless generate path for release testing
// (same GenerateWithContext as the GUI — Go auto / Python fallback).
func runGenerate(args []string) int {
	return runGenerateContext(context.Background(), args)
}

type generateWithContextFunc func(context.Context, string, generator.ROI, string, generator.Options, func(string), func(int)) error

func runGenerateContext(ctx context.Context, args []string) int {
	return runGenerateWithContext(ctx, args, generator.GenerateWithContext)
}

func runGenerateWithContext(ctx context.Context, args []string, generate generateWithContextFunc) int {
	fs := flag.NewFlagSet("generate", flag.ContinueOnError)
	fs.SetOutput(os.Stderr)
	video := fs.String("video", "", "video path (required)")
	output := fs.String("output", "", "output .funscript path (default: next to video)")
	roiStr := fs.String("roi", "", "x,y,w,h in pixels (required)")
	backend := fs.String("backend", "csrt", "tracking backend (csrt = Go-eligible)")
	preferPython := fs.Bool("prefer-python", false, "force Python even when Go path is eligible")
	maxFrames := fs.Int("max-frames", 0, "limit frames (0 = all)")
	autoRetry := fs.Bool("auto-retry", true, "signal-param auto-retry")
	rhythmGrid := fs.Bool("rhythm-grid", false, "stroke signal from the most rhythmic flow cell near the box (drift-robust, Go CSRT only)")
	contactPoints := fs.String("contact-points", "", "contact_points.py JSON: teachers' contact points steer the rhythm grid where the box is out of reach (needs --rhythm-grid)")
	contactMinAgree := fs.Int("contact-min-agree", 0, "keep only contact points at least this many teachers agreed on")
	contactVerify := fs.Float64("contact-verify", 0, "hybrid check: keep a contact point only where the engine's own rhythm is at least this many times stronger than at its chosen cell (1.5 measured; 0 = off)")
	sceneProposals := fs.String("scene-proposals", "", "scene_roles.py <clip>.scene.json: opt-in, use the proposed primary target as --roi (and its body part as region class) when --roi is not given; the partner is only logged unless --scene-apply")
	sceneApply := fs.Bool("scene-apply", false, "with --scene-proposals: apply the AI setup automatically - also the contact partner as ROI2 (tracked) when none is set and the profile is not a Tf/Tj distance profile; everything applied is logged")
	sceneAtMs := fs.Int64("scene-at-ms", 0, "video time (ms) whose scene proposal --scene-proposals uses")
	fs.Usage = func() {
		fmt.Fprintf(os.Stderr, "Usage: %s generate --video FILE (--roi x,y,w,h | --scene-proposals FILE) [--output FILE] [options]\n", os.Args[0])
		fs.PrintDefaults()
	}
	if err := fs.Parse(args); err != nil {
		return 2
	}
	if *video == "" || (*roiStr == "" && *sceneProposals == "") {
		fs.Usage()
		return 2
	}
	var roi generator.ROI
	if *roiStr != "" {
		var err error
		if roi, err = parseROI(*roiStr); err != nil {
			fmt.Fprintln(os.Stderr, "roi:", err)
			return 2
		}
	}
	out := *output
	if out == "" {
		ext := filepath.Ext(*video)
		out = strings.TrimSuffix(*video, ext) + ".funscript"
	}
	opts := generator.Options{
		Backend:      *backend,
		PreferPython: *preferPython,
		MaxFrames:    *maxFrames,
		AutoRetry:    *autoRetry,
		RhythmGrid:   *rhythmGrid,

		ContactPointsFile:     *contactPoints,
		ContactPointsMinAgree: *contactMinAgree,
		ContactVerifyK:        *contactVerify,
	}
	if *sceneProposals != "" {
		if err := applySceneProposals(*sceneProposals, *sceneAtMs, *sceneApply, &roi, &opts,
			func(line string) { fmt.Fprintln(os.Stderr, line) }); err != nil {
			fmt.Fprintln(os.Stderr, "scene proposals:", err)
			return 2
		}
	}
	err := generate(ctx, *video, roi, out, opts,
		func(line string) { fmt.Fprintln(os.Stderr, line) },
		nil)
	if err != nil {
		fmt.Fprintln(os.Stderr, "generate:", err)
		return 1
	}
	fmt.Println(out)
	if script, err := funscript.Load(out); err == nil && funscript.IsDistanceProfile(script.Metadata.Profile) {
		if err := sam.WriteEnrichedSidecar(out, script); err != nil {
			fmt.Fprintln(os.Stderr, "sam sidecar:", err)
		} else {
			fmt.Fprintln(os.Stderr, "sam:", sam.SidecarPath(out))
		}
	}
	return 0
}

func parseROI(s string) (generator.ROI, error) {
	var roi generator.ROI
	parts := strings.Split(s, ",")
	if len(parts) != 4 {
		return roi, fmt.Errorf("must be x,y,w,h")
	}
	v := make([]int, 4)
	for i, p := range parts {
		n, err := strconv.Atoi(strings.TrimSpace(p))
		if err != nil {
			return roi, err
		}
		v[i] = n
	}
	return generator.ROI{X: v[0], Y: v[1], W: v[2], H: v[3]}, nil
}

// applySceneProposals is the explicit opt-in: the user passed the file, so
// the proposed primary target fills an empty ROI; with withPartner
// ("Apply AI setup automatically", Owner 28 Sep) the contact partner also
// fills an empty ROI2. Every value set or skipped is logged.
func applySceneProposals(path string, atMs int64, withPartner bool, roi *generator.ROI, opts *generator.Options, log func(string)) error {
	s, err := generator.LoadSceneProposals(path)
	if err != nil {
		return err
	}
	p, ok := s.At(atMs)
	if !ok {
		return fmt.Errorf("%s has no proposal", path)
	}
	for _, line := range generator.ApplySceneProposal(roi, opts, p, withPartner) {
		log("scene proposals: " + line)
	}
	return nil
}
