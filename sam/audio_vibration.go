package sam

import (
	"github.com/funfunpayer/SamNPlayer/funscript"
)

// ApplyAudioVibration replaces the vibration channel of already-mapped
// device frames with an audio loudness curve (GUI "Vibration nur aus Audio").
// Suction is left as mapped. envelope[i] is the 0..1 level at video time
// i*stepMs; a frame at script time At plays at video time At+offsetMs (same
// convention as ReportVideoPosition). scale multiplies the audio level
// (strength preset); ≤0 means 1.
//
// When the audio runs longer than the script, frames are extended at the
// same tick with suction 0 so the vibration follows the whole video.
func ApplyAudioVibration(frames []funscript.Frame, envelope []float64, stepMs, offsetMs int64, scale float64) []funscript.Frame {
	if len(frames) == 0 || len(envelope) == 0 || stepMs <= 0 {
		return frames
	}
	if scale <= 0 {
		scale = 1
	}
	level := func(at int64) float64 {
		vt := at + offsetMs
		if vt < 0 {
			return 0
		}
		i := (vt + stepMs/2) / stepMs
		if i >= int64(len(envelope)) {
			return 0
		}
		return clamp01(envelope[i] * scale)
	}
	out := make([]funscript.Frame, len(frames), len(frames)+len(envelope))
	for i, f := range frames {
		out[i] = funscript.Frame{At: f.At, Vibration: level(f.At), Suction: f.Suction}
	}
	tick := stepMs
	if len(frames) >= 2 {
		if d := frames[len(frames)-1].At - frames[len(frames)-2].At; d > 0 {
			tick = d
		}
	}
	audioEnd := int64(len(envelope)-1)*stepMs - offsetMs
	for t := frames[len(frames)-1].At + tick; t <= audioEnd; t += tick {
		out = append(out, funscript.Frame{At: t, Vibration: level(t)})
	}
	return out
}
