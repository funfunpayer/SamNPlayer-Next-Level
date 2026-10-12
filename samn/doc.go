// Package samn is the SamNPlayer native script document (.samn).
//
// .samn is the source of truth for Neo 2 work (general + vibration + suction,
// recipe, chapters, strength presets). Community .funscript is import/export
// only — see docs/SAMN_FORMAT.md. Do not confuse with funscript.Project
// sidecars (*.snp.json).
package samn

import (
	"fmt"
	"path/filepath"
	"strings"

	"github.com/funfunpayer/SamNPlayer/funscript"
)

const (
	Kind           = "samnplayer.script"
	CurrentVersion = 1

	PlaybackRecipe = funscript.PlaybackSourceRecipe
	PlaybackAxes   = funscript.PlaybackSourceAxes

	Ext = ".samn"
)

// Point is one timed position (0-100), same semantics as funscript.Action.
type Point = funscript.Action

// StrengthPreset scales channels at playback without rewriting curves.
type StrengthPreset struct {
	Name           string  `json:"name"`
	VibrationScale float64 `json:"vibrationScale"`
	SuctionScale   float64 `json:"suctionScale"`
	ContactSpan    float64 `json:"contactSpan,omitempty"`
	ContactCurve   string  `json:"contactCurve,omitempty"`
}

// Document is the native script file.
type Document struct {
	Version int    `json:"version"`
	Kind    string `json:"kind"`

	Creator    string `json:"creator,omitempty"`
	DurationMs int64  `json:"durationMs,omitempty"`
	VideoPath  string `json:"videoPath,omitempty"`
	Profile    string `json:"profile,omitempty"`

	PlaybackSource string                 `json:"playbackSource,omitempty"`
	Recipe         funscript.DeviceRecipe `json:"recipe,omitempty"`

	General   []Point `json:"general"`
	Vibration []Point `json:"vibration,omitempty"`
	Suction   []Point `json:"suction,omitempty"`

	Chapters     []funscript.ChapterMark `json:"chapters,omitempty"`
	Bookmarks    []funscript.Bookmark    `json:"bookmarks,omitempty"`
	OMarkers     []funscript.OMarker     `json:"oMarkers,omitempty"`
	TrackingGaps []funscript.TrackingGap `json:"trackingGaps,omitempty"`

	StrengthPresets []StrengthPreset `json:"strengthPresets,omitempty"`
	ActiveStrength  string           `json:"activeStrength,omitempty"`

	// ContactMarks + Trajectory: Everyday feel (Stage A) and Play overlays.
	// Must survive companion .samn write — Play prefers .samn over .funscript.
	ContactMarks *funscript.ContactMarks   `json:"contactMarks,omitempty"`
	Trajectory   *funscript.TrajectoryData `json:"trajectory,omitempty"`

	// SceneMap: optional rhythm heatmap + marks (docs/SCENE_MAP_PLAN.md M4).
	// Written only when a map exists. Never exported to community .funscript.
	SceneMap *funscript.SceneMapData `json:"sceneMap,omitempty"`

	// Optional quality copy from generation (informational).
	QualityScore    *float64 `json:"qualityScore,omitempty"`
	QualityPassed   *bool    `json:"qualityPassed,omitempty"`
	QualityWarnings []string `json:"qualityWarnings,omitempty"`
}

// IsSamnPath reports a native script path.
func IsSamnPath(path string) bool {
	return strings.EqualFold(filepath.Ext(path), Ext)
}

// CompanionSamnPath returns path with .samn extension (beside a funscript/video).
func CompanionSamnPath(path string) string {
	ext := filepath.Ext(path)
	return path[:len(path)-len(ext)] + Ext
}

// CompanionFunscriptPath returns path with .funscript extension.
func CompanionFunscriptPath(path string) string {
	ext := filepath.Ext(path)
	return path[:len(path)-len(ext)] + ".funscript"
}

// Normalize fills defaults and clamps curves.
func (d *Document) Normalize() error {
	if d == nil {
		return fmt.Errorf("samn: nil document")
	}
	if d.Version == 0 {
		d.Version = CurrentVersion
	}
	if d.Version != CurrentVersion {
		return fmt.Errorf("samn: unsupported version %d (want %d)", d.Version, CurrentVersion)
	}
	if d.Kind == "" {
		d.Kind = Kind
	}
	if d.Kind != Kind {
		return fmt.Errorf("samn: unexpected kind %q", d.Kind)
	}
	if len(d.General) == 0 {
		return fmt.Errorf("samn: general curve is required")
	}
	var err error
	if d.General, err = clampPoints(d.General); err != nil {
		return err
	}
	if len(d.Vibration) > 0 {
		if d.Vibration, err = clampPoints(d.Vibration); err != nil {
			return err
		}
	}
	if len(d.Suction) > 0 {
		if d.Suction, err = clampPoints(d.Suction); err != nil {
			return err
		}
	}
	d.PlaybackSource = funscript.NormalizePlaybackSource(d.PlaybackSource)
	d.Profile = funscript.NormalizeProfile(d.Profile)
	if d.DurationMs <= 0 {
		d.DurationMs = d.General[len(d.General)-1].At
	}
	for i, m := range d.OMarkers {
		if err := m.Validate(); err != nil {
			return fmt.Errorf("samn: oMarkers[%d]: %w", i, err)
		}
	}
	return nil
}

func clampPoints(in []Point) ([]Point, error) {
	return funscript.SanitizeActions(in)
}
