// Package videox contains dependency-free ffmpeg/ffprobe helpers.
//
// Tool discovery (FFmpeg / FFprobe) prefers a copy next to the SamNPlayer
// binary or under the user tools dir — so portable releases can ship
// ffmpeg without asking the user to install it system-wide. PATH is last.
package videox

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"sync"
)

const (
	envFFmpeg  = "SAMNPLAYER_FFMPEG"
	envFFprobe = "SAMNPLAYER_FFPROBE"
)

var (
	ffmpegMu   sync.Mutex
	ffmpegPath string
	ffmpegErr  error
	ffmpegDone bool

	ffprobeMu   sync.Mutex
	ffprobePath string
	ffprobeErr  error
	ffprobeDone bool
)

// ResetToolCache clears cached ffmpeg/ffprobe paths (tests / after install).
func ResetToolCache() {
	ffmpegMu.Lock()
	ffmpegPath, ffmpegErr, ffmpegDone = "", nil, false
	ffmpegMu.Unlock()
	ffprobeMu.Lock()
	ffprobePath, ffprobeErr, ffprobeDone = "", nil, false
	ffprobeMu.Unlock()
}

// FFmpeg returns the path to an ffmpeg binary.
func FFmpeg() (string, error) {
	ffmpegMu.Lock()
	defer ffmpegMu.Unlock()
	if !ffmpegDone {
		ffmpegPath, ffmpegErr = resolveTool("ffmpeg", envFFmpeg)
		ffmpegDone = true
	}
	return ffmpegPath, ffmpegErr
}

// FFprobe returns the path to an ffprobe binary.
func FFprobe() (string, error) {
	ffprobeMu.Lock()
	defer ffprobeMu.Unlock()
	if !ffprobeDone {
		ffprobePath, ffprobeErr = resolveTool("ffprobe", envFFprobe)
		ffprobeDone = true
	}
	return ffprobePath, ffprobeErr
}

// Available reports whether ffmpeg resolves (bundled, tools dir, or PATH).
func Available() bool {
	_, err := FFmpeg()
	return err == nil
}

// ToolsDir is where EnsureTools installs ffmpeg/ffprobe for this user.
func ToolsDir() string {
	cfg, err := os.UserConfigDir()
	if err != nil || cfg == "" {
		return ""
	}
	return filepath.Join(cfg, "SamNPlayer", "tools")
}

func resolveTool(name, envKey string) (string, error) {
	if v := os.Getenv(envKey); v != "" {
		if okFile(v) {
			return v, nil
		}
	}
	base := name
	if runtime.GOOS == "windows" {
		base += ".exe"
	}
	for _, dir := range candidateDirs() {
		if dir == "" {
			continue
		}
		p := filepath.Join(dir, base)
		if okFile(p) {
			return p, nil
		}
	}
	if p, err := exec.LookPath(name); err == nil {
		return p, nil
	}
	return "", fmt.Errorf("%s not found (place next to SamNPlayer, in %%Config%%/SamNPlayer/tools, or on PATH)", name)
}

func candidateDirs() []string {
	var dirs []string
	if exe, err := os.Executable(); err == nil {
		exe = mustEval(exe)
		dir := filepath.Dir(exe)
		dirs = append(dirs, dir, filepath.Join(dir, "tools"), filepath.Join(dir, "ffmpeg"))
	}
	if td := ToolsDir(); td != "" {
		dirs = append(dirs, td)
	}
	// Dev checkout: repo-root/tools/ffmpeg when running `go run` from source.
	if wd, err := os.Getwd(); err == nil {
		dirs = append(dirs,
			filepath.Join(wd, "tools", "ffmpeg"),
			filepath.Join(wd, "tools"),
		)
	}
	return dirs
}

func mustEval(p string) string {
	if real, err := filepath.EvalSymlinks(p); err == nil {
		return real
	}
	return p
}

func okFile(p string) bool {
	st, err := os.Stat(p)
	return err == nil && !st.IsDir()
}
