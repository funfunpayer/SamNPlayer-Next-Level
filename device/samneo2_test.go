package device

import (
	"bytes"
	"sync"
	"testing"
)

// fakeChar records GATT writes.
type fakeChar struct {
	mu     sync.Mutex
	writes [][]byte
}

func (f *fakeChar) Write(p []byte) (int, error) {
	f.mu.Lock()
	defer f.mu.Unlock()
	f.writes = append(f.writes, append([]byte(nil), p...))
	return len(p), nil
}
func (f *fakeChar) WriteWithoutResponse(p []byte) (int, error) { return f.Write(p) }

func (f *fakeChar) count() int {
	f.mu.Lock()
	defer f.mu.Unlock()
	return len(f.writes)
}

func TestSamNeo2RawWritesUpdateChannelState(t *testing.T) {
	proto := SamNeo2Protocol{}
	fc := &fakeChar{}
	s := &SamNeo2{protocol: proto, char: fc}

	// Normal playback leaves vibration at full.
	if err := s.SetVibration(1); err != nil {
		t.Fatal(err)
	}
	// Diagnostics sweep ends with raw 0.
	if err := s.SetVibrationRaw(0); err != nil {
		t.Fatal(err)
	}
	off := proto.EncodeVibrationRaw(0)
	if !bytes.Equal(s.lastVibrationPacket, off) {
		t.Fatalf("keepalive state after raw 0 = % x, want % x (would restart the device)", s.lastVibrationPacket, off)
	}
	// The same normal value as before the raw write must be sent again,
	// not swallowed as "unchanged".
	n := fc.count()
	if err := s.SetVibration(1); err != nil {
		t.Fatal(err)
	}
	if fc.count() != n+1 {
		t.Fatal("SetVibration after a raw write was skipped as unchanged")
	}

	// Suction: same rules.
	if err := s.SetSuction(1); err != nil {
		t.Fatal(err)
	}
	if err := s.SetSuctionRaw(0); err != nil {
		t.Fatal(err)
	}
	if !bytes.Equal(s.lastSuctionPacket, proto.EncodeSuctionRaw(0)) {
		t.Fatalf("suction keepalive state after raw 0 = % x", s.lastSuctionPacket)
	}
	// A raw write repeats even when the packet equals the last one.
	n = fc.count()
	if err := s.SetSuctionRaw(0); err != nil {
		t.Fatal(err)
	}
	if fc.count() != n+1 {
		t.Fatal("raw write must always be sent")
	}
}
