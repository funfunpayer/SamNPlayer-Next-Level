package main

import (
	"fmt"
	"net/http"
	"os"
	"strings"
)

// Training with any video (Owner, 12 Oct; docs/V1_TARGET_FEATURE_MAP.md):
// besides the pixel clip, any film can run on the Training stage. Training
// drives the device; the film only plays along (loop / muted are GUI
// options). It has its own path and URL on the local video server, so it
// never replaces the Player's linked video.

// SetTrainingVideo selects the film for the Training stage and returns the
// URL the <video> element loads.
func (a *App) SetTrainingVideo(path string) (string, error) {
	path = strings.TrimSpace(path)
	if path == "" {
		return "", fmt.Errorf("no video path")
	}
	info, err := os.Stat(path)
	if err != nil || info.IsDir() {
		return "", fmt.Errorf("video file not found: %s", path)
	}
	a.stateMu.Lock()
	a.trainingVideoPath = path
	a.trainingVideoEpoch++
	a.stateMu.Unlock()
	return a.TrainingVideoURL(), nil
}

// ClearTrainingVideo goes back to the pixel clip.
func (a *App) ClearTrainingVideo() {
	a.stateMu.Lock()
	a.trainingVideoPath = ""
	a.trainingVideoEpoch++
	a.stateMu.Unlock()
}

// TrainingVideoURL is the local URL of the Training film ("" = none).
func (a *App) TrainingVideoURL() string {
	a.stateMu.RLock()
	path := a.trainingVideoPath
	epoch := a.trainingVideoEpoch
	a.stateMu.RUnlock()
	if path == "" {
		return ""
	}
	port, err := a.ensureVideoServer()
	if err != nil {
		return ""
	}
	return fmt.Sprintf("http://127.0.0.1:%d/training-video?v=%d", port, epoch)
}

// serveTrainingVideo serves only the one selected file (same rules as the
// Player video: localhost server, no path from the request).
func (a *App) serveTrainingVideo(w http.ResponseWriter, r *http.Request) {
	a.stateMu.RLock()
	path := a.trainingVideoPath
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
