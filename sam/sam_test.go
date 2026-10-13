package sam

import (
	"os"
	"path/filepath"
	"testing"
)

func TestParseRequiresVersion(t *testing.T) {
	_, err := Parse([]byte(`{"frames":[{"time":0,"motion":{"position":50}}]}`))
	if err == nil {
		t.Fatal("erwartet Fehler bei fehlender Versionsangabe")
	}
}

func TestParseRequiresFrames(t *testing.T) {
	_, err := Parse([]byte(`{"version":"0.1","frames":[]}`))
	if err == nil {
		t.Fatal("erwartet Fehler bei leeren frames")
	}
}

func TestParseRejectsInvalidJSON(t *testing.T) {
	_, err := Parse([]byte(`not json`))
	if err == nil {
		t.Fatal("erwartet Fehler bei ungültigem JSON")
	}
}

func TestParseSortsFramesByTime(t *testing.T) {
	s, err := Parse([]byte(`{"version":"0.1","frames":[
		{"time":500,"motion":{"position":10}},
		{"time":0,"motion":{"position":90}},
		{"time":250,"motion":{"position":50}}
	]}`))
	if err != nil {
		t.Fatalf("unerwarteter Fehler: %v", err)
	}
	want := []int64{0, 250, 500}
	for i, w := range want {
		if s.Frames[i].Time != w {
			t.Errorf("Frame %d: erwartet Time=%d, ist %d", i, w, s.Frames[i].Time)
		}
	}
}

func TestParseClampsOutOfRangeFields(t *testing.T) {
	s, err := Parse([]byte(`{"version":"0.1","frames":[
		{"time":0,"motion":{"position":150,"confidence":1.5,"energy":-0.3}}
	]}`))
	if err != nil {
		t.Fatalf("unerwarteter Fehler: %v", err)
	}
	m := s.Frames[0].Motion
	if m.Position != 100 {
		t.Errorf("Position sollte auf 100 geklemmt sein, ist %v", m.Position)
	}
	if m.Confidence != 1 {
		t.Errorf("Confidence sollte auf 1 geklemmt sein, ist %v", m.Confidence)
	}
	if m.Energy != 0 {
		t.Errorf("Energy sollte auf 0 geklemmt sein, ist %v", m.Energy)
	}
}

// Unbekannte Felder aus einer neueren Schemaversion dürfen einen älteren
// Reader nicht zerstören - das ist der ganze Sinn der omitempty/JSON-
// Toleranz aus docs/SAM_ARCHITECTURE.md ("unknown-field tolerance").
func TestParseIgnoresUnknownFutureFields(t *testing.T) {
	s, err := Parse([]byte(`{"version":"0.3","frames":[
		{"time":0,"motion":{"position":50,"future_field_v3":"irgendwas"},"future_top_level":123}
	]}`))
	if err != nil {
		t.Fatalf("unbekannte Felder sollten toleriert werden, Fehler: %v", err)
	}
	if s.Frames[0].Motion.Position != 50 {
		t.Errorf("bekannte Felder sollten trotzdem korrekt geparst werden, Position=%v", s.Frames[0].Motion.Position)
	}
}

func TestDuration(t *testing.T) {
	s := &Script{Frames: []Frame{{Time: 0}, {Time: 1000}, {Time: 5000}}}
	if got := s.Duration(); got != 5000 {
		t.Errorf("erwartet Duration=5000, ist %d", got)
	}
	empty := &Script{}
	if got := empty.Duration(); got != 0 {
		t.Errorf("leeres Skript: erwartet Duration=0, ist %d", got)
	}
}

func TestSaveLoadRoundtrip(t *testing.T) {
	dir := t.TempDir()
	path := filepath.Join(dir, "test.sam")

	original := &Script{
		Version: ScriptVersion,
		Frames: []Frame{
			{Time: 0, Motion: Motion{Type: MotionRhythmic, Position: 20, Energy: 0.72, Confidence: 0.91}},
			{Time: 500, Motion: Motion{Type: MotionRhythmic, Position: 80, Energy: 0.65, Confidence: 0.88}},
		},
		Metadata: Metadata{Creator: "test", Source: "unit-test"},
	}
	if err := original.Save(path); err != nil {
		t.Fatalf("Save fehlgeschlagen: %v", err)
	}
	loaded, err := Load(path)
	if err != nil {
		t.Fatalf("Load fehlgeschlagen: %v", err)
	}
	if len(loaded.Frames) != len(original.Frames) {
		t.Fatalf("Frame-Anzahl weicht ab: %d vs %d", len(loaded.Frames), len(original.Frames))
	}
	for i := range original.Frames {
		if loaded.Frames[i].Time != original.Frames[i].Time {
			t.Errorf("Frame %d: Time weicht ab", i)
		}
		if loaded.Frames[i].Motion.Type != original.Frames[i].Motion.Type {
			t.Errorf("Frame %d: Motion.Type weicht ab", i)
		}
		if loaded.Frames[i].Motion.Position != original.Frames[i].Motion.Position {
			t.Errorf("Frame %d: Motion.Position weicht ab", i)
		}
		if loaded.Frames[i].Motion.Energy != original.Frames[i].Motion.Energy {
			t.Errorf("Frame %d: Motion.Energy weicht ab", i)
		}
	}
	if loaded.Metadata.Creator != "test" {
		t.Errorf("Metadata.Creator ging verloren")
	}
}

func TestLoadMissingFile(t *testing.T) {
	if _, err := Load("/nonexistent/path.sam"); err == nil {
		t.Fatal("erwartet Fehler bei fehlender Datei")
	}
}

func TestLoadMalformedFileOnDisk(t *testing.T) {
	dir := t.TempDir()
	path := filepath.Join(dir, "broken.sam")
	if err := os.WriteFile(path, []byte(`{"version":"0.1","frames":[]}`), 0o644); err != nil {
		t.Fatalf("Testdatei konnte nicht geschrieben werden: %v", err)
	}
	if _, err := Load(path); err == nil {
		t.Fatal("erwartet Fehler bei leeren frames auf Disk")
	}
}
