package funscript

import "testing"

func TestRingDownEndsAtZeroAndKeepsPrefix(t *testing.T) {
	in := []Action{{At: 0, Pos: 10}, {At: 1000, Pos: 90}}
	out := RingDown(in, 1000, 90, 2)
	if len(out) <= len(in) {
		t.Fatalf("erwartet extra Punkte, got %d", len(out))
	}
	if out[0] != in[0] || out[1] != in[1] {
		t.Fatalf("Prefix muss unverändert bleiben: %+v", out[:2])
	}
	last := out[len(out)-1]
	if last.Pos != 0 {
		t.Fatalf("Ende muss 0 sein: %+v", last)
	}
	if last.At <= 1000 {
		t.Fatalf("Ring-down muss nach atMs liegen: %+v", last)
	}
}

func TestRingDownClampsCycles(t *testing.T) {
	a := RingDown(nil, 0, 80, 0)
	b := RingDown(nil, 0, 80, 9)
	if len(a) < 2 || len(b) < 2 {
		t.Fatal("auch geklemmte cycles erzeugen Punkte")
	}
}
