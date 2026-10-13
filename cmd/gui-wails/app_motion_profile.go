package main

import (
	"fmt"

	"github.com/funfunpayer/SamNPlayer/generator"
	"github.com/funfunpayer/SamNPlayer/generator/profilemodel"
	"github.com/funfunpayer/SamNPlayer/logging"
)

// GetMotionProfileModelStatus reports both the collected scene-memory samples
// and the persisted Go model. Reading JSON/JSONL is cheap and synchronous.
func (a *App) GetMotionProfileModelStatus() (profilemodel.Status, error) {
	if err := requireSuite(); err != nil {
		return profilemodel.Status{}, err
	}
	return profilemodel.StatusDefault()
}

// TrainMotionProfileModel learns explainable per-profile centroids from the
// saved motion signatures. Training/classification need no PyTorch, GPU or
// network; extracting a new video's signature still uses local Python/OpenCV.
func (a *App) TrainMotionProfileModel() (profilemodel.Status, error) {
	if err := requireSuite(); err != nil {
		return profilemodel.Status{}, err
	}
	model, err := profilemodel.TrainDefault()
	if err != nil {
		return profilemodel.Status{}, err
	}
	logging.Info("motion profile model trained",
		"samples", model.SampleCount,
		"classes", len(model.Classes),
		"model", profilemodel.DefaultModelPath())
	return profilemodel.StatusDefault()
}

// LabelSceneWithProfile records the scene name plus the profile the user has
// actually selected in Create. The profile is supervision, not an automatic
// prediction, and is later consumed by TrainMotionProfileModel.
func (a *App) LabelSceneWithProfile(videoPath, label, profile string) error {
	if err := requireSuite(); err != nil {
		return err
	}
	profile = profilemodel.NormalizeProfile(profile)
	if profile == "" {
		return fmt.Errorf("unsupported generator profile; expected standard, weich or autotune")
	}
	signature, err := generator.ExtractMotionSignature(videoPath)
	if err != nil {
		return err
	}
	return profilemodel.AppendLabel("", label, profile, signature)
}

func localMotionProfileSuggestion(videoPath string) (generator.ProfileSuggestion, bool) {
	model, err := profilemodel.Load("")
	if err != nil {
		return generator.ProfileSuggestion{}, false
	}
	signature, err := generator.ExtractMotionSignature(videoPath)
	if err != nil {
		logging.Warn("motion profile model: signature extraction failed", "error", err)
		return generator.ProfileSuggestion{}, false
	}
	result := profilemodel.Suggest(signature, model)
	if !result.Found {
		return generator.ProfileSuggestion{}, false
	}
	return generator.ProfileSuggestion{
		Found:      true,
		Label:      result.Profile,
		Kind:       "local_model",
		Confidence: result.Confidence,
	}, true
}
