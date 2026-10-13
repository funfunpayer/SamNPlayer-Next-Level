package generator

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"time"

	"github.com/funfunpayer/SamNPlayer/funscript"
	"github.com/funfunpayer/SamNPlayer/logging"
)

// errNativeCanceled is the build-tag-agnostic cancel sentinel from nativeTrackROI.
var errNativeCanceled = errors.New("generator/native: tracking canceled")

// percentFromProgress maps tracker (done,total) callbacks onto the GUI's
// 0–100 (or -1 when total is unknown) percent channel.
func percentFromProgress(onPercent func(int)) func(done, total int) {
	if onPercent == nil {
		return nil
	}
	return func(done, total int) {
		onPercent(percentOf(done, total))
	}
}

// NativePipelineEligible reports whether opts+roi can run on a Python-free
// path (CSRT via trackcv when OpenCV is linked, otherwise simpletrack/NCC
// via videox). Covers single-ROI and Tf/Tj two-point (ROI2). Other backends
// / per-scene / AI opinion still need Python. Audio-tempo check is post-hoc
// in Go (CheckAudioTempo) and does not force the Python path.
func NativePipelineEligible(opts Options, roi ROI) bool {
	return nativeOptionsEligible(opts, roi)
}

// NativePipelineSkipReason explains why opts+roi cannot use the Go path
// (empty when eligible). Used so the GUI log does not look like a mysterious
// “CSRT not available” OpenCV build failure when PerSceneROI forced Python
// (issue #338).
func NativePipelineSkipReason(opts Options, roi ROI) string {
	if NativePipelineEligible(opts, roi) {
		return ""
	}
	if roi.W <= 0 || roi.H <= 0 {
		return "empty tip ROI"
	}
	backend := opts.Backend
	if backend == "" {
		backend = "csrt"
	}
	if backend != "csrt" {
		return fmt.Sprintf("backend %q (Go path is CSRT only)", backend)
	}
	if opts.PerSceneROI {
		return "Advanced “Re-find region after each cut” (PerSceneROI) — still Python-only"
	}
	if opts.AIQualityOpinion {
		return "AI quality opinion — still Python-only"
	}
	if len(opts.MaskROIs) > 0 {
		if !opts.RhythmGrid || distancePartnersActive(opts) {
			return "soft masks without RhythmGrid (or with Tf/Tj) — still Python-only"
		}
	}
	return "options not eligible for Go CSRT"
}

// softenPythonOnlyOptsForGoCSRT drops Python-only toggles that would otherwise
// send Generate to pip OpenCV on builds that already link Go CSRT (#338).
// Rel35 policy: Everyday tracking stays on Go; do not require
// opencv-contrib-python. Per-scene ROI re-search is skipped (Go TrackROI still
// re-anchors on cuts via appearance memory). PreferPython is left alone.
func softenPythonOnlyOptsForGoCSRT(opts Options, roi ROI, onProgress func(string)) Options {
	if !NativeTrackingAvailable() || opts.PreferPython {
		return opts
	}
	if NativePipelineEligible(opts, roi) {
		return opts
	}
	if opts.PerSceneROI {
		opts.PerSceneROI = false
		if onProgress != nil {
			onProgress("Generate: Re-find region after each cut needs Python — keeping Go CSRT (cuts still re-anchor; per-scene ROI re-search skipped). No pip opencv-contrib needed.")
		}
	}
	return opts
}

func nativeOptionsEligible(opts Options, roi ROI) bool {
	if roi.W <= 0 || roi.H <= 0 {
		return false
	}
	backend := opts.Backend
	if backend == "" {
		backend = "csrt"
	}
	// "csrt" and empty mean "native Go pipeline preferred"; simpletrack
	// covers the same cases when OpenCV is missing. Two-point Tf/Tj uses
	// the same CSRT/NCC trackers on both ROIs.
	if backend != "csrt" {
		return false
	}
	// AutoRetry is handled inside finishNativeGenerate (signal-param retry),
	// so it no longer blocks the Go path — default GUI has AutoRetry on.
	// UseOpenCL is ignored for eligibility: Go CSRT does not use OpenCL;
	// Python enables OpenCL automatically when that path is taken anyway.
	if opts.PerSceneROI {
		return false
	}
	// AI quality opinion still shells out to Python; audio check runs in Go
	// after tracking and no longer blocks eligibility.
	if opts.AIQualityOpinion {
		return false
	}
	// Soft masks need Python for feature punch-outs unless RhythmGrid is on
	// the Go single-ROI path (M3: masks become exclude marks on the grid).
	if len(opts.MaskROIs) > 0 {
		if !opts.RhythmGrid || distancePartnersActive(opts) {
			return false
		}
	}
	return true
}

// GenerateNativeCSRT runs trackcv.TrackROI (or TrackTwoPoints when ROI2 is
// set) + posttrack.PositionsToActions and writes a .funscript. No Python
// subprocess. Dense Quality Doctor runs in Go when dense curve + tracker
// stats are available; AI/audio stay Python-only.
// Cancel ctx to abort CSRT mid-loop (same Abort path as Python CommandContext).
// Returns errNativeUnavailable when OpenCV/cgo is not linked; context.Canceled
// when aborted.
func GenerateNativeCSRT(ctx context.Context, videoPath string, roi ROI, outputPath string, opts Options, onProgress func(line string), onPercent func(pct int)) error {
	if ctx == nil {
		ctx = context.Background()
	}
	opts = withTipROI(opts, roi)
	if !NativeTrackingAvailable() {
		return errNativeUnavailable
	}
	if !nativeOptionsEligible(opts, roi) {
		return fmt.Errorf("generator: native pipeline not eligible for these options (CSRT; single ROI or Tf/Tj ROI2/+targets; no per-scene, AI opinion; soft masks need RhythmGrid)")
	}
	if err := ctx.Err(); err != nil {
		return err
	}

	progress := func(line string) {
		logging.Info("generator/native: " + line)
		if onProgress != nil {
			onProgress(line)
		}
	}
	twoPoint := distancePartnersActive(opts)
	if opts.ROI2.W > 0 && opts.ROI2.H > 0 && !twoPoint {
		progress("storing contact mark (feel) — tip CSRT writes the stroke (not tip↔partner distance)")
	}
	multi := twoPoint && len(opts.ExtraTargets) > 0
	if multi {
		progress("Go-native multi-partner pipeline (trackcv TrackMultiPoints + posttrack), no Python")
	} else if twoPoint {
		progress("Go-native two-point pipeline (trackcv TrackTwoPoints + posttrack), no Python")
	} else {
		progress("Go-native CSRT pipeline (trackcv + posttrack), no Python")
	}

	start := time.Now()
	trackOpts := nativeTrackOptions{
		MaxFrames:          opts.MaxFrames,
		StartTimeSec:       opts.StartTimeSec,
		CameraCompensation: !opts.DisableCameraCompensation,
		SceneCutDetection:  !opts.DisableSceneCutDetection,
		AppearanceMemory:   true,
		Axis:               opts.Axis,
		Cancel:             func() bool { return ctx.Err() != nil },
		FixedB:             opts.ROI2Fixed,
		CaptureTrajectory:  opts.CaptureTrajectory,
		RhythmGrid:         opts.RhythmGrid,
		SceneMarks:         mergeSceneMarksWithMasks(opts.SceneMarks, opts.MaskROIs),
	}
	if opts.ContactPointsFile != "" {
		if !opts.RhythmGrid || twoPoint {
			progress("TRACK: contact points ignored - they steer the rhythm grid (single ROI) only")
		} else {
			pts, err := LoadContactPoints(opts.ContactPointsFile, opts.ContactPointsMinAgree)
			if err != nil {
				return err
			}
			trackOpts.ContactPoints = pts
			trackOpts.ContactVerifyK = opts.ContactVerifyK
			progress(fmt.Sprintf("TRACK: %d contact points steer the rhythm grid where the box is out of reach", len(pts)))
			if opts.ContactVerifyK > 0 {
				progress(fmt.Sprintf("TRACK: contact points verified by the engine (kept where its rhythm is >= %.2gx stronger than at its own cell)", opts.ContactVerifyK))
			}
		}
	}
	if trackOpts.Axis == "" {
		trackOpts.Axis = "auto"
	}
	if opts.RhythmGrid && !twoPoint {
		progress("TRACK: rhythm grid on - stroke signal from the most rhythmic flow cell near the box")
		if len(opts.MaskROIs) > 0 {
			progress("TRACK: masks honoured by rhythm grid (Go)")
		}
	}

	var tr nativeTrackResult
	var err error
	if twoPoint {
		partners := []nativePartner{{ROI: opts.ROI2, Fixed: opts.ROI2Fixed}}
		for _, t := range opts.ExtraTargets {
			if t.W <= 0 || t.H <= 0 {
				continue
			}
			// Marks S2: honor Fixed from GUI (follow when tip path / not sticky).
			// Tf/Tj distance: tracked extras keep min-distance on moving contact;
			// Stay fixed preserves static anchors.
			partners = append(partners, nativePartner{
				ROI:   ROI{X: t.X, Y: t.Y, W: t.W, H: t.H},
				Fixed: t.Fixed,
			})
		}
		tr, err = nativeTrackMultiPoints(videoPath, roi, partners, trackOpts, onPercent)
	} else {
		tr, err = nativeTrackROI(videoPath, roi, trackOpts, onPercent)
	}
	if err != nil {
		if errors.Is(err, errNativeCanceled) || errors.Is(ctx.Err(), context.Canceled) {
			return context.Canceled
		}
		if errors.Is(ctx.Err(), context.DeadlineExceeded) {
			return context.DeadlineExceeded
		}
		return fmt.Errorf("generator/native: tracking: %w", err)
	}
	progress(fmt.Sprintf("%d frames tracked (%dx%d) in %s",
		len(tr.TimestampsMs), tr.Width, tr.Height, time.Since(start).Round(time.Millisecond)))
	if len(trackOpts.ContactPoints) > 0 && trackOpts.ContactVerifyK > 0 {
		progress(fmt.Sprintf("TRACK: %d of %d contact points confirmed by the engine", tr.ContactPointsUsed, len(trackOpts.ContactPoints)))
	}
	backend := "csrt"
	if multi {
		backend = "multi_point"
	} else if twoPoint {
		backend = "two_point"
	}
	return finishNativeGenerate(ctx, videoPath, outputPath, opts, tr, "trackcv", backend, progress, onPercent, start)
}

type nativePartner struct {
	ROI   ROI
	Fixed bool
}

type nativeTrackResult struct {
	TimestampsMs []int
	Positions    []float64
	Width        int
	Height       int
	SceneCuts    []int
	LostFrames   int
	TotalFrames  int
	VertRange    float64
	ValidFrames  int
	Confidence   float64
	Reason       string
	LostFlags    []bool // two-point: per-frame tracker loss
	// TrajectoryA/B: per-frame tip/partner center points (video-pixel
	// space), only set when nativeTrackOptions.CaptureTrajectory is true.
	TrajectoryA []NativePoint
	TrajectoryB []NativePoint
	// ContactPointsUsed: contact points that steered the rhythm grid.
	ContactPointsUsed int
	// SceneMap is filled when RhythmGrid produced a map (P4 / M4).
	SceneMap SceneMapDTO
	// SceneMarks: followed marks with Path (when TrackROI ran Follow).
	SceneMarks []SceneMark
}

// NativePoint mirrors trackcv.Point/simpletrack.Point without importing
// either build-tag-gated package from this tag-free file.
type NativePoint struct{ X, Y float64 }

type nativeTrackOptions struct {
	MaxFrames          int
	StartTimeSec       float64
	CameraCompensation bool
	SceneCutDetection  bool
	AppearanceMemory   bool
	Axis               string
	Cancel             func() bool
	FixedB             bool
	// CaptureTrajectory: see Options.CaptureTrajectory.
	CaptureTrajectory bool
	// RhythmGrid: see Options.RhythmGrid (single-ROI trackcv only).
	RhythmGrid bool
	// SceneMarks: exclude/source/region annotations for the rhythm grid +
	// camera punch-outs (includes soft MaskROIs converted to exclude).
	SceneMarks []SceneMark
	// ContactPoints: see Options.ContactPointsFile (rhythm grid only).
	ContactPoints []ContactPoint
	// ContactVerifyK: see Options.ContactVerifyK.
	ContactVerifyK float64
}

// mergeSceneMarksWithMasks appends soft MaskROIs as whole-clip exclude marks
// so the rhythm grid and camera path punch them out (M3). Follow=true so the
// mask moves with the subject (Owner: ignore regions must track).
func mergeSceneMarksWithMasks(marks []SceneMark, masks []ROI) []SceneMark {
	if len(masks) == 0 {
		return marks
	}
	out := make([]SceneMark, len(marks), len(marks)+len(masks))
	copy(out, marks)
	for i, m := range masks {
		if m.W <= 0 || m.H <= 0 {
			continue
		}
		out = append(out, SceneMark{
			Kind:   "exclude",
			ID:     fmt.Sprintf("mask%d", i+1),
			Rect:   m,
			Follow: true,
		})
	}
	return out
}

func writeNativeFunscript(path, videoPath string, actions []funscript.Action, opts Options, tr nativeTrackResult, quality funscript.ScriptQualityResult) error {
	return writeNativeFunscriptNamed(path, videoPath, actions, opts, tr, quality, nil, "trackcv", "csrt")
}

func writeNativeFunscriptNamed(path, videoPath string, actions []funscript.Action, opts Options, tr nativeTrackResult, quality funscript.ScriptQualityResult, audio *funscript.AudioCheck, tracking, backend string) error {
	if len(actions) == 0 {
		return fmt.Errorf("generator/native: keine Actions erzeugt")
	}
	duration := actions[len(actions)-1].At
	nativeMeta := map[string]any{
		"tracking": tracking,
		"signal":   "posttrack",
		"backend":  backend,
		"frames":   tr.TotalFrames,
		"lost":     tr.LostFrames,
		"range_px": tr.VertRange,
	}
	if tr.ValidFrames > 0 || tr.TotalFrames > 0 {
		nativeMeta["valid_frames"] = tr.ValidFrames
		nativeMeta["confidence"] = tr.Confidence
	}
	if tr.Reason != "" {
		nativeMeta["reason"] = tr.Reason
	}
	creator := "SamNPlayer generator/native (trackcv+posttrack, kein Python)"
	if tracking == "simpletrack" {
		creator = "SamNPlayer generator/native (simpletrack+posttrack, kein Python/OpenCV)"
	}
	meta := map[string]any{
		"creator":                    creator,
		"duration":                   duration,
		"native_pipeline":            nativeMeta,
		"quality_score":              quality.Score,
		"quality_passed":             quality.Passed,
		"quality_warnings":           quality.Warnings,
		"quality_kind":               quality.Kind,
		"estimated_from_script_only": quality.EstimatedFromScriptOnly,
	}
	gaps := trackingGapsFromFlags(tr.TimestampsMs, tr.LostFlags, 100)
	if len(gaps) > 0 {
		meta["tracking_gaps"] = gaps
	}
	var traj *funscript.TrajectoryData
	if len(tr.TrajectoryA) > 0 {
		traj = buildTrajectoryData(tr)
		meta["trajectory"] = traj
	}
	if (opts.Profile != "" && opts.Profile != "standard") || opts.ContactVibration {
		recipe := funscript.RecipeMeta(opts.Profile)
		if opts.Profile == "" || opts.Profile == "standard" {
			recipe = funscript.DeviceRecipe{
				Sync:      funscript.SyncIndependent.String(),
				Smoothing: 0.3,
			}
		}
		if opts.ContactVibration {
			recipe.ContactVibration = true
			if opts.ContactVibrationSpan > 0 {
				span := funscript.EffectiveContactSpan(opts.ContactVibrationSpan)
				if span != funscript.DefaultContactVibrationSpan {
					recipe.ContactVibrationSpan = span
				}
			}
			curve := funscript.NormalizeContactCurve(opts.ContactVibrationCurve)
			if curve != funscript.ContactCurveLinear {
				recipe.ContactVibrationCurve = curve
			}
		}
		if opts.Profile != "" && opts.Profile != "standard" {
			meta["profile"] = funscript.NormalizeProfile(opts.Profile)
		} else if opts.ContactVibration {
			meta["profile"] = "standard"
		}
		if funscript.IsDistanceProfile(opts.Profile) || opts.ContactVibration {
			meta["device_recipe"] = recipe
		}
	}
	if audio != nil {
		meta["audio_check"] = audio
	}
	if opts.StrokePreviewHint != nil {
		meta["stroke_preview"] = opts.StrokePreviewHint
	}
	stampContactMarks(meta, opts)
	// scene_map is .samn-only (size) — never stamp onto community .funscript.
	doc := map[string]any{
		"actions":  actions,
		"metadata": meta,
	}
	data, err := json.MarshalIndent(doc, "", "  ")
	if err != nil {
		return err
	}
	if err := writeFileAtomic(path, data, 0o644); err != nil {
		return err
	}
	sceneMarks := opts.SceneMarks
	if len(tr.SceneMarks) > 0 {
		sceneMarks = tr.SceneMarks
	}
	sceneMap := buildSceneMapMeta(videoPath, duration, tr.SceneMap, sceneMarks)
	return writeCompanionSamn(path, videoPath, actions, opts, gaps, quality, traj, sceneMap)
}

func writeFileAtomic(path string, data []byte, mode os.FileMode) error {
	tmp, err := os.CreateTemp(filepath.Dir(path), "."+filepath.Base(path)+".tmp-*")
	if err != nil {
		return err
	}
	tmpName := tmp.Name()
	defer func() {
		_ = tmp.Close()
		_ = os.Remove(tmpName)
	}()

	if err := tmp.Chmod(mode); err != nil {
		return err
	}
	if _, err := tmp.Write(data); err != nil {
		return err
	}
	if err := tmp.Sync(); err != nil {
		return err
	}
	if err := tmp.Close(); err != nil {
		return err
	}
	return os.Rename(tmpName, path)
}

// buildTrajectoryData zips tr.TrajectoryA/B with tr.TimestampsMs into the
// funscript.TrajectoryData shape written to metadata.trajectory. Only
// called when len(tr.TrajectoryA) > 0 (opts.CaptureTrajectory was set and
// the tracking backend supports it - see nativeTrackOptions.CaptureTrajectory).
func buildTrajectoryData(tr nativeTrackResult) *funscript.TrajectoryData {
	toPoints := func(pts []NativePoint) []funscript.TrajectoryPoint {
		if len(pts) == 0 {
			return nil
		}
		out := make([]funscript.TrajectoryPoint, len(pts))
		for i, p := range pts {
			atMs := int64(0)
			if i < len(tr.TimestampsMs) {
				atMs = int64(tr.TimestampsMs[i])
			}
			out[i] = funscript.TrajectoryPoint{AtMs: atMs, X: p.X, Y: p.Y}
		}
		return out
	}
	return &funscript.TrajectoryData{
		Width:   tr.Width,
		Height:  tr.Height,
		Tip:     toPoints(tr.TrajectoryA),
		Partner: toPoints(tr.TrajectoryB),
	}
}
