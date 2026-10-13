package generator

import (
	"context"
	"fmt"
	"math"
	"math/cmplx"
	"sort"

	"github.com/funfunpayer/SamNPlayer/funscript"
	"github.com/funfunpayer/SamNPlayer/videox"
)

// Classical audio-tempo plausibility check (port of audio_check.py).
// Informational only — never changes Quality Doctor pass/score.

const (
	audioMinTempoHz = 0.2
	audioMaxTempoHz = 5.0
)

var audioDefaultHarmonics = []float64{0.5, 1.0, 2.0}

// CheckAudioTempo compares script stroke tempo to the video audio-energy
// envelope. Returns nil when ffmpeg is missing or the file has no readable
// audio (same contract as Python: do not write metadata in that case).
func CheckAudioTempo(videoPath string, actions []funscript.Action) *funscript.AudioCheck {
	return CheckAudioTempoContext(context.Background(), videoPath, actions)
}

// CheckAudioTempoContext is CheckAudioTempo with cancellation propagated to
// the ffmpeg audio extraction subprocess. A nil context preserves the legacy
// background behavior.
func CheckAudioTempoContext(ctx context.Context, videoPath string, actions []funscript.Action) *funscript.AudioCheck {
	if ctx == nil {
		ctx = context.Background()
	}
	scriptHz := estimateScriptTempoHz(actions)
	samples, sr, err := extractAudioSamplesContext(ctx, videoPath, 8000)
	if err != nil {
		return nil
	}
	audioHz := estimateAudioTempoHz(samples, sr, 50.0)
	cmp := compareTempo(scriptHz, audioHz, 0.25, audioDefaultHarmonics)

	out := &funscript.AudioCheck{Warnings: nil}
	if scriptHz != nil {
		v := *scriptHz
		out.ScriptHz = &v
	}
	if audioHz != nil {
		v := *audioHz
		out.AudioHz = &v
	}
	if cmp != nil && !cmp.matches {
		out.Warnings = append(out.Warnings, fmt.Sprintf(
			"Script tempo (%.2f Hz) does not match an expected multiple of audio tempo (%.2f Hz; nearest %gx → %.2f Hz) — may be real atypical motion or a tracking error; no automatic correction",
			*scriptHz, *audioHz, cmp.harmonic, cmp.predictedHz))
	}
	// Speech-Hold + segment taxonomy: same post-hoc path, review hints only.
	segs, holdMs := analyzeSpeechHoldSegments(samples, sr)
	out.Segments = segs
	out.SpeechHoldMs = holdMs
	appendSpeechHoldHints(out, actions)
	return out
}

// AppendQualityAudioHint adds the G1.2 English hint when Signal Quality failed
// and audio Hz is clear. Never changes actions, QD score, or pass/fail.
// Idempotent if the hint string is already present.
func AppendQualityAudioHint(quality funscript.ScriptQualityResult, audio *funscript.AudioCheck) {
	if audio == nil || audio.AudioHz == nil || quality.Passed {
		return
	}
	hz := *audio.AudioHz
	msg := fmt.Sprintf(
		"Audio suggests ~%.2f Hz — check ROI / axis (Signal Quality failed; audio did not rewrite the curve)",
		hz)
	for _, w := range audio.Warnings {
		if w == msg {
			return
		}
	}
	audio.Warnings = append(audio.Warnings, msg)
}

func dominantFrequencyHz(values []float64, sampleRateHz, minHz, maxHz float64) *float64 {
	n := len(values)
	if n < 8 || sampleRateHz <= 0 {
		return nil
	}
	mean := 0.0
	for _, v := range values {
		mean += v
	}
	mean /= float64(n)
	signal := make([]float64, n)
	for i, v := range values {
		signal[i] = v - mean
	}

	// Zero-pad to next power of two for a real FFT.
	nfft := 1
	for nfft < n {
		nfft <<= 1
	}
	buf := make([]complex128, nfft)
	for i := 0; i < n; i++ {
		buf[i] = complex(signal[i], 0)
	}
	fftInPlace(buf)

	bestMag := -1.0
	var bestFreq float64
	found := false
	half := nfft / 2
	for k := 0; k <= half; k++ {
		freq := float64(k) * sampleRateHz / float64(nfft)
		if freq < minHz || freq > maxHz {
			continue
		}
		mag := cmplx.Abs(buf[k])
		if mag > bestMag {
			bestMag = mag
			bestFreq = freq
			found = true
		}
	}
	if !found || bestMag <= 0 {
		return nil
	}
	return &bestFreq
}

func audioEnvelope(samples []float64, sampleRate float64, windowMs float64) (envelope []float64, envelopeRateHz float64) {
	if sampleRate <= 0 || windowMs <= 0 {
		return nil, 0
	}
	window := int(math.Round(sampleRate * windowMs / 1000.0))
	if window < 1 {
		window = 1
	}
	nWindows := len(samples) / window
	if nWindows < 2 {
		return nil, 0
	}
	envelope = make([]float64, nWindows)
	for i := 0; i < nWindows; i++ {
		sum := 0.0
		base := i * window
		for j := 0; j < window; j++ {
			v := samples[base+j]
			sum += v * v
		}
		envelope[i] = math.Sqrt(sum / float64(window))
	}
	return envelope, 1000.0 / windowMs
}

func estimateAudioTempoHz(samples []float64, sampleRate float64, windowMs float64) *float64 {
	env, rate := audioEnvelope(samples, sampleRate, windowMs)
	if len(env) < 8 {
		return nil
	}
	return dominantFrequencyHz(env, rate, audioMinTempoHz, audioMaxTempoHz)
}

func estimateScriptTempoHz(actions []funscript.Action) *float64 {
	if len(actions) < 8 {
		return nil
	}
	type pair struct {
		at  float64
		pos float64
	}
	pts := make([]pair, len(actions))
	for i, a := range actions {
		pts[i] = pair{float64(a.At), float64(a.Pos)}
	}
	sort.Slice(pts, func(i, j int) bool { return pts[i].at < pts[j].at })
	if pts[len(pts)-1].at-pts[0].at <= 0 {
		return nil
	}
	diffs := make([]float64, 0, len(pts)-1)
	for i := 1; i < len(pts); i++ {
		diffs = append(diffs, pts[i].at-pts[i-1].at)
	}
	sort.Float64s(diffs)
	stepMs := diffs[len(diffs)/2]
	if stepMs < 10 {
		stepMs = 10
	}
	nGrid := int(math.Floor((pts[len(pts)-1].at-pts[0].at)/stepMs)) + 1
	if nGrid < 8 {
		return nil
	}
	gridPos := make([]float64, 0, nGrid)
	j := 0
	for g := 0; g < nGrid; g++ {
		t := pts[0].at + float64(g)*stepMs
		for j+1 < len(pts) && pts[j+1].at <= t {
			j++
		}
		if j+1 >= len(pts) {
			gridPos = append(gridPos, pts[len(pts)-1].pos)
			continue
		}
		if pts[j].at == pts[j+1].at {
			gridPos = append(gridPos, pts[j].pos)
			continue
		}
		frac := (t - pts[j].at) / (pts[j+1].at - pts[j].at)
		gridPos = append(gridPos, pts[j].pos+(pts[j+1].pos-pts[j].pos)*frac)
	}
	return dominantFrequencyHz(gridPos, 1000.0/stepMs, audioMinTempoHz, audioMaxTempoHz)
}

type tempoComparison struct {
	matches     bool
	harmonic    float64
	predictedHz float64
	relError    float64
}

func compareTempo(scriptHz, audioHz *float64, tolerance float64, harmonics []float64) *tempoComparison {
	if scriptHz == nil || audioHz == nil || *scriptHz <= 0 || *audioHz <= 0 {
		return nil
	}
	bestH := harmonics[0]
	bestErr := math.Abs(*scriptHz - *audioHz*bestH)
	for _, h := range harmonics[1:] {
		err := math.Abs(*scriptHz - *audioHz*h)
		if err < bestErr {
			bestErr = err
			bestH = h
		}
	}
	predicted := *audioHz * bestH
	rel := math.Abs(*scriptHz-predicted) / *scriptHz
	return &tempoComparison{
		matches:     rel <= tolerance,
		harmonic:    bestH,
		predictedHz: predicted,
		relError:    rel,
	}
}

func extractAudioSamplesContext(ctx context.Context, videoPath string, sampleRate int) ([]float64, float64, error) {
	return extractAudioSamplesLimit(ctx, videoPath, sampleRate, 0)
}

// audioTempoMaxSeconds bounds EstimateVideoAudioTempoHz: ten minutes of
// 8 kHz mono is ~38 MB of float64 and enough for a stable tempo.
const audioTempoMaxSeconds = 600

// EstimateVideoAudioTempoHz measures the dominant beat of the video's sound
// (same envelope + FFT as the post-hoc audio check, first ten minutes).
// Used by Play's BPM grid when the script carries no audio_check tempo.
// Returns 0 without error when the audio has no clear tempo.
func EstimateVideoAudioTempoHz(ctx context.Context, videoPath string) (float64, error) {
	samples, sr, err := extractAudioSamplesLimit(ctx, videoPath, 8000, audioTempoMaxSeconds)
	if err != nil {
		return 0, err
	}
	if hz := estimateAudioTempoHz(samples, sr, 50.0); hz != nil {
		return *hz, nil
	}
	return 0, nil
}

func extractAudioSamplesLimit(ctx context.Context, videoPath string, sampleRate int, maxSeconds int) ([]float64, float64, error) {
	if ctx == nil {
		ctx = context.Background()
	}
	if err := ctx.Err(); err != nil {
		return nil, 0, err
	}
	args := []string{"-i", videoPath}
	if maxSeconds > 0 {
		args = append(args, "-t", fmt.Sprintf("%d", maxSeconds))
	}
	args = append(args, "-vn", "-ac", "1", "-ar", fmt.Sprintf("%d", sampleRate),
		"-f", "f32le", "-loglevel", "error", "-")
	cmd, err := videox.CommandContext(ctx, args...)
	if err != nil {
		return nil, 0, fmt.Errorf("ffmpeg ist nicht installiert")
	}
	out, err := cmd.Output()
	if err != nil {
		if ctxErr := ctx.Err(); ctxErr != nil {
			return nil, 0, ctxErr
		}
		return nil, 0, fmt.Errorf("Keine Audiospur lesbar")
	}
	if len(out) == 0 {
		return nil, 0, fmt.Errorf("Keine Audiospur lesbar")
	}
	n := len(out) / 4
	samples := make([]float64, n)
	for i := 0; i < n; i++ {
		bits := uint32(out[i*4]) | uint32(out[i*4+1])<<8 | uint32(out[i*4+2])<<16 | uint32(out[i*4+3])<<24
		samples[i] = float64(math.Float32frombits(bits))
	}
	return samples, float64(sampleRate), nil
}

// Cooley–Tukey in-place radix-2 FFT.
func fftInPlace(a []complex128) {
	n := len(a)
	j := 0
	for i := 0; i < n; i++ {
		if i < j {
			a[i], a[j] = a[j], a[i]
		}
		m := n >> 1
		for m >= 1 && j >= m {
			j -= m
			m >>= 1
		}
		j += m
	}
	for length := 2; length <= n; length <<= 1 {
		ang := -2 * math.Pi / float64(length)
		wlen := complex(math.Cos(ang), math.Sin(ang))
		for i := 0; i < n; i += length {
			w := complex(1, 0)
			half := length / 2
			for k := 0; k < half; k++ {
				u := a[i+k]
				t := w * a[i+k+half]
				a[i+k] = u + t
				a[i+k+half] = u - t
				w *= wlen
			}
		}
	}
}
