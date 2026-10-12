package main

import (
	"bufio"
	"context"
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"sort"
	"time"

	"github.com/wailsapp/wails/v2/pkg/runtime"

	"github.com/funfunpayer/SamNPlayer/device"
	"github.com/funfunpayer/SamNPlayer/logging"
)

const maxDiagnosticsHistoryEntries = 30

// DiagnosticsHistoryEntry ist ein gespeicherter Diagnoselauf - dasselbe
// Ergebnis, das RunDeviceDiagnostics auch als "diagnostics:done" sendet, nur
// zusätzlich mit Zeitstempel und Geräte-Info für die Verlaufsliste.
type DiagnosticsHistoryEntry struct {
	Timestamp  string                   `json:"timestamp"`
	Mock       bool                     `json:"mock"`
	DeviceName string                   `json:"deviceName"`
	Report     device.DiagnosticsReport `json:"report"`
}

// diagnosticsRunning verhindert einen zweiten gleichzeitigen Diagnoselauf -
// der Test schreibt fortlaufend auf denselben Gerätekanal wie
// TestVibration/TestSuction, zwei Läufe gleichzeitig würden sich die
// Kommandos gegenseitig überschreiben.
var diagnosticsRunning bool

// RunDeviceDiagnostics fährt die in docs/SAM_NEO_2_RESEARCH.md §11/§12
// beschriebene Messreihe gegen die bestehende Testverbindung des
// Geräte-Tabs (siehe app_device.go's testDeviceOrErr - dieselbe
// Verbindung, kein eigener Connect/Disconnect hier). Läuft asynchron wie
// RunGoldenClipBenchmark, eigener Event-Namensraum "diagnostics:...".
//
// Objektiv gemessen wird nur, was sich ohne einen Sensor am Gerät
// tatsächlich feststellen lässt: welche Rohwerte angenommen werden, wie
// die Schreib-Latenz bei steigender Update-Rate reagiert, ob Kommandos auf
// beiden Kanälen gleichzeitig fehlerfrei durchgehen. Ausdrücklich NICHT
// gemessen: gefühlte Intensität, echte physische Anstiegs-/Abfallzeit -
// device.DiagnosticsReport.Notes sagt das auch der Oberfläche.
func (a *App) RunDeviceDiagnostics() error {
	a.stateMu.Lock()
	if diagnosticsRunning {
		a.stateMu.Unlock()
		return fmt.Errorf("a diagnostics run is already in progress")
	}
	diagnosticsRunning = true
	a.stateMu.Unlock()

	dev, err := a.testDeviceOrErr()
	if err != nil {
		a.stateMu.Lock()
		diagnosticsRunning = false
		a.stateMu.Unlock()
		return err
	}

	a.stateMu.RLock()
	mock := a.testDeviceMock
	connectLatencyMs := a.testDeviceConnectLatencyMs
	a.stateMu.RUnlock()
	status := a.GetDeviceStatus()

	go func() {
		defer func() {
			a.stateMu.Lock()
			diagnosticsRunning = false
			a.stateMu.Unlock()
		}()

		ctx, cancel := context.WithTimeout(context.Background(), 2*time.Minute)
		defer cancel()

		report := device.RunDiagnostics(ctx, dev, connectLatencyMs, func(e device.DiagnosticsEntry) {
			runtime.EventsEmit(a.ctx, "diagnostics:entry", e)
		})

		entry := DiagnosticsHistoryEntry{
			Timestamp:  time.Now().UTC().Format(time.RFC3339),
			Mock:       mock,
			DeviceName: status.Name,
			Report:     report,
		}
		if err := a.appendDiagnosticsHistory(entry); err != nil {
			logging.Warn("diagnostics: history could not be written", "error", err)
		}

		logging.Info("diagnostics: run finished",
			"mock", mock, "kommandos", len(report.Log), "abgebrochen", report.Interrupted)
		runtime.EventsEmit(a.ctx, "diagnostics:done", entry)
	}()

	return nil
}

func (a *App) appendDiagnosticsHistory(entry DiagnosticsHistoryEntry) error {
	path := a.settings.GetString(prefDiagnosticsHistory, defaultDiagnosticsHistoryPath())
	if path == "" {
		return fmt.Errorf("no history path available")
	}
	if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
		return err
	}
	f, err := os.OpenFile(path, os.O_APPEND|os.O_CREATE|os.O_WRONLY, 0o644)
	if err != nil {
		return err
	}
	defer f.Close()
	line, err := json.Marshal(entry)
	if err != nil {
		return err
	}
	_, err = f.Write(append(line, '\n'))
	return err
}

// GetDiagnosticsHistory liest die Verlaufsdatei - dasselbe
// Lesen/Sortieren/Begrenzen/Fehlerzeilen-Überspringen-Muster wie
// GetBenchmarkHistory (app_benchmark.go).
func (a *App) GetDiagnosticsHistory() ([]DiagnosticsHistoryEntry, error) {
	path := a.settings.GetString(prefDiagnosticsHistory, defaultDiagnosticsHistoryPath())
	if path == "" {
		return []DiagnosticsHistoryEntry{}, nil
	}
	f, err := os.Open(path)
	if err != nil {
		if os.IsNotExist(err) {
			return []DiagnosticsHistoryEntry{}, nil
		}
		return nil, err
	}
	defer f.Close()

	var results []DiagnosticsHistoryEntry
	scanner := bufio.NewScanner(f)
	scanner.Buffer(make([]byte, 0, 64*1024), 4*1024*1024)
	for scanner.Scan() {
		var e DiagnosticsHistoryEntry
		if err := json.Unmarshal(scanner.Bytes(), &e); err != nil {
			logging.Warn("diagnostics: history line skipped", "error", err)
			continue
		}
		results = append(results, e)
	}

	sort.Slice(results, func(i, j int) bool { return results[i].Timestamp > results[j].Timestamp })
	if len(results) > maxDiagnosticsHistoryEntries {
		results = results[:maxDiagnosticsHistoryEntries]
	}
	return results, nil
}
