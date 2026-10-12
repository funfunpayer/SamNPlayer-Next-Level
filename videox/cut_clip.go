package videox

import (
	"context"
	"fmt"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"time"
)

// Benchmark clip-prep defaults (scripts/benchmark-prep/cut_clip.py).
// Soft proxy caps at SoftProxyMaxWidth (1920); training/bench clips stay smaller.
const (
	ClipPrepDefaultMaxWidth    = 1280
	ClipPrepCRF                = 20
	ClipPrepPreset             = "veryfast"
	ClipPrepAudioBitrate       = "128k"
	ClipPrepProxyStyleMaxWidth = SoftProxyMaxWidth
)

// ClipPrepOptions configures a short downscaled cut for Benchmark / KI prep.
// Not part of Everyday Create — owner prep only.
type ClipPrepOptions struct {
	Input    string
	Output   string
	StartSec float64
	EndSec   float64 // exclusive window end; duration = EndSec - StartSec
	MaxWidth int     // 0 → ClipPrepDefaultMaxWidth; never upscales
	CRF      int     // 0 → ClipPrepCRF
	Preset   string  // empty → ClipPrepPreset
	NoAudio  bool
}

// ResolveClipPrepMaxWidth maps preset labels and optional overrides.
// preset: "720p"→1280, "960w"→960, "1080p"→1920. Empty + maxWidth 0 → 1280.
func ResolveClipPrepMaxWidth(preset string, maxWidth int) int {
	switch strings.ToLower(strings.TrimSpace(preset)) {
	case "720p":
		return 1280
	case "960w":
		return 960
	case "1080p":
		return ClipPrepProxyStyleMaxWidth
	}
	if maxWidth > 0 {
		return maxWidth
	}
	return ClipPrepDefaultMaxWidth
}

// ParseClipPrepTime accepts seconds (number/string) or HH:MM:SS[.ms] / MM:SS[.ms].
func ParseClipPrepTime(value string) (float64, error) {
	s := strings.TrimSpace(value)
	if s == "" {
		return 0, fmt.Errorf("empty time")
	}
	if !strings.Contains(s, ":") {
		sec, err := strconv.ParseFloat(s, 64)
		if err != nil {
			return 0, fmt.Errorf("bad time %q: %w", value, err)
		}
		return sec, nil
	}
	parts := strings.Split(s, ":")
	switch len(parts) {
	case 2:
		m, err := strconv.Atoi(parts[0])
		if err != nil {
			return 0, fmt.Errorf("bad time %q: %w", value, err)
		}
		sec, err := strconv.ParseFloat(parts[1], 64)
		if err != nil {
			return 0, fmt.Errorf("bad time %q: %w", value, err)
		}
		return float64(m)*60 + sec, nil
	case 3:
		h, err := strconv.Atoi(parts[0])
		if err != nil {
			return 0, fmt.Errorf("bad time %q: %w", value, err)
		}
		m, err := strconv.Atoi(parts[1])
		if err != nil {
			return 0, fmt.Errorf("bad time %q: %w", value, err)
		}
		sec, err := strconv.ParseFloat(parts[2], 64)
		if err != nil {
			return 0, fmt.Errorf("bad time %q: %w", value, err)
		}
		return float64(h)*3600 + float64(m)*60 + sec, nil
	default:
		return 0, fmt.Errorf("bad time %q", value)
	}
}

// FormatClipPrepSeconds formats a duration for ffmpeg -ss / -t.
func FormatClipPrepSeconds(sec float64) string {
	if sec < 0 {
		sec = 0
	}
	rounded := float64(int64(sec + 0.5))
	if absFloat(sec-rounded) < 1e-6 {
		return strconv.FormatInt(int64(rounded), 10)
	}
	s := strconv.FormatFloat(sec, 'f', 3, 64)
	s = strings.TrimRight(s, "0")
	return strings.TrimRight(s, ".")
}

func absFloat(v float64) float64 {
	if v < 0 {
		return -v
	}
	return v
}

// BuildClipPrepScaleFilter never upscales; keeps aspect; even height via -2.
func BuildClipPrepScaleFilter(maxWidth int) (string, error) {
	if maxWidth <= 0 {
		return "", fmt.Errorf("max_width must be > 0")
	}
	return fmt.Sprintf("scale='min(%d,iw)':-2:flags=lanczos", maxWidth), nil
}

// BuildClipPrepArgs builds ffmpeg argv after the binary path (no binary).
// Matches scripts/benchmark-prep/cut_clip.py encode defaults.
func BuildClipPrepArgs(opt ClipPrepOptions) ([]string, error) {
	if strings.TrimSpace(opt.Input) == "" {
		return nil, fmt.Errorf("input required")
	}
	if strings.TrimSpace(opt.Output) == "" {
		return nil, fmt.Errorf("output required")
	}
	if opt.StartSec < 0 {
		return nil, fmt.Errorf("start_sec must be >= 0")
	}
	if opt.EndSec <= opt.StartSec {
		return nil, fmt.Errorf("end must be after start")
	}
	duration := opt.EndSec - opt.StartSec
	if duration <= 0 {
		return nil, fmt.Errorf("duration must be > 0")
	}
	maxW := opt.MaxWidth
	if maxW <= 0 {
		maxW = ClipPrepDefaultMaxWidth
	}
	scale, err := BuildClipPrepScaleFilter(maxW)
	if err != nil {
		return nil, err
	}
	crf := opt.CRF
	if crf <= 0 {
		crf = ClipPrepCRF
	}
	preset := opt.Preset
	if preset == "" {
		preset = ClipPrepPreset
	}

	args := []string{
		"-y",
		"-hide_banner", "-loglevel", "error",
		"-ss", FormatClipPrepSeconds(opt.StartSec),
		"-i", opt.Input,
		"-t", FormatClipPrepSeconds(duration),
		"-vf", scale,
		"-c:v", "libx264",
		"-preset", preset,
		"-crf", strconv.Itoa(crf),
		"-pix_fmt", "yuv420p",
	}
	if opt.NoAudio {
		args = append(args, "-an")
	} else {
		args = append(args, "-c:a", "aac", "-b:a", ClipPrepAudioBitrate)
	}
	args = append(args, "-movflags", "+faststart", opt.Output)
	return args, nil
}

// ExportClipPrepCut runs ffmpeg to cut + downscale a short clip.
// Uses the resolved videox ffmpeg binary (bundled / tools / PATH).
func ExportClipPrepCut(ctx context.Context, opt ClipPrepOptions) error {
	if ctx == nil {
		ctx = context.Background()
	}
	args, err := BuildClipPrepArgs(opt)
	if err != nil {
		return err
	}
	if err := os.MkdirAll(filepath.Dir(opt.Output), 0o755); err != nil {
		return err
	}
	cmd, err := CommandContext(ctx, args...)
	if err != nil {
		return fmt.Errorf("ffmpeg missing — use portable zip or Settings → Install video tools (%w)", err)
	}
	if b, err := cmd.CombinedOutput(); err != nil {
		_ = os.Remove(opt.Output)
		msg := strings.TrimSpace(string(b))
		if msg == "" {
			msg = err.Error()
		}
		return fmt.Errorf("ffmpeg: %s", msg)
	}
	st, err := os.Stat(opt.Output)
	if err != nil || st.Size() < 64 {
		_ = os.Remove(opt.Output)
		return fmt.Errorf("ffmpeg produced empty or missing output")
	}
	return nil
}

// DefaultClipPrepTimeout bounds a GUI export (long sources + encode).
func DefaultClipPrepTimeout() time.Duration {
	return 10 * time.Minute
}
