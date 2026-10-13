package player

import (
	"context"
	"sync"
	"testing"
	"time"

	"github.com/funfunpayer/SamNPlayer/funscript"
)

type move struct {
	pos float64
	dur time.Duration
}

// linearDev is a stroker (The Handy over Intiface) for the tests.
type linearDev struct {
	recordingDev
	linear bool
	mu     sync.Mutex
	moves  []move
	stops  int
}

func (d *linearDev) HasLinear() bool { return d.linear }
func (d *linearDev) SetPosition(pos float64, dur time.Duration) error {
	d.mu.Lock()
	defer d.mu.Unlock()
	d.moves = append(d.moves, move{pos, dur})
	return nil
}
func (d *linearDev) Stop() error {
	d.mu.Lock()
	d.stops++
	d.mu.Unlock()
	return d.recordingDev.Stop()
}
func (d *linearDev) snapshotMoves() []move {
	d.mu.Lock()
	defer d.mu.Unlock()
	return append([]move(nil), d.moves...)
}

var strokeActions = []funscript.Action{{At: 0, Pos: 0}, {At: 1000, Pos: 100}, {At: 2000, Pos: 20}, {At: 3000, Pos: 90}}

func TestDriveStrokeSendsNextActionOnce(t *testing.T) {
	dev := &linearDev{linear: true}
	p := New(dev)
	p.SetStroke(strokeActions)
	for _, ms := range []int64{100, 150, 400} {
		if err := p.driveStroke(ms); err != nil {
			t.Fatal(err)
		}
	}
	m := dev.snapshotMoves()
	if len(m) != 1 || m[0].pos != 1 || m[0].dur != 900*time.Millisecond {
		t.Fatalf("one move to 100 %% over 900 ms, got %+v", m)
	}
	_ = p.driveStroke(1000) // the 1000 action is due → head to 2000
	_ = p.driveStroke(2050) // → 3000
	m = dev.snapshotMoves()
	if len(m) != 3 || m[1].pos != 0.2 || m[1].dur != time.Second || m[2].pos != 0.9 || m[2].dur != 950*time.Millisecond {
		t.Fatalf("each action is sent once, in order: %+v", m)
	}
	// Seek back: the earlier target is sent again.
	_ = p.driveStroke(500)
	m = dev.snapshotMoves()
	if len(m) != 4 || m[3].pos != 1 {
		t.Fatalf("seek back resends: %+v", m)
	}
	// Past the end: hold.
	_ = p.driveStroke(5000)
	if len(dev.snapshotMoves()) != 4 {
		t.Fatal("no move after the last action")
	}
}

func TestDriveStrokeHonoursSpeedAndMinimum(t *testing.T) {
	dev := &linearDev{linear: true}
	p := New(dev)
	p.SetStroke(strokeActions)
	p.SetSpeed(2)
	_ = p.driveStroke(0)
	if m := dev.snapshotMoves(); len(m) != 1 || m[0].dur != 500*time.Millisecond {
		t.Fatalf("2× speed halves the move time: %+v", m)
	}
	p.SetSpeed(1)
	_ = p.driveStroke(1995)
	if m := dev.snapshotMoves(); len(m) != 2 || m[1].dur != minStrokeMove {
		t.Fatalf("a very late position still gets the minimum move time: %+v", m)
	}
}

func TestDriveStrokeOnlyForLinearDevices(t *testing.T) {
	dev := &linearDev{linear: false}
	p := New(dev)
	p.SetStroke(strokeActions)
	_ = p.driveStroke(100)
	if len(dev.snapshotMoves()) != 0 {
		t.Fatal("a device without a linear channel gets no positions")
	}
	dev2 := &linearDev{linear: true}
	p2 := New(dev2)
	_ = p2.driveStroke(100)
	if len(dev2.snapshotMoves()) != 0 {
		t.Fatal("no stroke actions → no positions")
	}
}

// Sync: the stroker follows the video; a pause stops it, the next
// position resends the current target.
func TestSyncDrivesStrokeAndStopsOnPause(t *testing.T) {
	dev := &linearDev{linear: true}
	p := New(dev)
	p.SetStroke(strokeActions)
	frames := []funscript.Frame{{At: 0, Vibration: 0.5}, {At: 5000, Vibration: 0.5}}
	positions := make(chan int64, 4)
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	done := make(chan error, 1)
	go func() { done <- p.Sync(ctx, frames, positions) }()

	positions <- 200
	time.Sleep(50 * time.Millisecond)
	if m := dev.snapshotMoves(); len(m) != 1 || m[0].pos != 1 {
		t.Fatalf("first position sends the next stroke: %+v", m)
	}
	positions <- SyncIdleSentinel
	time.Sleep(50 * time.Millisecond)
	dev.mu.Lock()
	stops := dev.stops
	dev.mu.Unlock()
	if stops == 0 {
		t.Fatal("pause must stop the stroker")
	}
	positions <- 300
	time.Sleep(50 * time.Millisecond)
	if m := dev.snapshotMoves(); len(m) != 2 || m[1].pos != 1 || m[1].dur != 700*time.Millisecond {
		t.Fatalf("resume resends the current target: %+v", m)
	}
	close(positions)
	if err := <-done; err != nil {
		t.Fatal(err)
	}
}

// Play (no video): the stroker gets its targets from the player clock.
func TestPlayDrivesStroke(t *testing.T) {
	dev := &linearDev{linear: true}
	p := New(dev)
	p.SetStroke([]funscript.Action{{At: 0, Pos: 0}, {At: 60, Pos: 100}, {At: 120, Pos: 0}})
	frames := []funscript.Frame{{At: 0}, {At: 30}, {At: 60}, {At: 90}, {At: 120}}
	if err := p.Play(context.Background(), frames); err != nil {
		t.Fatal(err)
	}
	m := dev.snapshotMoves()
	if len(m) != 2 || m[0].pos != 1 || m[1].pos != 0 {
		t.Fatalf("two strokes: %+v", m)
	}
}

func TestStrokeRangeScalesPositions(t *testing.T) {
	dev := &linearDev{linear: true}
	p := New(dev)
	p.SetStroke(strokeActions)
	p.SetStrokeOutput(true, 0.2, 0.7)
	_ = p.driveStroke(100)  // → 100
	_ = p.driveStroke(1500) // → 20
	m := dev.snapshotMoves()
	if len(m) != 2 || m[0].pos != 0.7 || (m[1].pos-0.3) > 1e-9 || (0.3-m[1].pos) > 1e-9 {
		t.Fatalf("positions scaled into 20–70 %%: %+v", m)
	}
	p.SetStrokeOutput(false, 0, 1)
	_ = p.driveStroke(2500)
	if len(dev.snapshotMoves()) != 2 {
		t.Fatal("stroke off sends nothing")
	}
	p.SetStrokeOutput(true, 0, 1)
	_ = p.driveStroke(2500)
	if m := dev.snapshotMoves(); len(m) != 3 || m[2].pos != 0.9 {
		t.Fatalf("back on sends the current target: %+v", m)
	}
}

func TestClampStrokeRange(t *testing.T) {
	cases := [][4]float64{
		{0, 1, 0, 1},
		{0.8, 0.2, 0.2, 0.8},        // swapped
		{-1, 2, 0, 1},               // clamped
		{0.5, 0.52, 0.46, 0.56},     // widened around the middle
		{0.98, 1, 0.9, 1},           // widened at the top
		{0, 0.01, 0, MinStrokeSpan}, // widened at the bottom
	}
	for _, c := range cases {
		lo, hi := ClampStrokeRange(c[0], c[1])
		if d := lo - c[2]; d > 1e-9 || d < -1e-9 {
			t.Errorf("ClampStrokeRange(%v,%v) lo=%v want %v", c[0], c[1], lo, c[2])
		}
		if d := hi - c[3]; d > 1e-9 || d < -1e-9 {
			t.Errorf("ClampStrokeRange(%v,%v) hi=%v want %v", c[0], c[1], hi, c[3])
		}
	}
}
