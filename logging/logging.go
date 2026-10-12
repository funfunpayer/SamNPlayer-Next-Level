// Package logging stellt einen zentralen, strukturierten Logger (log/slog)
// bereit, der in eine Datei schreibt - für Fehlersuche und spätere
// Performance-Optimierung (BLE-Timing, Player-Verhalten, Subprozess-Aufrufe).
//
// Bewusst einfach gehalten: eine Logdatei mit simpler größenbasierter
// Rotation (ein Backup), Level zur Laufzeit umschaltbar (siehe SetLevel -
// die GUI hängt das an den Einstellungen-Tab). Kein externes Logging-
// Framework, nur die Standardbibliothek.
package logging

import (
	"fmt"
	"io"
	"log/slog"
	"os"
	"path/filepath"
	"sync"
)

const maxLogSizeBytes = 5 * 1024 * 1024 // 5 MB, dann rotieren

var (
	mu      sync.Mutex
	logger  *slog.Logger
	level   = new(slog.LevelVar) // erlaubt Level-Änderung zur Laufzeit
	logPath string
	logFile *os.File
)

func init() {
	level.Set(slog.LevelInfo)
	// Fallback-Logger, falls Init() nie aufgerufen wird (z.B. in Tests) -
	// schreibt nach stderr statt ins Leere zu laufen.
	logger = slog.New(slog.NewTextHandler(os.Stderr, &slog.HandlerOptions{Level: level}))
}

// Dir liefert das Verzeichnis, in dem die Logdatei liegt (plattformüblicher
// Nutzer-Konfigurationsordner + "SamNPlayer/logs").
func Dir() (string, error) {
	base, err := os.UserConfigDir()
	if err != nil {
		return "", fmt.Errorf("logging: Konfigurationsverzeichnis nicht ermittelbar: %w", err)
	}
	return filepath.Join(base, "SamNPlayer", "logs"), nil
}

// Init richtet den Datei-Logger ein. Muss einmal beim Programmstart
// aufgerufen werden (z.B. main()). Bei Fehlern (z.B. kein Schreibzugriff)
// bleibt der stderr-Fallback-Logger aktiv - Logging darf niemals das
// eigentliche Programm zum Absturz bringen.
func Init() error {
	dir, err := Dir()
	if err != nil {
		return err
	}
	if err := os.MkdirAll(dir, 0755); err != nil {
		return fmt.Errorf("logging: Verzeichnis konnte nicht angelegt werden: %w", err)
	}

	path := filepath.Join(dir, "SamNPlayer.log")
	rotateIfNeeded(path)

	f, err := os.OpenFile(path, os.O_APPEND|os.O_CREATE|os.O_WRONLY, 0644)
	if err != nil {
		return fmt.Errorf("logging: Logdatei konnte nicht geöffnet werden: %w", err)
	}

	mu.Lock()
	logFile = f
	logPath = path
	logger = slog.New(slog.NewTextHandler(io.MultiWriter(f, os.Stderr), &slog.HandlerOptions{Level: level}))
	mu.Unlock()

	Info("logging gestartet", "datei", path, "level", level.Level().String())
	return nil
}

func rotateIfNeeded(path string) {
	info, err := os.Stat(path)
	if err != nil || info.Size() < maxLogSizeBytes {
		return
	}
	backup := path + ".1"
	os.Remove(backup)       // altes Backup verwerfen
	os.Rename(path, backup) // aktuelle Datei wird zum neuen Backup
}

// Path gibt den aktuellen Logdatei-Pfad zurück ("" falls Init() nicht
// erfolgreich lief).
func Path() string {
	mu.Lock()
	defer mu.Unlock()
	return logPath
}

// SetLevel ändert das Log-Level zur Laufzeit (z.B. aus dem Einstellungen-Tab).
func SetLevel(l slog.Level) {
	level.Set(l)
}

// Level liefert das aktuell aktive Log-Level.
func Level() slog.Level {
	return level.Level()
}

// Close schließt die Logdatei sauber (beim Programmende aufrufen, optional -
// os.Exit räumt Dateihandles ohnehin auf, aber sauberer Abschluss schadet nicht).
func Close() {
	mu.Lock()
	defer mu.Unlock()
	if logFile != nil {
		logFile.Close()
		logFile = nil
	}
}

func L() *slog.Logger {
	mu.Lock()
	defer mu.Unlock()
	return logger
}

func Debug(msg string, args ...any) {
	appendRing("DEBUG", msg, args...)
	L().Debug(msg, args...)
}
func Info(msg string, args ...any) {
	appendRing("INFO", msg, args...)
	L().Info(msg, args...)
}
func Warn(msg string, args ...any) {
	appendRing("WARN", msg, args...)
	L().Warn(msg, args...)
}
func Error(msg string, args ...any) {
	appendRing("ERROR", msg, args...)
	L().Error(msg, args...)
}
