package main

import (
	"bufio"
	"encoding/csv"
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"sort"
	"strconv"
	"strings"
	"time"

	"github.com/wailsapp/wails/v2/pkg/runtime"

	"github.com/funfunpayer/SamNPlayer/logging"
)

// TrainingSessionSummary fasst eine einzelne mitgeschriebene Trainings-
// Session (siehe openSessionLog/sessionLogEntry in app_training.go) für die
// Anzeige zusammen - roh sind das oft hunderte Zyklen, hier eine Zeile pro
// Session.
type TrainingSessionSummary struct {
	FileName               string  `json:"fileName"`
	StartedAt              string  `json:"startedAt"`
	Technique              string  `json:"technique"`
	Channel                string  `json:"channel"`
	CyclesCompleted        int     `json:"cyclesCompleted"`
	CyclesStoppedEarly     int     `json:"cyclesStoppedEarly"`
	MeanPeakIntensity      float64 `json:"meanPeakIntensity"`
	MeanReachedPeakAfterMs float64 `json:"meanReachedPeakAfterMs"`
	MeanArousalReported    float64 `json:"meanArousalReported"`
	ArousalReportsCount    int     `json:"arousalReportsCount"`
}

const maxTrainingHistoryEntries = 50

// TrainingHistory liest die mitgeschriebenen Session-Protokolle
// (app_training.go: openSessionLog, eine JSONL-Datei pro Session) und fasst
// jede zu einer Zeile zusammen - die Rohdaten selbst bleiben unverändert
// auf der Platte (spätere Feinabstimmung, siehe openSessionLog's eigener
// Kommentar), das hier ist nur die erste Anzeige über mehrere Sessions
// hinweg (docs/NEXT.md "Later": "Training history across multiple
// sessions" - bisher wurde nur geschrieben, nie wieder gelesen).
// Neueste Session zuerst (nach dem Zeitstempel IM Protokoll sortiert, nicht
// nach Dateiname - der trägt die Technik vor dem Zeitstempel und würde
// sonst falsch sortieren), auf die letzten 50 begrenzt.
func (a *App) TrainingHistory() ([]TrainingSessionSummary, error) {
	dir, err := logging.Dir()
	if err != nil {
		return nil, err
	}
	sessionsDir := filepath.Join(dir, "sessions")
	entries, err := os.ReadDir(sessionsDir)
	if err != nil {
		if os.IsNotExist(err) {
			return []TrainingSessionSummary{}, nil
		}
		return nil, err
	}

	summaries := make([]TrainingSessionSummary, 0, len(entries))
	for _, e := range entries {
		if e.IsDir() || !strings.HasSuffix(e.Name(), ".jsonl") {
			continue
		}
		summary, err := summarizeSessionLog(filepath.Join(sessionsDir, e.Name()))
		if err != nil {
			logging.Warn("app: session log skipped", "file", e.Name(), "error", err)
			continue
		}
		if summary == nil {
			continue // leere/kaputte Datei ohne eine einzige lesbare Zeile
		}
		summary.FileName = e.Name()
		summaries = append(summaries, *summary)
	}

	sort.Slice(summaries, func(i, j int) bool { return summaries[i].StartedAt > summaries[j].StartedAt })
	if len(summaries) > maxTrainingHistoryEntries {
		summaries = summaries[:maxTrainingHistoryEntries]
	}
	return summaries, nil
}

// summarizeSessionLog liest eine einzelne JSONL-Datei zeilenweise (eine
// sessionLogEntry pro Zyklus, siehe app_training.go) und aggregiert sie.
// Eine einzelne kaputte Zeile verwirft nur diese Zeile, nicht die ganze
// Session - dieselbe Toleranz, die report_test.py bei ähnlichen JSONL-
// Protokollen schon vom Generator-Teil her verlangt.
func summarizeSessionLog(path string) (*TrainingSessionSummary, error) {
	f, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	defer f.Close()

	var (
		count        int
		stoppedEarly int
		sumPeak      float64
		sumReached   float64
		reachedCount int
		sumArousal   float64
		arousalCount int
		first        sessionLogEntry
	)

	scanner := bufio.NewScanner(f)
	for scanner.Scan() {
		var entry sessionLogEntry
		if err := json.Unmarshal(scanner.Bytes(), &entry); err != nil {
			continue
		}
		if count == 0 {
			first = entry
		}
		count++
		if entry.StoppedByUser {
			stoppedEarly++
		}
		sumPeak += entry.PeakIntensity
		if entry.ReachedPeakAfterMs > 0 {
			sumReached += float64(entry.ReachedPeakAfterMs)
			reachedCount++
		}
		if entry.ArousalBefore > 0 {
			sumArousal += float64(entry.ArousalBefore)
			arousalCount++
		}
	}
	if err := scanner.Err(); err != nil {
		return nil, err
	}
	if count == 0 {
		return nil, nil
	}

	summary := TrainingSessionSummary{
		StartedAt:           first.Timestamp,
		Technique:           first.Technique,
		Channel:             first.Channel,
		CyclesCompleted:     count,
		CyclesStoppedEarly:  stoppedEarly,
		MeanPeakIntensity:   sumPeak / float64(count),
		ArousalReportsCount: arousalCount,
	}
	if reachedCount > 0 {
		summary.MeanReachedPeakAfterMs = sumReached / float64(reachedCount)
	}
	if arousalCount > 0 {
		summary.MeanArousalReported = sumArousal / float64(arousalCount)
	}
	return &summary, nil
}

// trainingHistoryCSV renders TrainingHistory's summaries as CSV - a plain
// function (no dialog, no App) so the actual formatting is testable
// without touching the filesystem or Wails runtime. One row per session,
// newest first (same order TrainingHistory already returns).
func trainingHistoryCSV(history []TrainingSessionSummary) (string, error) {
	var buf strings.Builder
	w := csv.NewWriter(&buf)
	header := []string{"startedAt", "technique", "channel", "cyclesCompleted", "cyclesStoppedEarly",
		"meanPeakIntensityPct", "meanReachedPeakAfterMs", "meanArousalReported", "arousalReportsCount"}
	if err := w.Write(header); err != nil {
		return "", err
	}
	for _, s := range history {
		row := []string{
			s.StartedAt,
			s.Technique,
			s.Channel,
			strconv.Itoa(s.CyclesCompleted),
			strconv.Itoa(s.CyclesStoppedEarly),
			strconv.FormatFloat(s.MeanPeakIntensity*100, 'f', 1, 64),
			strconv.FormatFloat(s.MeanReachedPeakAfterMs, 'f', 0, 64),
			strconv.FormatFloat(s.MeanArousalReported, 'f', 1, 64),
			strconv.Itoa(s.ArousalReportsCount),
		}
		if err := w.Write(row); err != nil {
			return "", err
		}
	}
	w.Flush()
	if err := w.Error(); err != nil {
		return "", err
	}
	return buf.String(), nil
}

// ExportTrainingHistoryCSV lets the user save the full session history
// (cmd/gui-wails/app_training.go's sessionLogEntry, summarized by
// TrainingHistory) as a CSV file for their own tracking outside the app.
// Returns the chosen path, or "" if the user canceled the dialog - not
// an error, so the GUI can distinguish "nothing to report" from
// "cancel is fine, don't show an error".
func (a *App) ExportTrainingHistoryCSV() (string, error) {
	history, err := a.TrainingHistory()
	if err != nil {
		return "", err
	}
	content, err := trainingHistoryCSV(history)
	if err != nil {
		return "", err
	}
	defaultName := fmt.Sprintf("training-history-%s.csv", time.Now().Format("20060102"))
	path, err := runtime.SaveFileDialog(a.ctx, runtime.SaveDialogOptions{
		Title:                "Export training history as",
		DefaultFilename:      defaultName,
		CanCreateDirectories: true,
		Filters: []runtime.FileFilter{
			{DisplayName: "CSV (*.csv)", Pattern: "*.csv"},
		},
	})
	if err != nil || path == "" {
		return "", err
	}
	if err := os.WriteFile(path, []byte(content), 0644); err != nil {
		return "", err
	}
	return path, nil
}
