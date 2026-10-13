package main

import (
	"encoding/json"
	"flag"
	"fmt"
	"os"

	"github.com/funfunpayer/SamNPlayer/generator"
)

// runScanSceneMap: the rhythm-grid quick scan (what moves, per cell and
// window) as JSON - the motion input for scene_roles.py
// (docs/SCENE_UNDERSTANDING_PLAN.md stage 2). Needs the OpenCV build.
func runScanSceneMap(args []string) int {
	fs := flag.NewFlagSet("scan-scene-map", flag.ContinueOnError)
	fs.SetOutput(os.Stderr)
	windows := fs.Int("windows", generator.DefaultSceneMapWindows, "8 s windows spread over the clip (about duration/10 s for roles)")
	out := fs.String("out", "", "output JSON (default: stdout)")
	fs.Usage = func() {
		fmt.Fprintf(os.Stderr, "Usage:\n  %s scan-scene-map VIDEO [--windows N] [--out FILE]\n\n", os.Args[0])
		fs.PrintDefaults()
	}
	paths, flagArgs := splitCLIArgs(fs, args)
	if err := fs.Parse(flagArgs); err != nil {
		return 2
	}
	if len(paths) != 1 {
		fs.Usage()
		return 2
	}
	m, err := generator.ScanSceneMap(paths[0], *windows)
	if err != nil {
		fmt.Fprintf(os.Stderr, "Fehler: %v\n", err)
		return 1
	}
	b, err := json.Marshal(m)
	if err != nil {
		fmt.Fprintf(os.Stderr, "Fehler: %v\n", err)
		return 1
	}
	if *out == "" {
		fmt.Println(string(b))
		return 0
	}
	if err := os.WriteFile(*out, b, 0o644); err != nil {
		fmt.Fprintf(os.Stderr, "Fehler: %v\n", err)
		return 1
	}
	fmt.Fprintf(os.Stderr, "%d windows written to %s\n", len(m.Windows), *out)
	return 0
}
