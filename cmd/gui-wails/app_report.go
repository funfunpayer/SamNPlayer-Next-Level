package main

import (
	"fmt"
	"os"
	"path/filepath"

	"github.com/wailsapp/wails/v2/pkg/runtime"

	"github.com/funfunpayer/SamNPlayer/generator"
	"github.com/funfunpayer/SamNPlayer/logging"
)

// Hintergrund: sämtliche Schwellen im Quality Doctor sind an synthetischen
// Testvideos bestimmt worden - saubere Sinusbewegungen, deren Wahrheit per
// Konstruktion bekannt war. Echtes Material ist unregelmäßiger und liegt
// systematisch niedriger. Zum Nachziehen der Schwellen braucht es beides:
// die Kennzahlen echter Läufe UND ein menschliches Urteil dazu. Ohne das
// Urteil sind die Zahlen wertlos, weil niemand weiß, welche davon zu einem
// brauchbaren Ergebnis gehören.

// ReportFeedback ist ein Urteil zu einem erzeugten Skript.
type ReportFeedback struct {
	OutputPath string `json:"outputPath"`
	Verdict    string `json:"verdict"`
	Comment    string `json:"comment"`
}

// activeReportPath liefert den eingestellten Berichtspfad, oder "" wenn
// keiner gesetzt ist (dann wird kein Bericht geschrieben).
func (a *App) activeReportPath() string {
	return a.settings.GetString(prefReportPath, "")
}

// SubmitFeedback trägt ein Urteil zum zuletzt erzeugten Skript nach.
func (a *App) SubmitFeedback(fb ReportFeedback) error {
	path := a.activeReportPath()
	if path == "" {
		return fmt.Errorf("no report path configured — set it in Settings to save measurements and verdicts")
	}
	if err := generator.AddFeedback(path, fb.OutputPath, fb.Verdict, fb.Comment); err != nil {
		logging.Warn("report: verdict could not be saved", "error", err)
		return err
	}
	logging.Info("report: verdict saved", "verdict", fb.Verdict,
		"script", fb.OutputPath, "comment", fb.Comment)
	return nil
}

// ReportSummary gibt die nach Urteil gruppierte Auswertung zurück.
func (a *App) ReportSummary() (string, error) {
	path := a.activeReportPath()
	if path == "" {
		return "", fmt.Errorf("no report path configured")
	}
	return generator.ReportSummary(path)
}

// ReportExists sagt, ob schon Messwerte vorliegen - damit die Oberfläche die
// Auswertung nur anbietet, wenn es etwas auszuwerten gibt.
func (a *App) ReportExists() bool {
	path := a.activeReportPath()
	if path == "" {
		return false
	}
	info, err := os.Stat(path)
	return err == nil && info.Size() > 0
}

// PickReportPath lässt den Nutzer eine Zieldatei wählen.
func (a *App) PickReportPath() (string, error) {
	current := a.activeReportPath()
	if current == "" {
		current = defaultReportPath()
	}
	return runtime.SaveFileDialog(a.ctx, runtime.SaveDialogOptions{
		Title:                "Save measurements as",
		DefaultFilename:      filepath.Base(current),
		DefaultDirectory:     filepath.Dir(current),
		CanCreateDirectories: true,
		Filters: []runtime.FileFilter{
			{DisplayName: "Measurements (*.jsonl)", Pattern: "*.jsonl"},
		},
	})
}

// TrainQualityModel lernt aus den gesammelten Urteilen.
func (a *App) TrainQualityModel() (string, error) {
	if err := requireSuite(); err != nil {
		return "", err
	}
	path := a.activeReportPath()
	if path == "" {
		return "", fmt.Errorf("no report path configured — nothing to learn without recorded measurements and verdicts")
	}
	report, err := generator.TrainQualityModel(path)
	if err != nil {
		logging.Warn("model: training failed", "error", err)
		return report, err
	}
	logging.Info("model: training run finished")
	return report, nil
}

// QualityModelInfo beschreibt das aktuell verwendete Qualitätsmodell.
func (a *App) QualityModelInfo() (string, error) {
	return generator.QualityModelInfo()
}
