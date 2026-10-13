package main

import (
	"encoding/json"
	"os"
	"path/filepath"
	"sync"
	"time"

	"github.com/funfunpayer/SamNPlayer/funscript"
	"github.com/funfunpayer/SamNPlayer/logging"
)

// settingsStore ist eine bewusst simple JSON-Datei-Persistenz - kein
// externes Key-Value-Store-Paket, das wir nicht brauchen (siehe "was wir
// nicht brauchen muss nicht rein"). Liegt im selben Nutzerkonfigurations-
// ordner wie die Logdatei (logging.Dir()'s Elternverzeichnis).
type settingsStore struct {
	mu   sync.Mutex
	path string
	data map[string]any
}

func newSettingsStore() (*settingsStore, error) {
	base, err := os.UserConfigDir()
	if err != nil {
		return nil, err
	}
	dir := filepath.Join(base, "SamNPlayer")
	if err := os.MkdirAll(dir, 0755); err != nil {
		return nil, err
	}
	s := &settingsStore{path: filepath.Join(dir, "settings.json"), data: map[string]any{}}
	s.load()
	return s, nil
}

func (s *settingsStore) load() {
	s.mu.Lock()
	defer s.mu.Unlock()
	b, err := os.ReadFile(s.path)
	if err != nil {
		return // Datei existiert noch nicht - leere Defaults sind ok
	}
	var data map[string]any
	if err := json.Unmarshal(b, &data); err != nil {
		// Unlesbar (z.B. halb geschrieben): beiseitelegen statt beim
		// nächsten Regler-Klick mit Defaults zu überschreiben - sonst
		// wären alle Einstellungen unwiederbringlich weg.
		aside := s.path + ".corrupt-" + time.Now().Format("20060102-150405")
		if renameErr := os.Rename(s.path, aside); renameErr != nil {
			logging.Warn("settings: unreadable settings file", "path", s.path, "error", err, "rename_error", renameErr)
		} else {
			logging.Warn("settings: unreadable settings file kept aside, using defaults", "path", aside, "error", err)
		}
		return
	}
	if data != nil { // "null" ließe die Map nil - Set() würde abstürzen
		s.data = data
	}
}

func (s *settingsStore) save() error {
	s.mu.Lock()
	defer s.mu.Unlock()
	b, err := json.MarshalIndent(s.data, "", "  ")
	if err != nil {
		return err
	}
	// Atomar: jeder Regler speichert sofort, ein Absturz mitten im
	// Schreiben darf die Datei nicht halb zurücklassen.
	return funscript.WriteFileAtomic(s.path, b, 0o644)
}

func (s *settingsStore) GetString(key, fallback string) string {
	s.mu.Lock()
	defer s.mu.Unlock()
	if v, ok := s.data[key].(string); ok {
		return v
	}
	return fallback
}

func (s *settingsStore) GetBool(key string, fallback bool) bool {
	s.mu.Lock()
	defer s.mu.Unlock()
	if v, ok := s.data[key].(bool); ok {
		return v
	}
	return fallback
}

func (s *settingsStore) GetFloat(key string, fallback float64) float64 {
	s.mu.Lock()
	defer s.mu.Unlock()
	if v, ok := s.data[key].(float64); ok {
		return v
	}
	return fallback
}

func (s *settingsStore) Set(key string, value any) error {
	s.mu.Lock()
	s.data[key] = value
	s.mu.Unlock()
	return s.save()
}
