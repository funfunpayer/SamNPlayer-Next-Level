package funscript

import (
	"fmt"
	"math"
	"strings"
)

type SyncMode int

const (
	SyncIndependent SyncMode = iota
	SyncSynchronized
	SyncAlternating
	SyncVibrationOnly
	SyncSuctionOnly
	SyncSuctionPosition
)

func ParseSyncMode(s string) (SyncMode, error) {
	switch s {
	case "", "independent":
		return SyncIndependent, nil
	case "synchronized":
		return SyncSynchronized, nil
	case "alternating":
		return SyncAlternating, nil
	case "vibration_only":
		return SyncVibrationOnly, nil
	case "suction_only":
		return SyncSuctionOnly, nil
	case "suction_position":
		return SyncSuctionPosition, nil
	default:
		return SyncIndependent, fmt.Errorf("funscript: unbekannter sync-mode %q", s)
	}
}

func (m SyncMode) String() string {
	switch m {
	case SyncSynchronized:
		return "synchronized"
	case SyncAlternating:
		return "alternating"
	case SyncVibrationOnly:
		return "vibration_only"
	case SyncSuctionOnly:
		return "suction_only"
	case SyncSuctionPosition:
		return "suction_position"
	default:
		return "independent"
	}
}

type Frame struct {
	At        int64
	Vibration float64
	Suction   float64
}

// TrackingGap is a time window where at least one of the two Tf/Tj trackers
// lost its target. Contact vibration must stay off there — holding the last
// known distance would otherwise keep buzzing as if contact continued.
type TrackingGap struct {
	StartMs int64 `json:"start_ms"`
	EndMs   int64 `json:"end_ms"`
}

// Contact-curve names persisted in device_recipe.contact_vibration_curve.
const (
	ContactCurveLinear  = "linear"
	ContactCurveSoft    = "soft"    // weicher Einstieg: t²
	ContactCurvePeak    = "peak"    // stärkerer Peak: √t
	ContactCurveImpulse = "impulse" // experiment: quiet until deep, then sharp (contact/peaks)
)

// ImpulseContactThreshold: below this linear proximity t, impulse vib is 0.
// Above it, vib rises as a cubic — sits on strong peaks, not continuous fill.
const ImpulseContactThreshold = 0.55

// SpatialDepthWeight: when tip↔mark geometry is available (S2 prefer-spatial),
// depth envelope is attenuated so spatial contact events dominate the vib
// track — less “second stroke curve”. Between grazes, depth still contributes
// weakly (model B peak fill). 1.0 would restore pre-S2 max(depth, spatial).
const SpatialDepthWeight = 0.30

// SpatialContactEnvelopeSmooth: snappier attack/decay on spatial hits than
// DefaultContactEnvelopeSmooth — short clear buzz on tip↔mark graze.
const SpatialContactEnvelopeSmooth = 0.18

// DefaultContactEnvelopeSmooth: kurze Extra-Glättung nur für die
// Kontakt-Vibrationshüllkurve (Tracker-Jitter), unabhängig vom Sog-
// Smoothing der Recipe. Höher = träger.
const DefaultContactEnvelopeSmooth = 0.45

type MapOptions struct {
	TickMs       int64
	MaxSpeed     float64
	MinVibration float64
	MinSuction   float64
	Smoothing    float64
	Sync         SyncMode

	// ContactVibration: when true, vibration follows the top slice of this
	// script's own pos range (deep / near-contact). Works for SyncSuctionPosition
	// (Tf/Tj distance) and for stroke profiles (SyncIndependent / weich): at
	// depth the contact envelope drives vibe; elsewhere Tf/Tj stays silent and
	// stroke profiles keep speed-based vibe. See docs/TFTJ_PROFILE_DIRECTION.md.
	ContactVibration bool

	// ContactVibrationSpan (0-1): welcher Anteil des Positions-Spektrums
	// als "Kontakt" zählt. 0 / außerhalb → DefaultContactVibrationSpan.
	// Niedriger = früher an; höher = nur tief.
	ContactVibrationSpan float64

	// ContactVibrationCurve: "linear" (default), "soft", "peak", "impulse".
	ContactVibrationCurve string

	// ContactVibrationEnvelope: 0 → DefaultContactEnvelopeSmooth. Nur für
	// die Vibrationsspur bei Kontakt (Sog behält Smoothing).
	ContactVibrationEnvelope float64

	// TrackingGaps: Tracker-Verlustfenster — Vibration aus, Sog unverändert.
	TrackingGaps []TrackingGap

	// UseExplicitAxes: when true, vibe/suction come from VibrationAxis /
	// SuctionAxis (0-100) instead of Sync mapping on general actions.
	// See docs/MULTI_AXIS.md. Recipe Sync still influences floors/smoothing.
	UseExplicitAxes bool
	VibrationAxis   []Action
	SuctionAxis     []Action
}

// DefaultContactVibrationSpan: oberstes Viertel des beobachteten
// Positions-Spektrums. Bewusst hoch gewählt statt eines festen
// Pixel-/Positionswerts, damit die Erkennung pro Video adaptiv bleibt.
const DefaultContactVibrationSpan = 0.75

// contactVibrationMinSpan: liegt das gesamte Positions-Spektrum des Skripts
// darunter, gibt es zu wenig Variation, um "Kontakt" von normaler Bewegung
// zu unterscheiden - Vibration bleibt dann komplett aus, statt durchgehend
// zu brummen.
const contactVibrationMinSpan = 5.0

func DefaultMapOptions() MapOptions {
	return MapOptions{
		TickMs: 50, MaxSpeed: 0.6, MinVibration: 0.15,
		MinSuction: 0, Smoothing: 0.3, Sync: SyncIndependent,
	}
}

// NormalizeContactCurve liefert einen kanonischen Kurvennamen.
func NormalizeContactCurve(name string) string {
	switch strings.ToLower(strings.TrimSpace(name)) {
	case ContactCurveSoft, "soft_entry", "weicher":
		return ContactCurveSoft
	case ContactCurvePeak, "strong_peak", "peaky":
		return ContactCurvePeak
	case ContactCurveImpulse, "events", "contact_events", "peak_emphasis":
		return ContactCurveImpulse
	default:
		return ContactCurveLinear
	}
}

// EffectiveContactSpan klammert die Empfindlichkeit in einen sinnvollen
// Bereich; 0 oder ungültig → Default.
func EffectiveContactSpan(span float64) float64 {
	if span <= 0 || span >= 1 {
		return DefaultContactVibrationSpan
	}
	if span < 0.4 {
		return 0.4
	}
	if span > 0.95 {
		return 0.95
	}
	return span
}

// applyContactCurve formt den linearen Nähe-Anteil t∈[0,1] um.
func applyContactCurve(t float64, curve string) float64 {
	t = clamp01(t)
	switch NormalizeContactCurve(curve) {
	case ContactCurveSoft:
		return t * t
	case ContactCurvePeak:
		return math.Sqrt(t)
	case ContactCurveImpulse:
		// Quiet mid-window; sharp rise near contact/peak — Advanced experiment
		// (docs/vibration-contact-model.md S1). Not Everyday default.
		if t < ImpulseContactThreshold {
			return 0
		}
		u := (t - ImpulseContactThreshold) / (1 - ImpulseContactThreshold)
		return u * u * u
	default:
		return t
	}
}

func (s *Script) ToIntensityCurve(opts MapOptions) []Frame {
	actions := s.PlaybackActions()
	if len(actions) < 2 {
		return nil
	}
	if opts.TickMs <= 0 {
		opts.TickMs = 50
	}
	if opts.MaxSpeed <= 0 {
		opts.MaxSpeed = 0.6
	}
	duration := s.Duration()
	frames := make([]Frame, 0, duration/opts.TickMs+1)
	segIdx := 0
	var prevVib, prevSuc float64
	var prevContactVib float64
	envelope := opts.ContactVibrationEnvelope
	envelopeOn := true
	if envelope < 0 {
		envelopeOn = false
	} else if envelope == 0 || envelope >= 1 {
		envelope = DefaultContactEnvelopeSmooth
	}

	// Contact threshold once over the whole script (playback pos 0–100; Tf/Tj
	// actions are clamped 20–90 at generate time) — see ContactVibrationSpan.
	// Uses PlaybackActions so OFS inverted scripts map depth correctly.
	var contactMin, contactMax float64
	contactEnabled := opts.ContactVibration
	contactSpan := EffectiveContactSpan(opts.ContactVibrationSpan)
	contactCurve := NormalizeContactCurve(opts.ContactVibrationCurve)
	gaps := opts.TrackingGaps
	if contactEnabled {
		posMin, posMax := float64(actions[0].Pos), float64(actions[0].Pos)
		for _, a := range actions[1:] {
			p := float64(a.Pos)
			if p < posMin {
				posMin = p
			}
			if p > posMax {
				posMax = p
			}
		}
		if posMax-posMin < contactVibrationMinSpan {
			contactEnabled = false
		} else {
			contactMin = posMin + contactSpan*(posMax-posMin)
			contactMax = posMax
		}
	}
	inGap := func(t int64) bool {
		for _, g := range gaps {
			if t >= g.StartMs && t <= g.EndMs {
				return true
			}
		}
		return false
	}
	// contactVibAt: depth envelope from deep pos, and/or spatial proximity of
	// tip trajectory to marked contact areas (feel-decouple Stage A).
	// S2 (prefer spatial): when marks+trajectory present, weight spatial over
	// depth and shorten envelope on spatial hits — stroke curve unchanged.
	spatialOn := opts.ContactVibration &&
		marksHasContactAreas(s.Metadata.ContactMarks) &&
		s.Metadata.Trajectory != nil &&
		len(s.Metadata.Trajectory.Tip) > 0
	var lastSpatialHit bool
	contactVibAt := func(pos float64, t int64) float64 {
		lastSpatialHit = false
		if (!contactEnabled && !spatialOn) || inGap(t) {
			return 0
		}
		depth := 0.0
		if contactEnabled && pos >= contactMin {
			linear := clamp01((pos - contactMin) / (contactMax - contactMin))
			depth = applyContactCurve(linear, contactCurve)
		}
		spatial := 0.0
		if spatialOn {
			spatial = applyContactCurve(
				SpatialContactIntensity(s.Metadata.ContactMarks, s.Metadata.Trajectory, t),
				contactCurve,
			)
		}
		vib := depth
		if spatialOn {
			// Prefer spatial events; attenuate continuous depth fill.
			weightedDepth := depth * SpatialDepthWeight
			vib = weightedDepth
			if spatial > vib {
				vib = spatial
			}
			lastSpatialHit = spatial > 0 && spatial >= weightedDepth
		}
		if vib > 0 && opts.MinVibration > 0 {
			vib = liftFloor(vib, opts.MinVibration)
		}
		return vib
	}
	smoothContact := func(vib float64, t int64) float64 {
		if (!contactEnabled && !spatialOn) || !envelopeOn || inGap(t) {
			if inGap(t) {
				prevContactVib = 0
			}
			return vib
		}
		env := envelope
		if lastSpatialHit && SpatialContactEnvelopeSmooth < env {
			env = SpatialContactEnvelopeSmooth
		}
		if len(frames) > 0 {
			vib = env*prevContactVib + (1-env)*vib
		}
		prevContactVib = vib
		return vib
	}
	for t := int64(0); t <= duration; t += opts.TickMs {
		var vib, suc float64
		if opts.UseExplicitAxes {
			vib = clamp01(SampleAxisPos(opts.VibrationAxis, t) / 100.0)
			suc = clamp01(SampleAxisPos(opts.SuctionAxis, t) / 100.0)
			if inGap(t) {
				vib = 0
			}
		} else {
			for segIdx < len(actions)-2 && actions[segIdx+1].At <= t {
				segIdx++
			}
			a, b := actions[segIdx], actions[segIdx+1]
			dt := b.At - a.At
			if dt <= 0 {
				dt = 1
			}
			speed := math.Abs(float64(b.Pos-a.Pos)) / float64(dt)
			intensity := clamp01(speed / opts.MaxSpeed)
			frac := float64(t-a.At) / float64(dt)
			pos := float64(a.Pos) + frac*float64(b.Pos-a.Pos)
			posSignal := clamp01(pos / 100.0)
			switch opts.Sync {
			case SyncSynchronized:
				vib, suc = intensity, intensity
			case SyncAlternating:
				vib, suc = intensity, clamp01(1-intensity)
			case SyncVibrationOnly:
				vib, suc = intensity, 0
			case SyncSuctionOnly:
				vib, suc = 0, intensity
			case SyncSuctionPosition:
				vib, suc = 0, posSignal
				if contactEnabled || spatialOn {
					vib = contactVibAt(pos, t)
					vib = smoothContact(vib, t)
				} else if inGap(t) {
					vib = 0
				}
			default:
				vib, suc = intensity, posSignal
				if contactEnabled || spatialOn {
					if cv := contactVibAt(pos, t); cv > 0 {
						// Contact envelope owns vibe (depth and/or spatial marks).
						vib = smoothContact(cv, t)
					} else if inGap(t) {
						vib = 0
						prevContactVib = 0
					} else {
						prevContactVib = 0
					}
				}
			}
			if opts.Sync != SyncSuctionOnly && opts.Sync != SyncSuctionPosition {
				vib = liftFloor(vib, opts.MinVibration)
			}
			// SyncSuctionPosition: do NOT apply MinSuction via liftFloor.
			// Tf/Tj scripts already clamp actions to 20–90 at generation time, so
			// posSignal is already in [0.20, 0.90]. liftFloor(0.20, 0.20) would
			// remap that to 0.36 (and 0.90→0.92), stacking two floors and raising
			// resting suction for no gain — see docs/TF_TJ.md and the finding in
			// docs/FINDINGS_TIMING_TF.md. MinSuction remains in the recipe metadata
			// as the intended script-space floor (the clamp), not a second runtime remap.
			if opts.Sync != SyncVibrationOnly && opts.Sync != SyncSuctionPosition {
				suc = liftFloor(suc, opts.MinSuction)
			}
		}
		if opts.Smoothing > 0 && len(frames) > 0 {
			vib = opts.Smoothing*prevVib + (1-opts.Smoothing)*vib
			suc = opts.Smoothing*prevSuc + (1-opts.Smoothing)*suc
		}
		// Tracking gap wins over recipe smoothing (else prevVib leaks in).
		if inGap(t) && (opts.Sync == SyncSuctionPosition || opts.UseExplicitAxes || opts.ContactVibration) {
			vib = 0
		}
		prevVib, prevSuc = vib, suc
		frames = append(frames, Frame{At: t, Vibration: vib, Suction: suc})
	}
	return frames
}

// MapOptionsFromScript builds mapper options from metadata (recipe + axes).
func MapOptionsFromScript(s *Script) MapOptions {
	opts := DefaultMapOptions()
	if s == nil {
		return opts
	}
	profile := s.Metadata.Profile
	if profile != "" {
		opts = RecipeFor(profile)
	}
	if dr := s.Metadata.DeviceRecipe; dr != nil {
		if sync, err := ParseSyncMode(dr.Sync); err == nil && dr.Sync != "" {
			opts.Sync = sync
		}
		if dr.TickMs > 0 {
			opts.TickMs = dr.TickMs
		}
		if dr.MaxSpeed > 0 {
			opts.MaxSpeed = dr.MaxSpeed
		}
		if dr.Smoothing > 0 {
			opts.Smoothing = dr.Smoothing
		}
		if dr.MinSuction > 0 {
			opts.MinSuction = dr.MinSuction
		}
		opts.ContactVibration = dr.ContactVibration
		opts.ContactVibrationSpan = dr.ContactVibrationSpan
		opts.ContactVibrationCurve = dr.ContactVibrationCurve
		if NormalizePlaybackSource(dr.PlaybackSource) == PlaybackSourceAxes && s.Metadata.SamnAxes != nil {
			opts.UseExplicitAxes = true
			opts.VibrationAxis = s.Metadata.SamnAxes.Vibration
			opts.SuctionAxis = s.Metadata.SamnAxes.Suction
		}
	}
	if len(s.Metadata.TrackingGaps) > 0 {
		opts.TrackingGaps = s.Metadata.TrackingGaps
	}
	return opts
}

func liftFloor(value, floor float64) float64 {
	if floor <= 0 {
		return value
	}
	if floor >= 1 {
		return 1
	}
	return floor + value*(1-floor)
}

func clamp01(v float64) float64 {
	if v < 0 {
		return 0
	}
	if v > 1 {
		return 1
	}
	return v
}
