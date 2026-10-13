package player

import (
	"sync"
	"testing"
	"time"

	"github.com/funfunpayer/SamNPlayer/funscript"
)

type axisMove struct {
	axis string
	pos  float64
	dur  time.Duration
}

// multiDev is a stroker with extra axes (SR6 over TCode) for the tests.
type multiDev struct {
	linearDev
	amu   sync.Mutex
	takes map[string]bool
	moves []axisMove
}

func (d *multiDev) HasAxis(a string) bool { return d.takes[a] }
func (d *multiDev) SetAxis(a string, pos float64, dur time.Duration) error {
	d.amu.Lock()
	defer d.amu.Unlock()
	d.moves = append(d.moves, axisMove{a, pos, dur})
	return nil
}
func (d *multiDev) axisMoves() []axisMove {
	d.amu.Lock()
	defer d.amu.Unlock()
	return append([]axisMove(nil), d.moves...)
}

func TestDriveAxesSendsEachAxisOnce(t *testing.T) {
	dev := &multiDev{linearDev: linearDev{linear: true}, takes: map[string]bool{"twist": true, "roll": true}}
	p := New(dev)
	p.SetStroke(strokeActions)
	p.SetAxes(map[string][]funscript.Action{
		"twist": {{At: 0, Pos: 50}, {At: 800, Pos: 90}},
		"roll":  {{At: 600, Pos: 10}, {At: 0, Pos: 50}}, // unsorted on purpose
		"pitch": {{At: 0, Pos: 50}, {At: 500, Pos: 0}},  // device does not take it
		"sway":  nil,
	})
	_ = p.driveAxes(100)
	_ = p.driveAxes(200)
	m := dev.axisMoves()
	if len(m) != 2 || m[0].axis != "roll" || m[0].pos != 0.1 || m[0].dur != 500*time.Millisecond ||
		m[1].axis != "twist" || m[1].pos != 0.9 || m[1].dur != 700*time.Millisecond {
		t.Fatalf("one move per axis, sorted by name, untaken axes skipped: %+v", m)
	}
	p.SetStrokeOutput(false, 0, 1)
	_ = p.driveAxes(300)
	if len(dev.axisMoves()) != 2 {
		t.Fatal("stroke off stops the extra axes too")
	}
	p.SetStrokeOutput(true, 0.4, 0.6)
	_ = p.driveAxes(300)
	m = dev.axisMoves()
	if len(m) != 4 || m[2].pos != 0.1 || m[3].pos != 0.9 {
		t.Fatalf("back on resends; the stroke range does not scale extra axes: %+v", m)
	}
	p.resetAxes()
	_ = p.driveAxes(300)
	if len(dev.axisMoves()) != 6 {
		t.Fatal("reset (pause) resends every axis")
	}
}

func TestDriveAxesNeedsMultiAxisDevice(t *testing.T) {
	dev := &linearDev{linear: true}
	p := New(dev)
	p.SetAxes(map[string][]funscript.Action{"twist": {{At: 0, Pos: 50}, {At: 800, Pos: 90}}})
	if err := p.driveAxes(100); err != nil {
		t.Fatal(err)
	}
}
