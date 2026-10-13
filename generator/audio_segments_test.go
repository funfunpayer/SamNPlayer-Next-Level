package generator

import (
	"math"
	"testing"

	"github.com/funfunpayer/SamNPlayer/funscript"
)

func toneBand(freqHz, durationS, sampleRate, amp float64) []float64 {
	n := int(durationS * sampleRate)
	out := make([]float64, n)
	for i := 0; i < n; i++ {
		out[i] = amp * math.Sin(2*math.Pi*freqHz*float64(i)/sampleRate)
	}
	return out
}

func concatSamples(parts ...[]float64) []float64 {
	n := 0
	for _, p := range parts {
		n += len(p)
	}
	out := make([]float64, 0, n)
	for _, p := range parts {
		out = append(out, p...)
	}
	return out
}

func TestAnalyzeSpeechHoldSegmentsSpeechThenImpact(t *testing.T) {
	sr := 8000.0
	// Mid-band tone ≈ speech formant region, then low+high impact-ish energy.
	speech := toneBand(800, 2.0, sr, 0.35)
	impact := make([]float64, int(2.0*sr))
	for i := range impact {
		tSec := float64(i) / sr
		// Amplitude gated pulses (~2 Hz) with low + high carriers.
		env := 0.15 + 0.85*math.Max(0, math.Sin(2*math.Pi*2.0*tSec))
		impact[i] = env * (0.6*math.Sin(2*math.Pi*120*tSec) + 0.5*math.Sin(2*math.Pi*2800*tSec))
	}
	samples := concatSamples(speech, impact)
	segs, holdMs := analyzeSpeechHoldSegments(samples, sr)
	if len(segs) == 0 {
		t.Fatal("expected segments")
	}
	if holdMs <= 0 {
		t.Fatalf("expected speech-hold ms > 0, got %d; segs=%+v", holdMs, segs)
	}
	foundHold, foundMotion := false, false
	for _, s := range segs {
		if s.SpeechHold || s.Label == labelHolding {
			foundHold = true
		}
		if s.Label == labelGentle || s.Label == labelIntense || s.Label == labelClimax {
			foundMotion = true
		}
	}
	if !foundHold {
		t.Fatalf("expected a holding/speech-hold segment, got %+v", segs)
	}
	if !foundMotion {
		t.Fatalf("expected a gentle/intense/climax segment after speech, got %+v", segs)
	}
}

func TestAnalyzeSpeechHoldSegmentsTooShort(t *testing.T) {
	sr := 8000.0
	samples := toneBand(800, 0.15, sr, 0.3)
	segs, holdMs := analyzeSpeechHoldSegments(samples, sr)
	if segs != nil || holdMs != 0 {
		t.Fatalf("short signal must yield nil segments, got segs=%v hold=%d", segs, holdMs)
	}
}

func TestAppendSpeechHoldHints(t *testing.T) {
	out := &funscript.AudioCheck{
		SpeechHoldMs: 1500,
		Segments: []funscript.AudioSegmentHint{
			{Label: labelHolding, StartMs: 0, EndMs: 1500, SpeechHold: true, Reason: "speech_like"},
			{Label: labelIntense, StartMs: 1500, EndMs: 3000, Reason: "impact_rhythm"},
		},
	}
	// Active stroking through the speech-hold window.
	actions := make([]funscript.Action, 0, 40)
	for at := int64(0); at <= 1500; at += 40 {
		pos := 20 + int((at/40)%2)*60
		actions = append(actions, funscript.Action{At: at, Pos: pos})
	}
	appendSpeechHoldHints(out, actions)
	if len(out.Warnings) < 2 {
		t.Fatalf("want speech-hold + motion warnings, got %v", out.Warnings)
	}
	n := len(out.Warnings)
	appendSpeechHoldHints(out, actions)
	if len(out.Warnings) != n {
		t.Fatalf("warnings must be idempotent, %d → %d", n, len(out.Warnings))
	}
}

func TestAppendSpeechHoldHintsClimax(t *testing.T) {
	out := &funscript.AudioCheck{
		Segments: []funscript.AudioSegmentHint{
			{Label: labelClimax, StartMs: 8000, EndMs: 10000, Reason: "impact_rhythm"},
		},
	}
	appendSpeechHoldHints(out, nil)
	if len(out.Warnings) != 1 {
		t.Fatalf("want climax review warning, got %v", out.Warnings)
	}
}

func TestMergeSegmentHintsMinDuration(t *testing.T) {
	frames := []frameBands{
		{startMs: 0, endMs: 100},
		{startMs: 50, endMs: 150},
		{startMs: 100, endMs: 200},
		{startMs: 150, endMs: 250},
		{startMs: 200, endMs: 300},
		{startMs: 250, endMs: 350},
		{startMs: 300, endMs: 400},
		{startMs: 350, endMs: 450},
		{startMs: 400, endMs: 500},
		{startMs: 450, endMs: 550},
	}
	labels := []string{
		labelHolding, labelHolding, labelHolding, labelHolding, labelHolding,
		labelGentle, labelGentle, labelGentle, labelGentle, labelGentle,
	}
	holds := []bool{true, true, true, true, true, false, false, false, false, false}
	reasons := make([]string, len(labels))
	for i := range reasons {
		reasons[i] = "test"
	}
	segs := mergeSegmentHints(frames, labels, holds, reasons, 200)
	if len(segs) != 2 {
		t.Fatalf("want 2 merged segments, got %+v", segs)
	}
	if segs[0].Label != labelHolding || !segs[0].SpeechHold {
		t.Fatalf("first: %+v", segs[0])
	}
	if segs[1].Label != labelGentle || segs[1].SpeechHold {
		t.Fatalf("second: %+v", segs[1])
	}
}

func TestPromoteClimax(t *testing.T) {
	n := 20
	labels := make([]string, n)
	frames := make([]frameBands, n)
	for i := 0; i < n; i++ {
		labels[i] = labelGentle
		frames[i] = frameBands{rms: 1.0}
	}
	// Intense streak near the end with high energy.
	for i := 15; i < 19; i++ {
		labels[i] = labelIntense
		frames[i].rms = 3.0
	}
	promoteClimax(labels, frames, 1.0)
	climaxCount := 0
	for _, l := range labels {
		if l == labelClimax {
			climaxCount++
		}
	}
	if climaxCount < 2 {
		t.Fatalf("expected climax promotion, labels=%v", labels)
	}
}
