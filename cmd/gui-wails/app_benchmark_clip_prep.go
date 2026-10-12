package main

import (
	"context"
	"fmt"
	"os"
	"path/filepath"
	"strings"

	"github.com/wailsapp/wails/v2/pkg/runtime"

	"github.com/funfunpayer/SamNPlayer/logging"
	"github.com/funfunpayer/SamNPlayer/videox"
)

// BenchmarkClipExportRequest is the Bench tab Clip-Prep In/Out export form.
// Owner prep only — does not touch Everyday Create / CSRT.
type BenchmarkClipExportRequest struct {
	Source    string  `json:"source"`
	Output    string  `json:"output"`
	Start     string  `json:"start"`              // sec or MM:SS / HH:MM:SS
	End       string  `json:"end"`                // sec or MM:SS / HH:MM:SS
	StartSec  float64 `json:"startSec,omitempty"` // optional numeric override
	EndSec    float64 `json:"endSec,omitempty"`
	MaxWidth  int     `json:"maxWidth,omitempty"`  // 0 + empty preset → 1280
	PresetRes string  `json:"presetRes,omitempty"` // 720p | 960w | 1080p
	NoAudio   bool    `json:"noAudio,omitempty"`
}

// BenchmarkClipExportResult reports the written short clip path + window.
type BenchmarkClipExportResult struct {
	Output      string  `json:"output"`
	StartSec    float64 `json:"startSec"`
	EndSec      float64 `json:"endSec"`
	DurationSec float64 `json:"durationSec"`
	MaxWidth    int     `json:"maxWidth"`
}

// ParseBenchmarkClipTime exposes HH:MM:SS / MM:SS / seconds parsing to the GUI.
func (a *App) ParseBenchmarkClipTime(value string) (float64, error) {
	if err := requireSuite(); err != nil {
		return 0, err
	}
	return videox.ParseClipPrepTime(value)
}

// PickBenchmarkClipOutput opens a save dialog for the exported short clip.
func (a *App) PickBenchmarkClipOutput(suggestedName string) (string, error) {
	if err := requireSuite(); err != nil {
		return "", err
	}
	name := strings.TrimSpace(suggestedName)
	if name == "" {
		name = "bench_clip.mp4"
	}
	if !strings.HasSuffix(strings.ToLower(name), ".mp4") {
		name += ".mp4"
	}
	return runtime.SaveFileDialog(a.ctx, runtime.SaveDialogOptions{
		Title:                "Save benchmark clip",
		DefaultFilename:      filepath.Base(name),
		CanCreateDirectories: true,
		Filters: []runtime.FileFilter{
			{DisplayName: "Video (*.mp4)", Pattern: "*.mp4"},
		},
	})
}

// SuggestBenchmarkClipOutput proposes a sibling path next to the source video.
func (a *App) SuggestBenchmarkClipOutput(source, start, end string) (string, error) {
	if err := requireSuite(); err != nil {
		return "", err
	}
	source = strings.TrimSpace(source)
	if source == "" {
		return "", fmt.Errorf("source required")
	}
	stem := strings.TrimSuffix(filepath.Base(source), filepath.Ext(source))
	if stem == "" {
		stem = "bench_clip"
	}
	startTag := sanitizeClipTimeTag(start)
	endTag := sanitizeClipTimeTag(end)
	name := fmt.Sprintf("%s_%s-%s.mp4", stem, startTag, endTag)
	return filepath.Join(filepath.Dir(source), name), nil
}

func sanitizeClipTimeTag(raw string) string {
	s := strings.TrimSpace(raw)
	if s == "" {
		return "t"
	}
	var b strings.Builder
	for _, r := range s {
		switch {
		case r >= '0' && r <= '9', r == '.':
			b.WriteRune(r)
		case r == ':':
			b.WriteByte('-')
		default:
			b.WriteByte('_')
		}
	}
	out := b.String()
	if out == "" {
		return "t"
	}
	return out
}

// ExportBenchmarkClip cuts + Lanczos-downscales a short clip via ffmpeg
// (same defaults as scripts/benchmark-prep/cut_clip.py: ~1280-wide / CRF 20).
// Synchronous with a bounded timeout. Everyday Create / CSRT untouched.
func (a *App) ExportBenchmarkClip(req BenchmarkClipExportRequest) (BenchmarkClipExportResult, error) {
	if err := requireSuite(); err != nil {
		return BenchmarkClipExportResult{}, err
	}
	var empty BenchmarkClipExportResult
	source := strings.TrimSpace(req.Source)
	output := strings.TrimSpace(req.Output)
	if source == "" {
		return empty, fmt.Errorf("source video required")
	}
	if output == "" {
		return empty, fmt.Errorf("output path required")
	}
	st, err := os.Stat(source)
	if err != nil {
		return empty, err
	}
	if st.IsDir() {
		return empty, fmt.Errorf("source must be a video file")
	}

	startSec, endSec, err := resolveClipWindow(req)
	if err != nil {
		return empty, err
	}
	maxW := videox.ResolveClipPrepMaxWidth(req.PresetRes, req.MaxWidth)

	parent := context.Background()
	if a.ctx != nil {
		parent = a.ctx
	}
	ctx, cancel := context.WithTimeout(parent, videox.DefaultClipPrepTimeout())
	defer cancel()

	err = videox.ExportClipPrepCut(ctx, videox.ClipPrepOptions{
		Input:    source,
		Output:   output,
		StartSec: startSec,
		EndSec:   endSec,
		MaxWidth: maxW,
		NoAudio:  req.NoAudio,
	})
	if err != nil {
		logging.Error("benchmark clip-prep: export failed",
			"source", source, "output", output, "error", err)
		return empty, err
	}
	logging.Info("benchmark clip-prep: exported",
		"output", output, "start", startSec, "end", endSec, "maxWidth", maxW)
	return BenchmarkClipExportResult{
		Output:      filepath.Clean(output),
		StartSec:    startSec,
		EndSec:      endSec,
		DurationSec: endSec - startSec,
		MaxWidth:    maxW,
	}, nil
}

func resolveClipWindow(req BenchmarkClipExportRequest) (startSec, endSec float64, err error) {
	if strings.TrimSpace(req.Start) != "" {
		startSec, err = videox.ParseClipPrepTime(req.Start)
		if err != nil {
			return 0, 0, fmt.Errorf("start: %w", err)
		}
	} else {
		startSec = req.StartSec
	}
	if strings.TrimSpace(req.End) != "" {
		endSec, err = videox.ParseClipPrepTime(req.End)
		if err != nil {
			return 0, 0, fmt.Errorf("end: %w", err)
		}
	} else {
		endSec = req.EndSec
	}
	if endSec <= startSec {
		return 0, 0, fmt.Errorf("end must be after start")
	}
	return startSec, endSec, nil
}
