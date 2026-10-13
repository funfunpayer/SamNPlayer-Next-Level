// Package videox contains ffmpeg/ffprobe helpers plus lean self-built
// fallbacks (ISO-BMFF probe) so common MP4 metadata works without ffprobe.
//
// See docs/SELF_BUILD.md and docs/FFMPEG_TOOLS.md.
package videox

import (
	"context"
	"encoding/json"
	"fmt"
	"strconv"
	"strings"
	"time"
)

// Info describes a video stream as reported by ffprobe.
type Info struct {
	Width    int
	Height   int
	FPS      float64 // average frame rate
	RFPS     float64 // nominal/real frame rate
	Duration time.Duration
	Codec    string
	PixFmt   string
	Rotation int  // 0, 90, 180 or 270
	VFR      bool // true only when avg and nominal rate differ meaningfully
}

// Rotated reports the display dimensions after applying container rotation.
func (i Info) Rotated() (w, h int) {
	if i.Rotation == 90 || i.Rotation == 270 {
		return i.Height, i.Width
	}
	return i.Width, i.Height
}

type ffprobeJSON struct {
	Streams []struct {
		CodecName    string `json:"codec_name"`
		Width        int    `json:"width"`
		Height       int    `json:"height"`
		PixFmt       string `json:"pix_fmt"`
		RFrameRate   string `json:"r_frame_rate"`
		AvgFrameRate string `json:"avg_frame_rate"`
		Duration     string `json:"duration"`
		SideData     []struct {
			Rotation int `json:"rotation"`
		} `json:"side_data_list"`
		Tags struct {
			Rotate string `json:"rotate"`
		} `json:"tags"`
	} `json:"streams"`
	Format struct {
		Duration string `json:"duration"`
	} `json:"format"`
}

// Probe reads metadata for the first video stream.
//
// Prefers ffprobe when available (never silently downgrade). Falls back to
// a lean ISO-BMFF reader for common .mp4/.m4v/.mov only when ffprobe fails
// (docs/SELF_BUILD.md — equal geometry gate vs ffprobe on clips).
//
// Difference to the original: -select_streams v:0 is mandatory. Without it the
// first stream carrying width/height may be attached cover art (mjpeg), which
// yields a bogus resolution and a frame rate of 1/1.
func Probe(ctx context.Context, path string) (Info, error) {
	info, err := probeWithFF(ctx, path)
	if err == nil {
		return info, nil
	}
	if lean, leanErr := probeISOBMFF(path); leanErr == nil {
		return lean, nil
	}
	return Info{}, err
}

func probeWithFF(ctx context.Context, path string) (Info, error) {
	cmd, err := ProbeCommandContext(ctx,
		"-v", "error",
		"-select_streams", "v:0",
		"-print_format", "json",
		"-show_streams", "-show_format",
		path,
	)
	if err != nil {
		return Info{}, err
	}
	var stderr strings.Builder
	cmd.Stderr = &stderr

	out, err := cmd.Output()
	if err != nil {
		if msg := strings.TrimSpace(stderr.String()); msg != "" {
			return Info{}, fmt.Errorf("ffprobe: %s", msg)
		}
		return Info{}, fmt.Errorf("ffprobe: %w", err)
	}

	var raw ffprobeJSON
	if err := json.Unmarshal(out, &raw); err != nil {
		return Info{}, fmt.Errorf("ffprobe output: %w", err)
	}
	if len(raw.Streams) == 0 {
		return Info{}, fmt.Errorf("no video stream in %s", path)
	}
	s := raw.Streams[0]
	if s.Width <= 0 || s.Height <= 0 {
		return Info{}, fmt.Errorf("video stream has no usable dimensions")
	}

	avg := parseRate(s.AvgFrameRate)
	nominal := parseRate(s.RFrameRate)
	if avg <= 0 {
		avg = nominal
	}

	d, _ := strconv.ParseFloat(raw.Format.Duration, 64)
	if d <= 0 {
		d, _ = strconv.ParseFloat(s.Duration, 64)
	}

	rot := 0
	for _, sd := range s.SideData {
		if sd.Rotation != 0 {
			rot = sd.Rotation
		}
	}
	if rot == 0 && s.Tags.Rotate != "" {
		if v, err := strconv.Atoi(s.Tags.Rotate); err == nil {
			rot = v
		}
	}
	rot = ((rot % 360) + 360) % 360

	return Info{
		Width:    s.Width,
		Height:   s.Height,
		FPS:      avg,
		RFPS:     nominal,
		Duration: time.Duration(d * float64(time.Second)),
		Codec:    s.CodecName,
		PixFmt:   s.PixFmt,
		Rotation: rot,
		VFR:      isVFR(avg, nominal),
	}, nil
}

// isVFR compares the two reported rates with a tolerance.
//
// Difference to the original: comparing the rate *strings* flags almost every
// file as VFR, because containers routinely report 30000/1001 as avg and 30/1
// as nominal. Only a real relative deviation counts.
func isVFR(avg, nominal float64) bool {
	if avg <= 0 || nominal <= 0 {
		return false
	}
	diff := avg - nominal
	if diff < 0 {
		diff = -diff
	}
	return diff/nominal > 0.02
}

func parseRate(s string) float64 {
	parts := strings.Split(s, "/")
	switch len(parts) {
	case 1:
		v, _ := strconv.ParseFloat(parts[0], 64)
		return v
	case 2:
		a, _ := strconv.ParseFloat(parts[0], 64)
		b, _ := strconv.ParseFloat(parts[1], 64)
		if b == 0 {
			return 0
		}
		return a / b
	}
	return 0
}
