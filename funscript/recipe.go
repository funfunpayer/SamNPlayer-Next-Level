package funscript

import "strings"

// Profile-Kürzel. tf und tj sind zwei Namen für dasselbe Rezept:
// Abstand zweier Regionen, auf dem Neo 2 nur der Sog (Position),
// Vibration bleibt aus.
const (
	ProfileStandard = "standard"
	ProfileWeich    = "weich"
	ProfileTF       = "tf"
	ProfileTJ       = "tj"
)

// NormalizeProfile macht aus Schreibweisen einen kanonischen Namen.
func NormalizeProfile(name string) string {
	switch strings.ToLower(strings.TrimSpace(name)) {
	case "tf", "tj":
		return ProfileTJ
	case "weich":
		return ProfileWeich
	case "", "standard":
		return ProfileStandard
	default:
		return strings.ToLower(strings.TrimSpace(name))
	}
}

// IsDistanceProfile: Signal ist der Abstand zweier Regionen, Sog führt.
func IsDistanceProfile(name string) bool {
	n := NormalizeProfile(name)
	return n == ProfileTJ || n == ProfileTF
}

// IsStrokeProfile: product stroke family (hub / soft / autotune). Contact
// vibration is a feel layer on these — not Tf/Tj-only (TFTJ step 6).
func IsStrokeProfile(name string) bool {
	n := NormalizeProfile(name)
	if n == ProfileStandard || n == ProfileWeich {
		return true
	}
	return strings.EqualFold(strings.TrimSpace(name), "autotune")
}

// AllowsContactSettings: Play may edit contact recipe for stroke or distance
// scripts. Rejects unknown/empty non-stroke profiles without a clear family.
func AllowsContactSettings(name string) bool {
	return IsDistanceProfile(name) || IsStrokeProfile(name)
}

// DefaultSyncForProfile fills an empty device_recipe.sync from the profile
// family. Distance (Tf/Tj) needs suction_position; stroke stays independent.
// Never invent suction_position for Stroke — that drives the wrong actuator.
func DefaultSyncForProfile(profile string) string {
	if IsDistanceProfile(profile) {
		return SyncSuctionPosition.String()
	}
	return SyncIndependent.String()
}

// DeviceRecipe ist das, was in die Funscript-Metadata geschrieben wird,
// damit die Wiedergabe ohne extra Klick denselben Aktor trifft.
type DeviceRecipe struct {
	Sync       string  `json:"sync"`
	MinSuction float64 `json:"min_suction"`
	TickMs     int64   `json:"tick_ms"`
	MaxSpeed   float64 `json:"max_speed"`
	Smoothing  float64 `json:"smoothing"`

	// ContactVibration: optional on any profile (Tf/Tj distance or stroke).
	// When set, playback adds depth/contact vibration — see MapOptions.
	// Omitted/false: Tf/Tj stays silent; stroke profiles keep speed-based vibe.
	ContactVibration bool `json:"contact_vibration,omitempty"`

	// ContactVibrationSpan / Curve: nur sinnvoll mit ContactVibration.
	// Fehlen → Mapper-Defaults (0.75 / linear). Siehe MapOptions.
	ContactVibrationSpan  float64 `json:"contact_vibration_span,omitempty"`
	ContactVibrationCurve string  `json:"contact_vibration_curve,omitempty"`

	// PlaybackSource: "recipe" (default) derives vibe/suction from general
	// actions; "axes" drives Neo 2 from metadata.samn_axes. See docs/MULTI_AXIS.md.
	PlaybackSource string `json:"playback_source,omitempty"`
}

// RecipeFor liefert MapOptions für ein Profil.
// tf/tj: Sog folgt der Position (Kompression), Vibration bleibt 0, außer der
// Aufrufer setzt zusätzlich MapOptions.ContactVibration (siehe dort) -
// leichter Sog-Boden damit der Kontakt nicht abreisst.
func RecipeFor(name string) MapOptions {
	opts := DefaultMapOptions()
	switch NormalizeProfile(name) {
	case ProfileTJ, ProfileTF:
		opts.Sync = SyncSuctionPosition
		opts.MinVibration = 0
		opts.MinSuction = 0.20
		opts.TickMs = 50
		opts.MaxSpeed = 0.50
		opts.Smoothing = 0.22
	case ProfileWeich:
		opts.Sync = SyncIndependent
		opts.MinVibration = 0.12
		opts.MinSuction = 0.10
		opts.Smoothing = 0.28
	}
	return opts
}

// RecipeMeta ist die JSON-Seite von RecipeFor.
func RecipeMeta(name string) DeviceRecipe {
	o := RecipeFor(name)
	return DeviceRecipe{
		Sync:       o.Sync.String(),
		MinSuction: o.MinSuction,
		TickMs:     o.TickMs,
		MaxSpeed:   o.MaxSpeed,
		Smoothing:  o.Smoothing,
	}
}

// ApplyRecipe füllt nur Felder, die der Aufrufer nicht gesetzt hat.
func ApplyRecipe(base MapOptions, name string, syncSet bool) MapOptions {
	if !IsDistanceProfile(name) && NormalizeProfile(name) != ProfileWeich {
		return base
	}
	r := RecipeFor(name)
	if !syncSet {
		base.Sync = r.Sync
	}
	if base.MinSuction == 0 && r.MinSuction > 0 {
		base.MinSuction = r.MinSuction
	}
	return base
}
