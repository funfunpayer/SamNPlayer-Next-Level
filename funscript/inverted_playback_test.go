package funscript

import (
	"encoding/json"
	"os"
	"path/filepath"
	"testing"
)

func TestParseTopLevelInverted(t *testing.T) {
	s, err := Parse([]byte(`{"actions":[{"at":0,"pos":10},{"at":1000,"pos":90}],"inverted":true,"metadata":{"duration":1}}`))
	if err != nil {
		t.Fatal(err)
	}
	if !s.Inverted {
		t.Fatal("expected Inverted=true")
	}
	if s.Actions[0].Pos != 10 || s.Actions[1].Pos != 90 {
		t.Fatalf("disk actions must stay unflipped: %+v", s.Actions)
	}
}

func TestPlaybackActionsFlipsWhenInverted(t *testing.T) {
	s := &Script{
		Actions:  []Action{{At: 0, Pos: 10}, {At: 1000, Pos: 90}},
		Inverted: true,
	}
	got := s.PlaybackActions()
	want := []Action{{At: 0, Pos: 90}, {At: 1000, Pos: 10}}
	for i := range want {
		if got[i] != want[i] {
			t.Fatalf("PlaybackActions[%d]=%+v, want %+v", i, got[i], want[i])
		}
	}
	if s.Actions[0].Pos != 10 {
		t.Fatal("stored Actions must be unchanged")
	}
}

func TestPlaybackActionsPassthroughWhenNotInverted(t *testing.T) {
	s := &Script{Actions: []Action{{At: 0, Pos: 10}, {At: 1000, Pos: 90}}}
	got := s.PlaybackActions()
	if &got[0] != &s.Actions[0] {
		// Same backing slice is fine; content must match.
	}
	if got[0].Pos != 10 || got[1].Pos != 90 {
		t.Fatalf("passthrough: %+v", got)
	}
}

func TestToIntensityCurveHonorsInverted(t *testing.T) {
	plain := &Script{Actions: []Action{
		{At: 0, Pos: 0}, {At: 200, Pos: 100}, {At: 400, Pos: 0},
	}}
	inv := &Script{
		Actions:  []Action{{At: 0, Pos: 100}, {At: 200, Pos: 0}, {At: 400, Pos: 100}},
		Inverted: true,
	}
	opts := DefaultMapOptions()
	opts.Smoothing = 0
	opts.MinVibration = 0
	a := plain.ToIntensityCurve(opts)
	b := inv.ToIntensityCurve(opts)
	if len(a) == 0 || len(a) != len(b) {
		t.Fatalf("frame counts %d vs %d", len(a), len(b))
	}
	for i := range a {
		if a[i].At != b[i].At {
			t.Fatalf("at mismatch at %d", i)
		}
		if a[i].Vibration != b[i].Vibration || a[i].Suction != b[i].Suction {
			t.Fatalf("at %dms: plain vib/suc=%.3f/%.3f inverted-playback=%.3f/%.3f",
				a[i].At, a[i].Vibration, a[i].Suction, b[i].Vibration, b[i].Suction)
		}
	}
}

func TestWriteInvertedClearsAndSetsFlag(t *testing.T) {
	dir := t.TempDir()
	path := filepath.Join(dir, "x.funscript")
	body := `{"actions":[{"at":0,"pos":0},{"at":1000,"pos":100}],"inverted":true,"range":100,"metadata":{"creator":"t"}}`
	if err := os.WriteFile(path, []byte(body), 0o644); err != nil {
		t.Fatal(err)
	}
	if err := WriteInverted(path, false); err != nil {
		t.Fatal(err)
	}
	raw, err := os.ReadFile(path)
	if err != nil {
		t.Fatal(err)
	}
	var doc map[string]json.RawMessage
	if err := json.Unmarshal(raw, &doc); err != nil {
		t.Fatal(err)
	}
	if _, ok := doc["inverted"]; ok {
		t.Fatalf("inverted should be removed, got %s", doc["inverted"])
	}
	if _, ok := doc["range"]; !ok {
		t.Fatal("range must be preserved")
	}
	if err := WriteInverted(path, true); err != nil {
		t.Fatal(err)
	}
	s, err := Load(path)
	if err != nil {
		t.Fatal(err)
	}
	if !s.Inverted {
		t.Fatal("expected inverted true after WriteInverted")
	}
}
