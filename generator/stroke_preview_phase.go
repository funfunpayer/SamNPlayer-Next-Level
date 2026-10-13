package generator

import (
	"context"
	"fmt"

	"github.com/funfunpayer/SamNPlayer/generator/strokepreview"
	"github.com/funfunpayer/SamNPlayer/logging"
)

// applyStrokePreview runs Stage A before tracking and folds safe hints into
// opts (audio gate, optional peak-distance bias). Failures are non-fatal —
// Generate continues; we only lose the hint.
func applyStrokePreview(ctx context.Context, videoPath string, opts Options, onProgress func(string)) Options {
	if opts.SkipStrokePreview {
		return opts
	}
	if onProgress != nil {
		onProgress("STROKE_PREVIEW starting (sparse extrema / cut / pan)…")
	}
	rep, err := strokepreview.RunQuick(ctx, videoPath)
	if err != nil {
		msg := fmt.Sprintf("STROKE_PREVIEW skipped: %v", err)
		logging.Warn("generator: stroke preview failed", "error", err)
		if onProgress != nil {
			onProgress(msg)
		}
		return opts
	}
	h := rep.ToHint()
	if onProgress != nil {
		onProgress(h.ProgressLine())
	}
	opts.StrokePreviewHint = h.MetadataMap()
	return applyStrokePreviewSteers(opts, h, onProgress)
}

// applyStrokePreviewSteers folds Stage-B flags into the current Generate run
// (same pattern as the audio gate): high cut rate → PerSceneROI; high pan →
// camera compensation on. Never invents curve positions. Peak-distance stays
// advisory-only in metadata/progress.
func applyStrokePreviewSteers(opts Options, h strokepreview.Hint, onProgress func(string)) Options {
	steered := false

	// Audio only when preview looks bad — never invents positions.
	if h.SuggestAudioCheck && !opts.AudioCheck {
		opts.AudioCheck = true
		steered = true
		if onProgress != nil {
			onProgress("STROKE_PREVIEW: enabling audio tempo check (weak/unstable preview)")
		}
	}
	// Peak-distance suggestion is advisory only — do not mutate MinPeakDistanceMs
	// (0 means “use posttrack default 150”, not “let preview invent a value”).
	if h.SuggestedMinPeakDistanceMs > 0 && onProgress != nil {
		onProgress(fmt.Sprintf("STROKE_PREVIEW: tip peak spacing ~%dms (from ~%.2f Hz) — Advanced can override",
			h.SuggestedMinPeakDistanceMs, h.StrokeHz))
	}
	// BF-3: cut-rate → enable Re-find region (PerSceneROI) for this run.
	// On Go-CSRT builds (#338 / Rel35): do NOT steer into Python — Go TrackROI
	// already re-anchors on cuts; per-scene ROI re-search stays Python-only
	// until Go auto_roi parity. Soft-clear happens in softenPythonOnlyOptsForGoCSRT
	// if the user already checked the Advanced box.
	if h.CutRatePerMin > 4 && !opts.PerSceneROI {
		if NativeTrackingAvailable() {
			if onProgress != nil {
				onProgress(fmt.Sprintf(
					"STROKE_PREVIEW: high cut rate (%.1f/min) — staying on Go CSRT (Re-find region is Python-only; cuts still re-anchor)",
					h.CutRatePerMin))
			}
		} else {
			opts.PerSceneROI = true
			steered = true
			if onProgress != nil {
				onProgress(fmt.Sprintf(
					"STROKE_PREVIEW: high cut rate (%.1f/min) — enabling “Re-find region after each cut”",
					h.CutRatePerMin))
			}
		}
	} else if h.CutRatePerMin > 4 && onProgress != nil {
		if NativeTrackingAvailable() {
			onProgress(fmt.Sprintf(
				"STROKE_PREVIEW: high cut rate (%.1f/min) — Re-find region ignored on Go CSRT build (Python-only)",
				h.CutRatePerMin))
		} else {
			onProgress(fmt.Sprintf(
				"STROKE_PREVIEW: high cut rate (%.1f/min) — Re-find region already on",
				h.CutRatePerMin))
		}
	}
	// BF-3: pan → camera compensation for this run (tip when already on).
	if h.PanShare > 0.4 && opts.DisableCameraCompensation {
		opts.DisableCameraCompensation = false
		steered = true
		if onProgress != nil {
			onProgress(fmt.Sprintf(
				"STROKE_PREVIEW: pan-like energy share=%.0f%% — enabling camera motion compensation",
				h.PanShare*100))
		}
	} else if h.PanShare > 0.4 && onProgress != nil {
		onProgress(fmt.Sprintf(
			"STROKE_PREVIEW: pan-like energy share=%.0f%% — camera compensation on",
			h.PanShare*100))
	}

	if steered && opts.StrokePreviewHint != nil {
		opts.StrokePreviewHint["stage"] = "B"
		if opts.PerSceneROI && h.CutRatePerMin > 4 {
			opts.StrokePreviewHint["steered_per_scene_roi"] = true
		}
		if !opts.DisableCameraCompensation && h.PanShare > 0.4 {
			opts.StrokePreviewHint["steered_camera_compensation"] = true
		}
	}
	return opts
}
