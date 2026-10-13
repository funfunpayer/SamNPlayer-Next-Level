package funscript

import (
	"encoding/json"
	"fmt"
	"os"
	"sort"
)

// OMarker ist ein vom Nutzer (oder später einer KI-Erkennung) gesetzter
// Zeitbereich, der als authored data direkt im Skript mitgeführt wird -
// anders als die bestehende, per Sidecar-Datei gespeicherte Live-Erkennung
// für Extended-O (siehe cmd/gui-wails/app_markers.go, Marker/markerPath):
// diese bleibt unverändert bestehen ("da soll es drin bleiben, aber
// erkannt werden" - additiv, kein Ersatz). OMarker ist stattdessen Teil
// des Skripts selbst, sichtbar für jeden, der die Datei später öffnet.
//
// Ein "primary"-Marker markiert den Höhepunkt; optionale "secondary"-
// Marker liegen früher in der Szene bei geringerer Intensität ("nicht so
// doll").
type OMarker struct {
	StartMs   int64   `json:"startMs"`
	EndMs     int64   `json:"endMs"`
	Kind      string  `json:"kind"`      // "primary" | "secondary"
	Intensity float64 `json:"intensity"` // 0..1, nur informativ - beeinflusst noch keine Wiedergabe
}

const (
	OMarkerPrimary   = "primary"
	OMarkerSecondary = "secondary"
)

// Validate prüft ein einzelnes OMarker auf in sich konsistente Werte -
// wird von SaveOMarkers für jeden Eintrag aufgerufen, damit keine kaputten
// Marker in die Datei geschrieben werden.
func (m OMarker) Validate() error {
	if m.EndMs <= m.StartMs {
		return fmt.Errorf("funscript: O-Marker Ende (%dms) muss nach dem Start (%dms) liegen", m.EndMs, m.StartMs)
	}
	if m.Kind != OMarkerPrimary && m.Kind != OMarkerSecondary {
		return fmt.Errorf("funscript: unbekannte O-Marker-Art %q (erwartet %q oder %q)", m.Kind, OMarkerPrimary, OMarkerSecondary)
	}
	if m.Intensity < 0 || m.Intensity > 1 {
		return fmt.Errorf("funscript: O-Marker-Intensität %v außerhalb von 0..1", m.Intensity)
	}
	return nil
}

// LoadOMarkers liest nur die oMarkers aus der metadata eines .funscript -
// über eine typisierte Teilstruktur statt des vollen Script-Typs, damit
// unbekannte Felder gar nicht erst eingelesen (und beim nächsten Save
// potenziell verloren) werden müssen; SaveOMarkers unten arbeitet ohnehin
// auf rohem JSON weiter. Liefert eine leere Liste (kein Fehler), wenn die
// Datei noch keine oMarkers hat.
func LoadOMarkers(path string) ([]OMarker, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		return nil, fmt.Errorf("funscript: Datei konnte nicht gelesen werden: %w", err)
	}
	var doc struct {
		Metadata struct {
			OMarkers []OMarker `json:"oMarkers"`
		} `json:"metadata"`
	}
	if err := json.Unmarshal(data, &doc); err != nil {
		return nil, fmt.Errorf("funscript: ungültiges JSON: %w", err)
	}
	return doc.Metadata.OMarkers, nil
}

// SaveOMarkers schreibt die O-Marker in die metadata eines bestehenden
// .funscript zurück - OHNE die Datei über den typisierten Script-Typ neu
// zu serialisieren. Das ist bewusst so: Script.Metadata kennt nur die
// Felder, die dieses Programm selbst braucht (device_recipe,
// quality_score, ...) - ein anderes Werkzeug (etwa FunGen) kann zusätzliche
// eigene Felder in derselben Datei abgelegt haben. Ein naives
// Read-into-struct-then-Marshal würde jedes unbekannte Feld stillschweigend
// verwerfen. Stattdessen wird nur der "oMarkers"-Schlüssel innerhalb von
// "metadata" gesetzt/ersetzt, alles andere bleibt Byte für Byte erhalten.
func SaveOMarkers(path string, markers []OMarker) error {
	for i, m := range markers {
		if err := m.Validate(); err != nil {
			return fmt.Errorf("funscript: O-Marker %d: %w", i, err)
		}
	}
	sorted := append([]OMarker(nil), markers...)
	sort.Slice(sorted, func(i, j int) bool { return sorted[i].StartMs < sorted[j].StartMs })

	data, err := os.ReadFile(path)
	if err != nil {
		return fmt.Errorf("funscript: Datei konnte nicht gelesen werden: %w", err)
	}
	var doc map[string]json.RawMessage
	if err := json.Unmarshal(data, &doc); err != nil {
		return fmt.Errorf("funscript: ungültiges JSON: %w", err)
	}
	var meta map[string]json.RawMessage
	if raw, ok := doc["metadata"]; ok {
		if err := json.Unmarshal(raw, &meta); err != nil {
			return fmt.Errorf("funscript: ungültige metadata: %w", err)
		}
	}
	if meta == nil {
		meta = map[string]json.RawMessage{}
	}
	markersJSON, err := json.Marshal(sorted)
	if err != nil {
		return err
	}
	meta["oMarkers"] = markersJSON
	metaJSON, err := json.Marshal(meta)
	if err != nil {
		return err
	}
	doc["metadata"] = metaJSON

	out, err := json.MarshalIndent(doc, "", "  ")
	if err != nil {
		return err
	}
	return writeFileAtomic(path, out, 0o644)
}
