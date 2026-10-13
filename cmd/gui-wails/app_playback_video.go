package main

import (
	"context"
	"fmt"
	"net/http"
	"os"
	"path/filepath"
	"strings"
	"time"

	"github.com/funfunpayer/SamNPlayer/logging"
	"github.com/funfunpayer/SamNPlayer/videox"
)

// PlaybackVideoInfo is metadata for the Wiedergabe tab (probe + playability).
type PlaybackVideoInfo struct {
	Path           string  `json:"path"`
	Codec          string  `json:"codec"`
	Width          int     `json:"width"`
	Height         int     `json:"height"`
	DurationSec    float64 `json:"durationSec"`
	LikelyPlayable bool    `json:"likelyPlayable"`
	UsingProxy     bool    `json:"usingProxy"`
	ProxyPath      string  `json:"proxyPath,omitempty"`
	Warning        string  `json:"warning,omitempty"`
}

// ProbePlaybackVideo runs ffprobe on the current (or given) playback path.
func (a *App) ProbePlaybackVideo(path string) (PlaybackVideoInfo, error) {
	path = strings.TrimSpace(path)
	if path == "" {
		a.stateMu.RLock()
		path = a.videoPath
		a.stateMu.RUnlock()
	}
	if path == "" {
		return PlaybackVideoInfo{}, fmt.Errorf("no video loaded")
	}
	ctx, cancel := context.WithTimeout(context.Background(), videox.ProbeTimeout)
	defer cancel()
	info, err := videox.Probe(ctx, path)
	if err != nil {
		return PlaybackVideoInfo{Path: path, Warning: err.Error()}, err
	}
	w, h := info.Rotated()
	out := PlaybackVideoInfo{
		Path:           path,
		Codec:          info.Codec,
		Width:          w,
		Height:         h,
		DurationSec:    info.Duration.Seconds(),
		LikelyPlayable: info.LikelyPlayable(),
	}
	ext := strings.ToLower(filepath.Ext(path))
	if out.LikelyPlayable && (ext == ".mkv" || ext == ".avi") {
		out.LikelyPlayable = false
		out.Warning = "Container often problematic in the player — H.264 MP4 copy recommended"
	} else if !out.LikelyPlayable {
		out.Warning = fmt.Sprintf("Codec %q is often not playable in the embedded player", info.Codec)
	}
	return out, nil
}

// EnsurePlayablePlaybackVideo converts to an H.264/AAC MP4 proxy when needed
// and switches the active playback path. Returns updated VideoFileURL info.
func (a *App) EnsurePlayablePlaybackVideo() (PlaybackVideoInfo, error) {
	a.stateMu.RLock()
	src := a.videoPath
	a.stateMu.RUnlock()
	if src == "" {
		return PlaybackVideoInfo{}, fmt.Errorf("no video loaded")
	}
	ctx, cancel := context.WithTimeout(context.Background(), 30*time.Minute)
	defer cancel()
	out, converted, err := videox.EnsurePlayableProxy(ctx, src, func(line string) {
		logging.Info("playback: proxy", "line", line)
	})
	if err != nil {
		return PlaybackVideoInfo{Path: src, Warning: err.Error()}, err
	}
	a.stateMu.Lock()
	a.setVideoPathLocked(out)
	a.stateMu.Unlock()
	info, _ := a.ProbePlaybackVideo(out)
	info.UsingProxy = converted || out != src
	if converted {
		info.ProxyPath = out
	}
	info.LikelyPlayable = true
	info.Warning = ""
	return info, nil
}

func videoContentType(path string) string {
	switch strings.ToLower(filepath.Ext(path)) {
	case ".mp4", ".m4v":
		return "video/mp4"
	case ".webm":
		return "video/webm"
	case ".mov":
		return "video/quicktime"
	case ".mkv":
		return "video/x-matroska"
	case ".avi":
		return "video/x-msvideo"
	case ".ts", ".m2ts":
		return "video/mp2t"
	case ".flv":
		return "video/x-flv"
	case ".mpg", ".mpeg":
		return "video/mpeg"
	case ".3gp":
		return "video/3gpp"
	case ".ogv":
		return "video/ogg"
	default:
		return "application/octet-stream"
	}
}

// servePlaybackVideo writes the current video with a sensible Content-Type.
func (a *App) servePlaybackVideo(w http.ResponseWriter, r *http.Request) {
	a.stateMu.RLock()
	path := a.videoPath
	a.stateMu.RUnlock()
	if path == "" {
		http.NotFound(w, r)
		return
	}
	if _, err := os.Stat(path); err != nil {
		http.NotFound(w, r)
		return
	}
	w.Header().Set("Content-Type", videoContentType(path))
	http.ServeFile(w, r, path)
}
