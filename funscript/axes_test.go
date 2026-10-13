package funscript

import (
	"encoding/json"
	"os"
	"path/filepath"
	"testing"
)

func TestBakeAxesAndExplicitPlayback(t *testing.T) {
	actions := []Action{
		{At: 0, Pos: 20},
		{At: 1000, Pos: 90},
		{At: 2000, Pos: 20},
	}
	opts := RecipeFor(ProfileTJ)
	opts.ContactVibration = true
	opts.ContactVibrationSpan = 0.5
	opts.TickMs = 50
	opts.Smoothing = 0 // deterministic bake

	axes := BakeAxesFromRecipe(actions, opts, 100)
	if len(axes.Suction) < 2 {
		t.Fatalf("expected suction axis points, got %d", len(axes.Suction))
	}
	if len(axes.Vibration) < 2 {
		t.Fatalf("expected vibration axis points, got %d", len(axes.Vibration))
	}
	// Mid contact: suction should be high; vibe should be > 0 with span 0.5.
	midSuc := SampleAxisPos(axes.Suction, 1000)
	midVib := SampleAxisPos(axes.Vibration, 1000)
	if midSuc < 80 {
		t.Fatalf("suction at peak pos: got %.1f want ~90", midSuc)
	}
	if midVib <= 0 {
		t.Fatalf("vibration at contact peak: got %.1f, want > 0", midVib)
	}

	s := &Script{Actions: actions}
	s.Metadata.Profile = ProfileTJ
	s.Metadata.DeviceRecipe = &DeviceRecipe{
		Sync:             SyncSuctionPosition.String(),
		PlaybackSource:   PlaybackSourceAxes,
		ContactVibration: true,
		TickMs:           50,
		MaxSpeed:         0.5,
		Smoothing:        0,
		MinSuction:       0.2,
	}
	s.Metadata.SamnAxes = &axes

	mapOpts := MapOptionsFromScript(s)
	if !mapOpts.UseExplicitAxes {
		t.Fatal("expected UseExplicitAxes from playback_source=axes")
	}
	frames := s.ToIntensityCurve(mapOpts)
	if len(frames) == 0 {
		t.Fatal("no frames")
	}
	var foundVib bool
	for _, f := range frames {
		if f.Vibration > 0.1 {
			foundVib = true
		}
		if f.Suction < 0 || f.Suction > 1 {
			t.Fatalf("suction out of range: %v", f)
		}
	}
	if !foundVib {
		t.Fatal("explicit axes playback never raised vibration")
	}
}

func TestSaveAxisActionsAndPlaybackSource(t *testing.T) {
	dir := t.TempDir()
	path := filepath.Join(dir, "t.funscript")
	body := `{"actions":[{"at":0,"pos":10},{"at":1000,"pos":90}],"metadata":{"profile":"tj","device_recipe":{"sync":"suction_position","min_suction":0.2,"tick_ms":50,"max_speed":0.5,"smoothing":0.22}}}`
	if err := os.WriteFile(path, []byte(body), 0o644); err != nil {
		t.Fatal(err)
	}
	vib := []Action{{At: 0, Pos: 0}, {At: 500, Pos: 80}, {At: 1000, Pos: 0}}
	if err := SaveAxisActions(path, AxisVibration, vib); err != nil {
		t.Fatal(err)
	}
	data, err := os.ReadFile(path)
	if err != nil {
		t.Fatal(err)
	}
	var doc map[string]json.RawMessage
	if err := json.Unmarshal(data, &doc); err != nil {
		t.Fatal(err)
	}
	var meta struct {
		SamnAxes     *SamnAxes     `json:"samn_axes"`
		DeviceRecipe *DeviceRecipe `json:"device_recipe"`
	}
	if err := json.Unmarshal(doc["metadata"], &meta); err != nil {
		t.Fatal(err)
	}
	if meta.SamnAxes == nil || len(meta.SamnAxes.Vibration) != 3 {
		t.Fatalf("samn_axes.vibration missing: %+v", meta.SamnAxes)
	}
	if NormalizePlaybackSource(meta.DeviceRecipe.PlaybackSource) != PlaybackSourceAxes {
		t.Fatalf("playback_source=%q want axes", meta.DeviceRecipe.PlaybackSource)
	}

	if err := SavePlaybackSource(path, PlaybackSourceRecipe); err != nil {
		t.Fatal(err)
	}
	s, err := Load(path)
	if err != nil {
		t.Fatal(err)
	}
	if NormalizePlaybackSource(s.Metadata.DeviceRecipe.PlaybackSource) != PlaybackSourceRecipe {
		t.Fatalf("after toggle: %q", s.Metadata.DeviceRecipe.PlaybackSource)
	}
	if s.Metadata.SamnAxes == nil || len(s.Metadata.SamnAxes.Vibration) != 3 {
		t.Fatal("axes should remain after switching source back to recipe")
	}
}

func TestRecipeModeIgnoresAxes(t *testing.T) {
	actions := []Action{{At: 0, Pos: 20}, {At: 1000, Pos: 90}}
	s := &Script{Actions: actions}
	s.Metadata.Profile = ProfileTJ
	s.Metadata.DeviceRecipe = &DeviceRecipe{
		Sync:           SyncSuctionPosition.String(),
		PlaybackSource: PlaybackSourceRecipe,
		TickMs:         50,
		Smoothing:      0,
	}
	s.Metadata.SamnAxes = &SamnAxes{
		Vibration: []Action{{At: 0, Pos: 100}, {At: 1000, Pos: 100}},
		Suction:   []Action{{At: 0, Pos: 0}, {At: 1000, Pos: 0}},
	}
	opts := MapOptionsFromScript(s)
	if opts.UseExplicitAxes {
		t.Fatal("recipe mode must not use explicit axes")
	}
	frames := s.ToIntensityCurve(opts)
	for _, f := range frames {
		if f.Vibration != 0 {
			t.Fatalf("tj without contact should keep vib=0 in recipe mode, got %v", f)
		}
		if f.Suction <= 0 {
			t.Fatalf("expected suction from position, got %v", f)
		}
	}
}
