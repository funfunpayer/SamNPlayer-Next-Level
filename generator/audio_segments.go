package generator

import (
	"fmt"
	"math"
	"math/cmplx"

	"github.com/funfunpayer/SamNPlayer/funscript"
)

// Classical Speech-Hold + segment taxonomy (funscript-ai-inspired ideas).
// Same AUDIO_WORKFLOW contract as tempo check: review hints only — never
// invents 0–100 stroke actions. Uses multi-band short-time energy on the
// already-decoded mono samples (no Silero/PANNs models).

const (
	segFrameMs = 100.0
	segHopMs   = 50.0

	// Band edges for 8 kHz mono (Nyquist 4 kHz). Aligns with funscript-ai
	// impact / speech / transient splits, truncated to available bandwidth.
	bandImpactLo = 50.0
	bandImpactHi = 250.0
	bandSpeechLo = 250.0
	bandSpeechHi = 2000.0
	bandHighLo   = 2000.0
	bandHighHi   = 3990.0

	labelHolding = "holding"
	labelGentle  = "gentle"
	labelIntense = "intense"
	labelClimax  = "climax"

	minSegmentMs = 300.0
)

type frameBands struct {
	startMs float64
	endMs   float64
	rms     float64
	impact  float64
	speech  float64
	high    float64
}

// analyzeSpeechHoldSegments classifies short-time audio energy into
// holding/gentle/intense/climax review segments. Returns nil segments when
// the signal is too short for a stable taxonomy.
func analyzeSpeechHoldSegments(samples []float64, sampleRate float64) (segs []funscript.AudioSegmentHint, speechHoldMs int64) {
	frames := bandEnergyFrames(samples, sampleRate, segFrameMs, segHopMs)
	if len(frames) < 4 {
		return nil, 0
	}
	rmsMed := medianFloat(frameRMS(frames))
	if rmsMed <= 0 {
		rmsMed = 1e-9
	}
	quiet := rmsMed * 0.35
	labels := make([]string, len(frames))
	speechHold := make([]bool, len(frames))
	reasons := make([]string, len(frames))

	for i, f := range frames {
		total := f.impact + f.speech + f.high
		if total <= 0 {
			labels[i] = labelHolding
			speechHold[i] = false
			reasons[i] = "silence"
			continue
		}
		impactR := f.impact / total
		speechR := f.speech / total
		highR := f.high / total
		rel := f.rms / rmsMed

		switch {
		case speechR >= 0.55 && impactR <= 0.28 && highR <= 0.35 && rel < 1.8:
			// Speech-like mid-band dominance → hold cue (dialogue / soft vocal).
			labels[i] = labelHolding
			speechHold[i] = true
			reasons[i] = "speech_like"
		case (impactR + highR) >= 0.50:
			// Impact/transient texture wins over pulse troughs (which look
			// "quiet" in RMS but are still non-speech contact energy).
			if rel >= 1.4 {
				labels[i] = labelIntense
				reasons[i] = "impact_rhythm"
			} else {
				labels[i] = labelGentle
				reasons[i] = "soft_impact"
			}
		case f.rms < quiet:
			labels[i] = labelHolding
			speechHold[i] = speechR > 0.45 && impactR < 0.35
			if speechHold[i] {
				reasons[i] = "quiet_speech"
			} else {
				reasons[i] = "quiet"
			}
		case rel < 0.7:
			labels[i] = labelHolding
			speechHold[i] = speechR > impactR
			reasons[i] = "low_energy"
		default:
			labels[i] = labelGentle
			reasons[i] = "moderate_energy"
		}
	}

	promoteClimax(labels, frames, rmsMed)
	smoothLabels(labels, speechHold, reasons)
	segs = mergeSegmentHints(frames, labels, speechHold, reasons, minSegmentMs)
	for _, s := range segs {
		if s.SpeechHold {
			speechHoldMs += s.EndMs - s.StartMs
		}
	}
	return segs, speechHoldMs
}

// smoothLabels majority-filters isolated 1-frame flickers when both
// neighbours agree. Does not pull weaker centres toward a stronger
// neighbour (that collapsed speech→intense across the clip).
func smoothLabels(labels []string, speechHold []bool, reasons []string) {
	if len(labels) < 3 {
		return
	}
	origL := append([]string(nil), labels...)
	origH := append([]bool(nil), speechHold...)
	origR := append([]string(nil), reasons...)
	for i := 1; i < len(labels)-1; i++ {
		a, b, c := origL[i-1], origL[i], origL[i+1]
		if a == c && a != b {
			labels[i] = a
			speechHold[i] = origH[i-1]
			reasons[i] = origR[i-1]
		}
	}
}

// appendSpeechHoldHints adds review warnings when speech-hold windows exist
// and optionally when the script still moves a lot during those windows.
// Never mutates actions.
func appendSpeechHoldHints(out *funscript.AudioCheck, actions []funscript.Action) {
	if out == nil || len(out.Segments) == 0 {
		return
	}
	holdSec := float64(out.SpeechHoldMs) / 1000.0
	if holdSec >= 0.8 {
		msg := fmt.Sprintf(
			"Speech-hold ~%.1fs — during dialogue/quiet prefer hold/pause review (audio did not rewrite the curve)",
			holdSec)
		appendUniqueWarning(out, msg)
	}
	if motionDuringSpeechHold(actions, out.Segments) {
		appendUniqueWarning(out,
			"Script moves during speech-hold windows — review hold/pause (audio did not rewrite the curve)")
	}
	if hasLabel(out.Segments, labelClimax) {
		appendUniqueWarning(out,
			"Audio segment taxonomy marks a climax window — review chapter/finish (audio did not rewrite the curve)")
	}
}

func appendUniqueWarning(out *funscript.AudioCheck, msg string) {
	for _, w := range out.Warnings {
		if w == msg {
			return
		}
	}
	out.Warnings = append(out.Warnings, msg)
}

func hasLabel(segs []funscript.AudioSegmentHint, label string) bool {
	for _, s := range segs {
		if s.Label == label {
			return true
		}
	}
	return false
}

func motionDuringSpeechHold(actions []funscript.Action, segs []funscript.AudioSegmentHint) bool {
	if len(actions) < 2 {
		return false
	}
	var holdRanges [][2]int64
	for _, s := range segs {
		if s.SpeechHold && s.EndMs > s.StartMs {
			holdRanges = append(holdRanges, [2]int64{s.StartMs, s.EndMs})
		}
	}
	if len(holdRanges) == 0 {
		return false
	}
	// Count stroke-like travel (pos delta) while at is inside a speech-hold window.
	travel := 0
	samples := 0
	for i := 1; i < len(actions); i++ {
		at := actions[i].At
		inside := false
		for _, r := range holdRanges {
			if at >= r[0] && at <= r[1] {
				inside = true
				break
			}
		}
		if !inside {
			continue
		}
		d := actions[i].Pos - actions[i-1].Pos
		if d < 0 {
			d = -d
		}
		travel += d
		samples++
	}
	if samples < 4 {
		return false
	}
	// Average pos step > 8 on 0–100 scale ≈ active stroking through hold.
	return float64(travel)/float64(samples) > 8.0
}

func bandEnergyFrames(samples []float64, sampleRate, frameMs, hopMs float64) []frameBands {
	if sampleRate <= 0 || frameMs <= 0 || hopMs <= 0 || len(samples) == 0 {
		return nil
	}
	frameN := int(math.Round(sampleRate * frameMs / 1000.0))
	hopN := int(math.Round(sampleRate * hopMs / 1000.0))
	if frameN < 16 || hopN < 1 {
		return nil
	}
	nfft := 1
	for nfft < frameN {
		nfft <<= 1
	}
	var out []frameBands
	for start := 0; start+frameN <= len(samples); start += hopN {
		frame := samples[start : start+frameN]
		rms := 0.0
		buf := make([]complex128, nfft)
		for i, v := range frame {
			rms += v * v
			// Hann window reduces spectral leakage for band ratios.
			w := 0.5 * (1 - math.Cos(2*math.Pi*float64(i)/float64(frameN-1)))
			buf[i] = complex(v*w, 0)
		}
		rms = math.Sqrt(rms / float64(frameN))
		fftInPlace(buf)
		impact := bandPower(buf, sampleRate, bandImpactLo, bandImpactHi)
		speech := bandPower(buf, sampleRate, bandSpeechLo, bandSpeechHi)
		high := bandPower(buf, sampleRate, bandHighLo, bandHighHi)
		startMs := 1000.0 * float64(start) / sampleRate
		endMs := 1000.0 * float64(start+frameN) / sampleRate
		out = append(out, frameBands{
			startMs: startMs,
			endMs:   endMs,
			rms:     rms,
			impact:  impact,
			speech:  speech,
			high:    high,
		})
	}
	return out
}

func bandPower(spectrum []complex128, sampleRate, loHz, hiHz float64) float64 {
	n := len(spectrum)
	if n < 2 || sampleRate <= 0 {
		return 0
	}
	half := n / 2
	sum := 0.0
	for k := 1; k <= half; k++ {
		freq := float64(k) * sampleRate / float64(n)
		if freq < loHz || freq > hiHz {
			continue
		}
		mag := cmplx.Abs(spectrum[k])
		sum += mag * mag
	}
	return sum
}

func promoteClimax(labels []string, frames []frameBands, rmsMed float64) {
	if len(labels) == 0 || rmsMed <= 0 {
		return
	}
	// Prefer climax near the end: strongest intense streak in the last 25%.
	start := len(labels) * 3 / 4
	bestI, bestLen, bestScore := -1, 0, 0.0
	i := start
	for i < len(labels) {
		if labels[i] != labelIntense {
			i++
			continue
		}
		j := i
		score := 0.0
		for j < len(labels) && labels[j] == labelIntense {
			score += frames[j].rms / rmsMed
			j++
		}
		run := j - i
		if run >= 2 && (score > bestScore || (score == bestScore && run > bestLen)) {
			bestI, bestLen, bestScore = i, run, score
		}
		i = j
	}
	if bestI >= 0 && bestScore >= 4.0 {
		for k := bestI; k < bestI+bestLen; k++ {
			labels[k] = labelClimax
		}
	}
}

func mergeSegmentHints(frames []frameBands, labels []string, speechHold []bool, reasons []string, minMs float64) []funscript.AudioSegmentHint {
	if len(frames) == 0 {
		return nil
	}
	type run struct {
		label  string
		hold   bool
		reason string
		start  float64
		end    float64
	}
	var runs []run
	cur := run{
		label:  labels[0],
		hold:   speechHold[0],
		reason: reasons[0],
		start:  frames[0].startMs,
		end:    frames[0].endMs,
	}
	for i := 1; i < len(frames); i++ {
		same := labels[i] == cur.label && speechHold[i] == cur.hold
		if same {
			cur.end = frames[i].endMs
			continue
		}
		runs = append(runs, cur)
		cur = run{
			label:  labels[i],
			hold:   speechHold[i],
			reason: reasons[i],
			start:  frames[i].startMs,
			end:    frames[i].endMs,
		}
	}
	runs = append(runs, cur)

	// Merge short runs into the neighbour. A long, stable run keeps its
	// label when it absorbs a short flicker (do not upgrade holding→intense
	// just because a 200ms pulse touched it). Short+short combines toward
	// the stronger activity label.
	outRuns := make([]run, 0, len(runs))
	labelRank := func(l string) int {
		switch l {
		case labelClimax:
			return 4
		case labelIntense:
			return 3
		case labelGentle:
			return 2
		default:
			return 1
		}
	}
	for _, r := range runs {
		if len(outRuns) == 0 {
			outRuns = append(outRuns, r)
			continue
		}
		prev := &outRuns[len(outRuns)-1]
		prevDur := prev.end - prev.start
		rDur := r.end - r.start
		if rDur < minMs {
			prev.end = r.end
			if prevDur < minMs && labelRank(r.label) > labelRank(prev.label) {
				prev.label = r.label
				prev.hold = r.hold
				prev.reason = r.reason
			}
			continue
		}
		if prevDur < minMs {
			if labelRank(prev.label) > labelRank(r.label) {
				prev.end = r.end
				continue
			}
			prev.label = r.label
			prev.hold = r.hold
			prev.reason = r.reason
			prev.end = r.end
			continue
		}
		if prev.label == r.label && prev.hold == r.hold {
			prev.end = r.end
			continue
		}
		outRuns = append(outRuns, r)
	}

	out := make([]funscript.AudioSegmentHint, 0, len(outRuns))
	for _, r := range outRuns {
		out = append(out, funscript.AudioSegmentHint{
			Label:      r.label,
			StartMs:    int64(math.Round(r.start)),
			EndMs:      int64(math.Round(r.end)),
			SpeechHold: r.hold,
			Reason:     r.reason,
		})
	}
	return out
}

func frameRMS(frames []frameBands) []float64 {
	out := make([]float64, len(frames))
	for i, f := range frames {
		out[i] = f.rms
	}
	return out
}

func medianFloat(vals []float64) float64 {
	if len(vals) == 0 {
		return 0
	}
	cp := append([]float64(nil), vals...)
	// Insertion sort is fine for short frame lists in unit tests / clips.
	for i := 1; i < len(cp); i++ {
		v := cp[i]
		j := i
		for j > 0 && cp[j-1] > v {
			cp[j] = cp[j-1]
			j--
		}
		cp[j] = v
	}
	mid := len(cp) / 2
	if len(cp)%2 == 0 {
		return 0.5 * (cp[mid-1] + cp[mid])
	}
	return cp[mid]
}
