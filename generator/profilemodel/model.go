// Package profilemodel learns a small, explainable generator-profile model
// from the motion signatures that SamNPlayer already stores locally.
//
// It deliberately is not a video detector: OpenCV extracts the eight measured
// motion features, while this package owns training, model persistence and
// inference in pure Go. A model result is only a suggestion; the caller must
// require an explicit user action before changing generator settings.
package profilemodel

import (
	"bufio"
	"encoding/json"
	"errors"
	"fmt"
	"math"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"sync"
	"time"
)

const (
	ModelVersion     = 1
	SignatureVersion = 1
	minTrainSamples  = 2
	defaultMaxDist   = 0.15
)

var SignatureFields = []string{
	"vertical_share",
	"motion_center_y",
	"motion_center_x",
	"motion_spread",
	"region_count",
	"camera_motion",
	"stroke_symmetry",
	"rhythm_strength",
}

var allowedProfiles = map[string]bool{
	"standard": true,
	"weich":    true,
	"autotune": true,
}

var labelWriteMu sync.Mutex

type labelRecord struct {
	Version    int                `json:"version"`
	Label      string             `json:"label"`
	Signature  map[string]float64 `json:"signature"`
	Parameters map[string]any     `json:"parameters"`
}

type ProfileClass struct {
	Profile  string             `json:"profile"`
	Count    int                `json:"count"`
	Centroid map[string]float64 `json:"centroid"`
	Radius   float64            `json:"radius"`
}

type Model struct {
	Version          int            `json:"version"`
	SignatureVersion int            `json:"signatureVersion"`
	Fields           []string       `json:"fields"`
	TrainedAt        string         `json:"trainedAt"`
	SampleCount      int            `json:"sampleCount"`
	Classes          []ProfileClass `json:"classes"`
}

type ProfileCount struct {
	Profile string `json:"profile"`
	Count   int    `json:"count"`
}

type Status struct {
	LabelsPath     string         `json:"labelsPath"`
	ModelPath      string         `json:"modelPath"`
	LabelledScenes int            `json:"labelledScenes"`
	UsableSamples  int            `json:"usableSamples"`
	ReadyToTrain   bool           `json:"readyToTrain"`
	ModelAvailable bool           `json:"modelAvailable"`
	TrainedSamples int            `json:"trainedSamples"`
	TrainedAt      string         `json:"trainedAt"`
	ModelWarning   string         `json:"modelWarning,omitempty"`
	Profiles       []ProfileCount `json:"profiles"`
}

type Suggestion struct {
	Found        bool    `json:"found"`
	Profile      string  `json:"profile"`
	Confidence   float64 `json:"confidence"`
	Distance     float64 `json:"distance"`
	Margin       float64 `json:"margin"`
	SampleCount  int     `json:"sampleCount"`
	ModelVersion int     `json:"modelVersion"`
}

func DefaultLabelsPath() string {
	return configPath("szenensignaturen.jsonl")
}

func DefaultModelPath() string {
	return configPath(filepath.Join("models", "motion_profile_model.json"))
}

func configPath(name string) string {
	if local := os.Getenv("LOCALAPPDATA"); local != "" {
		return filepath.Join(local, "SamNPlayer", name)
	}
	xdg := os.Getenv("XDG_CONFIG_HOME")
	if xdg == "" {
		home, err := os.UserHomeDir()
		if err != nil {
			return ""
		}
		xdg = filepath.Join(home, ".config")
	}
	return filepath.Join(xdg, "SamNPlayer", name)
}

func NormalizeProfile(profile string) string {
	profile = strings.ToLower(strings.TrimSpace(profile))
	if profile == "tf" || profile == "tj" {
		return "standard"
	}
	if allowedProfiles[profile] {
		return profile
	}
	return ""
}

func validateSignature(signature map[string]float64) error {
	for _, field := range SignatureFields {
		v, ok := signature[field]
		if !ok || math.IsNaN(v) || math.IsInf(v, 0) {
			return fmt.Errorf("motion signature field %q is missing or invalid", field)
		}
	}
	return nil
}

// AppendLabel stores one confirmed scene and the generator profile selected by
// the user. The append-only JSONL format remains compatible with the existing
// Python motion_signature loader.
func AppendLabel(path, label, profile string, signature map[string]float64) error {
	labelWriteMu.Lock()
	defer labelWriteMu.Unlock()

	label = strings.TrimSpace(label)
	profile = NormalizeProfile(profile)
	if label == "" {
		return errors.New("scene name is required")
	}
	if profile == "" {
		return errors.New("generator profile must be standard, weich or autotune")
	}
	if err := validateSignature(signature); err != nil {
		return err
	}
	if path == "" {
		path = DefaultLabelsPath()
	}
	if path == "" {
		return errors.New("could not determine motion-signature path")
	}
	if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
		return err
	}
	record := labelRecord{
		Version:    SignatureVersion,
		Label:      label,
		Signature:  signature,
		Parameters: map[string]any{"profile": profile},
	}
	data, err := json.Marshal(record)
	if err != nil {
		return err
	}
	f, err := os.OpenFile(path, os.O_CREATE|os.O_APPEND|os.O_WRONLY, 0o644)
	if err != nil {
		return err
	}
	defer f.Close()
	if _, err := f.Write(append(data, '\n')); err != nil {
		return err
	}
	return f.Sync()
}

func readLabels(path string) (all []labelRecord, usable []labelRecord, err error) {
	f, err := os.Open(path)
	if err != nil {
		if os.IsNotExist(err) {
			return nil, nil, nil
		}
		return nil, nil, err
	}
	defer f.Close()

	scanner := bufio.NewScanner(f)
	// JSONL records are currently tiny, but leave room for future parameters.
	scanner.Buffer(make([]byte, 64*1024), 1024*1024)
	for scanner.Scan() {
		line := strings.TrimSpace(scanner.Text())
		if line == "" {
			continue
		}
		var record labelRecord
		if json.Unmarshal([]byte(line), &record) != nil || record.Version != SignatureVersion {
			continue
		}
		if validateSignature(record.Signature) != nil {
			continue
		}
		all = append(all, record)
		if profileForRecord(record) != "" {
			usable = append(usable, record)
		}
	}
	return all, usable, scanner.Err()
}

func profileForRecord(record labelRecord) string {
	if record.Parameters != nil {
		if value, ok := record.Parameters["profile"].(string); ok {
			if profile := NormalizeProfile(value); profile != "" {
				return profile
			}
		}
	}
	// Backward compatibility: older entries sometimes used the profile itself
	// as the free-form scene label.
	return NormalizeProfile(record.Label)
}

func StatusAt(labelsPath, modelPath string) (Status, error) {
	if labelsPath == "" {
		labelsPath = DefaultLabelsPath()
	}
	if modelPath == "" {
		modelPath = DefaultModelPath()
	}
	status := Status{LabelsPath: labelsPath, ModelPath: modelPath}
	all, usable, err := readLabels(labelsPath)
	if err != nil {
		return status, err
	}
	status.LabelledScenes = len(all)
	status.UsableSamples = len(usable)
	status.ReadyToTrain = len(usable) >= minTrainSamples
	counts := make(map[string]int)
	for _, record := range usable {
		counts[profileForRecord(record)]++
	}
	for profile, count := range counts {
		status.Profiles = append(status.Profiles, ProfileCount{Profile: profile, Count: count})
	}
	sort.Slice(status.Profiles, func(i, j int) bool { return status.Profiles[i].Profile < status.Profiles[j].Profile })
	if model, err := Load(modelPath); err == nil {
		status.ModelAvailable = true
		status.TrainedSamples = model.SampleCount
		status.TrainedAt = model.TrainedAt
	} else if !os.IsNotExist(err) {
		// The JSONL source data is still usable, and retraining is the repair
		// path. Do not disable that button just because the derived artifact is
		// corrupt or from an incompatible future/old version.
		status.ModelWarning = err.Error()
	}
	return status, nil
}

func StatusDefault() (Status, error) {
	return StatusAt("", "")
}

func Train(labelsPath, modelPath string) (Model, error) {
	if labelsPath == "" {
		labelsPath = DefaultLabelsPath()
	}
	if modelPath == "" {
		modelPath = DefaultModelPath()
	}
	_, usable, err := readLabels(labelsPath)
	if err != nil {
		return Model{}, err
	}
	if len(usable) < minTrainSamples {
		return Model{}, fmt.Errorf("need at least %d scenes saved with a generator profile; found %d", minTrainSamples, len(usable))
	}

	grouped := make(map[string][]map[string]float64)
	for _, record := range usable {
		profile := profileForRecord(record)
		grouped[profile] = append(grouped[profile], record.Signature)
	}
	model := Model{
		Version:          ModelVersion,
		SignatureVersion: SignatureVersion,
		Fields:           append([]string(nil), SignatureFields...),
		TrainedAt:        time.Now().UTC().Format(time.RFC3339),
		SampleCount:      len(usable),
	}
	for profile, samples := range grouped {
		centroid := make(map[string]float64, len(SignatureFields))
		for _, field := range SignatureFields {
			for _, sample := range samples {
				centroid[field] += sample[field]
			}
			centroid[field] /= float64(len(samples))
		}
		radius := 0.0
		for _, sample := range samples {
			radius += distance(sample, centroid)
		}
		radius /= float64(len(samples))
		model.Classes = append(model.Classes, ProfileClass{
			Profile: profile, Count: len(samples), Centroid: centroid, Radius: radius,
		})
	}
	sort.Slice(model.Classes, func(i, j int) bool { return model.Classes[i].Profile < model.Classes[j].Profile })
	if err := saveAtomic(modelPath, model); err != nil {
		return Model{}, err
	}
	return model, nil
}

func TrainDefault() (Model, error) {
	return Train("", "")
}

func saveAtomic(path string, model Model) error {
	if path == "" {
		return errors.New("could not determine motion-profile model path")
	}
	if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
		return err
	}
	data, err := json.MarshalIndent(model, "", "  ")
	if err != nil {
		return err
	}
	tmp, err := os.CreateTemp(filepath.Dir(path), ".motion-profile-*.tmp")
	if err != nil {
		return err
	}
	tmpName := tmp.Name()
	defer os.Remove(tmpName)
	if _, err := tmp.Write(append(data, '\n')); err != nil {
		tmp.Close()
		return err
	}
	if err := tmp.Sync(); err != nil {
		tmp.Close()
		return err
	}
	if err := tmp.Close(); err != nil {
		return err
	}
	if err := os.Rename(tmpName, path); err == nil {
		return nil
	}
	// Windows does not reliably replace an existing destination with Rename.
	// Keep the old model as a rollback until the fully written+synced temporary
	// file is in place.
	backup := path + ".previous"
	_ = os.Remove(backup)
	if err := os.Rename(path, backup); err != nil && !os.IsNotExist(err) {
		return err
	}
	if err := os.Rename(tmpName, path); err != nil {
		_ = os.Rename(backup, path)
		return err
	}
	_ = os.Remove(backup)
	return nil
}

func Load(path string) (Model, error) {
	if path == "" {
		path = DefaultModelPath()
	}
	data, err := os.ReadFile(path)
	if err != nil {
		return Model{}, err
	}
	var model Model
	if err := json.Unmarshal(data, &model); err != nil {
		return Model{}, fmt.Errorf("invalid motion-profile model: %w", err)
	}
	if model.Version != ModelVersion || model.SignatureVersion != SignatureVersion {
		return Model{}, fmt.Errorf("unsupported motion-profile model version %d/signature %d", model.Version, model.SignatureVersion)
	}
	if len(model.Classes) == 0 {
		return Model{}, errors.New("motion-profile model has no classes")
	}
	return model, nil
}

func Suggest(signature map[string]float64, model Model) Suggestion {
	result := Suggestion{ModelVersion: model.Version, SampleCount: model.SampleCount}
	if validateSignature(signature) != nil || len(model.Classes) == 0 {
		return result
	}
	type candidate struct {
		class ProfileClass
		dist  float64
	}
	candidates := make([]candidate, 0, len(model.Classes))
	for _, class := range model.Classes {
		candidates = append(candidates, candidate{class: class, dist: distance(signature, class.Centroid)})
	}
	sort.Slice(candidates, func(i, j int) bool { return candidates[i].dist < candidates[j].dist })
	best := candidates[0]
	maxDist := defaultMaxDist
	// Multi-sample classes may legitimately be broader than a single example,
	// but keep a hard ceiling so an unfamiliar scene is never force-classified.
	if best.class.Count > 1 {
		maxDist = math.Min(0.30, math.Max(defaultMaxDist, best.class.Radius*2+0.05))
	}
	result.Profile = best.class.Profile
	result.Distance = best.dist
	if len(candidates) > 1 {
		result.Margin = candidates[1].dist - best.dist
	}
	if best.dist > maxDist {
		return result
	}
	// Require separation when another learned profile is almost equally close.
	if len(candidates) > 1 && result.Margin < 0.02 {
		return result
	}
	result.Found = true
	result.Confidence = clamp01(1 - best.dist/maxDist)
	if len(candidates) > 1 {
		result.Confidence *= 0.5 + 0.5*clamp01(result.Margin/0.15)
	}
	return result
}

func SuggestFromPath(signature map[string]float64, modelPath string) (Suggestion, error) {
	model, err := Load(modelPath)
	if err != nil {
		return Suggestion{}, err
	}
	return Suggest(signature, model), nil
}

func distance(a, b map[string]float64) float64 {
	var sum float64
	for _, field := range SignatureFields {
		d := a[field] - b[field]
		sum += d * d
	}
	return math.Sqrt(sum / float64(len(SignatureFields)))
}

func clamp01(value float64) float64 {
	if value < 0 {
		return 0
	}
	if value > 1 {
		return 1
	}
	return value
}
