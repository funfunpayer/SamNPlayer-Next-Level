package player

import (
	"sort"
	"time"

	"github.com/funfunpayer/SamNPlayer/device"
	"github.com/funfunpayer/SamNPlayer/funscript"
)

// Extra axes for multi-axis devices (D4 output): an SR6 / OSR2 gets the
// companion files (clip.twist.funscript …) next to the stroke. Same rule as
// the stroke: at each position the NEXT action of every axis is sent once,
// as a timed move. The stroke safety range applies to the stroke only —
// these axes centre on 0.5 and are sent as written. Stroke off (Inspector)
// switches them off too.

type axisTrack struct {
	actions []funscript.Action
	next    int
}

// SetAxes gives the player the extra axes (axis name → actions, inversion
// already applied). Axes the connected device does not take are ignored.
func (p *Player) SetAxes(axes map[string][]funscript.Action) {
	p.axes = map[string]*axisTrack{}
	for name, acts := range axes {
		if len(acts) == 0 {
			continue
		}
		cp := append([]funscript.Action(nil), acts...)
		sort.SliceStable(cp, func(i, j int) bool { return cp[i].At < cp[j].At })
		p.axes[name] = &axisTrack{actions: cp, next: -1}
	}
}

func (p *Player) driveAxes(posMs int64) error {
	if len(p.axes) == 0 {
		return nil
	}
	md, ok := p.Device.(device.MultiAxisDevice)
	if !ok {
		return nil
	}
	if on, _, _ := p.StrokeOutput(); !on {
		for _, t := range p.axes {
			t.next = -1
		}
		return nil
	}
	speed := p.Speed()
	// Fixed order keeps the commands deterministic.
	names := make([]string, 0, len(p.axes))
	for n := range p.axes {
		names = append(names, n)
	}
	sort.Strings(names)
	for _, name := range names {
		t := p.axes[name]
		if !md.HasAxis(name) {
			continue
		}
		next := sort.Search(len(t.actions), func(i int) bool { return t.actions[i].At > posMs })
		if next >= len(t.actions) || next == t.next {
			continue
		}
		a := t.actions[next]
		dur := time.Duration(float64(a.At-posMs)/speed) * time.Millisecond
		if dur < minStrokeMove {
			dur = minStrokeMove
		}
		if err := md.SetAxis(name, float64(a.Pos)/100, dur); err != nil {
			return err
		}
		t.next = next
	}
	return nil
}

func (p *Player) resetAxes() {
	for _, t := range p.axes {
		t.next = -1
	}
}
