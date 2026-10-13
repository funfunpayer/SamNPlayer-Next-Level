// Package sam definiert SAM: ein internes Bewegungsmodell, reichhaltiger als
// funscript.Action{At, Pos} - siehe docs/SAM_ARCHITECTURE.md für die
// Gesamtrichtung. .funscript bleibt die Kompatibilitätsschicht (siehe
// FromFunscript/ToFunscript in funscript.go dieses Pakets), SAM ersetzt es
// nicht.
//
// Leitprinzip (docs/SAM_ARCHITECTURE.md): .funscript beschreibt, wo ein
// Gerät zu einem Zeitpunkt stehen soll. SAM soll beschreiben, was für eine
// Bewegung stattfinden soll.
package sam

import (
	"encoding/json"
	"fmt"
	"os"
	"sort"
)

// ScriptVersion ist die aktuell erzeugte Schemaversion. Parse lehnt weder
// ältere noch neuere Versionsangaben ab - geschrieben wird immer diese.
const ScriptVersion = "0.1"

// MotionType siehe docs/SAM_ARCHITECTURE.md, "Motion classification"
// (P2.2): die Liste ist Teil des Schemas, ein automatischer Klassifikator
// existiert noch nicht - bis dahin bleibt MotionUnknown der Normalfall für
// jeden heutigen Producer (z.B. FromFunscript).
type MotionType string

const (
	MotionUnknown      MotionType = ""
	MotionStatic       MotionType = "static"
	MotionRhythmic     MotionType = "rhythmic"
	MotionLinear       MotionType = "linear"
	MotionOscillating  MotionType = "oscillating"
	MotionAccelerating MotionType = "accelerating"
	MotionDecelerating MotionType = "decelerating"
	MotionIrregular    MotionType = "irregular"
	MotionCamera       MotionType = "camera_movement"
	MotionTransition   MotionType = "transition"
)

// Motion ist ein einzelner Zustand im SAM-Modell - beschreibt Intent
// ("rhythmisch, Energie 0.72"), nicht nur eine rohe Geräteposition.
//
// Position ist der einzige praktisch immer gesetzte Wert (0-100, wie
// funscript.Action.Pos); alle anderen Felder sind optional (omitempty) -
// noch nicht jedes Feld hat einen Producer, siehe docs/SAM_ARCHITECTURE.md,
// "Deferred". Range/Intensity/Energy/Smoothness/Variation/Tension/
// Anticipation/Confidence sind auf 0.0-1.0 normiert (Konzeptdokument-
// Vorgabe); Direction/Velocity/Acceleration/Tempo haben bewusst keine feste
// Grenze (Tempo ist BPM-artig, z.B. 112).
//
// Neue Felder späterer Versionen dürfen hinzukommen, ohne alte Leser zu
// brechen: encoding/json ignoriert unbekannte JSON-Felder beim Unmarshal
// automatisch - kein Reader dieses Pakets muss dafür geändert werden.
type Motion struct {
	Type         MotionType `json:"type,omitempty"`
	Position     float64    `json:"position"`
	Direction    float64    `json:"direction,omitempty"`
	Velocity     float64    `json:"velocity,omitempty"`
	Acceleration float64    `json:"acceleration,omitempty"`
	Range        float64    `json:"range,omitempty"`
	Intensity    float64    `json:"intensity,omitempty"`
	Energy       float64    `json:"energy,omitempty"`
	Tempo        float64    `json:"tempo,omitempty"`
	Smoothness   float64    `json:"smoothness,omitempty"`
	Variation    float64    `json:"variation,omitempty"`
	Tension      float64    `json:"tension,omitempty"`
	Anticipation float64    `json:"anticipation,omitempty"`
	Confidence   float64    `json:"confidence,omitempty"`
}

// Clamp begrenzt Position auf [0,100] und die normierten Felder auf [0,1] -
// wie funscript.Parse es bereits für Pos tut, statt out-of-range-Werte
// (z.B. aus einer fehlerhaften Fremdquelle) abzulehnen.
func (m *Motion) Clamp() {
	m.Position = clampRange(m.Position, 0, 100)
	m.Range = clamp01(m.Range)
	m.Intensity = clamp01(m.Intensity)
	m.Energy = clamp01(m.Energy)
	m.Smoothness = clamp01(m.Smoothness)
	m.Variation = clamp01(m.Variation)
	m.Tension = clamp01(m.Tension)
	m.Anticipation = clamp01(m.Anticipation)
	m.Confidence = clamp01(m.Confidence)
}

func clamp01(v float64) float64 { return clampRange(v, 0, 1) }

func clampRange(v, lo, hi float64) float64 {
	if v < lo {
		return lo
	}
	if v > hi {
		return hi
	}
	return v
}

// Frame ist ein Zeitpunkt im Skript. Das JSON-Feld heißt "time" statt "at",
// um dem Konzeptdokument-Beispiel (docs/SAM_ARCHITECTURE.md) zu folgen -
// SAM ist bewusst kein bloßes Alias für funscript.Action.
type Frame struct {
	Time   int64  `json:"time"`
	Motion Motion `json:"motion"`
}

// Metadata bleibt für v0.1 bewusst minimal, wie funscript.Script.Metadata
// über omitempty erweiterbar.
type Metadata struct {
	Creator string `json:"creator,omitempty"`
	// Source nennt die Herkunft, z.B. "funscript-import" - informativ,
	// keine feste Werteliste in v0.1.
	Source string `json:"source,omitempty"`
	// Profile spiegelt funscript.Script.Metadata.Profile (z.B. "tj") -
	// bestimmt beim Zurückwandeln (funscript.go, ToFunscript), ob die
	// Wiedergabe die Abstand- statt Hub-Zuordnung verwendet
	// (funscript.IsDistanceProfile). Ohne dieses Feld würde ein
	// SAM-Roundtrip die Hardware-Zuordnung eines Tf/Tj-Skripts
	// stillschweigend verlieren - gefunden beim Testen mit echten
	// tj-Skripten aus diesem Projekt.
	Profile string `json:"profile,omitempty"`
	// DeviceRecipe spiegelt funscript.DeviceRecipe. Eigener Typ statt
	// Wiederverwendung von funscript.DeviceRecipe, damit dieses Paket
	// unabhängig vom funscript-Paket bleibt (siehe Paket-Docstring) -
	// funscript.go übernimmt die Feld-für-Feld-Umwandlung.
	DeviceRecipe *DeviceRecipe `json:"device_recipe,omitempty"`
	// TrackingGaps: Tracker-Verlustfenster aus der Generierung — Enrich
	// setzt dort Confidence=0; Kontakt-Vibration bleibt stumm (siehe
	// funscript.TrackingGap / mapper.go).
	TrackingGaps []TrackingGap `json:"tracking_gaps,omitempty"`
}

// TrackingGap spiegelt funscript.TrackingGap für den SAM-Roundtrip.
type TrackingGap struct {
	StartMs int64 `json:"start_ms"`
	EndMs   int64 `json:"end_ms"`
}

// DeviceRecipe spiegelt funscript.DeviceRecipe für den Roundtrip - siehe
// dortige Feldkommentare (funscript/recipe.go) für die Bedeutung der
// einzelnen Werte.
type DeviceRecipe struct {
	Sync                  string  `json:"sync"`
	MinSuction            float64 `json:"min_suction"`
	TickMs                int64   `json:"tick_ms"`
	MaxSpeed              float64 `json:"max_speed"`
	Smoothing             float64 `json:"smoothing"`
	ContactVibration      bool    `json:"contact_vibration,omitempty"`
	ContactVibrationSpan  float64 `json:"contact_vibration_span,omitempty"`
	ContactVibrationCurve string  `json:"contact_vibration_curve,omitempty"`
}

// Script ist das geparste SAM-Motion-Script-Dokument.
type Script struct {
	Version  string   `json:"version"`
	Frames   []Frame  `json:"frames"`
	Metadata Metadata `json:"metadata,omitempty"`
}

// Load liest und parst eine .sam-Datei.
func Load(path string) (*Script, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		return nil, fmt.Errorf("sam: Datei konnte nicht gelesen werden: %w", err)
	}
	return Parse(data)
}

// Parse liest ein SAM Motion Script aus JSON-Bytes. Frames werden nach Time
// sortiert (Reihenfolge in der Quelle ist nicht garantiert) und ihre
// normierten Felder auf ihren gültigen Bereich geklemmt.
func Parse(data []byte) (*Script, error) {
	var s Script
	if err := json.Unmarshal(data, &s); err != nil {
		return nil, fmt.Errorf("sam: ungültiges JSON: %w", err)
	}
	if s.Version == "" {
		return nil, fmt.Errorf("sam: fehlende Versionsangabe")
	}
	if len(s.Frames) == 0 {
		return nil, fmt.Errorf("sam: keine frames im Skript gefunden")
	}
	for i := range s.Frames {
		s.Frames[i].Motion.Clamp()
	}
	sort.Slice(s.Frames, func(i, j int) bool {
		return s.Frames[i].Time < s.Frames[j].Time
	})
	return &s, nil
}

// Save schreibt das Skript als eingerücktes JSON.
func (s *Script) Save(path string) error {
	data, err := json.MarshalIndent(s, "", "  ")
	if err != nil {
		return fmt.Errorf("sam: konnte nicht serialisiert werden: %w", err)
	}
	if err := os.WriteFile(path, data, 0o644); err != nil {
		return fmt.Errorf("sam: Datei konnte nicht geschrieben werden: %w", err)
	}
	return nil
}

// Duration ist der Zeitstempel des letzten Frames (0 bei leerem Skript).
func (s *Script) Duration() int64 {
	if len(s.Frames) == 0 {
		return 0
	}
	return s.Frames[len(s.Frames)-1].Time
}
