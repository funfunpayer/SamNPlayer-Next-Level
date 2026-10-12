package strokepreview

import (
	"context"
	"fmt"
	"math"
	"time"
)

// Hint is what Stage A may safely feed into Generate (not a curve rewrite).
type Hint struct {
	Quality           string  `json:"quality"`
	StrokeHz          float64 `json:"stroke_hz,omitempty"`
	SuggestAudioCheck bool    `json:"suggest_audio_check"`
	CutCount          int     `json:"cut_count"`
	CutRatePerMin     float64 `json:"cut_rate_per_min"`
	PanShare          float64 `json:"pan_share"`
	// SuggestedMinPeakDistanceMs biases posttrack when caller left peak
	// distance at 0 (profile default). 0 = no suggestion.
	SuggestedMinPeakDistanceMs int    `json:"suggested_min_peak_distance_ms,omitempty"`
	Reason                     string `json:"reason,omitempty"`
	WallMs                     int64  `json:"wall_ms"`
}

// ToHint converts a Report into generate-facing hints.
func (r Report) ToHint() Hint {
	h := Hint{
		Quality:           r.Quality,
		StrokeHz:          r.StrokeHz,
		SuggestAudioCheck: r.SuggestAudio,
		CutCount:          len(r.Cuts),
		CutRatePerMin:     r.CutRatePerMin,
		PanShare:          r.PanShare,
		Reason:            r.Reason,
		WallMs:            r.ElapsedWallMs,
	}
	if r.StrokeHz >= 0.3 && r.StrokeHz <= 3.5 {
		// ~35% of median period — keeps keyframes from going denser than the
		// sparse preview's own stroke tempo without inventing positions.
		ms := int(math.Round(1000.0 / r.StrokeHz * 0.35))
		if ms < 120 {
			ms = 120
		}
		if ms > 450 {
			ms = 450
		}
		h.SuggestedMinPeakDistanceMs = ms
	}
	return h
}

// ProgressLine is one human-readable generate:progress line.
func (h Hint) ProgressLine() string {
	return fmt.Sprintf(
		"STROKE_PREVIEW quality=%s hz=%.2f cuts=%d pan=%.0f%% audio_hint=%v peak_ms=%d wall=%dms — %s",
		h.Quality, h.StrokeHz, h.CutCount, h.PanShare*100, h.SuggestAudioCheck,
		h.SuggestedMinPeakDistanceMs, h.WallMs, h.Reason,
	)
}

// MetadataMap is stamped into funscript metadata.stroke_preview.
func (h Hint) MetadataMap() map[string]any {
	m := map[string]any{
		"quality":             h.Quality,
		"suggest_audio_check": h.SuggestAudioCheck,
		"cut_count":           h.CutCount,
		"cut_rate_per_min":    h.CutRatePerMin,
		"pan_share":           h.PanShare,
		"wall_ms":             h.WallMs,
		"stage":               "A",
	}
	if h.StrokeHz > 0 {
		m["stroke_hz"] = h.StrokeHz
	}
	if h.SuggestedMinPeakDistanceMs > 0 {
		m["suggested_min_peak_distance_ms"] = h.SuggestedMinPeakDistanceMs
	}
	if h.Reason != "" {
		m["reason"] = h.Reason
	}
	return m
}

// RunQuick is Analyze with Stage-A defaults for the generate pre-pass
// (cap wall work: 320px, 12 fps, sample every 2, first 90s max).
func RunQuick(ctx context.Context, videoPath string) (Report, error) {
	if ctx == nil {
		ctx = context.Background()
	}
	// Bound preview so long videos stay cheap; flags still useful early.
	ctx, cancel := context.WithTimeout(ctx, 45*time.Second)
	defer cancel()
	return Analyze(ctx, videoPath, Options{
		MaxWidth:    320,
		AnalysisFPS: 12,
		SampleEvery: 2,
		MaxSeconds:  90,
	})
}
