package funscript

import (
	"fmt"
	"math"
)

// ContactVibPreviewRequest holds Feel-step knobs for a live contact-vib probe.
type ContactVibPreviewRequest struct {
	Span  float64 `json:"span"`
	Curve string  `json:"curve"`
}

// ContactVibPreviewPoint is one sample on the Feel contact-vib probe.
type ContactVibPreviewPoint struct {
	AtMs   int64 `json:"atMs"`
	Stroke int   `json:"stroke"` // 0–100 synthetic stroke
	Vib    int   `json:"vib"`    // 0–100 contact vib intensity
}

// ContactVibPreviewResult is MakeVibrationsExt-style live feedback for Feel
// span/curve — synthetic bounce only, never the user's clip or stroke rewrite.
type ContactVibPreviewResult struct {
	Sample    []ContactVibPreviewPoint `json:"sample,omitempty"`
	Hint      string                   `json:"hint"`
	PeakVib   float64                  `json:"peakVib"`   // 0–1
	ActivePct float64                  `json:"activePct"` // % of ticks with vib > 0
}

// PreviewContactVibration maps a fixed synthetic bounce stroke through the
// depth contact-vib envelope (span + curve). Spatial Stage A is off — probe
// shows Feel depth settings only.
func PreviewContactVibration(req ContactVibPreviewRequest) ContactVibPreviewResult {
	span := EffectiveContactSpan(req.Span)
	curve := NormalizeContactCurve(req.Curve)

	script := syntheticContactProbeScript()
	opts := MapOptions{
		TickMs:                   40,
		Sync:                     SyncSuctionPosition, // vib = contact envelope only (no speed fill)
		ContactVibration:         true,
		ContactVibrationSpan:     span,
		ContactVibrationCurve:    curve,
		ContactVibrationEnvelope: -1, // precise probe, no envelope blur
		Smoothing:                0,
		MaxSpeed:                 0.6,
	}
	frames := script.ToIntensityCurve(opts)
	if len(frames) == 0 {
		return ContactVibPreviewResult{Hint: "Contact vib probe unavailable"}
	}

	active := 0
	peak := 0.0
	sample := make([]ContactVibPreviewPoint, 0, len(frames)/2+1)
	step := 2 // downsample for SVG
	for i, f := range frames {
		if f.Vibration > peak {
			peak = f.Vibration
		}
		if f.Vibration > 0.001 {
			active++
		}
		if i%step != 0 && i != len(frames)-1 {
			continue
		}
		sample = append(sample, ContactVibPreviewPoint{
			AtMs:   f.At,
			Stroke: clampPos(interpAt(script.Actions, f.At)),
			Vib:    clampPos(int(math.Round(f.Vibration * 100))),
		})
	}
	activePct := 0.0
	if len(frames) > 0 {
		activePct = 100 * float64(active) / float64(len(frames))
	}

	hint := fmt.Sprintf("Feel probe · span %.2f · %s · vib active ~%.0f%% · peak %.0f",
		span, curveLabel(curve), activePct, peak*100)
	return ContactVibPreviewResult{
		Sample:    sample,
		Hint:      hint,
		PeakVib:   peak,
		ActivePct: activePct,
	}
}

func curveLabel(curve string) string {
	switch NormalizeContactCurve(curve) {
	case ContactCurveSoft:
		return "soft onset"
	case ContactCurvePeak:
		return "stronger peak"
	case ContactCurveImpulse:
		return "impulse (peaks only)"
	default:
		return "linear"
	}
}

// syntheticContactProbeScript: ~4s bounce 20↔90 — same depth window Feel uses.
func syntheticContactProbeScript() *Script {
	const (
		durMs  = 4000
		stepMs = 80
		posMin = 20.0
		posMax = 90.0
		cycles = 3.0
	)
	actions := make([]Action, 0, durMs/stepMs+1)
	for t := int64(0); t <= durMs; t += stepMs {
		phase := 2 * math.Pi * cycles * float64(t) / float64(durMs)
		// Peaks at 90, troughs at 20.
		norm := 0.5 + 0.5*math.Sin(phase-math.Pi/2)
		pos := posMin + (posMax-posMin)*norm
		actions = append(actions, Action{At: t, Pos: int(math.Round(pos))})
	}
	return &Script{Actions: actions}
}
