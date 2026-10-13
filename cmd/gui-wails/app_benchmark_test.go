//go:build suite

package main

import (
	"encoding/json"
	"fmt"
	"math"
	"os"
	"path/filepath"
	"testing"

	"github.com/funfunpayer/SamNPlayer/funscript"
)

func writeBenchmarkHistory(t *testing.T, path string, lines []string) {
	t.Helper()
	if err := os.WriteFile(path, []byte(joinLines(lines)), 0644); err != nil {
		t.Fatal(err)
	}
}

func TestGetBenchmarkHistorySortsNewestFirst(t *testing.T) {
	t.Setenv("XDG_CONFIG_HOME", t.TempDir())
	path := filepath.Join(t.TempDir(), "history.jsonl")
	writeBenchmarkHistory(t, path, []string{
		`{"timestamp":"2026-09-15T10:00:00Z","git_commit":"aaa","summary":{"total":2,"ok":2}}`,
		`{"timestamp":"2026-09-16T10:00:00Z","git_commit":"bbb","summary":{"total":2,"ok":1}}`,
	})

	a := NewApp()
	if err := a.settings.Set(prefBenchmarkHistoryPath, path); err != nil {
		t.Fatal(err)
	}

	history, err := a.GetBenchmarkHistory()
	if err != nil {
		t.Fatal(err)
	}
	if len(history) != 2 {
		t.Fatalf("erwartete 2 Läufe, got %d: %+v", len(history), history)
	}
	if history[0].GitCommit != "bbb" {
		t.Fatalf("neuester Lauf sollte zuerst kommen: %+v", history)
	}
	if history[1].GitCommit != "aaa" {
		t.Fatalf("ältester Lauf sollte zuletzt kommen: %+v", history)
	}
}

func TestGetBenchmarkHistorySkipsBrokenLinesKeepsRest(t *testing.T) {
	t.Setenv("XDG_CONFIG_HOME", t.TempDir())
	path := filepath.Join(t.TempDir(), "history.jsonl")
	writeBenchmarkHistory(t, path, []string{
		`{"timestamp":"2026-09-15T10:00:00Z","git_commit":"aaa","summary":{"total":1,"ok":1}}`,
		`nicht einmal JSON`,
		`{"timestamp":"2026-09-16T10:00:00Z","git_commit":"bbb","summary":{"total":1,"ok":1}}`,
	})

	a := NewApp()
	if err := a.settings.Set(prefBenchmarkHistoryPath, path); err != nil {
		t.Fatal(err)
	}

	history, err := a.GetBenchmarkHistory()
	if err != nil {
		t.Fatal(err)
	}
	if len(history) != 2 {
		t.Fatalf("kaputte Zeile hätte übersprungen werden müssen, got %d Läufe: %+v", len(history), history)
	}
}

func TestGetBenchmarkHistoryMissingFileIsNotAnError(t *testing.T) {
	t.Setenv("XDG_CONFIG_HOME", t.TempDir())
	a := NewApp()
	if err := a.settings.Set(prefBenchmarkHistoryPath, filepath.Join(t.TempDir(), "fehlt.jsonl")); err != nil {
		t.Fatal(err)
	}

	history, err := a.GetBenchmarkHistory()
	if err != nil {
		t.Fatal(err)
	}
	if len(history) != 0 {
		t.Fatalf("erwartete leere Liste ohne Verlaufsdatei, got %+v", history)
	}
}

func TestGetBenchmarkHistoryLimitsToMaxEntries(t *testing.T) {
	t.Setenv("XDG_CONFIG_HOME", t.TempDir())
	path := filepath.Join(t.TempDir(), "history.jsonl")
	var lines []string
	for i := 0; i < maxBenchmarkHistoryEntries+5; i++ {
		lines = append(lines, fmt.Sprintf(
			`{"timestamp":"2026-09-15T10:%02d:00Z","git_commit":"c","summary":{"total":1,"ok":1}}`, i))
	}
	writeBenchmarkHistory(t, path, lines)

	a := NewApp()
	if err := a.settings.Set(prefBenchmarkHistoryPath, path); err != nil {
		t.Fatal(err)
	}

	history, err := a.GetBenchmarkHistory()
	if err != nil {
		t.Fatal(err)
	}
	if len(history) != maxBenchmarkHistoryEntries {
		t.Fatalf("erwartete Begrenzung auf %d Einträge, got %d", maxBenchmarkHistoryEntries, len(history))
	}
}

func TestScoreScriptPairIdentical(t *testing.T) {
	t.Setenv("XDG_CONFIG_HOME", t.TempDir())
	dir := t.TempDir()
	actions := []funscript.Action{}
	for i := 0; i < 80; i++ {
		pos := 50 + int(40*math.Sin(2*math.Pi*float64(i)/20))
		actions = append(actions, funscript.Action{At: int64(i * 200), Pos: pos})
	}
	ref := filepath.Join(dir, "ref.funscript")
	cand := filepath.Join(dir, "cand.funscript")
	writePairFunscript(t, ref, actions)
	writePairFunscript(t, cand, actions)

	a := NewApp()
	labelsDir := t.TempDir()
	if err := a.settings.Set(prefBenchmarkHistoryPath, filepath.Join(labelsDir, "history.jsonl")); err != nil {
		t.Fatal(err)
	}
	score, err := a.ScoreScriptPair(ref, cand, "")
	if err != nil {
		t.Fatal(err)
	}
	if score.Label != funscript.LabelGood || !score.Passed {
		t.Fatalf("want good, got %+v", score)
	}
	out, err := a.AppendBenchmarkPairLabel(score)
	if err != nil {
		t.Fatal(err)
	}
	if _, err := os.Stat(out); err != nil {
		t.Fatal(err)
	}
}

func writePairFunscript(t *testing.T, path string, actions []funscript.Action) {
	t.Helper()
	s := funscript.Script{Actions: actions}
	data, err := json.Marshal(s)
	if err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(path, data, 0o644); err != nil {
		t.Fatal(err)
	}
}
