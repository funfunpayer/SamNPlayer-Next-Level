// Package funscript implementiert einen Parser für das .funscript-Dateiformat.
package funscript

import (
	"encoding/json"
	"fmt"
	"math"
	"os"
	"sort"
)

// Action ist ein einzelner Punkt im Skript.
type Action struct {
	At  int64 `json:"at"`
	Pos int   `json:"pos"`
}

// UnmarshalJSON liest at/pos auch als Kommazahl ("at": 500.4, "pos": 99.6),
// wie manche Werkzeuge sie schreiben, und rundet. Vorher scheiterte daran
// das ganze Skript. Nicht-Zahlen bleiben ein Fehler; geschrieben wird
// weiter mit ganzen Zahlen.
func (a *Action) UnmarshalJSON(b []byte) error {
	var raw struct {
		At  json.Number `json:"at"`
		Pos json.Number `json:"pos"`
	}
	if err := json.Unmarshal(b, &raw); err != nil {
		return err
	}
	at, err := roundNumber(raw.At)
	if err != nil {
		return fmt.Errorf("at: %w", err)
	}
	pos, err := roundNumber(raw.Pos)
	if err != nil {
		return fmt.Errorf("pos: %w", err)
	}
	a.At, a.Pos = at, int(pos)
	return nil
}

func roundNumber(n json.Number) (int64, error) {
	if n == "" {
		return 0, nil
	}
	if i, err := n.Int64(); err == nil {
		return i, nil
	}
	f, err := n.Float64()
	if err != nil {
		return 0, err
	}
	return int64(math.Round(math.Max(-1e15, math.Min(1e15, f)))), nil
}

// MetaDuration is metadata.duration in ms. OFS writes it in seconds, often
// as a fraction (631.8): read like OFS bookmark times (flexTimeMs), and Parse
// scales an integer that is far too short for ms. Informational only - an
// unreadable value ("10:31") becomes 0 instead of failing the whole script,
// which is what the plain int64 did for every such OFS file.
type MetaDuration int64

func (d *MetaDuration) UnmarshalJSON(b []byte) error {
	var t flexTimeMs
	if err := t.UnmarshalJSON(b); err != nil {
		*d = 0
		return nil
	}
	*d = MetaDuration(t)
	return nil
}

// Script ist das geparste .funscript-Dokument.
type Script struct {
	Actions []Action `json:"actions"`
	// Inverted is the official funscript top-level flag: players flip
	// positions (100−pos) at playback. Actions on disk stay as written.
	// Review "Invert" bakes 100−pos into actions and clears this flag so
	// playback does not flip twice.
	Inverted bool `json:"inverted,omitempty"`
	Metadata struct {
		Duration        MetaDuration  `json:"duration"`
		Creator         string        `json:"creator"`
		QualityScore    *float64      `json:"quality_score,omitempty"`
		QualityPassed   *bool         `json:"quality_passed,omitempty"`
		QualityWarnings []string      `json:"quality_warnings,omitempty"`
		Profile         string        `json:"profile,omitempty"`
		DeviceRecipe    *DeviceRecipe `json:"device_recipe,omitempty"`
		SamnAxes        *SamnAxes     `json:"samn_axes,omitempty"`
		TrackingGaps    []TrackingGap `json:"tracking_gaps,omitempty"`
		AIOpinion       *AIOpinion    `json:"ai_opinion,omitempty"`
		AudioCheck      *AudioCheck   `json:"audio_check,omitempty"`
		// Trajectory: optional per-frame tip/partner track positions
		// (MT-Debug Review/Play overlay). Only present when generated
		// with the opt-in "capture trajectory" flag - off by default.
		Trajectory *TrajectoryData `json:"trajectory,omitempty"`
		// ContactMarks: optional tip/contact boxes + classes from Generate
		// when Contact vibration is on. Everyday stroke curve stays tip-CSRT
		// (drive_stroke=false); marks are for feel / later feel-decouple.
		ContactMarks *ContactMarks `json:"contact_marks,omitempty"`
	} `json:"metadata,omitempty"`
}

// TrajectoryPoint is one sampled tip/partner position in video-pixel space
// (0,0 = top-left), timestamped against the same clock as Actions.
type TrajectoryPoint struct {
	AtMs int64   `json:"atMs"`
	X    float64 `json:"x"`
	Y    float64 `json:"y"`
}

// TrajectoryData is the optional MT-Debug trajectory payload: Width/Height
// are the video's pixel dimensions at generation time (the coordinate
// space Tip/Partner points are in). Partner is empty when the script has
// no second tracked point (single-ROI Stroke) or when generated against
// 2+ contact partners (ambiguous "the" partner).
type TrajectoryData struct {
	Width   int               `json:"width"`
	Height  int               `json:"height"`
	Tip     []TrajectoryPoint `json:"tip,omitempty"`
	Partner []TrajectoryPoint `json:"partner,omitempty"`
}

// AIOpinion ist die optionale KI-Zweitmeinung zur Qualität (--ai-quality-
// opinion, generator/ai_quality.py) - rein informativ, verändert
// QualityScore/QualityPassed nicht.
type AIOpinion struct {
	Verdict string `json:"verdict"`
	Reason  string `json:"reason"`
}

// AudioCheck ist das optionale Ergebnis der Audio-Tempo-Plausibilitätsprüfung
// (--audio-check; Go: generator.CheckAudioTempo, Python: audio_check.py) -
// wie AIOpinion rein informativ, verändert QualityScore/QualityPassed nicht.
// Nur vorhanden, wenn die Prüfung tatsächlich lief (ffmpeg + lesbare Audiospur).
//
// Segments / SpeechHoldMs are optional review/chapter taxonomy hints
// (Speech-Hold + holding|gentle|intense|climax). They never invent stroke
// actions — same AUDIO_WORKFLOW rule as the tempo check.
type AudioCheck struct {
	ScriptHz     *float64           `json:"script_hz"`
	AudioHz      *float64           `json:"audio_hz"`
	Warnings     []string           `json:"warnings"`
	Segments     []AudioSegmentHint `json:"segments,omitempty"`
	SpeechHoldMs int64              `json:"speech_hold_ms,omitempty"`
}

// AudioSegmentHint is a review/chapter label from classical speech-vs-impact
// energy (funscript-ai-inspired taxonomy). Informational only.
type AudioSegmentHint struct {
	Label      string `json:"label"` // holding | gentle | intense | climax
	StartMs    int64  `json:"start_ms"`
	EndMs      int64  `json:"end_ms"`
	SpeechHold bool   `json:"speech_hold,omitempty"` // dialogue/quiet hold cue
	Reason     string `json:"reason,omitempty"`
}

func Load(path string) (*Script, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		return nil, fmt.Errorf("funscript: Datei konnte nicht gelesen werden: %w", err)
	}
	return Parse(data)
}

func Parse(data []byte) (*Script, error) {
	var s Script
	if err := json.Unmarshal(data, &s); err != nil {
		return nil, fmt.Errorf("funscript: ungültiges JSON: %w", err)
	}
	if len(s.Actions) == 0 {
		return nil, fmt.Errorf("funscript: keine actions im Skript gefunden")
	}
	for i := range s.Actions {
		if s.Actions[i].Pos < 0 {
			s.Actions[i].Pos = 0
		} else if s.Actions[i].Pos > 100 {
			s.Actions[i].Pos = 100
		}
	}
	sort.Slice(s.Actions, func(i, j int) bool {
		return s.Actions[i].At < s.Actions[j].At
	})
	// Ganzzahlige Sekunden (OFS "duration": 631): als ms wäre das weniger
	// als 1 % der letzten Aktion - so kurz kann die Dauer nicht sein.
	if d, last := int64(s.Metadata.Duration), s.Duration(); d > 0 && d*100 < last {
		s.Metadata.Duration = MetaDuration(d * 1000)
	}
	return &s, nil
}

func (s *Script) Duration() int64 {
	if len(s.Actions) == 0 {
		return 0
	}
	return s.Actions[len(s.Actions)-1].At
}

// PlaybackActions is the stroke list the device and Play curve should use.
// When Inverted is set, positions are 100−pos; the stored Actions are unchanged.
func (s *Script) PlaybackActions() []Action {
	if s == nil {
		return nil
	}
	if !s.Inverted || len(s.Actions) == 0 {
		return s.Actions
	}
	out := make([]Action, len(s.Actions))
	for i, a := range s.Actions {
		pos := 100 - a.Pos
		if pos < 0 {
			pos = 0
		} else if pos > 100 {
			pos = 100
		}
		out[i] = Action{At: a.At, Pos: pos}
	}
	return out
}

// WriteInverted sets or clears the top-level inverted flag without touching
// actions. Cleared after Review Invert bakes 100−pos into the point list.
func WriteInverted(path string, inverted bool) error {
	data, err := os.ReadFile(path)
	if err != nil {
		return fmt.Errorf("funscript: Datei konnte nicht gelesen werden: %w", err)
	}
	var doc map[string]json.RawMessage
	if err := json.Unmarshal(data, &doc); err != nil {
		return fmt.Errorf("funscript: ungültiges JSON: %w", err)
	}
	if !inverted {
		delete(doc, "inverted")
	} else {
		b, err := json.Marshal(true)
		if err != nil {
			return err
		}
		doc["inverted"] = b
	}
	out, err := json.MarshalIndent(doc, "", "  ")
	if err != nil {
		return err
	}
	tmp := path + ".tmp"
	if err := os.WriteFile(tmp, out, 0o644); err != nil {
		return err
	}
	if err := os.Rename(tmp, path); err != nil {
		_ = os.Remove(tmp)
		return err
	}
	return nil
}
