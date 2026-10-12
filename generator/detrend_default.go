package generator

import (
	"fmt"

	"github.com/funfunpayer/SamNPlayer/funscript"
)

// Stroke curves only carry meaning in their oscillation - an absolute
// baseline shift (tracker slowly walking off target, or reacquiring a few
// hundred px away) otherwise moves the whole rest of the curve into a narrow
// band after global normalization. Measured on clip_voll (280s) against both
// FunGen references: 62% of 10s windows were stuck in a <25-point band with
// no detrend; windowed r vs FunGen went 0.275/0.261 -> 0.386/0.552 with a
// 2x-stroke-period rolling-mean detrend (see docs/AGENT_COORD.md 23 Sep).
const (
	detrendFallbackMs = 3000.0
	detrendMinMs      = 1500.0
	detrendMaxMs      = 4000.0
)

// applyDefaultDetrend turns detrend on for stroke profiles when the caller
// left DetrendWindowMs unset (0). Negative = explicitly off. Distance
// profiles (Tf/Tj) are left alone - there the absolute value is the signal
// (distance near 0 = contact).
func applyDefaultDetrend(opts Options, onProgress func(string)) Options {
	if opts.DetrendWindowMs != 0 || funscript.IsDistanceProfile(opts.Profile) {
		return opts
	}
	ms, why := adaptiveDetrendMs(opts.StrokePreviewHint)
	opts.DetrendWindowMs = ms
	if onProgress != nil {
		onProgress(fmt.Sprintf("POST: detrend %.0fms (%s)", ms, why))
	}
	return opts
}

// adaptiveDetrendMs: a rolling mean over exactly two stroke periods averages
// the stroke itself out to ~0, so it only removes what is slower than the
// stroke - the drift - without attenuating the stroke amplitude.
func adaptiveDetrendMs(hint map[string]any) (float64, string) {
	if hint == nil || hint["quality"] != "ok" {
		return detrendFallbackMs, "no reliable stroke tempo, default window"
	}
	hz, _ := hint["stroke_hz"].(float64)
	if hz < 0.3 || hz > 3.5 {
		return detrendFallbackMs, "stroke tempo out of range, default window"
	}
	ms := 2 * 1000 / hz
	if ms < detrendMinMs {
		ms = detrendMinMs
	}
	if ms > detrendMaxMs {
		ms = detrendMaxMs
	}
	return ms, fmt.Sprintf("2x stroke period at %.2f Hz", hz)
}
