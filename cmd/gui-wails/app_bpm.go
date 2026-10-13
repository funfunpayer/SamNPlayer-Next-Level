package main

import (
	"context"
	"fmt"
	"math"
	"time"

	"github.com/funfunpayer/SamNPlayer/generator"
)

// estimateAudioTempoFn is swapped in tests (no ffmpeg needed).
var estimateAudioTempoFn = generator.EstimateVideoAudioTempoHz

// EstimatePlaybackAudioBPM measures the loaded video's audio tempo for the
// Play BPM grid (Owner UX ask "BPM-auto"). Display only — nothing is
// written. Returns 0 when the sound has no clear beat.
func (a *App) EstimatePlaybackAudioBPM() (float64, error) {
	path := a.currentVideoPath()
	if path == "" {
		return 0, fmt.Errorf("no video loaded")
	}
	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Minute)
	defer cancel()
	hz, err := estimateAudioTempoFn(ctx, path)
	if err != nil {
		return 0, err
	}
	return bpmFromHz(hz), nil
}

// bpmFromHz folds a tempo into the grid's 20–240 BPM input range by
// octaves and rounds it; 0 stays 0 (no clear beat).
func bpmFromHz(hz float64) float64 {
	if !(hz > 0) || math.IsInf(hz, 0) {
		return 0
	}
	bpm := hz * 60
	for bpm < 20 {
		bpm *= 2
	}
	for bpm > 240 {
		bpm /= 2
	}
	return math.Round(bpm)
}
