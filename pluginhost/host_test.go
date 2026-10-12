package pluginhost

import "testing"

func TestEnableStartsHost(t *testing.T) {
	s := NewSlot()
	if err := s.Enable(); err != nil {
		t.Fatal(err)
	}
	if !s.Running() {
		t.Fatal("expected running after enable")
	}
	st := s.Status()
	if !st.Enabled || !st.Running || st.Stage != Stage {
		t.Fatalf("status %+v", st)
	}
}

func TestTickNoOpWhenDisabled(t *testing.T) {
	s := NewSlot()
	s.Tick(1000, 50, 0.2, 0.1)
	if s.TickCount() != 0 || s.NowMs() != 0 {
		t.Fatalf("disabled tick must be no-op count=%d now=%d", s.TickCount(), s.NowMs())
	}
	if err := s.Enable(); err != nil {
		t.Fatal(err)
	}
	s.Tick(2500, 40, 0.5, 0.3)
	if s.TickCount() != 1 || s.NowMs() != 2500 {
		t.Fatalf("count=%d now=%d", s.TickCount(), s.NowMs())
	}
	got := s.LastEmit()
	if got.AtMs != 2500 || got.Vibe != 0.5 || got.Suck != 0.3 {
		t.Fatalf("emit %+v", got)
	}
	s.Disable()
	s.Tick(3000, 10, 0.1, 0)
	if s.TickCount() != 1 || s.Running() {
		t.Fatalf("after disable count=%d running=%v", s.TickCount(), s.Running())
	}
}

func TestEnableIdempotent(t *testing.T) {
	s := NewSlot()
	_ = s.Enable()
	if err := s.Enable(); err != nil {
		t.Fatal(err)
	}
	if !s.Running() {
		t.Fatal("still running")
	}
}
