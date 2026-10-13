package funscript

import (
	"encoding/json"
	"os"
	"testing"
)

func TestSaveActionsRoundtrip(t *testing.T) {
	path := writeTestScript(t, `{"actions":[{"at":0,"pos":0},{"at":1000,"pos":100}],"metadata":{}}`)
	if err := SaveActions(path, []Action{
		{At: 500, Pos: 20},
		{At: 0, Pos: 0},
		{At: 1000, Pos: 100},
	}); err != nil {
		t.Fatalf("SaveActions: %v", err)
	}
	s, err := Load(path)
	if err != nil {
		t.Fatalf("Load nach SaveActions: %v", err)
	}
	if len(s.Actions) != 3 {
		t.Fatalf("erwartet 3 Punkte, bekommen %d: %+v", len(s.Actions), s.Actions)
	}
	// SaveActions sortiert nach At - die Eingabe war absichtlich
	// unsortiert (wie während eines Ziehens im Editor).
	if s.Actions[0].At != 0 || s.Actions[1].At != 500 || s.Actions[2].At != 1000 {
		t.Errorf("Punkte nach dem Speichern nicht sortiert: %+v", s.Actions)
	}
}

func TestSaveActionsClampsPosition(t *testing.T) {
	path := writeTestScript(t, `{"actions":[{"at":0,"pos":0}],"metadata":{}}`)
	if err := SaveActions(path, []Action{
		{At: 0, Pos: -10},
		{At: 1000, Pos: 150},
	}); err != nil {
		t.Fatalf("SaveActions: %v", err)
	}
	s, err := Load(path)
	if err != nil {
		t.Fatalf("Load: %v", err)
	}
	if s.Actions[0].Pos != 0 || s.Actions[1].Pos != 100 {
		t.Errorf("Positionen wurden nicht geklemmt: %+v", s.Actions)
	}
}

func TestSaveActionsRejectsEmptyAndNegativeTime(t *testing.T) {
	path := writeTestScript(t, `{"actions":[{"at":0,"pos":0}],"metadata":{}}`)

	if err := SaveActions(path, nil); err == nil {
		t.Error("erwartet Fehler für leere Punktliste")
	}
	if err := SaveActions(path, []Action{{At: -1, Pos: 0}}); err == nil {
		t.Error("erwartet Fehler für negative Zeit")
	}

	// Eine fehlgeschlagene Validierung darf die Datei nicht angerührt haben.
	s, err := Load(path)
	if err != nil {
		t.Fatalf("Load nach fehlgeschlagenem Save: %v", err)
	}
	if len(s.Actions) != 1 || s.Actions[0].At != 0 {
		t.Errorf("Datei wurde trotz Validierungsfehler verändert: %+v", s.Actions)
	}
}

// TestSaveActionsPreservesUnknownFields ist derselbe Kern-Beweis wie
// TestSaveOMarkersPreservesUnknownFields: SaveActions arbeitet auf rohem
// JSON, damit ein Feld, das ein anderes Werkzeug (z.B. FunGen) zusätzlich
// in derselben Datei abgelegt hat - oder O-Marker, die dieses Programm
// selbst über SaveOMarkers geschrieben hat - beim Bearbeiten der Kurve
// NICHT verloren geht.
func TestSaveActionsPreservesUnknownFields(t *testing.T) {
	original := `{
		"actions": [{"at": 0, "pos": 0}, {"at": 1000, "pos": 100}],
		"metadata": {
			"creator": "SamNPlayer",
			"quality_score": 0.9,
			"oMarkers": [{"startMs": 500, "endMs": 900, "kind": "primary", "intensity": 1.0}],
			"fungen_custom_field": {"nested": [1, 2, 3]}
		},
		"top_level_unknown": true
	}`
	path := writeTestScript(t, original)

	if err := SaveActions(path, []Action{{At: 0, Pos: 0}, {At: 2000, Pos: 50}}); err != nil {
		t.Fatalf("SaveActions: %v", err)
	}

	data, err := os.ReadFile(path)
	if err != nil {
		t.Fatalf("Datei lesen: %v", err)
	}
	var doc map[string]json.RawMessage
	if err := json.Unmarshal(data, &doc); err != nil {
		t.Fatalf("ungültiges JSON nach dem Speichern: %v", err)
	}
	if _, ok := doc["top_level_unknown"]; !ok {
		t.Error("top_level_unknown ist nach SaveActions verschwunden")
	}
	var meta map[string]json.RawMessage
	if err := json.Unmarshal(doc["metadata"], &meta); err != nil {
		t.Fatalf("metadata ungültig: %v", err)
	}
	for _, key := range []string{"creator", "quality_score", "oMarkers", "fungen_custom_field"} {
		if _, ok := meta[key]; !ok {
			t.Errorf("metadata.%s ist nach SaveActions verschwunden", key)
		}
	}
}

// Foreign FunGen (or other) scripts must keep their creator on curve edit —
// SaveActions must never invent native_pipeline (Generate-only telemetry).
func TestSaveActionsPreservesForeignCreatorNoNativePipeline(t *testing.T) {
	original := `{
		"actions": [{"at": 0, "pos": 10}, {"at": 800, "pos": 90}],
		"metadata": {
			"creator": "FunGen 2.6.3 (fungen.app)",
			"duration": 50
		}
	}`
	path := writeTestScript(t, original)
	if err := SaveActions(path, []Action{{At: 0, Pos: 10}, {At: 900, Pos: 40}}); err != nil {
		t.Fatalf("SaveActions: %v", err)
	}
	data, err := os.ReadFile(path)
	if err != nil {
		t.Fatal(err)
	}
	var doc map[string]any
	if err := json.Unmarshal(data, &doc); err != nil {
		t.Fatal(err)
	}
	meta, _ := doc["metadata"].(map[string]any)
	if meta["creator"] != "FunGen 2.6.3 (fungen.app)" {
		t.Fatalf("creator overwritten: %v", meta["creator"])
	}
	if _, ok := meta["native_pipeline"]; ok {
		t.Fatal("SaveActions must not invent native_pipeline on a foreign script")
	}
}
