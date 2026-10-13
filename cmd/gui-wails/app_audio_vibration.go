package main

import (
	"context"
	"os"
	"sync"
	"time"

	"github.com/funfunpayer/SamNPlayer/funscript"
	"github.com/funfunpayer/SamNPlayer/generator"
	"github.com/funfunpayer/SamNPlayer/logging"
	"github.com/funfunpayer/SamNPlayer/sam"
)

// prefPlaybackAudioVibration: Play drives the vibration channel from the
// video's audio loudness instead of the script (suction still follows the
// script). Owner 9 Oct: switchable, default off — no silent feel change.
const prefPlaybackAudioVibration = "playback.audio_vibration"

// audioVibTimeout caps the one-off ffmpeg decode on Play start.
const audioVibTimeout = 2 * time.Minute

// audioVibrationEnvelopeFn is swapped in tests (no ffmpeg in unit tests).
var audioVibrationEnvelopeFn = generator.AudioVibrationEnvelope

// audioVibCache keeps the last decoded envelope so Play/Stop/Play and the
// warm-up from SetSetting do not decode the same video twice.
type audioVibCache struct {
	mu      sync.Mutex
	path    string
	size    int64
	modTime time.Time
	stepMs  int64
	env     []float64
}

func (a *App) audioVibrationEnabled() bool {
	return a.settings != nil && a.settings.GetBool(prefPlaybackAudioVibration, false)
}

func (a *App) currentVideoPath() string {
	a.stateMu.RLock()
	defer a.stateMu.RUnlock()
	return a.videoPath
}

// audioVibrationEnvelope returns the cached envelope for path/stepMs or
// decodes it. The cache lock is held during decode so a warm-up and a Play
// start never run ffmpeg twice for the same file.
func (a *App) audioVibrationEnvelope(ctx context.Context, path string, stepMs int64) ([]float64, error) {
	st, err := os.Stat(path)
	if err != nil {
		return nil, err
	}
	c := &a.audioVib
	c.mu.Lock()
	defer c.mu.Unlock()
	if c.env != nil && c.path == path && c.stepMs == stepMs &&
		c.size == st.Size() && c.modTime.Equal(st.ModTime()) {
		return c.env, nil
	}
	env, err := audioVibrationEnvelopeFn(ctx, path, stepMs)
	if err != nil {
		return nil, err
	}
	c.path, c.size, c.modTime, c.stepMs, c.env = path, st.Size(), st.ModTime(), stepMs, env
	return env, nil
}

// warmAudioVibration decodes the current video in the background so the next
// Play start does not wait on ffmpeg. Best effort; errors are only logged.
func (a *App) warmAudioVibration(stepMs int64) {
	path := a.currentVideoPath()
	if path == "" {
		return
	}
	go func() {
		ctx, cancel := context.WithTimeout(context.Background(), audioVibTimeout)
		defer cancel()
		if _, err := a.audioVibrationEnvelope(ctx, path, stepMs); err != nil {
			logging.Warn("playback: audio vibration warm-up failed", "video", path, "error", err)
		}
	}()
}

// withAudioVibration swaps the vibration channel for the current video's
// audio loudness when the setting is on. Returns the frames to play and a
// one-line note for the Play log ("" when the setting is off). On any
// failure the script vibration stays — Play never fails because of audio.
func (a *App) withAudioVibration(frames []funscript.Frame, tickMs int64, scale float64) ([]funscript.Frame, string) {
	if !a.audioVibrationEnabled() || len(frames) == 0 {
		return frames, ""
	}
	path := a.currentVideoPath()
	if path == "" {
		return frames, "Audio vibration: no video loaded — using script vibration."
	}
	if tickMs <= 0 {
		tickMs = 50
	}
	ctx, cancel := context.WithTimeout(context.Background(), audioVibTimeout)
	defer cancel()
	env, err := a.audioVibrationEnvelope(ctx, path, tickMs)
	if err != nil {
		logging.Warn("playback: audio vibration unavailable", "video", path, "error", err)
		return frames, "Audio vibration unavailable (" + err.Error() + ") — using script vibration."
	}
	a.stateMu.RLock()
	offset := a.scriptOffsetMs
	a.stateMu.RUnlock()
	return sam.ApplyAudioVibration(frames, env, tickMs, offset, scale),
		"Vibration follows the video audio (suction follows the script)."
}
