package main

import (
	"context"
	"encoding/base64"
	"fmt"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"time"

	"github.com/funfunpayer/SamNPlayer/videox"
)

// CreateVideoInfo is the Create project header (V1 target create.png):
// real values from ffprobe plus one poster frame. Nothing is invented — a
// value the probe cannot read stays zero and the GUI leaves it out.
type CreateVideoInfo struct {
	Path        string  `json:"path"`
	Name        string  `json:"name"`
	Width       int     `json:"width"`
	Height      int     `json:"height"`
	FPS         float64 `json:"fps"`
	VFR         bool    `json:"vfr"`
	DurationSec float64 `json:"durationSec"`
	Codec       string  `json:"codec"`
	PosterURL   string  `json:"posterUrl"` // data:image/jpeg;base64,… or ""
}

const createPosterHeight = 144

// createProbeFn and createPosterFn are swapped in tests.
var createProbeFn = videox.Probe

var createPosterFn = func(ctx context.Context, video string, atMs int64, out string) error {
	cmd, err := videox.CommandContext(ctx,
		"-y", "-hide_banner", "-loglevel", "error",
		"-ss", strconv.FormatFloat(float64(atMs)/1000, 'f', 3, 64),
		"-i", video, "-frames:v", "1",
		"-vf", "scale=-2:"+strconv.Itoa(createPosterHeight), "-q:v", "5", out)
	if err != nil {
		return err
	}
	if b, err := cmd.CombinedOutput(); err != nil {
		return fmt.Errorf("ffmpeg: %v %s", err, strings.TrimSpace(string(b)))
	}
	return nil
}

// ProbeCreateVideo reads the header data for the Create view. The poster is
// taken at 10 % of the clip (at most 30 s in) so it is rarely a black intro
// frame. A failed poster is not an error: the header shows the facts only.
func (a *App) ProbeCreateVideo(path string) (CreateVideoInfo, error) {
	path = strings.TrimSpace(path)
	if path == "" {
		return CreateVideoInfo{}, fmt.Errorf("no video path")
	}
	if st, err := os.Stat(path); err != nil || st.IsDir() {
		return CreateVideoInfo{}, fmt.Errorf("video file not found: %s", path)
	}
	ctx, cancel := context.WithTimeout(context.Background(), 30*time.Second)
	defer cancel()
	info, err := createProbeFn(ctx, path)
	if err != nil {
		return CreateVideoInfo{Path: path, Name: filepath.Base(path)}, err
	}
	w, h := info.Rotated()
	out := CreateVideoInfo{
		Path: path, Name: filepath.Base(path), Width: w, Height: h,
		FPS: info.FPS, VFR: info.VFR, DurationSec: info.Duration.Seconds(), Codec: info.Codec,
	}
	at := info.Duration.Milliseconds() / 10
	if at > 30000 {
		at = 30000
	}
	dir, err := os.MkdirTemp("", "samn-poster-")
	if err != nil {
		return out, nil
	}
	defer os.RemoveAll(dir)
	jpg := filepath.Join(dir, "poster.jpg")
	if createPosterFn(ctx, path, at, jpg) == nil {
		if b, err := os.ReadFile(jpg); err == nil && len(b) > 0 {
			out.PosterURL = "data:image/jpeg;base64," + base64.StdEncoding.EncodeToString(b)
		}
	}
	return out, nil
}
