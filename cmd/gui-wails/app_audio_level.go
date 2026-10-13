package main

import (
	"context"
)

// audioLevelStepMs is the envelope resolution for the timeline's Audio Level
// lane. Same step as the Play tick default, so the lane and audio vibration
// share one decode (audioVibCache keys on path + step).
const audioLevelStepMs = 50

// GetAudioLevelEnvelope returns the loudness envelope (0..1 per 50 ms step)
// of the current Player video for the Audio Level lane under the curve
// (V1 timeline, docs/V1_PLAN.md S4). Empty when no video is linked. The
// decode runs ffmpeg once per video and is cached with audio vibration.
func (a *App) GetAudioLevelEnvelope() ([]float64, error) {
	path := a.currentVideoPath()
	if path == "" {
		return []float64{}, nil
	}
	ctx, cancel := context.WithTimeout(context.Background(), audioVibTimeout)
	defer cancel()
	env, err := a.audioVibrationEnvelope(ctx, path, audioLevelStepMs)
	if err != nil {
		return nil, err
	}
	return env, nil
}
