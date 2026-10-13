package player

import (
	"sort"
	"time"

	"github.com/funfunpayer/SamNPlayer/device"
	"github.com/funfunpayer/SamNPlayer/funscript"
	"github.com/funfunpayer/SamNPlayer/logging"
)

// Stroke output for linear devices (docs/DEVICE_SUPPORT.md, D2): a stroker
// (The Handy, OSR2 …) gets the script's real positions, not the Neo 2 feel
// recipe. At every playback position the player sends the NEXT action as
// "move to pos within the time until it is due" — the device interpolates.
// One command per action; a seek or a pause resends.

// minStrokeMove: a shorter move is sent with this duration, so a late
// position never asks the device for an impossible jump.
const minStrokeMove = 20 * time.Millisecond

// SetStroke gives the player the stroke actions (0–100, inversion already
// applied — funscript.Script.PlaybackActions). Without them, or on a device
// that cannot move linearly, nothing changes.
func (p *Player) SetStroke(actions []funscript.Action) {
	cp := make([]funscript.Action, len(actions))
	copy(cp, actions)
	sort.SliceStable(cp, func(i, j int) bool { return cp[i].At < cp[j].At })
	p.stroke = cp
	p.strokeNext = -1
}

// MinStrokeSpan: the safety range is never narrower than this (0–1), so a
// stray setting cannot pin the device to one point.
const MinStrokeSpan = 0.1

// SetStrokeOutput sets the stroke output, also while playing: on/off and
// the range the device may move in (lo, hi in 0–1; positions are scaled
// into it, so a script's full stroke uses the whole allowed range).
func (p *Player) SetStrokeOutput(on bool, lo, hi float64) {
	lo, hi = ClampStrokeRange(lo, hi)
	p.intensityMu.Lock()
	p.strokeOff = !on
	p.strokeLo = lo
	p.strokeHiCut = 1 - hi
	p.intensityMu.Unlock()
}

// StrokeOutput returns on/off and the range (lo, hi).
func (p *Player) StrokeOutput() (on bool, lo, hi float64) {
	p.intensityMu.Lock()
	defer p.intensityMu.Unlock()
	return !p.strokeOff, p.strokeLo, 1 - p.strokeHiCut
}

// ClampStrokeRange keeps lo / hi in 0–1, ordered and at least
// MinStrokeSpan apart (widening around the middle of the two when needed).
func ClampStrokeRange(lo, hi float64) (float64, float64) {
	c := func(v float64) float64 {
		if !(v >= 0) {
			return 0
		}
		if v > 1 {
			return 1
		}
		return v
	}
	lo, hi = c(lo), c(hi)
	if lo > hi {
		lo, hi = hi, lo
	}
	if hi-lo < MinStrokeSpan {
		mid := (lo + hi) / 2
		lo, hi = mid-MinStrokeSpan/2, mid+MinStrokeSpan/2
		if lo < 0 {
			lo, hi = 0, MinStrokeSpan
		}
		if hi > 1 {
			lo, hi = 1-MinStrokeSpan, 1
		}
	}
	return lo, hi
}

// linearDevice returns the device as a LinearDevice when it reports a
// driven linear channel (checked after Connect, when channels are known).
func (p *Player) linearDevice() device.LinearDevice {
	if len(p.stroke) == 0 {
		return nil
	}
	ld, ok := p.Device.(device.LinearDevice)
	if !ok || !ld.HasLinear() {
		return nil
	}
	return ld
}

// driveStroke sends the next stroke target for script time posMs.
func (p *Player) driveStroke(posMs int64) error {
	ld := p.linearDevice()
	if ld == nil {
		return nil
	}
	on, lo, hi := p.StrokeOutput()
	if !on {
		p.strokeNext = -1 // switched back on → the next position sends
		return nil
	}
	next := sort.Search(len(p.stroke), func(i int) bool { return p.stroke[i].At > posMs })
	if next >= len(p.stroke) {
		return nil // past the last action: hold
	}
	if next == p.strokeNext {
		return nil // already moving there
	}
	a := p.stroke[next]
	speed := p.Speed()
	dur := time.Duration(float64(a.At-posMs)/speed) * time.Millisecond
	if dur < minStrokeMove {
		dur = minStrokeMove
	}
	pos := lo + float64(a.Pos)/100*(hi-lo)
	if err := ld.SetPosition(pos, dur); err != nil {
		return err
	}
	p.strokeNext = next
	return nil
}

// resetStroke forgets the current target (pause / seek), so the next
// position sends again.
func (p *Player) resetStroke() {
	if p.strokeNext >= 0 {
		logging.Debug("player: stroke target reset")
	}
	p.strokeNext = -1
}
