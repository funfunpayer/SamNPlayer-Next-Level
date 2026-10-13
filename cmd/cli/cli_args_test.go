package main

import (
	"flag"
	"testing"
)

func argumentTestFlagSet() *flag.FlagSet {
	fs := flag.NewFlagSet("test", flag.ContinueOnError)
	fs.Int("max-lag-ms", 0, "")
	fs.Int("lag-step-ms", 0, "")
	fs.String("output", "", "")
	fs.Bool("thin", false, "")
	return fs
}

func TestSplitCLIArgsNegativeFlagValue(t *testing.T) {
	paths, flags := splitCLIArgs(argumentTestFlagSet(), []string{"a.funscript", "b.funscript", "--max-lag-ms", "-500"})
	if len(paths) != 2 || paths[0] != "a.funscript" || paths[1] != "b.funscript" {
		t.Fatalf("paths=%v", paths)
	}
	if len(flags) != 2 || flags[0] != "--max-lag-ms" || flags[1] != "-500" {
		t.Fatalf("flags=%v", flags)
	}
}

func TestSplitCLIArgsEqualsForm(t *testing.T) {
	paths, flags := splitCLIArgs(argumentTestFlagSet(), []string{"--max-lag-ms=-200", "ref.funscript", "cand.funscript"})
	if len(paths) != 2 {
		t.Fatalf("paths=%v", paths)
	}
	if len(flags) != 1 || flags[0] != "--max-lag-ms=-200" {
		t.Fatalf("flags=%v", flags)
	}
}

func TestSplitCLIArgsDoesNotSwallowNextOption(t *testing.T) {
	paths, flags := splitCLIArgs(argumentTestFlagSet(), []string{"--lag-step-ms", "--max-lag-ms", "100", "a.funscript", "b.funscript"})
	// --lag-step-ms has no value token; next is another option
	if len(flags) < 2 || flags[0] != "--lag-step-ms" || flags[1] != "--max-lag-ms" {
		t.Fatalf("flags=%v", flags)
	}
	if len(paths) != 2 {
		t.Fatalf("paths=%v", paths)
	}
}

func TestSplitCLIArgsDoubleDash(t *testing.T) {
	paths, flags := splitCLIArgs(argumentTestFlagSet(), []string{"--max-lag-ms", "100", "--", "-odd.funscript", "b.funscript"})
	if len(flags) != 2 {
		t.Fatalf("flags=%v", flags)
	}
	if len(paths) != 2 || paths[0] != "-odd.funscript" {
		t.Fatalf("paths=%v", paths)
	}
}

func TestSplitCLIArgsSamOutputAfterFile(t *testing.T) {
	// Regression: sam FILE --output OUT must keep --output (not swallow into default sidecar).
	paths, flags := splitCLIArgs(argumentTestFlagSet(), []string{"clip.funscript", "--output", "custom.sam", "--thin"})
	if len(paths) != 1 || paths[0] != "clip.funscript" {
		t.Fatalf("paths=%v", paths)
	}
	if len(flags) != 3 || flags[0] != "--output" || flags[1] != "custom.sam" || flags[2] != "--thin" {
		t.Fatalf("flags=%v", flags)
	}
}

func TestSplitCLIArgsBoolBeforePath(t *testing.T) {
	paths, flags := splitCLIArgs(argumentTestFlagSet(), []string{"--thin", "clip.funscript"})
	if len(paths) != 1 || paths[0] != "clip.funscript" {
		t.Fatalf("paths=%v", paths)
	}
	if len(flags) != 1 || flags[0] != "--thin" {
		t.Fatalf("flags=%v", flags)
	}
}
