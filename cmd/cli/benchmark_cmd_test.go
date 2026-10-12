package main

import (
	"encoding/json"
	"math"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"

	"github.com/funfunpayer/SamNPlayer/funscript"
)

func writeTestFunscript(t *testing.T, dir, name string, actions []funscript.Action) string {
	t.Helper()
	path := filepath.Join(dir, name)
	s := funscript.Script{Actions: actions}
	data, err := json.Marshal(s)
	if err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(path, data, 0o644); err != nil {
		t.Fatal(err)
	}
	return path
}

func benchSineActions(durationMs, periodMs, stepMs int, t0 int64, amplitude, center float64) []funscript.Action {
	var actions []funscript.Action
	for t := 0; t <= durationMs; t += stepMs {
		pos := center + amplitude*math.Sin(2*math.Pi*float64(t)/float64(periodMs))
		actions = append(actions, funscript.Action{At: t0 + int64(t), Pos: int(math.Round(pos))})
	}
	return actions
}

func TestRunBenchmarkIdenticalExit0(t *testing.T) {
	dir := t.TempDir()
	actions := benchSineActions(20000, 2000, 40, 0, 40, 50)
	ref := writeTestFunscript(t, dir, "ref.funscript", actions)
	cand := writeTestFunscript(t, dir, "cand.funscript", actions)
	labels := filepath.Join(dir, "labels.jsonl")

	code := runBenchmark([]string{
		"--reference", ref,
		"--candidate", cand,
		"--json",
		"--labels-out", labels,
	})
	if code != 0 {
		t.Fatalf("exit=%d, want 0 for identical pair", code)
	}
	data, err := os.ReadFile(labels)
	if err != nil {
		t.Fatal(err)
	}
	var rec benchmarkLabelRecord
	if err := json.Unmarshal(data, &rec); err != nil {
		t.Fatal(err)
	}
	if rec.Label != funscript.LabelGood || !rec.Passed {
		t.Fatalf("label record: %+v", rec)
	}
	if rec.Kind != "benchmark_pair_label" {
		t.Fatalf("kind=%q", rec.Kind)
	}
}

func TestRunBenchmarkShiftedExitNonZero(t *testing.T) {
	dir := t.TempDir()
	ref := writeTestFunscript(t, dir, "ref.funscript", benchSineActions(20000, 2000, 40, 0, 40, 50))
	cand := writeTestFunscript(t, dir, "cand.funscript", benchSineActions(20000, 2000, 40, 800, 40, 50))
	code := runBenchmark([]string{ref, cand, "--max-lag-ms", "2000"})
	if code == 0 {
		t.Fatal("timing-shifted pair must not exit 0")
	}
}

func TestRunBenchmarkMissingArgs(t *testing.T) {
	if code := runBenchmark(nil); code != 2 {
		t.Fatalf("exit=%d, want 2", code)
	}
}

func TestRunBenchmarkMixedPositionalAndNamedPaths(t *testing.T) {
	dir := t.TempDir()
	actions := benchSineActions(20000, 2000, 40, 0, 40, 50)
	ref := writeTestFunscript(t, dir, "ref.funscript", actions)
	cand := writeTestFunscript(t, dir, "cand.funscript", actions)

	for name, args := range map[string][]string{
		"named reference": {"--reference", ref, cand},
		"named candidate": {ref, "--candidate", cand},
	} {
		t.Run(name, func(t *testing.T) {
			if code := runBenchmark(args); code != 0 {
				t.Fatalf("exit=%d, want 0", code)
			}
		})
	}
}

func TestRunBenchmarkRejectsExtraPositionalPath(t *testing.T) {
	dir := t.TempDir()
	actions := benchSineActions(20000, 2000, 40, 0, 40, 50)
	ref := writeTestFunscript(t, dir, "ref.funscript", actions)
	cand := writeTestFunscript(t, dir, "cand.funscript", actions)

	if code := runBenchmark([]string{"--reference", ref, "--candidate", cand, "extra.funscript"}); code != 2 {
		t.Fatalf("exit=%d, want 2", code)
	}
}

func TestRunBenchmarkGoldenClipFixture(t *testing.T) {
	root := findRepoRoot(t)
	ref := filepath.Join(root, "generator/testdata/golden_clips/clip_ausschnitt_native/mit_yolo/clip_ausschnitt.funscript")
	cand := filepath.Join(root, "generator/testdata/golden_clips/clip_ausschnitt_native/mit_yolo/clip_ausschnitt__hub.funscript")
	if _, err := os.Stat(ref); err != nil {
		t.Skip("golden fixture missing:", err)
	}
	code := runBenchmark([]string{"--reference", ref, "--candidate", cand, "--json", "--max-lag-ms", "1000"})
	if code != 0 && code != 1 {
		t.Fatalf("exit=%d, want 0 or 1", code)
	}
}

func findRepoRoot(t *testing.T) string {
	t.Helper()
	if out, err := exec.Command("git", "rev-parse", "--show-toplevel").Output(); err == nil {
		return strings.TrimSpace(string(out))
	}
	wd, err := os.Getwd()
	if err != nil {
		t.Fatal(err)
	}
	dir := wd
	for i := 0; i < 6; i++ {
		if _, err := os.Stat(filepath.Join(dir, "go.mod")); err == nil {
			return dir
		}
		dir = filepath.Dir(dir)
	}
	t.Fatal("repo root not found from", wd)
	return ""
}
