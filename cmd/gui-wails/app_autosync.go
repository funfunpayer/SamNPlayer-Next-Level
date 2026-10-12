package main

import (
	"context"
	"fmt"
	"math"
	"sort"
	"time"

	"github.com/funfunpayer/SamNPlayer/funscript"
)

// AudioSyncSuggestion is the Auto sync (audio) result (V1 S8b). Suggest
// only: nothing changes until the user applies OffsetMs as script offset.
type AudioSyncSuggestion struct {
	OffsetMs   int64   `json:"offsetMs"`   // suggested script offset (positive = script later)
	CurrentMs  int64   `json:"currentMs"`  // offset in use now
	R          float64 `json:"r"`          // correlation at the best lag
	Margin     float64 `json:"margin"`     // best r minus the best r away from that peak
	Confidence string  `json:"confidence"` // "high" / "low" / "none"
	Note       string  `json:"note"`
}

const (
	autoSyncMaxLagMs  = 3000
	autoSyncWindowMs  = 10 * 60 * 1000 // first 10 min, like BPM "From audio"
	autoSyncSmoothMs  = 250
	autoSyncPeakGapMs = 300
)

// Thresholds are first guesses, not yet measured on real clips (Owner
// 11 Oct: build now, measure later). "high" needs a clear single peak.
const (
	autoSyncHighR      = 0.30
	autoSyncHighMargin = 0.08
	autoSyncLowR       = 0.12
)

type syncEstimate struct {
	LagMs  int64
	R      float64
	Margin float64
	OK     bool
}

// scriptActivity samples the script's motion speed (|Δpos| per ms) every
// stepMs for n samples. Sound follows motion (hits, breath, music accents)
// more than it follows the stroke position.
func scriptActivity(acts []funscript.Action, stepMs int64, n int) []float64 {
	out := make([]float64, n)
	if len(acts) < 2 {
		return out
	}
	sorted := append([]funscript.Action(nil), acts...)
	sort.Slice(sorted, func(i, j int) bool { return sorted[i].At < sorted[j].At })
	j := 0
	for i := range out {
		t := int64(i) * stepMs
		for j+1 < len(sorted)-1 && sorted[j+1].At <= t {
			j++
		}
		a, b := sorted[j], sorted[j+1]
		if t < a.At || t > b.At || b.At <= a.At {
			continue
		}
		out[i] = math.Abs(float64(b.Pos-a.Pos)) / float64(b.At-a.At)
	}
	return out
}

func movingAverage(x []float64, w int) []float64 {
	if w <= 1 || len(x) == 0 {
		return x
	}
	out := make([]float64, len(x))
	var sum float64
	for i := range x {
		sum += x[i]
		if i >= w {
			sum -= x[i-w]
		}
		n := w
		if i+1 < w {
			n = i + 1
		}
		out[i] = sum / float64(n)
	}
	return out
}

// pearsonLag correlates audio[i] with act[i-lag] over the overlap.
func pearsonLag(audio, act []float64, lag int) (float64, bool) {
	var n, sa, sb, saa, sbb, sab float64
	for i := range audio {
		k := i - lag
		if k < 0 || k >= len(act) {
			continue
		}
		a, b := audio[i], act[k]
		n++
		sa += a
		sb += b
		saa += a * a
		sbb += b * b
		sab += a * b
	}
	if n < 40 {
		return 0, false
	}
	va := saa - sa*sa/n
	vb := sbb - sb*sb/n
	if va <= 1e-12 || vb <= 1e-12 {
		return 0, false
	}
	return (sab - sa*sb/n) / math.Sqrt(va*vb), true
}

// estimateSyncLag finds the lag (ms) at which the audio loudness best
// follows the script activity. Positive lag = the sound comes later than
// the script, so the script must be delayed by that much.
func estimateSyncLag(audio []float64, stepMs int64, acts []funscript.Action, maxLagMs int64) syncEstimate {
	if stepMs <= 0 || len(audio) < 40 || len(acts) < 10 {
		return syncEstimate{}
	}
	act := scriptActivity(acts, stepMs, len(audio))
	w := int(autoSyncSmoothMs / stepMs)
	a := movingAverage(audio, w)
	s := movingAverage(act, w)
	maxLag := int(maxLagMs / stepMs)
	rs := make(map[int]float64, 2*maxLag+1)
	best, bestR, found := 0, -2.0, false
	for lag := -maxLag; lag <= maxLag; lag++ {
		r, ok := pearsonLag(a, s, lag)
		if !ok {
			continue
		}
		rs[lag] = r
		if r > bestR {
			best, bestR, found = lag, r, true
		}
	}
	if !found {
		return syncEstimate{}
	}
	gap := int(autoSyncPeakGapMs / stepMs)
	second := -2.0
	for lag, r := range rs {
		if (lag < best-gap || lag > best+gap) && r > second {
			second = r
		}
	}
	margin := bestR
	if second > -2 {
		margin = bestR - second
	}
	return syncEstimate{LagMs: int64(best) * stepMs, R: bestR, Margin: margin, OK: true}
}

func autoSyncConfidence(e syncEstimate) string {
	switch {
	case !e.OK || e.R < autoSyncLowR:
		return "none"
	case e.R >= autoSyncHighR && e.Margin >= autoSyncHighMargin:
		return "high"
	default:
		return "low"
	}
}

// EstimateAudioSyncOffset suggests a script offset from the linked video's
// sound (Player → Script Tools → Auto sync). Uses the first 10 minutes and
// the raw script timing; the current offset is reported, not applied.
func (a *App) EstimateAudioSyncOffset() (AudioSyncSuggestion, error) {
	script := a.loadedScript()
	if script == nil {
		return AudioSyncSuggestion{}, fmt.Errorf("no script loaded")
	}
	video := a.currentVideoPath()
	if video == "" {
		return AudioSyncSuggestion{}, fmt.Errorf("no video linked")
	}
	current := a.GetScriptOffset()
	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Minute)
	defer cancel()
	env, err := a.audioVibrationEnvelope(ctx, video, audioLevelStepMs)
	if err != nil {
		return AudioSyncSuggestion{}, err
	}
	if lim := int(autoSyncWindowMs / audioLevelStepMs); len(env) > lim {
		env = env[:lim]
	}
	e := estimateSyncLag(env, audioLevelStepMs, script.PlaybackActions(), autoSyncMaxLagMs)
	out := AudioSyncSuggestion{CurrentMs: current, R: e.R, Margin: e.Margin, Confidence: autoSyncConfidence(e)}
	switch out.Confidence {
	case "none":
		out.OffsetMs = current
		out.Note = "No clear match between the sound and the script — keep the offset or set it by ear."
	case "low":
		out.OffsetMs = e.LagMs
		out.Note = fmt.Sprintf("Weak match: %+d ms might fit. Check by watching before you apply.", e.LagMs)
	default:
		out.OffsetMs = e.LagMs
		out.Note = fmt.Sprintf("Clear match: %+d ms.", e.LagMs)
	}
	return out, nil
}
