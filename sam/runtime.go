package sam

import (
	"github.com/funfunpayer/SamNPlayer/funscript"
)

// RuntimeAdjust is SAM milestone-2 live correction: scale intensity / suction
// during playback without rewriting the .funscript or .sam on disk.
// Matches docs/SAM_ARCHITECTURE.md “Second milestone” (P1.1/P1.2 scoped to
// offline scripts — no webcam, no prediction).
type RuntimeAdjust struct {
	// IntensityScale multiplies Vibration after mapping (1 = unchanged).
	// Clamped to [0, 2]. Zero-value struct → defaults via Normalize.
	IntensityScale float64

	// IntensityBoost is added after scale, then clamped to [0,1].
	IntensityBoost float64

	// SuctionScale multiplies Suction (1 = unchanged). Clamped to [0, 2].
	SuctionScale float64

	// ExtraSmooth is an additional EMA on vib+suc after mapping (0 = off).
	ExtraSmooth float64

	// MuteContact forces Vibration to 0 (GUI “Kontakt-Vibration ab”).
	MuteContact bool
}

// DefaultRuntimeAdjust leaves device frames unchanged.
func DefaultRuntimeAdjust() RuntimeAdjust {
	return RuntimeAdjust{IntensityScale: 1, SuctionScale: 1}
}

// Normalize fills zero-value defaults and clamps ranges.
func (a RuntimeAdjust) Normalize() RuntimeAdjust {
	if a == (RuntimeAdjust{}) {
		return DefaultRuntimeAdjust()
	}
	out := a
	if out.IntensityScale == 0 && !out.MuteContact {
		out.IntensityScale = 1
	}
	if out.IntensityScale < 0 {
		out.IntensityScale = 0
	} else if out.IntensityScale > 2 {
		out.IntensityScale = 2
	}
	if out.SuctionScale == 0 {
		out.SuctionScale = 1
	}
	if out.SuctionScale < 0 {
		out.SuctionScale = 0
	} else if out.SuctionScale > 2 {
		out.SuctionScale = 2
	}
	if out.ExtraSmooth < 0 {
		out.ExtraSmooth = 0
	} else if out.ExtraSmooth > 0.95 {
		out.ExtraSmooth = 0.95
	}
	if out.IntensityBoost < -1 {
		out.IntensityBoost = -1
	} else if out.IntensityBoost > 1 {
		out.IntensityBoost = 1
	}
	return out
}

// IsIdentity reports whether AdjustDeviceFrames would be a no-op.
func (a RuntimeAdjust) IsIdentity() bool {
	n := a.Normalize()
	return !n.MuteContact &&
		n.IntensityScale == 1 && n.IntensityBoost == 0 &&
		n.SuctionScale == 1 && n.ExtraSmooth == 0
}

// AdjustDeviceFrames applies live correction on already-mapped frames.
// Original SAM / funscript files stay untouched.
func AdjustDeviceFrames(frames []funscript.Frame, adj RuntimeAdjust) []funscript.Frame {
	if len(frames) == 0 {
		return frames
	}
	adj = adj.Normalize()
	if adj.IsIdentity() {
		return frames
	}
	out := make([]funscript.Frame, len(frames))
	var prevVib, prevSuc float64
	for i, f := range frames {
		vib, suc := f.Vibration, f.Suction
		if adj.MuteContact {
			vib = 0
		} else {
			vib = clamp01(vib*adj.IntensityScale + adj.IntensityBoost)
		}
		suc = clamp01(suc * adj.SuctionScale)
		if adj.ExtraSmooth > 0 && i > 0 {
			vib = adj.ExtraSmooth*prevVib + (1-adj.ExtraSmooth)*vib
			suc = adj.ExtraSmooth*prevSuc + (1-adj.ExtraSmooth)*suc
		}
		prevVib, prevSuc = vib, suc
		out[i] = funscript.Frame{At: f.At, Vibration: vib, Suction: suc}
	}
	return out
}
