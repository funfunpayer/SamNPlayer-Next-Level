package main

import (
	"fmt"
	"os"
	"path/filepath"
	"strings"

	"github.com/funfunpayer/SamNPlayer/generator"
	"github.com/funfunpayer/SamNPlayer/logging"
)

// Der Cache speichert Trackingergebnisse, damit Signalparameter ausprobiert
// werden können, ohne das Video jedes Mal neu zu dekodieren - der Unterschied
// liegt bei rund Faktor 36. Dafür wächst er mit jedem verarbeiteten Video.
// Deshalb hier: Größe anzeigen, von Hand leeren, und optional beim Beenden
// automatisch leeren.

// CacheInfo beschreibt den Zustand des Zwischenspeichers.
type CacheInfo struct {
	Path       string `json:"path"`
	Files      int    `json:"files"`
	Bytes      int64  `json:"bytes"`
	HumanSize  string `json:"humanSize"`
	ClearOnExi bool   `json:"clearOnExit"`
}

func humanBytes(n int64) string {
	const unit = 1024
	if n < unit {
		return fmt.Sprintf("%d B", n)
	}
	div, exp := int64(unit), 0
	for m := n / unit; m >= unit; m /= unit {
		div *= unit
		exp++
	}
	return fmt.Sprintf("%.1f %cB", float64(n)/float64(div), "KMGT"[exp])
}

// GetCacheInfo liefert Ort, Dateizahl und Größe des Zwischenspeichers.
func (a *App) GetCacheInfo() CacheInfo {
	path := generator.DefaultCacheDir()
	info := CacheInfo{Path: path, ClearOnExi: a.settings.GetBool(prefClearCacheOnExit, false)}
	entries, err := os.ReadDir(path)
	if err != nil {
		return info // Verzeichnis existiert noch nicht - das ist kein Fehler
	}
	for _, entry := range entries {
		if entry.IsDir() {
			continue
		}
		stat, err := entry.Info()
		if err != nil {
			continue
		}
		info.Files++
		info.Bytes += stat.Size()
	}
	info.HumanSize = humanBytes(info.Bytes)
	return info
}

// ClearCache löscht die zwischengespeicherten Trackingergebnisse.
//
// Bewusst nur Dateien mit dem erwarteten Namensmuster im erwarteten
// Verzeichnis: ein rekursives Löschen eines aus Einstellungen gelesenen
// Pfades ist ein Fehler, den man genau einmal macht.
func (a *App) ClearCache() (CacheInfo, error) {
	path := generator.DefaultCacheDir()
	if path == "" {
		return CacheInfo{}, fmt.Errorf("no cache directory known")
	}
	entries, err := os.ReadDir(path)
	if err != nil {
		if os.IsNotExist(err) {
			return a.GetCacheInfo(), nil
		}
		return CacheInfo{}, err
	}
	removed := 0
	for _, entry := range entries {
		name := entry.Name()
		if entry.IsDir() || !strings.HasPrefix(name, "track-") || !strings.HasSuffix(name, ".npz") {
			continue
		}
		if err := os.Remove(filepath.Join(path, name)); err == nil {
			removed++
		}
	}
	logging.Info("cache: cleared", "files", removed, "path", path)
	return a.GetCacheInfo(), nil
}

// clearCacheIfRequested wird beim Beenden aufgerufen.
func (a *App) clearCacheIfRequested() {
	if !a.settings.GetBool(prefClearCacheOnExit, false) {
		return
	}
	if _, err := a.ClearCache(); err != nil {
		logging.Warn("cache: clear on shutdown failed", "error", err)
	}
}
