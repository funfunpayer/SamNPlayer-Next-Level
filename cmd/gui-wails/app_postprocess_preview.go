package main

import (
	"github.com/funfunpayer/SamNPlayer/generator/posttrack"
)

// PostprocessPreviewRequest is the GUI Expert-tuning live probe
// (DeepFunGen-style postprocess displays — not E2E stroke generation).
type PostprocessPreviewRequest struct {
	SmoothWindow          int     `json:"smoothWindow"`
	MinPeakDistanceMs     int     `json:"minPeakDistanceMs"`
	PeakProminence        float64 `json:"peakProminence"`
	RDPTolerance          float64 `json:"rdpTolerance"`
	AdaptiveKeyframeError float64 `json:"adaptiveKeyframeError"`
	MaxSpeed              float64 `json:"maxSpeed"`
}

// PostprocessPreviewResult is returned to the Create Expert panel.
type PostprocessPreviewResult = posttrack.PreviewResult

// PreviewPostprocess runs a fixed synthetic probe through posttrack so Expert
// knobs (smooth / prominence / peak spacing / RDP / max speed) show live-ish
// keyframe feedback. Everyday Create stays unchanged; this is Advanced/Expert only.
func (a *App) PreviewPostprocess(req PostprocessPreviewRequest) (PostprocessPreviewResult, error) {
	return posttrack.PreviewStats(posttrack.PreviewRequest{
		SmoothWindow:      req.SmoothWindow,
		MinPeakDistanceMs: req.MinPeakDistanceMs,
		PeakProminence:    req.PeakProminence,
		RDPTolerance:      req.RDPTolerance,
		AdaptiveError:     req.AdaptiveKeyframeError,
		MaxSpeed:          req.MaxSpeed,
	})
}
