package main

import (
	"fmt"
	"os"
	"path/filepath"
	"strings"

	"github.com/funfunpayer/SamNPlayer/funscript"
	"github.com/funfunpayer/SamNPlayer/generator"
	"github.com/funfunpayer/SamNPlayer/sam"
	"github.com/funfunpayer/SamNPlayer/samn"
)

// ImproveScriptRequest is the FunGen-like post-generate polish step:
// trim start/end, fill gaps, heal tracking-loss windows, optional audio tempo check.
type ImproveScriptRequest struct {
	Path      string  `json:"path"`
	VideoPath string  `json:"videoPath"`
	StartSec  float64 `json:"startSec"`
	EndSec    float64 `json:"endSec"`
	FillGaps  bool    `json:"fillGaps"`
	MaxGapMs  int64   `json:"maxGapMs"`
	// HealTrackingGaps rewrites metadata.tracking_gaps windows (strip junk +
	// linear bridge). Empty/missing gaps = no-op. Does not re-run CSRT.
	HealTrackingGaps bool `json:"healTrackingGaps"`
	// HealRhythm (opt-in): when healing tracking_gaps, bridge with stroke
	// rhythm instead of a straight line (line fallback per window). Default off.
	HealRhythm bool `json:"healRhythm"`
	// RepairSpans are user-selected "repair this span" windows (ms). Empty = no-op.
	RepairSpans []funscript.TrackingGap `json:"repairSpans"`
	// AudioCheck re-runs tempo check and stamps metadata (does not rewrite curve).
	AudioCheck bool `json:"audioCheck"`
	// UseAudioForFill uses audio Hz (when available) for fill-gap step spacing.
	UseAudioForFill bool `json:"useAudioForFill"`
}

// ImproveScriptResult is returned to the GUI Review step.
type ImproveScriptResult struct {
	Path          string   `json:"path"`
	BeforeCount   int      `json:"beforeCount"`
	AfterCount    int      `json:"afterCount"`
	Trimmed       bool     `json:"trimmed"`
	GapsFilled    int      `json:"gapsFilled"`
	PointsAdded   int      `json:"pointsAdded"`
	FillGapMs     int64    `json:"fillGapMs"`
	FillStepMs    int64    `json:"fillStepMs"`
	WindowsHealed int      `json:"windowsHealed"`
	RhythmBridged int      `json:"rhythmBridged"`
	SpansRepaired int      `json:"spansRepaired"`
	AudioHz       *float64 `json:"audioHz,omitempty"`
	ScriptHz      *float64 `json:"scriptHz,omitempty"`
	AudioWarnings []string `json:"audioWarnings,omitempty"`
	// Speech-Hold / segment taxonomy (review hints only — never stroke rewrite).
	SpeechHoldMs  int64                        `json:"speechHoldMs,omitempty"`
	AudioSegments []funscript.AudioSegmentHint `json:"audioSegments,omitempty"`
	Message       string                       `json:"message"`
}

// ImproveGeneratedScript polishes a just-generated script in place
// (.samn preferred, companion .funscript kept in sync).
func (a *App) ImproveGeneratedScript(req ImproveScriptRequest) (ImproveScriptResult, error) {
	out := ImproveScriptResult{}
	if req.Path == "" {
		return out, fmt.Errorf("no path")
	}
	path := preferSamnCompanion(req.Path)
	out.Path = path

	script, err := a.loadScriptDocument(path)
	if err != nil {
		return out, err
	}
	actions := append([]funscript.Action(nil), script.Actions...)

	var audioHz float64
	var audioMeta *funscript.AudioCheck
	video := strings.TrimSpace(req.VideoPath)
	if video == "" {
		video = guessVideoBesideScript(path)
	}
	if (req.AudioCheck || req.UseAudioForFill) && video != "" && generator.AudioCheckAvailable() {
		audioMeta = generator.CheckAudioTempo(video, actions)
		if audioMeta != nil && audioMeta.AudioHz != nil {
			audioHz = *audioMeta.AudioHz
			out.AudioHz = audioMeta.AudioHz
		}
		if audioMeta != nil && audioMeta.ScriptHz != nil {
			out.ScriptHz = audioMeta.ScriptHz
		}
		if audioMeta != nil {
			out.AudioWarnings = append([]string(nil), audioMeta.Warnings...)
			out.SpeechHoldMs = audioMeta.SpeechHoldMs
			if len(audioMeta.Segments) > 0 {
				out.AudioSegments = append([]funscript.AudioSegmentHint(nil), audioMeta.Segments...)
			}
		}
	}

	opts := funscript.ImproveOpts{
		FillGaps:         req.FillGaps,
		MaxGapMs:         req.MaxGapMs,
		HealTrackingGaps: req.HealTrackingGaps,
		HealRhythm:       req.HealRhythm,
		TrackingGaps:     append([]funscript.TrackingGap(nil), script.Metadata.TrackingGaps...),
		RepairSpans:      append([]funscript.TrackingGap(nil), req.RepairSpans...),
	}
	if req.StartSec > 0 {
		opts.StartMs = int64(req.StartSec * 1000)
	}
	if req.EndSec > 0 {
		opts.EndMs = int64(req.EndSec * 1000)
	}
	if req.UseAudioForFill && audioHz > 0 {
		opts.AudioHz = audioHz
	}

	improved, err := funscript.ImproveScript(actions, opts)
	if err != nil {
		return out, err
	}
	out.BeforeCount = improved.BeforeCount
	out.AfterCount = improved.AfterCount
	out.Trimmed = improved.Trimmed
	out.GapsFilled = improved.GapsFilled
	out.PointsAdded = improved.PointsAdded
	out.FillGapMs = improved.FillGapMs
	out.FillStepMs = improved.FillStepMs
	out.WindowsHealed = improved.WindowsHealed
	out.RhythmBridged = improved.RhythmBridged
	out.SpansRepaired = improved.SpansRepaired

	changed := improved.Trimmed || improved.PointsAdded > 0 || improved.WindowsHealed > 0 ||
		improved.SpansRepaired > 0 || improved.GapsFilled > 0 ||
		improved.AfterCount != improved.BeforeCount
	if changed {
		if err := saveImprovedActions(path, improved.Actions, improved.ClearTrackingGaps); err != nil {
			return out, err
		}
		funPath := path
		if samn.IsSamnPath(path) {
			funPath = samn.CompanionFunscriptPath(path)
		}
		if req.AudioCheck && audioMeta != nil {
			_ = funscript.StampAudioCheck(funPath, audioMeta)
		}
		// Tf/Tj: re-derive the .sam sidecar from the just-rewritten funscript
		// so it matches the improved curve instead of the raw-Generate one
		// (same gate as the original write in app_generator.go).
		if funscript.IsDistanceProfile(script.Metadata.Profile) {
			if refreshed, err := funscript.Load(funPath); err == nil {
				_ = sam.WriteEnrichedSidecar(funPath, refreshed)
			}
		}
		loaded := a.loadedScriptPath()
		if loaded == path || loaded == funPath || loaded == req.Path {
			_ = a.reloadLoadedScript()
		}
	} else if req.AudioCheck && audioMeta != nil {
		funPath := path
		if samn.IsSamnPath(path) {
			funPath = samn.CompanionFunscriptPath(path)
		}
		if err := funscript.StampAudioCheck(funPath, audioMeta); err != nil && !samn.IsSamnPath(path) {
			return out, err
		}
	}

	parts := []string{}
	if improved.Trimmed {
		parts = append(parts, "trimmed start/end")
	}
	if improved.WindowsHealed > 0 {
		parts = append(parts, fmt.Sprintf("healed %d tracking gap(s)", improved.WindowsHealed))
	}
	if improved.SpansRepaired > 0 {
		parts = append(parts, fmt.Sprintf("repaired %d span(s)", improved.SpansRepaired))
	}
	if improved.RhythmBridged > 0 {
		parts = append(parts, fmt.Sprintf("%d rhythm bridge(s)", improved.RhythmBridged))
	}
	if improved.GapsFilled > 0 {
		fillPts := improved.PointsAdded
		if improved.WindowsHealed > 0 && fillPts > 0 {
			// PointsAdded includes heal bridges; say "points" not "fill-only".
			parts = append(parts, fmt.Sprintf("filled %d gap(s) (+%d points total)", improved.GapsFilled, fillPts))
		} else {
			parts = append(parts, fmt.Sprintf("filled %d gap(s) (+%d points)", improved.GapsFilled, fillPts))
		}
	} else if improved.WindowsHealed > 0 && improved.PointsAdded > 0 {
		parts = append(parts, fmt.Sprintf("+%d bridge points", improved.PointsAdded))
	}
	if req.AudioCheck && audioMeta != nil {
		if len(audioMeta.Warnings) > 0 {
			parts = append(parts, "audio check: warnings")
		} else {
			parts = append(parts, "audio check: ok")
		}
	}
	if len(parts) == 0 {
		out.Message = "Nothing to change"
	} else {
		out.Message = strings.Join(parts, " · ")
	}
	return out, nil
}

// saveImprovedActions writes the edited action list back and refreshes the
// quality score/warnings to match it - the score/warnings baked in at raw
// Generate time otherwise silently describe the pre-edit curve forever
// (QC-B Finding 1, PR #184: reported as a stale-looking .sam sidecar and
// "only 32% movement" warning on a script that Improve had since reshaped).
// Dense per-frame tracker data isn't available here, so this recomputes the
// actions-only Script Doctor quality (EstimatedFromScriptOnly) rather than
// reproducing the original dense-signal score - a genuine, if weaker,
// estimate of the current curve beats a stale snapshot of a different one.
// clearGaps drops tracking_gaps / TrackingGaps in the same write so Contact
// vib is not muted on healed windows and companion export stays consistent.
func saveImprovedActions(path string, actions []funscript.Action, clearGaps bool) error {
	quality := funscript.EvaluateScriptQuality(actions)
	if samn.IsSamnPath(path) {
		doc, err := samn.Load(path)
		if err != nil {
			return err
		}
		doc.General = actions
		doc.QualityScore = &quality.Score
		doc.QualityPassed = &quality.Passed
		doc.QualityWarnings = quality.Warnings
		if clearGaps {
			doc.TrackingGaps = nil
		}
		if err := samn.Save(path, doc); err != nil {
			return err
		}
		return doc.ExportFunscript(samn.CompanionFunscriptPath(path))
	}
	if err := funscript.SaveActions(path, actions); err != nil {
		return err
	}
	if err := funscript.StampQuality(path, quality); err != nil {
		return err
	}
	if clearGaps {
		return funscript.StampTrackingGaps(path, nil)
	}
	return nil
}

func guessVideoBesideScript(scriptPath string) string {
	base := scriptPath
	for _, ext := range []string{".samn", ".funscript", filepath.Ext(scriptPath)} {
		base = strings.TrimSuffix(base, ext)
	}
	for _, ext := range []string{".mp4", ".mkv", ".webm", ".mov", ".avi"} {
		cand := base + ext
		if st, err := os.Stat(cand); err == nil && !st.IsDir() {
			return cand
		}
	}
	return ""
}
