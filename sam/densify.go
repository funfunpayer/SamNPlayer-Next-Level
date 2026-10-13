package sam

import (
	"github.com/funfunpayer/SamNPlayer/funscript"
)

// Densify expands a keyframe SAM script onto a regular tick grid and
// recomputes contact Intensity from interpolated Position (same Span/Curve
// rules as Enrich / ToIntensityCurve). MapOptions override DeviceRecipe
// span/curve when set — live playback correction without rewriting files.
//
// Confidence still follows TrackingGaps ∪ opts.TrackingGaps.
// Velocity is Δpos/Δt on the dense grid.
func Densify(s *Script, tickMs int64, opts funscript.MapOptions) *Script {
	if s == nil || len(s.Frames) < 2 {
		return s
	}
	if tickMs <= 0 {
		tickMs = 50
	}
	duration := s.Duration()
	if duration <= 0 {
		return s
	}

	span := funscript.DefaultContactVibrationSpan
	curve := funscript.ContactCurveLinear
	contactOn := opts.ContactVibration
	if dr := s.Metadata.DeviceRecipe; dr != nil {
		if !contactOn {
			contactOn = dr.ContactVibration
		}
		span = funscript.EffectiveContactSpan(dr.ContactVibrationSpan)
		curve = funscript.NormalizeContactCurve(dr.ContactVibrationCurve)
	}
	if opts.ContactVibrationSpan > 0 {
		span = funscript.EffectiveContactSpan(opts.ContactVibrationSpan)
	}
	if opts.ContactVibrationCurve != "" {
		curve = funscript.NormalizeContactCurve(opts.ContactVibrationCurve)
	}

	posMin, posMax := s.Frames[0].Motion.Position, s.Frames[0].Motion.Position
	for _, f := range s.Frames[1:] {
		p := f.Motion.Position
		if p < posMin {
			posMin = p
		}
		if p > posMax {
			posMax = p
		}
	}
	posSpan := posMax - posMin
	contactOK := contactOn && posSpan >= 5.0
	contactMin := posMin + span*posSpan
	contactMax := posMax

	gaps := append(toFunscriptGaps(s.Metadata.TrackingGaps), opts.TrackingGaps...)
	inGap := func(t int64) bool {
		for _, g := range gaps {
			if t >= g.StartMs && t <= g.EndMs {
				return true
			}
		}
		return false
	}

	n := int(duration/tickMs) + 1
	out := make([]Frame, 0, n)
	segIdx := 0
	var prevPos float64
	for t := int64(0); t <= duration; t += tickMs {
		for segIdx < len(s.Frames)-2 && s.Frames[segIdx+1].Time <= t {
			segIdx++
		}
		a, b := s.Frames[segIdx], s.Frames[segIdx+1]
		dt := b.Time - a.Time
		if dt <= 0 {
			dt = 1
		}
		frac := float64(t-a.Time) / float64(dt)
		pos := a.Motion.Position + frac*(b.Motion.Position-a.Motion.Position)
		m := Motion{
			Position: pos,
			Range:    clamp01(pos / 100.0),
		}
		if inGap(t) {
			m.Confidence = 0
			m.Intensity = 0
		} else {
			m.Confidence = 1
			if contactOK && pos >= contactMin {
				linear := clamp01((pos - contactMin) / (contactMax - contactMin))
				m.Intensity = applyContactCurveLocal(linear, curve)
			}
		}
		if len(out) > 0 {
			m.Velocity = (pos - prevPos) / float64(tickMs)
		}
		prevPos = pos
		out = append(out, Frame{Time: t, Motion: m})
	}

	cp := *s
	cp.Frames = out
	switch cp.Metadata.Source {
	case "", "funscript-enriched", "funscript-sidecar", "funscript-import":
		cp.Metadata.Source = "funscript-dense"
	}
	return &cp
}
