package main

import (
	"fmt"
	"os"
	"path/filepath"
	"strings"

	"github.com/funfunpayer/SamNPlayer/generator"
	"github.com/funfunpayer/SamNPlayer/generator/bodyparts"
	"github.com/funfunpayer/SamNPlayer/samn"
)

// ScanSceneMap runs the quick rhythm heatmap (P1 / docs/SCENE_MAP_PLAN.md).
// Explicit Advanced control only — not automatic before Generate (Owner 24 Sep).
// n <= 0 uses generator.DefaultSceneMapWindows (6).
func (a *App) ScanSceneMap(videoPath string, n int) (generator.SceneMapDTO, error) {
	return generator.ScanSceneMap(videoPath, n)
}

// SceneMapAvailable reports whether the OpenCV-backed quick scan can run.
func (a *App) SceneMapAvailable() bool {
	return generator.NativeTrackingAvailable()
}

// LoadSceneMapForVideo restores Advanced scene-map state from a companion
// .samn beside the video (P4 persist → Create). Missing/old files return
// Found=false without error. Does not change Generate defaults.
func (a *App) LoadSceneMapForVideo(videoPath string) (generator.SceneMapLoad, error) {
	return generator.LoadSceneMapBesideVideo(videoPath)
}

// ExportSceneMapLearning writes local scene_map_learning artifacts for one
// .samn (or companion beside a video/funscript). Requires Settings
// Collect learning data. Never writes YOLO images/labels/train.
func (a *App) ExportSceneMapLearning(path string) (generator.LearningExportResult, error) {
	if err := requireSuite(); err != nil {
		return generator.LearningExportResult{}, err
	}
	samnPath, err := resolveSamnForLearning(path)
	if err != nil {
		return generator.LearningExportResult{}, err
	}
	optIn := false
	if a.settings != nil {
		optIn = a.settings.GetBool(prefCollectLearningData, false)
	}
	outDir := ""
	if a.settings != nil {
		if root := strings.TrimSpace(a.settings.GetString(prefRoiDatasetDir, "")); root != "" {
			outDir = filepath.Join(root, generator.SceneMapLearningSubdir)
		}
	}
	return generator.ExportSceneMapLearning(samnPath, generator.LearningExportOptions{
		OptIn:     optIn,
		OutputDir: outDir,
	})
}

// DeleteSceneMapLearningData removes only scene_map_learning under the
// configured ROI dataset root (or the default). Hand YOLO samples stay.
func (a *App) DeleteSceneMapLearningData() error {
	if err := requireSuite(); err != nil {
		return err
	}
	root := ""
	if a.settings != nil {
		root = strings.TrimSpace(a.settings.GetString(prefRoiDatasetDir, ""))
	}
	return generator.DeleteSceneMapLearningData(root)
}

// SuggestExcludePriors returns L1 suggest-only Ignore marks from local
// Collect exports (exclude_decisions.jsonl). Never auto-applies; GUI merges
// after the user clicks. Everyday CSRT / Rhythm / Enforcement untouched.
func (a *App) SuggestExcludePriors(width, height int) (generator.ExcludePriorResult, error) {
	learnDir := ""
	if a.settings != nil {
		if root := strings.TrimSpace(a.settings.GetString(prefRoiDatasetDir, "")); root != "" {
			learnDir = filepath.Join(root, generator.SceneMapLearningSubdir)
		}
	}
	return generator.SuggestExcludePriors(width, height, learnDir)
}

// ReviewAutoContactCandidate confirms (reviewed:true) or rejects (deletes)
// one author:auto contact candidate in the companion .samn beside videoPath.
// Map-view Accept / Reject for teacher-contact marks (V3). No Generate change.
func (a *App) ReviewAutoContactCandidate(videoPath, markID string, accept bool) error {
	if err := requireSuite(); err != nil {
		return err
	}
	return generator.ReviewAutoContactCandidate(videoPath, markID, accept)
}

// ImportContactCandidatesForVideo writes teacher-consensus contact boxes from
// a contact_points JSON into the companion .samn as author:auto reviewed:false
// marks. Requires an existing scene map (Create with rhythm grid first).
func (a *App) ImportContactCandidatesForVideo(videoPath, contactPath string) (int, error) {
	if err := requireSuite(); err != nil {
		return 0, err
	}
	return generator.ImportContactCandidatesForVideo(videoPath, contactPath)
}

// SceneProposalLoad is the Create GUI view of one window from
// LoadSceneProposals + .At(ms). Found=false when the companion file is
// missing (no error) or the file has no usable proposals.
type SceneProposalLoad struct {
	Found       bool                    `json:"found"`
	Path        string                  `json:"path"`
	Width       int                     `json:"width"`
	Height      int                     `json:"height"`
	Count       int                     `json:"count"`
	Proposal    generator.SceneProposal `json:"proposal"`
	RegionClass string                  `json:"regionClass"` // canonical primary class, else ""
}

// SceneProposalsPathBesideVideo returns <clip>.scene.json next to the video.
func SceneProposalsPathBesideVideo(videoPath string) string {
	videoPath = strings.TrimSpace(videoPath)
	if videoPath == "" {
		return ""
	}
	ext := filepath.Ext(videoPath)
	return strings.TrimSuffix(videoPath, ext) + ".scene.json"
}

// LoadSceneProposalsBesideVideo restores Scene2 proposals from the companion
// <clip>.scene.json. Missing file → Found=false, no error. Does not apply ROI
// (TFTJ: user applies; no silent ROI2).
func (a *App) LoadSceneProposalsBesideVideo(videoPath string, atMs int64) (SceneProposalLoad, error) {
	path := SceneProposalsPathBesideVideo(videoPath)
	if path == "" {
		return SceneProposalLoad{}, nil
	}
	if _, err := os.Stat(path); err != nil {
		if os.IsNotExist(err) {
			return SceneProposalLoad{Path: path}, nil
		}
		return SceneProposalLoad{}, err
	}
	return a.LoadSceneProposalAt(path, atMs)
}

// LoadSceneProposalAt reads a scene_roles.py JSON and returns the proposal
// for atMs (.At). Empty proposals → Found=false. Primary class is exposed as
// RegionClass only when canonical (same rule as CLI --scene-proposals).
func (a *App) LoadSceneProposalAt(path string, atMs int64) (SceneProposalLoad, error) {
	path = strings.TrimSpace(path)
	out := SceneProposalLoad{Path: path}
	if path == "" {
		return out, fmt.Errorf("no scene proposals path")
	}
	s, err := generator.LoadSceneProposals(path)
	if err != nil {
		return out, err
	}
	out.Width, out.Height, out.Count = s.Width, s.Height, len(s.Proposals)
	p, ok := s.At(atMs)
	if !ok {
		return out, nil
	}
	out.Found = true
	out.Proposal = p
	if bodyparts.IsCanonical(p.Primary.Class) {
		out.RegionClass = bodyparts.Normalize(p.Primary.Class)
	}
	return out, nil
}

func resolveSamnForLearning(path string) (string, error) {
	path = strings.TrimSpace(path)
	if path == "" {
		return "", fmt.Errorf("no video or .samn path")
	}
	if strings.EqualFold(filepath.Ext(path), ".samn") {
		return path, nil
	}
	return samn.CompanionSamnPath(path), nil
}
