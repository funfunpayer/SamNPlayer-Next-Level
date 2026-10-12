package funscript

import (
	"encoding/json"
	"os"
	"path/filepath"
	"testing"
)

func writeTestScript(t *testing.T, content string) string {
	t.Helper()
	path := filepath.Join(t.TempDir(), "test.funscript")
	if err := os.WriteFile(path, []byte(content), 0644); err != nil {
		t.Fatalf("Testskript anlegen: %v", err)
	}
	return path
}

func TestLoadOMarkersEmptyWhenAbsent(t *testing.T) {
	path := writeTestScript(t, `{"actions":[{"at":0,"pos":0}],"metadata":{"creator":"x"}}`)
	markers, err := LoadOMarkers(path)
	if err != nil {
		t.Fatalf("LoadOMarkers: %v", err)
	}
	if len(markers) != 0 {
		t.Errorf("erwartet keine Marker, bekommen: %+v", markers)
	}
}

func TestSaveAndLoadOMarkersRoundtrip(t *testing.T) {
	path := writeTestScript(t, `{"actions":[{"at":0,"pos":0}],"metadata":{"creator":"x"}}`)
	markers := []OMarker{
		{StartMs: 5000, EndMs: 8000, Kind: OMarkerSecondary, Intensity: 0.4},
		{StartMs: 20000, EndMs: 22000, Kind: OMarkerPrimary, Intensity: 1.0},
	}
	if err := SaveOMarkers(path, markers); err != nil {
		t.Fatalf("SaveOMarkers: %v", err)
	}
	got, err := LoadOMarkers(path)
	if err != nil {
		t.Fatalf("LoadOMarkers: %v", err)
	}
	if len(got) != 2 {
		t.Fatalf("erwartet 2 Marker, bekommen %d: %+v", len(got), got)
	}
	// SaveOMarkers sortiert nach StartMs - die beiden Eingaben waren schon
	// sortiert, hier trotzdem explizit geprüft statt nur die Länge.
	if got[0].Kind != OMarkerSecondary || got[0].StartMs != 5000 {
		t.Errorf("erster Marker falsch: %+v", got[0])
	}
	if got[1].Kind != OMarkerPrimary || got[1].StartMs != 20000 {
		t.Errorf("zweiter Marker falsch: %+v", got[1])
	}
}

// TestSaveOMarkersPreservesUnknownFields ist der eigentliche Kern dieses
// Designs: SaveOMarkers arbeitet bewusst auf rohem JSON statt über den
// typisierten Script-Typ, damit ein Feld, das ein anderes Werkzeug (z.B.
// FunGen) zusätzlich in derselben Datei abgelegt hat, beim Schreiben der
// O-Marker NICHT verloren geht. Dieser Test beweist genau das, statt es
// nur zu behaupten.
func TestSaveOMarkersPreservesUnknownFields(t *testing.T) {
	original := `{
		"actions": [{"at": 0, "pos": 0}, {"at": 1000, "pos": 100}],
		"metadata": {
			"creator": "SamNPlayer",
			"quality_score": 0.9,
			"fungen_custom_field": {"nested": [1, 2, 3]},
			"another_unknown_field": "bleibt erhalten"
		},
		"top_level_unknown": true
	}`
	path := writeTestScript(t, original)

	if err := SaveOMarkers(path, []OMarker{
		{StartMs: 1000, EndMs: 2000, Kind: OMarkerPrimary, Intensity: 0.9},
	}); err != nil {
		t.Fatalf("SaveOMarkers: %v", err)
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
		t.Error("top_level_unknown ist nach SaveOMarkers verschwunden")
	}
	var meta map[string]json.RawMessage
	if err := json.Unmarshal(doc["metadata"], &meta); err != nil {
		t.Fatalf("metadata ungültig: %v", err)
	}
	for _, key := range []string{"creator", "quality_score", "fungen_custom_field", "another_unknown_field"} {
		if _, ok := meta[key]; !ok {
			t.Errorf("metadata.%s ist nach SaveOMarkers verschwunden", key)
		}
	}

	// Und die eigentlichen actions dürfen sich natürlich auch nicht
	// verändert haben.
	s, err := Load(path)
	if err != nil {
		t.Fatalf("Load nach SaveOMarkers: %v", err)
	}
	if len(s.Actions) != 2 || s.Actions[0].At != 0 || s.Actions[1].At != 1000 {
		t.Errorf("actions wurden verändert: %+v", s.Actions)
	}
}

func TestSaveOMarkersRejectsInvalidMarker(t *testing.T) {
	path := writeTestScript(t, `{"actions":[{"at":0,"pos":0}],"metadata":{}}`)

	cases := []struct {
		name string
		m    OMarker
	}{
		{"Ende vor Start", OMarker{StartMs: 2000, EndMs: 1000, Kind: OMarkerPrimary, Intensity: 0.5}},
		{"unbekannte Art", OMarker{StartMs: 1000, EndMs: 2000, Kind: "tertiary", Intensity: 0.5}},
		{"Intensität zu hoch", OMarker{StartMs: 1000, EndMs: 2000, Kind: OMarkerPrimary, Intensity: 1.5}},
		{"Intensität negativ", OMarker{StartMs: 1000, EndMs: 2000, Kind: OMarkerPrimary, Intensity: -0.1}},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			if err := SaveOMarkers(path, []OMarker{tc.m}); err == nil {
				t.Errorf("erwartet Fehler für %+v, bekam keinen", tc.m)
			}
		})
	}

	// Eine fehlgeschlagene Validierung darf die Datei nicht angerührt haben.
	markers, err := LoadOMarkers(path)
	if err != nil {
		t.Fatalf("LoadOMarkers nach fehlgeschlagenem Save: %v", err)
	}
	if len(markers) != 0 {
		t.Errorf("Datei wurde trotz Validierungsfehler verändert: %+v", markers)
	}
}

func TestSaveOMarkersEmptyListClearsMarkers(t *testing.T) {
	path := writeTestScript(t, `{"actions":[{"at":0,"pos":0}],"metadata":{}}`)
	if err := SaveOMarkers(path, []OMarker{
		{StartMs: 1000, EndMs: 2000, Kind: OMarkerPrimary, Intensity: 1.0},
	}); err != nil {
		t.Fatalf("SaveOMarkers (setzen): %v", err)
	}
	if err := SaveOMarkers(path, nil); err != nil {
		t.Fatalf("SaveOMarkers (leeren): %v", err)
	}
	markers, err := LoadOMarkers(path)
	if err != nil {
		t.Fatalf("LoadOMarkers: %v", err)
	}
	if len(markers) != 0 {
		t.Errorf("erwartet leere Liste nach dem Leeren, bekommen: %+v", markers)
	}
}
