package main

import (
	"flag"
	"fmt"
	"os"
	"path/filepath"
	"strings"

	"github.com/funfunpayer/SamNPlayer/funscript"
	"github.com/funfunpayer/SamNPlayer/sam"
)

// runSam: erste Stufe SAM — Funscript → angereichertes .sam
// (Confidence/Intensity/Range/Velocity aus Gaps + Tf/Contact-Rezept).
// Paths may appear before or after flags (same as phase/compare).
func runSam(args []string) int {
	fs := flag.NewFlagSet("sam", flag.ContinueOnError)
	fs.SetOutput(os.Stderr)
	out := fs.String("output", "", "Ausgabe-.sam (Default: Eingabe mit .sam)")
	thin := fs.Bool("thin", false, "Nur Position (FromFunscript), ohne Enrich")
	fs.Usage = func() {
		fmt.Fprintf(os.Stderr, "Usage: %s sam FILE.funscript [--output FILE.sam] [--thin]\n\n", os.Args[0])
		fmt.Fprintf(os.Stderr, "Erste SAM-Stufe: schreibt Velocity, Confidence (Tracking-Gaps),\n")
		fmt.Fprintf(os.Stderr, "bei Tf/Tj+Kontakt auch Intensity/Range — ohne GUI-Umbau.\n")
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
	inPath := paths[0]
	script, err := funscript.Load(inPath)
	if err != nil {
		fmt.Fprintf(os.Stderr, "Fehler: %v\n", err)
		return 1
	}
	var s *sam.Script
	if *thin {
		s = sam.FromFunscript(script)
	} else {
		s = sam.FromFunscriptEnriched(script)
	}
	outPath := *out
	if outPath == "" {
		outPath = strings.TrimSuffix(inPath, filepath.Ext(inPath)) + ".sam"
	}
	if err := s.Save(outPath); err != nil {
		fmt.Fprintf(os.Stderr, "Fehler beim Speichern: %v\n", err)
		return 1
	}
	var withIntensity, lowConf int
	for _, f := range s.Frames {
		if f.Motion.Intensity > 0.001 {
			withIntensity++
		}
		if !*thin && f.Motion.Confidence == 0 {
			lowConf++
		}
	}
	fmt.Printf("SAM geschrieben: %s (%d frames, profile=%q, contact_intensity_frames=%d, gap_frames=%d)\n",
		outPath, len(s.Frames), s.Metadata.Profile, withIntensity, lowConf)
	return 0
}
