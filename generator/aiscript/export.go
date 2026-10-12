package aiscript

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"time"
)

// ImitationSample is one classical good-run pair for S1 training export.
// Features stay deliberately small and classical — later models may grow them.
type ImitationSample struct {
	Version    int       `json:"version"`
	Exported   time.Time `json:"exportedAt"`
	VideoPath  string    `json:"videoPath,omitempty"`
	ScriptPath string    `json:"scriptPath,omitempty"`
	Engine     string    `json:"engine"` // e.g. "csrt"
	QDPassed   *bool     `json:"qdPassed,omitempty"`
	QDScore    *float64  `json:"qdScore,omitempty"`
	// TipROI optional seed used at generate time (pixel box).
	TipX       float64  `json:"tipX,omitempty"`
	TipY       float64  `json:"tipY,omitempty"`
	TipW       float64  `json:"tipW,omitempty"`
	TipH       float64  `json:"tipH,omitempty"`
	DurationMs int64    `json:"durationMs,omitempty"`
	Actions    []Action `json:"actions"`
	Notes      string   `json:"notes,omitempty"`
}

// ExportImitationSample writes one JSON sample under dir (created if needed).
// Opt-in only — callers must not invoke from Everyday Generate silently.
// Does not train a model; fuel for offline S1→S2 work.
func ExportImitationSample(dir string, sample ImitationSample) (string, error) {
	if dir == "" {
		return "", fmt.Errorf("aiscript: export dir required")
	}
	if len(sample.Actions) < 2 {
		return "", fmt.Errorf("aiscript: need at least 2 actions to export")
	}
	if err := os.MkdirAll(dir, 0o755); err != nil {
		return "", fmt.Errorf("aiscript: mkdir: %w", err)
	}
	if sample.Version == 0 {
		sample.Version = 1
	}
	if sample.Exported.IsZero() {
		sample.Exported = time.Now().UTC()
	}
	if sample.Engine == "" {
		sample.Engine = "csrt"
	}
	name := fmt.Sprintf("imitation_%d.json", sample.Exported.UnixNano())
	path := filepath.Join(dir, name)
	raw, err := json.MarshalIndent(sample, "", "  ")
	if err != nil {
		return "", err
	}
	if err := os.WriteFile(path, raw, 0o644); err != nil {
		return "", fmt.Errorf("aiscript: write: %w", err)
	}
	return path, nil
}

// DefaultImitationDir is …/roi_training_dataset/ai_script_imitation (local only).
func DefaultImitationDir() string {
	// Avoid importing generator here (cycle). Callers pass the ROI dataset root
	// or leave empty for a relative folder.
	return "ai_script_imitation"
}

// ImitationDirUnder returns <roiRoot>/ai_script_imitation.
func ImitationDirUnder(roiRoot string) string {
	if roiRoot == "" {
		return DefaultImitationDir()
	}
	return filepath.Join(roiRoot, "ai_script_imitation")
}
