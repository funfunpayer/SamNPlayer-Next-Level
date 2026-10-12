package main

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func writeSessionLog(t *testing.T, path string, lines []string) {
	t.Helper()
	if err := os.WriteFile(path, []byte(joinLines(lines)), 0644); err != nil {
		t.Fatal(err)
	}
}

func joinLines(lines []string) string {
	out := ""
	for _, l := range lines {
		out += l + "\n"
	}
	return out
}

func TestSummarizeSessionLogAggregatesCycles(t *testing.T) {
	path := filepath.Join(t.TempDir(), "s.jsonl")
	writeSessionLog(t, path, []string{
		`{"timestamp":"2026-09-10T10:00:00Z","technique":"stopstart","channel":"vibration","cycleIndex":0,"cyclesTotal":3,"peakIntensity":0.6,"stoppedByUser":false,"reachedPeakAfterMs":4000,"arousalBefore":0}`,
		`{"timestamp":"2026-09-10T10:01:00Z","technique":"stopstart","channel":"vibration","cycleIndex":1,"cyclesTotal":3,"peakIntensity":0.8,"stoppedByUser":true,"reachedPeakAfterMs":2000,"arousalBefore":8}`,
		`{"timestamp":"2026-09-10T10:02:00Z","technique":"stopstart","channel":"vibration","cycleIndex":2,"cyclesTotal":3,"peakIntensity":1.0,"stoppedByUser":false,"reachedPeakAfterMs":0,"arousalBefore":0}`,
	})

	summary, err := summarizeSessionLog(path)
	if err != nil {
		t.Fatal(err)
	}
	if summary == nil {
		t.Fatal("erwartete eine Zusammenfassung, got nil")
	}
	if summary.CyclesCompleted != 3 {
		t.Fatalf("CyclesCompleted: %d", summary.CyclesCompleted)
	}
	if summary.CyclesStoppedEarly != 1 {
		t.Fatalf("CyclesStoppedEarly: %d", summary.CyclesStoppedEarly)
	}
	wantPeak := (0.6 + 0.8 + 1.0) / 3
	if diff := summary.MeanPeakIntensity - wantPeak; diff > 1e-9 || diff < -1e-9 {
		t.Fatalf("MeanPeakIntensity: %v, want %v", summary.MeanPeakIntensity, wantPeak)
	}
	// Nur die zwei Zyklen mit reachedPeakAfterMs > 0 fließen ein - der dritte
	// (0, "kein Peak erreicht") darf den Schnitt nicht künstlich nach unten ziehen.
	if summary.MeanReachedPeakAfterMs != 3000 {
		t.Fatalf("MeanReachedPeakAfterMs: %v", summary.MeanReachedPeakAfterMs)
	}
	if summary.ArousalReportsCount != 1 || summary.MeanArousalReported != 8 {
		t.Fatalf("Arousal: count=%d mean=%v", summary.ArousalReportsCount, summary.MeanArousalReported)
	}
	if summary.Technique != "stopstart" || summary.Channel != "vibration" {
		t.Fatalf("Technique/Channel: %+v", summary)
	}
	if summary.StartedAt != "2026-09-10T10:00:00Z" {
		t.Fatalf("StartedAt sollte vom ERSTEN Eintrag kommen: %s", summary.StartedAt)
	}
}

func TestSummarizeSessionLogSkipsBrokenLinesKeepsRest(t *testing.T) {
	path := filepath.Join(t.TempDir(), "s.jsonl")
	writeSessionLog(t, path, []string{
		`{"timestamp":"2026-09-10T10:00:00Z","technique":"plateau","channel":"suction","peakIntensity":0.5}`,
		`nicht einmal JSON`,
		`{"timestamp":"2026-09-10T10:01:00Z","technique":"plateau","channel":"suction","peakIntensity":0.7}`,
	})

	summary, err := summarizeSessionLog(path)
	if err != nil {
		t.Fatal(err)
	}
	if summary.CyclesCompleted != 2 {
		t.Fatalf("kaputte Zeile hätte übersprungen werden müssen, CyclesCompleted=%d", summary.CyclesCompleted)
	}
}

func TestSummarizeSessionLogEmptyFileReturnsNil(t *testing.T) {
	path := filepath.Join(t.TempDir(), "empty.jsonl")
	writeSessionLog(t, path, nil)

	summary, err := summarizeSessionLog(path)
	if err != nil {
		t.Fatal(err)
	}
	if summary != nil {
		t.Fatalf("leere Datei sollte nil liefern, got %+v", summary)
	}
}

func TestTrainingHistorySortsNewestFirstAndSkipsNonJsonl(t *testing.T) {
	tmp := t.TempDir()
	t.Setenv("XDG_CONFIG_HOME", tmp)
	sessionsDir := filepath.Join(tmp, "SamNPlayer", "logs", "sessions")
	if err := os.MkdirAll(sessionsDir, 0755); err != nil {
		t.Fatal(err)
	}

	// Dateiname trägt die TECHNIK vor dem Zeitstempel ("training-plateau-..."
	// vs "training-stopstart-...") - eine reine Dateinamen-Sortierung würde
	// hier die falsche Reihenfolge liefern. TrainingHistory muss nach dem
	// Zeitstempel IM Protokoll sortieren, nicht nach dem Dateinamen.
	writeSessionLog(t, filepath.Join(sessionsDir, "training-stopstart-20260901-100000.jsonl"), []string{
		`{"timestamp":"2026-09-01T10:00:00Z","technique":"stopstart","channel":"vibration","peakIntensity":0.5}`,
	})
	writeSessionLog(t, filepath.Join(sessionsDir, "training-plateau-20260910-100000.jsonl"), []string{
		`{"timestamp":"2026-09-10T10:00:00Z","technique":"plateau","channel":"suction","peakIntensity":0.9}`,
	})
	// Nicht-JSONL-Datei im selben Ordner darf nicht mitgezählt werden.
	if err := os.WriteFile(filepath.Join(sessionsDir, "readme.txt"), []byte("hi"), 0644); err != nil {
		t.Fatal(err)
	}

	a := NewApp()
	history, err := a.TrainingHistory()
	if err != nil {
		t.Fatal(err)
	}
	if len(history) != 2 {
		t.Fatalf("erwartete 2 Sessions, got %d: %+v", len(history), history)
	}
	if history[0].Technique != "plateau" {
		t.Fatalf("neueste Session (Datum, nicht Dateiname) sollte zuerst kommen: %+v", history)
	}
	if history[1].Technique != "stopstart" {
		t.Fatalf("älteste Session sollte zuletzt kommen: %+v", history)
	}
}

func TestTrainingHistoryNoSessionsDirIsNotAnError(t *testing.T) {
	t.Setenv("XDG_CONFIG_HOME", t.TempDir())
	a := NewApp()
	history, err := a.TrainingHistory()
	if err != nil {
		t.Fatal(err)
	}
	if len(history) != 0 {
		t.Fatalf("erwartete leere Liste ohne sessions-Ordner, got %+v", history)
	}
}

// trainingHistoryCSV is the pure formatting half of ExportTrainingHistoryCSV
// (the other half opens a real Wails save dialog, untestable without a
// bound frontend - see app_training_history.go's own comment on the split).
func TestTrainingHistoryCSVFormatsRows(t *testing.T) {
	history := []TrainingSessionSummary{
		{
			StartedAt: "2026-09-22T10:00:00Z", Technique: "stopstart", Channel: "vibration",
			CyclesCompleted: 5, CyclesStoppedEarly: 2, MeanPeakIntensity: 0.75,
			MeanReachedPeakAfterMs: 4000, MeanArousalReported: 7.5, ArousalReportsCount: 3,
		},
		{
			StartedAt: "2026-09-20T10:00:00Z", Technique: "vibration-wave-suction-focus", Channel: "script",
			CyclesCompleted: 6, CyclesStoppedEarly: 0, MeanPeakIntensity: 0.6,
		},
	}
	csv, err := trainingHistoryCSV(history)
	if err != nil {
		t.Fatalf("trainingHistoryCSV: %v", err)
	}
	lines := strings.Split(strings.TrimRight(csv, "\n"), "\n")
	if len(lines) != 3 {
		t.Fatalf("expected a header + 2 rows, got %d lines: %q", len(lines), csv)
	}
	if !strings.HasPrefix(lines[0], "startedAt,technique,channel,") {
		t.Errorf("unexpected header: %q", lines[0])
	}
	if !strings.Contains(lines[1], "stopstart") || !strings.Contains(lines[1], "75.0") {
		t.Errorf("first row missing expected fields: %q", lines[1])
	}
	if !strings.Contains(lines[2], "vibration-wave-suction-focus") || !strings.Contains(lines[2], "script") {
		t.Errorf("second row (script session) missing expected fields: %q", lines[2])
	}
}

func TestTrainingHistoryCSVEmptyHistoryIsJustHeader(t *testing.T) {
	csv, err := trainingHistoryCSV(nil)
	if err != nil {
		t.Fatalf("trainingHistoryCSV: %v", err)
	}
	if strings.Count(csv, "\n") != 1 {
		t.Fatalf("expected exactly one line (the header) for empty history, got %q", csv)
	}
}

// Eigene Scripts dürfen beliebige Namen tragen ("Abend / kurz", "Edge: 10
// min?"); der Name landete bisher roh im Dateinamen des Sessionprotokolls.
// "/" ließ os.Create scheitern (auf Windows auch ":" und "?"), die Session
// wurde nicht protokolliert und fehlte in Verlauf und Anpassung.
func TestOpenSessionLogSanitizesScriptName(t *testing.T) {
	tmp := t.TempDir()
	t.Setenv("XDG_CONFIG_HOME", tmp)
	a := &App{}
	for _, name := range []string{"Abend / kurz: sanft?", "../../escape", "!!!"} {
		f, err := a.openSessionLog(name)
		if err != nil {
			t.Fatalf("%q: %v", name, err)
		}
		f.Close()
		dir, _ := filepath.Split(f.Name())
		if filepath.Clean(dir) != filepath.Join(tmp, "SamNPlayer", "logs", "sessions") {
			t.Errorf("%q: Protokoll außerhalb des Sessions-Ordners: %s", name, f.Name())
		}
		if base := filepath.Base(f.Name()); strings.ContainsAny(base, ` /\:?`) {
			t.Errorf("%q: unsicherer Dateiname %q", name, base)
		}
	}
}
