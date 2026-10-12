package main

import (
	"flag"
	"fmt"
	"os"

	"github.com/funfunpayer/SamNPlayer/generator"
)

// runImportContactCandidates: teacher consensus (contact_points.py) ->
// reviewable "contact" region marks in a .samn scene map (author auto,
// reviewed:false). Nothing reaches training before a person confirms it.
func runImportContactCandidates(args []string) int {
	fs := flag.NewFlagSet("import-contact-candidates", flag.ContinueOnError)
	fs.SetOutput(os.Stderr)
	contact := fs.String("contact", "", "contact_points.py result (.contact.json), required")
	minAgree := fs.Int("min-agree", 2, "only points at least this many teachers agreed on")
	minGap := fs.Int64("min-gap-ms", 2000, "at most one candidate per this many ms")
	fs.Usage = func() {
		fmt.Fprintf(os.Stderr, "Usage:\n  %s import-contact-candidates FILE.samn --contact FILE.contact.json [--min-agree N]\n\n", os.Args[0])
		fmt.Fprintf(os.Stderr, "Adds teacher-consensus contact boxes as scene-map region marks\n")
		fmt.Fprintf(os.Stderr, "(author auto, reviewed:false) for review; replaces earlier unconfirmed ones.\n")
		fs.PrintDefaults()
	}
	paths, flagArgs := splitCLIArgs(fs, args)
	if err := fs.Parse(flagArgs); err != nil {
		return 2
	}
	if len(paths) != 1 || *contact == "" {
		fs.Usage()
		return 2
	}
	n, err := generator.ImportContactCandidates(paths[0], *contact, generator.ContactCandidateOptions{
		MinAgree: *minAgree, MinGapMs: *minGap,
	})
	if err != nil {
		fmt.Fprintf(os.Stderr, "Fehler: %v\n", err)
		return 1
	}
	fmt.Printf("%d contact candidates written to %s (reviewed:false - confirm them before training)\n", n, paths[0])
	return 0
}
