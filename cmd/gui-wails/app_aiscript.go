package main

import (
	"fmt"
	"os"
	"path/filepath"
	"strings"

	"github.com/funfunpayer/SamNPlayer/funscript"
	"github.com/funfunpayer/SamNPlayer/generator"
	"github.com/funfunpayer/SamNPlayer/generator/aiscript"
	"github.com/funfunpayer/SamNPlayer/samn"
)

func aiScriptImitationDir() string {
	return aiscript.ImitationDirUnder(generator.DefaultRoiDatasetDir())
}

func aiScriptModelPath(a *App) string {
	if a == nil || a.settings == nil {
		return ""
	}
	return a.settings.GetString("generator.aiScriptModelPath", "")
}

// AIScriptWriterStatus reports the experimental AI draft-script path
// (docs/AI_SCRIPT_WRITER.md). Everyday Create stays CSRT. Available when the
// local imitation library has ≥1 exported classical sample (S2-imitation).
func (a *App) AIScriptWriterStatus() aiscript.Status {
	return aiscript.StatusWithLibrary(aiScriptModelPath(a), aiScriptImitationDir())
}

// DraftAIScript is the opt-in AI stroke draft. Uses the imitation library
// when samples exist; GUI must not call this unless Status.Available.
// tip* and durationMs are optional (zeros = unused / keep sample span).
func (a *App) DraftAIScript(videoPath string, tipX, tipY, tipW, tipH, durationMs float64) (aiscript.DraftResult, error) {
	req := aiscript.DraftRequest{
		VideoPath:    strings.TrimSpace(videoPath),
		ModelPath:    aiScriptModelPath(a),
		ImitationDir: aiScriptImitationDir(),
		DurationMs:   int64(durationMs),
	}
	if tipW > 0 && tipH > 0 {
		req.TipX, req.TipY, req.TipW, req.TipH = tipX, tipY, tipW, tipH
	}
	// Prefer companion classical script duration when caller left duration 0.
	if req.DurationMs <= 0 && req.VideoPath != "" {
		if d := companionScriptDurationMs(req.VideoPath); d > 0 {
			req.DurationMs = d
		}
	}
	res, err := aiscript.Draft(req)
	if err != nil {
		return res, err
	}
	fsActions := toFunscriptActions(res.Actions)
	q := funscript.EvaluateScriptQuality(fsActions)
	res.QDPassed = &q.Passed
	res.QDScore = &q.Score
	res.QDWarnings = append([]string(nil), q.Warnings...)
	return res, nil
}

// KeepAIScriptDraftResult is returned after writing an accepted AI draft.
type KeepAIScriptDraftResult struct {
	Path       string   `json:"path"`
	Message    string   `json:"message"`
	QDPassed   bool     `json:"qdPassed"`
	QDScore    float64  `json:"qdScore"`
	QDWarnings []string `json:"qdWarnings,omitempty"`
}

// KeepAIScriptDraft writes an explicit user-accepted AI draft beside the
// video as .samn (+ companion .funscript). Never called from Everyday Create.
func (a *App) KeepAIScriptDraft(videoPath string, actions []aiscript.Action) (KeepAIScriptDraftResult, error) {
	out := KeepAIScriptDraftResult{}
	videoPath = strings.TrimSpace(videoPath)
	if videoPath == "" {
		return out, fmt.Errorf("video path required")
	}
	if len(actions) < 2 {
		return out, fmt.Errorf("draft needs at least 2 points")
	}
	fsActions := toFunscriptActions(actions)
	quality := funscript.EvaluateScriptQuality(fsActions)
	out.QDPassed = quality.Passed
	out.QDScore = quality.Score
	out.QDWarnings = append([]string(nil), quality.Warnings...)

	base := strings.TrimSuffix(videoPath, filepath.Ext(videoPath))
	samnPath := base + ".samn"
	funPath := base + ".funscript"

	script := &funscript.Script{Actions: fsActions}
	script.Metadata.Creator = "SamNPlayer AI draft (imitation)"
	script.Metadata.QualityScore = &quality.Score
	script.Metadata.QualityPassed = &quality.Passed
	script.Metadata.QualityWarnings = quality.Warnings
	doc := samn.FromFunscript(script, videoPath)
	doc.Creator = "SamNPlayer AI draft (imitation)"
	if err := samn.Save(samnPath, doc); err != nil {
		return out, err
	}
	if err := doc.ExportFunscript(funPath); err != nil {
		return out, err
	}
	out.Path = samnPath
	out.Message = fmt.Sprintf("Kept AI draft → %s (QD score %.2f, passed=%v)", filepath.Base(samnPath), quality.Score, quality.Passed)
	return out, nil
}

// ExportAIScriptImitationResult is returned after writing one S1 sample.
type ExportAIScriptImitationResult struct {
	Path    string `json:"path"`
	Message string `json:"message"`
}

// ExportAIScriptImitation writes one classical good-run sample for offline
// AI draft training (S1). Opt-in only — never called from Everyday Generate.
// tipX/Y/W/H are optional Create tip ROI seed (zeros = omitted).
func (a *App) ExportAIScriptImitation(scriptPath, videoPath string, tipX, tipY, tipW, tipH float64) (ExportAIScriptImitationResult, error) {
	if err := requireSuite(); err != nil {
		return ExportAIScriptImitationResult{}, err
	}
	out := ExportAIScriptImitationResult{}
	scriptPath = strings.TrimSpace(scriptPath)
	if scriptPath == "" {
		return out, fmt.Errorf("no script path")
	}
	script, err := a.loadScriptDocument(preferSamnCompanion(scriptPath))
	if err != nil {
		return out, err
	}
	if len(script.Actions) < 2 {
		return out, fmt.Errorf("script needs at least 2 points")
	}
	dir := aiScriptImitationDir()
	sample := aiscript.ImitationSample{
		VideoPath:  strings.TrimSpace(videoPath),
		ScriptPath: scriptPath,
		Engine:     "csrt",
		Actions:    toAIScriptActions(script.Actions),
		QDPassed:   script.Metadata.QualityPassed,
		QDScore:    script.Metadata.QualityScore,
		Notes:      "S1 classical export — opt-in; Everyday CSRT unchanged",
	}
	if tipW > 0 && tipH > 0 {
		sample.TipX, sample.TipY, sample.TipW, sample.TipH = tipX, tipY, tipW, tipH
	}
	if len(script.Actions) >= 2 {
		sample.DurationMs = script.Actions[len(script.Actions)-1].At - script.Actions[0].At
	}
	path, err := aiscript.ExportImitationSample(dir, sample)
	if err != nil {
		return out, err
	}
	out.Path = path
	out.Message = fmt.Sprintf("Exported training sample → %s (%d in library)", filepath.Base(path), aiscript.CountValidSamples(dir))
	return out, nil
}

func toAIScriptActions(in []funscript.Action) []aiscript.Action {
	out := make([]aiscript.Action, len(in))
	for i, a := range in {
		out[i] = aiscript.Action{At: a.At, Pos: a.Pos}
	}
	return out
}

func toFunscriptActions(in []aiscript.Action) []funscript.Action {
	out := make([]funscript.Action, len(in))
	for i, a := range in {
		out[i] = funscript.Action{At: a.At, Pos: a.Pos}
	}
	return out
}

func companionScriptDurationMs(videoPath string) int64 {
	base := strings.TrimSuffix(videoPath, filepath.Ext(videoPath))
	for _, path := range []string{base + ".samn", base + ".funscript"} {
		if _, err := os.Stat(path); err != nil {
			continue
		}
		script, err := loadScriptBeside(path)
		if err != nil || len(script.Actions) < 2 {
			continue
		}
		return script.Actions[len(script.Actions)-1].At - script.Actions[0].At
	}
	return 0
}

func loadScriptBeside(path string) (*funscript.Script, error) {
	path = preferSamnCompanion(path)
	if samn.IsSamnPath(path) {
		doc, err := samn.Load(path)
		if err != nil {
			return nil, err
		}
		return doc.ToFunscript()
	}
	return funscript.Load(path)
}
