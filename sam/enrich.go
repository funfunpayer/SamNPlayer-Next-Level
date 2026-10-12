package sam

import (
	"math"

	"github.com/funfunpayer/SamNPlayer/funscript"
)

// EnrichOptions steuert, welche bereits vorhandenen Signale in SAM-Felder
// geschrieben werden. Erste Stufe (docs/SAM_ARCHITECTURE.md): kein
// Motion-Klassifikator, sondern Ableitung aus Funscript + Tracking-Gaps +
// Tf/Tj-Kontakt-Rezept — dieselben Infos, die Generation/Training schon
// mitschreiben.
type EnrichOptions struct {
	// SkipContact lässt Intensity/Range unberührt (nur Velocity/Confidence).
	SkipContact bool
}

// Enrich füllt Velocity, Confidence und — bei Distanzprofil + Kontakt —
// Intensity/Range aus dem Funscript nach. FromFunscript bleibt absichtlich
// dünn (nur Position); Enrich ist der erste echte Producer für reichere
// Felder, ohne GUI/Playback umzubauen.
//
// Confidence: 1 außerhalb von TrackingGaps, 0 darin (Tracker-Verlust —
// dieselbe Semantik wie Kontakt-Vibration-Mute in funscript/mapper.go).
// Intensity: Kontakt-Nähe wie ToIntensityCurve (Span/Kurve aus DeviceRecipe).
// Range: normiertes Positionssignal (0–1) bei Distanzprofilen.
// Velocity: Δpos/Δt (pos/ms), Vorzeichen = Richtung.
func Enrich(s *Script, fs *funscript.Script, opts EnrichOptions) {
	if s == nil || fs == nil || len(s.Frames) == 0 {
		return
	}
	gaps := fs.Metadata.TrackingGaps
	if len(s.Metadata.TrackingGaps) > 0 {
		gaps = toFunscriptGaps(s.Metadata.TrackingGaps)
	}
	s.Metadata.TrackingGaps = fromFunscriptGaps(gaps)

	contactOn := false
	span := funscript.DefaultContactVibrationSpan
	curve := funscript.ContactCurveLinear
	if !opts.SkipContact && funscript.IsDistanceProfile(fs.Metadata.Profile) {
		if dr := fs.Metadata.DeviceRecipe; dr != nil && dr.ContactVibration {
			contactOn = true
			span = funscript.EffectiveContactSpan(dr.ContactVibrationSpan)
			curve = funscript.NormalizeContactCurve(dr.ContactVibrationCurve)
		}
	}

	var posMin, posMax float64
	if len(s.Frames) > 0 {
		posMin = s.Frames[0].Motion.Position
		posMax = posMin
		for _, f := range s.Frames[1:] {
			p := f.Motion.Position
			if p < posMin {
				posMin = p
			}
			if p > posMax {
				posMax = p
			}
		}
	}
	posSpan := posMax - posMin
	contactMin := posMin + span*posSpan
	contactMax := posMax
	contactOK := contactOn && posSpan >= 5.0

	inGap := func(t int64) bool {
		for _, g := range gaps {
			if t >= g.StartMs && t <= g.EndMs {
				return true
			}
		}
		return false
	}

	for i := range s.Frames {
		f := &s.Frames[i]
		m := &f.Motion
		if inGap(f.Time) {
			m.Confidence = 0
		} else {
			m.Confidence = 1
		}
		if i > 0 {
			dt := float64(f.Time - s.Frames[i-1].Time)
			if dt > 0 {
				m.Velocity = (m.Position - s.Frames[i-1].Motion.Position) / dt
			}
		}
		if funscript.IsDistanceProfile(fs.Metadata.Profile) {
			m.Range = clamp01(m.Position / 100.0)
		}
		if contactOK && !inGap(f.Time) && m.Position >= contactMin {
			linear := clamp01((m.Position - contactMin) / (contactMax - contactMin))
			m.Intensity = applyContactCurveLocal(linear, curve)
		} else if contactOn {
			m.Intensity = 0
		}
	}
}

// FromFunscriptEnriched = FromFunscript + Enrich — erste Stufe für Tf/Contact.
func FromFunscriptEnriched(fs *funscript.Script) *Script {
	s := FromFunscript(fs)
	Enrich(s, fs, EnrichOptions{})
	s.Metadata.Source = "funscript-enriched"
	return s
}

func applyContactCurveLocal(t float64, curve string) float64 {
	t = clamp01(t)
	switch funscript.NormalizeContactCurve(curve) {
	case funscript.ContactCurveSoft:
		return t * t
	case funscript.ContactCurvePeak:
		return math.Sqrt(t)
	default:
		return t
	}
}

func fromFunscriptGaps(gaps []funscript.TrackingGap) []TrackingGap {
	if len(gaps) == 0 {
		return nil
	}
	out := make([]TrackingGap, len(gaps))
	for i, g := range gaps {
		out[i] = TrackingGap{StartMs: g.StartMs, EndMs: g.EndMs}
	}
	return out
}

func toFunscriptGaps(gaps []TrackingGap) []funscript.TrackingGap {
	if len(gaps) == 0 {
		return nil
	}
	out := make([]funscript.TrackingGap, len(gaps))
	for i, g := range gaps {
		out[i] = funscript.TrackingGap{StartMs: g.StartMs, EndMs: g.EndMs}
	}
	return out
}
