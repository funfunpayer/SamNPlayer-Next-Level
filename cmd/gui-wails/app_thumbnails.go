package main

import (
	"context"
	"encoding/base64"
	"fmt"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"sync"
	"time"

	"github.com/funfunpayer/SamNPlayer/videox"
)

// VideoThumb is one frame of the timeline thumbnail strip (V1 S4b).
type VideoThumb struct {
	AtMs    int64  `json:"atMs"`
	DataURL string `json:"dataUrl"` // data:image/jpeg;base64,… ("" if the frame failed)
}

const (
	thumbMaxCount = 48
	thumbHeight   = 72
	thumbWorkers  = 4
)

// thumbDurationFn and thumbFrameFn are swapped in tests.
var thumbDurationFn = func(ctx context.Context, video string) (time.Duration, error) {
	info, err := videox.Probe(ctx, video)
	if err != nil {
		return 0, err
	}
	return info.Duration, nil
}

var thumbFrameFn = func(ctx context.Context, video string, atMs int64, out string) error {
	cmd, err := videox.CommandContext(ctx,
		"-y", "-hide_banner", "-loglevel", "error",
		"-ss", strconv.FormatFloat(float64(atMs)/1000, 'f', 3, 64),
		"-i", video, "-frames:v", "1",
		"-vf", "scale=-2:"+strconv.Itoa(thumbHeight), "-q:v", "6", out)
	if err != nil {
		return err
	}
	if b, err := cmd.CombinedOutput(); err != nil {
		return fmt.Errorf("ffmpeg: %v %s", err, strings.TrimSpace(string(b)))
	}
	return nil
}

// Memory cache only: a strip is a few hundred KB and must not grow the
// tracking cache directory (Settings → Clear cache only knows its files).
var thumbCache struct {
	sync.Mutex
	key    string
	thumbs []VideoThumb
}

// thumbTimes spreads count frames over the video, each in the middle of its
// slot so the first frame is not the (often black) frame 0.
func thumbTimes(durMs int64, count int) []int64 {
	if durMs <= 0 || count <= 0 {
		return nil
	}
	out := make([]int64, count)
	slot := float64(durMs) / float64(count)
	for i := range out {
		out[i] = int64(slot*float64(i) + slot/2)
	}
	return out
}

// GetVideoThumbnails returns count frames spread over the linked Player
// video for the timeline thumbnail strip. Frames that fail stay empty, so
// the strip keeps its slots. Cached per file, size and mtime.
func (a *App) GetVideoThumbnails(count int) ([]VideoThumb, error) {
	video := a.currentVideoPath()
	if video == "" {
		return nil, fmt.Errorf("no video linked")
	}
	if count < 1 {
		count = 1
	}
	if count > thumbMaxCount {
		count = thumbMaxCount
	}
	st, err := os.Stat(video)
	if err != nil {
		return nil, err
	}
	key := fmt.Sprintf("%s|%d|%d|%d", video, st.Size(), st.ModTime().UnixNano(), count)
	thumbCache.Lock()
	defer thumbCache.Unlock()
	if thumbCache.key == key {
		return thumbCache.thumbs, nil
	}

	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Minute)
	defer cancel()
	dur, err := thumbDurationFn(ctx, video)
	if err != nil {
		return nil, err
	}
	times := thumbTimes(dur.Milliseconds(), count)
	if len(times) == 0 {
		return nil, fmt.Errorf("video has no duration")
	}
	dir, err := os.MkdirTemp("", "samn-thumbs-")
	if err != nil {
		return nil, err
	}
	defer os.RemoveAll(dir)

	thumbs := make([]VideoThumb, len(times))
	sem := make(chan struct{}, thumbWorkers)
	var wg sync.WaitGroup
	for i, t := range times {
		thumbs[i].AtMs = t
		wg.Add(1)
		go func(i int, t int64) {
			defer wg.Done()
			sem <- struct{}{}
			defer func() { <-sem }()
			out := filepath.Join(dir, fmt.Sprintf("t%03d.jpg", i))
			if thumbFrameFn(ctx, video, t, out) != nil {
				return
			}
			b, err := os.ReadFile(out)
			if err != nil || len(b) == 0 {
				return
			}
			thumbs[i].DataURL = "data:image/jpeg;base64," + base64.StdEncoding.EncodeToString(b)
		}(i, t)
	}
	wg.Wait()
	thumbCache.key, thumbCache.thumbs = key, thumbs
	return thumbs, nil
}
