package main

import (
	"context"
	"encoding/json"
	"errors"
	"os"
	"path/filepath"
	"strings"
	"time"

	"github.com/wailsapp/wails/v2/pkg/runtime"

	"github.com/funfunpayer/SamNPlayer/funscript"
	"github.com/funfunpayer/SamNPlayer/generator"
	"github.com/funfunpayer/SamNPlayer/generator/bodyparts"
	"github.com/funfunpayer/SamNPlayer/logging"
	"github.com/funfunpayer/SamNPlayer/sam"
	"github.com/funfunpayer/SamNPlayer/samn"
)

func (a *App) CheckGeneratorDependencies() error {
	return generator.CheckDependencies()
}

type FramePreview struct {
	Width  int    `json:"width"`
	Height int    `json:"height"`
	PNGB64 string `json:"pngBase64"`
}

func (a *App) LoadFirstFrame(videoPath string) (FramePreview, error) {
	return a.LoadFrameAt(videoPath, 0)
}

// LoadFrameAt extracts a preview PNG at timeSec (seconds). Uses ffmpeg so
// seeking past a black intro does not need Python.
func (a *App) LoadFrameAt(videoPath string, timeSec float64) (FramePreview, error) {
	tmpPNG, err := tempFile("frame-preview-*.png")
	if err != nil {
		return FramePreview{}, err
	}
	defer removeFile(tmpPNG)
	// Bounded ctx so a hung/huge-file ffmpeg on a scrub preview can actually
	// be killed (MT-Infra ctx-kill hygiene) — same pattern as
	// app_playback_video.go's Probe/EnsurePlayableProxy timeouts.
	ctx, cancel := context.WithTimeout(context.Background(), 20*time.Second)
	defer cancel()
	w, h, err := generator.DumpFrameAt(ctx, videoPath, tmpPNG, timeSec)
	if err != nil {
		// Fall back to Python dump for time 0 if ffmpeg path fails.
		if timeSec <= 0 {
			w, h, err = generator.DumpFirstFrame(videoPath, tmpPNG)
		}
		if err != nil {
			return FramePreview{}, err
		}
	}
	b64, err := fileToBase64(tmpPNG)
	if err != nil {
		return FramePreview{}, err
	}
	return FramePreview{Width: w, Height: h, PNGB64: b64}, nil
}

type GenerateOptions struct {
	VideoPath         string `json:"videoPath"`
	X                 int    `json:"x"`
	Y                 int    `json:"y"`
	W                 int    `json:"w"`
	H                 int    `json:"h"`
	X2                int    `json:"x2"`
	Y2                int    `json:"y2"`
	W2                int    `json:"w2"`
	H2                int    `json:"h2"`
	Invert            bool   `json:"invert"`
	SmoothWindow      int    `json:"smoothWindow"`
	MinPeakDistanceMs int    `json:"minPeakDistanceMs"`
	// PeakProminence: fraction of position span for peak gate (Expert).
	// 0 = leave to profile defaults on the native path (Everyday unchanged).
	PeakProminence            float64 `json:"peakProminence"`
	DisableCameraCompensation bool    `json:"disableCameraCompensation"`
	DisableSceneCutDetection  bool    `json:"disableSceneCutDetection"`
	RDPTolerance              float64 `json:"rdpTolerance"`
	PerSceneROI               bool    `json:"perSceneRoi"`
	AdaptiveKeyframeError     float64 `json:"adaptiveKeyframeError"`
	MaxSpeed                  float64 `json:"maxSpeed"`
	Axis                      string  `json:"axis"`
	AutoRetry                 bool    `json:"autoRetry"`
	Backend                   string  `json:"backend"`
	UseOpenCL                 bool    `json:"useOpenCl"`
	DynamicRangeMs            float64 `json:"dynamicRangeMs"`
	Profile                   string  `json:"profile"`
	Overwrite                 bool    `json:"overwrite"`
	AIQualityOpinion          bool    `json:"aiQualityOpinion"`
	ContactVibration          bool    `json:"contactVibration"`
	ContactVibrationSpan      float64 `json:"contactVibrationSpan"`
	ContactVibrationCurve     string  `json:"contactVibrationCurve"`
	AutoOZoneMarker           bool    `json:"autoOZoneMarker"`
	AudioCheck                bool    `json:"audioCheck"`
	NativePipeline            bool    `json:"nativePipeline"`
	// StartTimeSec skips the first N seconds (GUI seek past black intro).
	StartTimeSec float64 `json:"startTimeSec"`
	// FlowDownscale is only used by the optical-flow backend (0/1 = full res).
	FlowDownscale float64 `json:"flowDownscale"`
	// Roi2Fixed keeps the second Tf/Tj box static (contact target).
	Roi2Fixed bool `json:"roi2Fixed"`
	// RegionClass / RegionClass2 are optional body-part IDs (docs/BODY_REGIONS.md).
	RegionClass  string `json:"regionClass"`
	RegionClass2 string `json:"regionClass2"`
	// ExtraTargets: extra contact anchors; Fixed=false follows when tip path on.
	ExtraTargets []generator.NamedROI `json:"extraTargets"`
	// MaskROIs: soft-exclude boxes for feature masks.
	MaskROIs []generator.ROI `json:"maskRois"`
	// SceneMarks: Advanced scene-map exclude/source/region marks (M3).
	SceneMarks []generator.SceneMark `json:"sceneMapMarks"`
	// CaptureTrajectory: opt-in raw tip/partner (x,y) recording for the
	// Review/Play MT-Debug trajectory overlay. Off by default; Go CSRT only.
	CaptureTrajectory bool `json:"captureTrajectory"`
	// RhythmGrid: opt-in drift-robust stroke signal (Go CSRT single ROI).
	RhythmGrid bool `json:"rhythmGrid"`
	// ContactPointsFile: contact_points.py JSON; only used with RhythmGrid.
	// Empty = off (bit-identical). GUI opt-in; never a silent default.
	ContactPointsFile string `json:"contactPointsFile"`
	// ContactVerifyK: hybrid check — keep a teacher contact point only where
	// the engine's own rhythm is ≥ K× stronger than at its chosen cell
	// (measured default 1.5). 0 = off. Only meaningful with RhythmGrid +
	// ContactPointsFile; GUI switch default off.
	ContactVerifyK float64 `json:"contactVerifyK"`
}

// AutoDetectROI sucht die Region automatisch. engine "ai" nutzt den lokalen
// ONNX-Objekterkenner (ai_roi.py, siehe docs/AI_ADAPTER.md); "auto_two" /
// "ai_two" schlagen ROI1+ROI2 für Tf/Tj vor (nur Vorschlag). Jeder andere
// Wert (leer, "auto", ...) bleibt bei der klassischen Rhythmus-Heuristik
// (auto_roi.py) - so bricht ein alter Frontend-Aufruf ohne engine-Argument
// nicht, er bekommt nur weiterhin das klassische Verhalten.
//
// Each call bumps roiSeq; only the latest seq may emit generate:autoroi
// (stale finds are dropped). Payload always includes videoPath + seq.
func (a *App) AutoDetectROI(videoPath string, engine string) {
	// Cancellable so CancelROIDetection kills classic/AI find (CommandContext).
	mySeq, roiCtx, finish := a.beginROIRequest(true)

	go func() {
		defer finish()
		emitErr := func(msg string) {
			a.emitAutoROI(videoPath, mySeq, map[string]any{"error": msg})
		}
		onLine := func(line string) {
			if a.roiRequestCurrent(mySeq) {
				runtime.EventsEmit(a.ctx, "generate:progress", line)
			}
		}
		onPct := func(pct int) {
			if a.roiRequestCurrent(mySeq) {
				runtime.EventsEmit(a.ctx, "generate:percent", pct)
			}
		}
		switch engine {
		case "ai_two":
			roi, roi2, err := generator.FindTwoROIsAIWithContext(roiCtx, videoPath,
				a.settings.GetString(prefAIRoiModelPath, ""),
				a.settings.GetString(prefAIPreferredClasses, ""),
				onLine, onPct)
			if err != nil {
				emitErr(err.Error())
				return
			}
			payload := map[string]any{
				"x": roi.X, "y": roi.Y, "w": roi.W, "h": roi.H, "engine": "ai",
			}
			if roi2.W > 0 && roi2.H > 0 {
				payload["x2"] = roi2.X
				payload["y2"] = roi2.Y
				payload["w2"] = roi2.W
				payload["h2"] = roi2.H
			}
			attachROIVerify(payload, videoPath, roi)
			a.emitAutoROI(videoPath, mySeq, payload)
		case "auto_two":
			roi, roi2, err := generator.FindTwoROIsWithContext(roiCtx, videoPath, onLine, onPct)
			if err != nil {
				emitErr(err.Error())
				return
			}
			payload := map[string]any{
				"x": roi.X, "y": roi.Y, "w": roi.W, "h": roi.H, "engine": "auto",
			}
			if roi2.W > 0 && roi2.H > 0 {
				payload["x2"] = roi2.X
				payload["y2"] = roi2.Y
				payload["w2"] = roi2.W
				payload["h2"] = roi2.H
			}
			attachROIVerify(payload, videoPath, roi)
			a.emitAutoROI(videoPath, mySeq, payload)
		case "ai":
			roi, err := generator.FindROIAIWithContext(roiCtx, videoPath,
				a.settings.GetString(prefAIRoiModelPath, ""),
				a.settings.GetString(prefAIPreferredClasses, ""),
				onLine, onPct)
			if err != nil {
				emitErr(err.Error())
				return
			}
			payload := map[string]any{
				"x": roi.X, "y": roi.Y, "w": roi.W, "h": roi.H, "engine": engine,
			}
			attachROIVerify(payload, videoPath, roi)
			a.emitAutoROI(videoPath, mySeq, payload)
		default:
			roi, err := generator.FindROIWithContext(roiCtx, videoPath, onLine, onPct)
			if err != nil {
				emitErr(err.Error())
				return
			}
			payload := map[string]any{
				"x": roi.X, "y": roi.Y, "w": roi.W, "h": roi.H, "engine": engine,
			}
			attachROIVerify(payload, videoPath, roi)
			a.emitAutoROI(videoPath, mySeq, payload)
		}
	}()
}

func (a *App) beginROIRequest(cancellable bool) (uint64, context.Context, func()) {
	a.stateMu.Lock()
	if a.roiCancel != nil {
		a.roiCancel()
		a.roiCancel = nil
	}
	a.roiSeq++
	seq := a.roiSeq
	ctx := a.ctx
	if ctx == nil {
		ctx = context.Background()
	}
	var cancel context.CancelFunc
	if cancellable {
		ctx, cancel = context.WithCancel(ctx)
		a.roiCancel = cancel
	}
	a.stateMu.Unlock()
	finish := func() {
		if cancel != nil {
			cancel()
		}
		a.stateMu.Lock()
		if a.roiSeq == seq {
			a.roiCancel = nil
		}
		a.stateMu.Unlock()
	}
	return seq, ctx, finish
}

func (a *App) roiRequestCurrent(seq uint64) bool {
	a.stateMu.RLock()
	defer a.stateMu.RUnlock()
	return a.roiSeq == seq
}

// CancelROIDetection invalidates queued result/progress events and stops an
// active strict ONNX subprocess. It is safe when no detector is running.
func (a *App) CancelROIDetection() {
	a.stateMu.Lock()
	defer a.stateMu.Unlock()
	if a.roiCancel != nil {
		a.roiCancel()
		a.roiCancel = nil
	}
	a.roiSeq++
}

// DetectExpectedTipROI asks the local ONNX model for exactly one canonical
// body class. It emits a proposal on generate:tip-detection; the frontend must
// require Apply before changing the active ROI. Strict failures never call the
// generic motion finder and never substitute a different detected class.
func (a *App) DetectExpectedTipROI(videoPath, expectedClass string, timeSec float64, requestID uint64) {
	expectedClass = bodyparts.Normalize(expectedClass)
	mySeq, detectorCtx, finish := a.beginROIRequest(true)

	go func() {
		defer finish()
		payload := map[string]any{
			"videoPath": videoPath, "seq": mySeq,
			"requestId":     requestID,
			"expectedClass": expectedClass, "match": false,
		}
		emit := func() {
			a.stateMu.RLock()
			current := a.roiSeq
			a.stateMu.RUnlock()
			if mySeq == current {
				runtime.EventsEmit(a.ctx, "generate:tip-detection", payload)
			}
		}
		if !bodyparts.IsCanonical(expectedClass) {
			payload["status"] = "class_unresolved"
			payload["errorCode"] = "class_unresolved"
			payload["error"] = "Choose a known expected body class before strict AI detection"
			emit()
			return
		}
		onLine := func(line string) {
			if a.roiRequestCurrent(mySeq) {
				runtime.EventsEmit(a.ctx, "generate:tip-progress", map[string]any{
					"requestId": requestID, "videoPath": videoPath, "line": line,
				})
			}
		}
		onPct := func(pct int) {
			if a.roiRequestCurrent(mySeq) {
				runtime.EventsEmit(a.ctx, "generate:tip-percent", map[string]any{
					"requestId": requestID, "videoPath": videoPath, "percent": pct,
				})
			}
		}
		previewPNG, err := tempFile("strict-target-preview-*.png")
		if err != nil {
			payload["status"] = "preview_failed"
			payload["errorCode"] = "preview_failed"
			payload["error"] = err.Error()
			emit()
			return
		}
		defer removeFile(previewPNG)
		if _, _, err = generator.DumpFrameAt(detectorCtx, videoPath, previewPNG, timeSec); err != nil {
			payload["status"] = "preview_failed"
			payload["errorCode"] = "preview_failed"
			payload["error"] = err.Error()
			emit()
			return
		}
		result, err := generator.FindExpectedTipImageAIWithProgressContext(
			detectorCtx,
			previewPNG,
			a.settings.GetString(prefAIRoiModelPath, ""),
			expectedClass,
			onLine,
			onPct,
		)
		if err != nil {
			payload["status"] = "detector_failed"
			payload["errorCode"] = "detector_failed"
			payload["error"] = err.Error()
			var strictErr *generator.StrictTipDetectionError
			if errors.As(err, &strictErr) {
				payload["status"] = strictErr.Code
				payload["errorCode"] = strictErr.Code
				if strictErr.ExpectedClass != "" {
					payload["expectedClass"] = strictErr.ExpectedClass
				}
			}
			emit()
			return
		}
		payload["x"], payload["y"], payload["w"], payload["h"] = result.X, result.Y, result.W, result.H
		payload["expectedClass"] = result.ExpectedClass
		payload["matchedClass"] = result.MatchedClass
		payload["matchedClassId"] = result.MatchedClassID
		payload["confidence"] = result.Confidence
		payload["sampleIndex"] = result.SampleIndex
		payload["match"] = result.Match
		payload["status"] = result.Status
		attachROIVerify(payload, videoPath, generator.ROI{X: result.X, Y: result.Y, W: result.W, H: result.H})
		emit()
	}()
}

// emitAutoROI drops superseded finds (newer AutoDetectROI started).
func (a *App) emitAutoROI(videoPath string, seq uint64, payload map[string]any) {
	a.stateMu.RLock()
	current := a.roiSeq
	a.stateMu.RUnlock()
	if seq != current {
		return
	}
	payload["videoPath"] = videoPath
	payload["seq"] = seq
	runtime.EventsEmit(a.ctx, "generate:autoroi", payload)
}

// SuggestROICandidates lists ranked motion regions without applying any ROI.
// Event generate:roi-candidates — TFTJ step 4b (pick primary yourself).
func (a *App) SuggestROICandidates(videoPath string) {
	mySeq, roiCtx, finish := a.beginROIRequest(true)
	go func() {
		defer finish()
		onLine := func(line string) {
			if a.roiRequestCurrent(mySeq) {
				runtime.EventsEmit(a.ctx, "generate:progress", line)
			}
		}
		onPct := func(pct int) {
			if a.roiRequestCurrent(mySeq) {
				runtime.EventsEmit(a.ctx, "generate:percent", pct)
			}
		}
		cands, err := generator.FindROICandidatesWithContext(roiCtx, videoPath, onLine, onPct)
		if err != nil {
			if a.roiRequestCurrent(mySeq) {
				runtime.EventsEmit(a.ctx, "generate:roi-candidates", map[string]any{
					"error": err.Error(), "videoPath": videoPath, "seq": mySeq,
				})
			}
			return
		}
		list := make([]map[string]any, 0, len(cands))
		for _, c := range cands {
			list = append(list, map[string]any{
				"x": c.X, "y": c.Y, "w": c.W, "h": c.H,
				"score": c.Score, "index": c.Index,
			})
		}
		if a.roiRequestCurrent(mySeq) {
			runtime.EventsEmit(a.ctx, "generate:roi-candidates", map[string]any{
				"candidates": list, "videoPath": videoPath, "seq": mySeq,
			})
		}
	}()
}

// ListAIDetections lists all ONNX class boxes on the current preview time
// (nipples/breasts/glans/…). Event generate:ai-detections. Does not set tip ROI.
func (a *App) ListAIDetections(videoPath string, timeSec float64) {
	mySeq, roiCtx, finish := a.beginROIRequest(true)
	go func() {
		defer finish()
		onLine := func(line string) {
			if a.roiRequestCurrent(mySeq) {
				runtime.EventsEmit(a.ctx, "generate:progress", line)
			}
		}
		onPct := func(pct int) {
			if a.roiRequestCurrent(mySeq) {
				runtime.EventsEmit(a.ctx, "generate:percent", pct)
			}
		}
		model := a.settings.GetString(prefAIRoiModelPath, "")
		pref := a.settings.GetString(prefAIPreferredClasses, "")
		dets, err := generator.ListAIDetectionsWithContext(roiCtx, videoPath, model, pref, timeSec, onLine, onPct)
		if err != nil {
			if a.roiRequestCurrent(mySeq) {
				runtime.EventsEmit(a.ctx, "generate:ai-detections", map[string]any{
					"error": err.Error(), "videoPath": videoPath, "seq": mySeq,
				})
			}
			return
		}
		list := make([]map[string]any, 0, len(dets))
		for _, d := range dets {
			list = append(list, map[string]any{
				"x": d.X, "y": d.Y, "w": d.W, "h": d.H,
				"confidence": d.Confidence, "classId": d.ClassID,
				"className": d.ClassName, "index": d.Index,
			})
		}
		if a.roiRequestCurrent(mySeq) {
			runtime.EventsEmit(a.ctx, "generate:ai-detections", map[string]any{
				"detections": list, "videoPath": videoPath, "seq": mySeq,
			})
		}
	}()
}

// attachROIVerify runs a lightweight Go second-pass motion check and adds
// warning fields when the proposed box looks weak. Never changes the box.
func attachROIVerify(payload map[string]any, videoPath string, roi generator.ROI) {
	v := generator.VerifyROI(context.Background(), videoPath, roi)
	if v.Score > 0 {
		payload["verifyScore"] = v.Score
	}
	if v.Warning != "" {
		payload["verifyWarning"] = v.Warning
	}
}

// CheckAIRoiAvailable meldet, ob die KI-Regionssuche grundsätzlich nutzbar
// ist (onnxruntime installiert + Modell vorhanden) - ohne selbst ein Video zu
// öffnen. Die GUI nutzt das, um den KI-Knopf zu aktivieren/auszublenden statt
// ihn anzubieten und dann bei jedem Versuch scheitern zu lassen.
func (a *App) CheckAIRoiAvailable() bool {
	return generator.AIRoiAvailable(a.settings.GetString(prefAIRoiModelPath, ""))
}

// CheckAIServerAvailable probes the optional local Colibri / OpenAI-compatible
// server (Settings → AI server URL). Empty URL uses the default
// http://127.0.0.1:8080. Never raises; unreachable is the normal offline state.
func (a *App) CheckAIServerAvailable() bool {
	return generator.ColibriAvailable(a.settings.GetString(prefAIBaseURL, ""))
}

// SupportSignalsAvailable reports experimental depth/pose helper flags (see
// docs/DEPTH_POSE.md). No GUI toggle in this release — CLI/env only.
func (a *App) SupportSignalsAvailable() map[string]bool {
	return generator.SupportSignalsAvailable("", "")
}

// CheckAudioCheckAvailable meldet, ob --audio-check grundsätzlich nutzbar
// ist (ffmpeg auf dem PATH) - die GUI nutzt das, um die Checkbox zu
// aktivieren/auszublenden statt sie anzubieten und dann scheitern zu lassen.
func (a *App) CheckAudioCheckAvailable() bool {
	return generator.AudioCheckAvailable()
}

// SuggestProfile tries the trained local Go model first. If it has no safe
// answer, the existing nearest-scene and optional Colibri fallbacks remain.
// Every result is only a proposal; the frontend requires an explicit Apply.
// Found=false is normal (unknown/ambiguous scene, no server reachable).
func (a *App) SuggestProfile(videoPath string) (generator.ProfileSuggestion, error) {
	if suggestion, ok := localMotionProfileSuggestion(videoPath); ok {
		return suggestion, nil
	}
	return generator.SuggestProfile(videoPath, a.settings.GetString(prefAIBaseURL, ""))
}

// LabelScene merkt sich die Bewegungssignatur des Videos unter label, damit
// spätere ähnliche Szenen darüber ein Profil vorgeschlagen bekommen
// (SuggestProfile).
func (a *App) LabelScene(videoPath, label string) error {
	return generator.LabelScene(videoPath, label)
}

func (a *App) ScriptExistsForVideo(videoPath string) bool {
	fs := scriptPathForVideo(videoPath)
	if _, err := os.Stat(fs); err == nil {
		return true
	}
	_, err := os.Stat(samn.CompanionSamnPath(fs))
	return err == nil
}

func (a *App) GenerateScript(opts GenerateOptions) {
	go func() {
		// Claim a seq immediately so early "already exists" errors cannot
		// clobber a newer Create that started after this goroutine was queued.
		a.stateMu.Lock()
		a.genSeq++
		mySeq := a.genSeq
		a.stateMu.Unlock()
		runtime.EventsEmit(a.ctx, "generate:started", map[string]any{"seq": mySeq})

		emitDone := func(payload map[string]any) {
			a.stateMu.RLock()
			cur := a.genSeq
			a.stateMu.RUnlock()
			if cur != mySeq {
				return
			}
			runtime.EventsEmit(a.ctx, "generate:done", payload)
		}
		emitProgress := func(line string) {
			a.stateMu.RLock()
			cur := a.genSeq
			a.stateMu.RUnlock()
			if cur != mySeq {
				return
			}
			runtime.EventsEmit(a.ctx, "generate:progress", line)
		}
		emitPercent := func(pct int) {
			a.stateMu.RLock()
			cur := a.genSeq
			a.stateMu.RUnlock()
			if cur != mySeq {
				return
			}
			runtime.EventsEmit(a.ctx, "generate:percent", pct)
		}

		outPath := scriptPathForVideo(opts.VideoPath)
		if !opts.Overwrite {
			if _, err := os.Stat(outPath); err == nil {
				emitDone(map[string]any{
					"error": "Script already exists: " + outPath + " — generation cancelled to avoid overwriting it.",
					"seq":   mySeq,
				})
				return
			}
			if companion := samn.CompanionSamnPath(outPath); companion != "" {
				if _, err := os.Stat(companion); err == nil {
					emitDone(map[string]any{
						"error": "Native script already exists: " + companion + " — generation cancelled to avoid overwriting it.",
						"seq":   mySeq,
					})
					return
				}
			}
		}
		roi := generator.ROI{X: opts.X, Y: opts.Y, W: opts.W, H: opts.H}
		genOpts := generator.Options{
			PerSceneROI:               opts.PerSceneROI,
			AdaptiveKeyframeError:     opts.AdaptiveKeyframeError,
			MaxSpeed:                  opts.MaxSpeed,
			Axis:                      opts.Axis,
			AutoRetry:                 opts.AutoRetry,
			Backend:                   opts.Backend,
			ReportPath:                a.activeReportPath(),
			UseOpenCL:                 opts.UseOpenCL,
			DynamicRangeMs:            opts.DynamicRangeMs,
			Profile:                   opts.Profile,
			Invert:                    opts.Invert,
			SmoothWindow:              opts.SmoothWindow,
			MinPeakDistanceMs:         opts.MinPeakDistanceMs,
			PeakProminence:            opts.PeakProminence,
			DisableCameraCompensation: opts.DisableCameraCompensation,
			DisableSceneCutDetection:  opts.DisableSceneCutDetection,
			RDPTolerance:              opts.RDPTolerance,
			AIQualityOpinion:          opts.AIQualityOpinion,
			AIBaseURL:                 a.settings.GetString(prefAIBaseURL, ""),
			ContactVibration:          opts.ContactVibration,
			ContactVibrationSpan:      opts.ContactVibrationSpan,
			ContactVibrationCurve:     opts.ContactVibrationCurve,
			AudioCheck:                opts.AudioCheck,
			NativePipeline:            opts.NativePipeline,
			StartTimeSec:              opts.StartTimeSec,
			FlowDownscale:             opts.FlowDownscale,
			CaptureTrajectory:         opts.CaptureTrajectory,
			RhythmGrid:                opts.RhythmGrid,
			ContactPointsFile:         opts.ContactPointsFile,
			ContactVerifyK:            0,
			DetrendWindowMs:           0,
			BandpassLowHz:             0,
			BandpassHighHz:            0,
		}
		// Hybrid verify only with Rhythm + teacher points path (GUI switch).
		if opts.RhythmGrid && strings.TrimSpace(opts.ContactPointsFile) != "" && opts.ContactVerifyK > 0 {
			genOpts.ContactVerifyK = opts.ContactVerifyK
		}
		if opts.W2 > 0 && opts.H2 > 0 {
			genOpts.ROI2 = generator.ROI{X: opts.X2, Y: opts.Y2, W: opts.W2, H: opts.H2}
			genOpts.ROI2Fixed = opts.Roi2Fixed
		}
		genOpts.RegionClass = opts.RegionClass
		genOpts.RegionClass2 = opts.RegionClass2
		genOpts.ExtraTargets = opts.ExtraTargets
		genOpts.MaskROIs = opts.MaskROIs
		genOpts.SceneMarks = opts.SceneMarks
		ctx, cancel := context.WithCancel(context.Background())
		a.stateMu.Lock()
		if a.genSeq != mySeq {
			a.stateMu.Unlock()
			cancel()
			return
		}
		prev := a.genCancel
		a.genCancel = cancel
		a.stateMu.Unlock()
		if prev != nil {
			prev()
		}
		defer func() {
			a.stateMu.Lock()
			if a.genSeq == mySeq {
				a.genCancel = nil
			}
			a.stateMu.Unlock()
			cancel()
		}()
		err := generator.GenerateWithContext(ctx, opts.VideoPath, roi, outPath, genOpts,
			emitProgress, emitPercent)
		if err != nil {
			if errors.Is(err, context.Canceled) {
				emitDone(map[string]any{"error": "Generation cancelled", "cancelled": true, "seq": mySeq})
				return
			}
			emitDone(map[string]any{"error": err.Error(), "seq": mySeq})
			return
		}
		payload := map[string]any{"path": openPathAfterGenerate(outPath), "funscriptPath": outPath, "pipeline": "python", "seq": mySeq}
		if data, readErr := os.ReadFile(outPath); readErr == nil {
			var raw map[string]any
			if json.Unmarshal(data, &raw) == nil {
				if meta, ok := raw["metadata"].(map[string]any); ok {
					if np, ok := meta["native_pipeline"].(map[string]any); ok {
						payload["pipeline"] = "go"
						if t, ok := np["tracking"].(string); ok {
							payload["tracking"] = t
						}
						if b, ok := np["backend"].(string); ok {
							payload["backend"] = b
						}
						if r, ok := np["reason"].(string); ok && r != "" {
							payload["trackingReason"] = r
						}
						if lost, ok := np["lost"].(float64); ok {
							payload["trackingLostFrames"] = int(lost)
						}
					}
					if gaps, ok := meta["tracking_gaps"].([]any); ok && len(gaps) > 0 {
						payload["trackingGapCount"] = len(gaps)
					}
				}
			}
		}
		if script, loadErr := funscript.Load(outPath); loadErr == nil {
			if script.Metadata.QualityScore != nil {
				payload["qualityScore"] = *script.Metadata.QualityScore
				payload["qualityWarnings"] = script.Metadata.QualityWarnings
				if script.Metadata.QualityPassed != nil {
					payload["qualityPassed"] = *script.Metadata.QualityPassed
				}
				logging.Info("generator: quality score", "output", outPath, "score", *script.Metadata.QualityScore)
				if script.Metadata.AIOpinion != nil {
					payload["aiOpinionVerdict"] = script.Metadata.AIOpinion.Verdict
					payload["aiOpinionReason"] = script.Metadata.AIOpinion.Reason
				}
			}
			if script.Metadata.AudioCheck != nil {
				payload["audioCheckWarnings"] = script.Metadata.AudioCheck.Warnings
				if script.Metadata.AudioCheck.SpeechHoldMs > 0 {
					payload["speechHoldMs"] = script.Metadata.AudioCheck.SpeechHoldMs
				}
				if len(script.Metadata.AudioCheck.Segments) > 0 {
					payload["audioSegments"] = script.Metadata.AudioCheck.Segments
				}
			}
			if len(script.Metadata.TrackingGaps) > 0 {
				payload["trackingGapCount"] = len(script.Metadata.TrackingGaps)
			}
			if opts.AutoOZoneMarker {
				zone, err := applyAutoOZoneMarker(outPath, script.Actions)
				if err != nil {
					logging.Warn("generator: O markers could not be saved", "output", outPath, "error", err)
				} else if zone.OK {
					payload["oZoneMarkerStartMs"] = zone.StartMs
					payload["oZoneMarkerEndMs"] = zone.EndMs
				}
			}
			// Tf/Tj: SAM-Sidecar neben dem Funscript (Modell, kein Format-Ersatz).
			if funscript.IsDistanceProfile(script.Metadata.Profile) {
				if err := sam.WriteEnrichedSidecar(outPath, script); err != nil {
					logging.Warn("generator: SAM sidecar not written", "output", outPath, "error", err)
				} else {
					payload["samPath"] = sam.SidecarPath(outPath)
					logging.Info("generator: SAM sidecar written", "path", payload["samPath"])
				}
			}
		} else {
			logging.Warn("generator: generated script not readable", "output", outPath, "error", loadErr)
		}
		emitDone(payload)
	}()
}

// applyAutoOZoneMarker schreibt einen automatisch aus dem Positionssignal
// vorgeschlagenen O-Marker (funscript.SuggestOZone, letztes Achtel mit der
// höchsten mittleren Position) in die gerade erzeugte Datei, falls die
// Suggestion greift - klassisch aus dem Signal, kein eigenes KI-Modell
// (docs/NEXT.md Priorität 6/7). Eigene Funktion statt Inline-Code in
// GenerateScript, damit sie ohne echten Video-Generierungslauf testbar ist.
// Schreibt zusätzlich bis zu zwei schwächere, frühere Marker, falls das
// Signal welche hergibt (funscript.SuggestSecondaryOZones) - derselbe
// "primary + optionale sekundäre" Vorschlag wie beim manuellen
// "O-Zone vorschlagen"-Knopf (app_suggest.go), hier am Generierungspfad.
func applyAutoOZoneMarker(outPath string, actions []funscript.Action) (funscript.OZoneSuggestion, error) {
	zone := funscript.SuggestOZone(actions)
	if !zone.OK {
		return zone, nil
	}
	markers := []funscript.OMarker{{
		StartMs: zone.StartMs, EndMs: zone.EndMs,
		Kind: funscript.OMarkerPrimary, Intensity: 1,
	}}
	for _, sec := range funscript.SuggestSecondaryOZones(actions, zone, 2) {
		markers = append(markers, secondaryMarkerFromSuggestion(sec, zone))
	}
	if err := funscript.SaveOMarkers(outPath, markers); err != nil {
		return zone, err
	}
	// Keep companion .samn in sync when present (native source of truth).
	companion := samn.CompanionSamnPath(outPath)
	if st, err := os.Stat(companion); err == nil && !st.IsDir() {
		doc, err := samn.Load(companion)
		if err != nil {
			return zone, err
		}
		doc.OMarkers = markers
		if err := samn.Save(companion, doc); err != nil {
			return zone, err
		}
	}
	return zone, nil
}

func scriptPathForVideo(videoPath string) string {
	return strings.TrimSuffix(videoPath, filepath.Ext(videoPath)) + ".funscript"
}

// openPathAfterGenerate prefers the native .samn companion when present.
func openPathAfterGenerate(funscriptPath string) string {
	return preferSamnCompanion(funscriptPath)
}

func (a *App) GetHardwareInfo() (string, error) {
	return generator.HardwareInfo()
}

// CancelGenerate bricht die laufende Generierung ab (GenerateWithContext).
// Idempotent, wenn gerade nichts läuft.
func (a *App) CancelGenerate() {
	a.stateMu.Lock()
	cancel := a.genCancel
	a.stateMu.Unlock()
	if cancel != nil {
		cancel()
	}
}
