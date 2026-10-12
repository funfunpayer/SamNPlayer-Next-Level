package main

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func settingsFile(t *testing.T) string {
	t.Helper()
	dir := filepath.Join(os.Getenv("XDG_CONFIG_HOME"), "SamNPlayer")
	if err := os.MkdirAll(dir, 0o755); err != nil {
		t.Fatal(err)
	}
	return filepath.Join(dir, "settings.json")
}

// Jeder Regler speichert sofort. Ist settings.json nach einem Absturz
// halb geschrieben, lud die App still leere Defaults, und der nächste
// Regler-Klick überschrieb die Datei: alle Einstellungen weg. Jetzt wird
// die kaputte Datei beiseitegelegt statt überschrieben.
func TestSettingsStoreKeepsCorruptFileAside(t *testing.T) {
	t.Setenv("XDG_CONFIG_HOME", t.TempDir())
	p := settingsFile(t)
	broken := `{"playback.tick_ms": 80, "training.cycles": 7, "playback.max_sp`
	if err := os.WriteFile(p, []byte(broken), 0o644); err != nil {
		t.Fatal(err)
	}
	s, err := newSettingsStore()
	if err != nil {
		t.Fatal(err)
	}
	if got := s.GetFloat("playback.tick_ms", 50); got != 50 {
		t.Fatalf("unreadable file must give defaults, got %v", got)
	}
	if err := s.Set("playback.smoothing", 0.4); err != nil {
		t.Fatal(err)
	}
	matches, _ := filepath.Glob(p + ".corrupt*")
	if len(matches) != 1 {
		t.Fatalf("corrupt settings not kept aside: %v", matches)
	}
	if b, _ := os.ReadFile(matches[0]); string(b) != broken {
		t.Errorf("kept file changed: %q", b)
	}
	s2, err := newSettingsStore()
	if err != nil {
		t.Fatal(err)
	}
	if got := s2.GetFloat("playback.smoothing", 0); got != 0.4 {
		t.Errorf("new value not saved: %v", got)
	}
}

// settings.json mit "null" setzte die Map auf nil - der nächste Set()
// endete in "assignment to entry in nil map".
func TestSettingsStoreNullFile(t *testing.T) {
	t.Setenv("XDG_CONFIG_HOME", t.TempDir())
	if err := os.WriteFile(settingsFile(t), []byte("null"), 0o644); err != nil {
		t.Fatal(err)
	}
	s, err := newSettingsStore()
	if err != nil {
		t.Fatal(err)
	}
	if err := s.Set("log.level", "debug"); err != nil {
		t.Fatal(err)
	}
	if got := s.GetString("log.level", ""); got != "debug" {
		t.Errorf("got %q", got)
	}
}

// Gespeichert wird über eine Zwischendatei; es bleibt keine liegen.
func TestSettingsStoreSaveLeavesNoTempFiles(t *testing.T) {
	t.Setenv("XDG_CONFIG_HOME", t.TempDir())
	s, err := newSettingsStore()
	if err != nil {
		t.Fatal(err)
	}
	for i := 0; i < 5; i++ {
		if err := s.Set("training.cycles", float64(i)); err != nil {
			t.Fatal(err)
		}
	}
	entries, _ := os.ReadDir(filepath.Dir(settingsFile(t)))
	for _, e := range entries {
		if strings.Contains(e.Name(), "tmp") {
			t.Errorf("temp file left: %s", e.Name())
		}
	}
}
