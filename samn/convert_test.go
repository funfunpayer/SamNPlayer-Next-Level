package samn

import (
	"os"
	"path/filepath"
	"testing"

	"github.com/funfunpayer/SamNPlayer/funscript"
)

func TestRoundTripFunscriptSamn(t *testing.T) {
	dir := t.TempDir()
	fsPath := filepath.Join(dir, "clip.funscript")
	body := `{
  "actions": [{"at":0,"pos":20},{"at":1000,"pos":90},{"at":2000,"pos":20}],
  "metadata": {
    "creator": "test",
    "duration": 2000,
    "profile": "tj",
    "device_recipe": {
      "sync": "suction_position",
      "min_suction": 0.2,
      "tick_ms": 50,
      "max_speed": 0.5,
      "smoothing": 0.22,
      "contact_vibration": true,
      "contact_vibration_span": 0.5,
      "contact_vibration_curve": "soft"
    },
    "chapters": [{"name": "A", "startTime": 0, "endTime": 1000}],
    "bookmarks": [{"name": "B", "time": 500}]
  }
}`
	if err := os.WriteFile(fsPath, []byte(body), 0o644); err != nil {
		t.Fatal(err)
	}

	d, err := LoadFunscriptFile(fsPath)
	if err != nil {
		t.Fatal(err)
	}
	if len(d.General) != 3 {
		t.Fatalf("general: %d", len(d.General))
	}
	if len(d.Chapters) != 1 || len(d.Bookmarks) != 1 {
		t.Fatalf("chapters/bookmarks: %+v %+v", d.Chapters, d.Bookmarks)
	}
	d.OMarkers = []funscript.OMarker{{
		StartMs: 800, EndMs: 1200, Kind: funscript.OMarkerPrimary, Intensity: 1,
	}}
	if err := d.BakeNeoAxes(); err != nil {
		t.Fatal(err)
	}
	if d.PlaybackSource != PlaybackAxes {
		t.Fatalf("source=%s", d.PlaybackSource)
	}
	if len(d.Vibration) < 2 || len(d.Suction) < 2 {
		t.Fatalf("baked axes empty: vib=%d suc=%d", len(d.Vibration), len(d.Suction))
	}
	d.StrengthPresets = DefaultStrengthPresets()
	d.ActiveStrength = "normal"

	samnPath := filepath.Join(dir, "clip.samn")
	if err := Save(samnPath, d); err != nil {
		t.Fatal(err)
	}
	got, err := Load(samnPath)
	if err != nil {
		t.Fatal(err)
	}
	if got.ActiveStrength != "normal" || len(got.StrengthPresets) != 3 {
		t.Fatalf("presets: %+v", got)
	}

	// Player bridge
	script, err := got.ToFunscript()
	if err != nil {
		t.Fatal(err)
	}
	opts := funscript.MapOptionsFromScript(script)
	if !opts.UseExplicitAxes {
		t.Fatal("expected axes playback from .samn")
	}
	frames := script.ToIntensityCurve(opts)
	if len(frames) == 0 {
		t.Fatal("no frames")
	}

	// Export must not embed samn_axes
	outFS := filepath.Join(dir, "out.funscript")
	if err := got.ExportFunscript(outFS); err != nil {
		t.Fatal(err)
	}
	raw, err := os.ReadFile(outFS)
	if err != nil {
		t.Fatal(err)
	}
	if contains(string(raw), "samn_axes") {
		t.Fatal("export must not include samn_axes")
	}
	re, err := funscript.Load(outFS)
	if err != nil {
		t.Fatal(err)
	}
	if len(re.Actions) != 3 {
		t.Fatalf("exported actions: %d", len(re.Actions))
	}
	oms, err := funscript.LoadOMarkers(outFS)
	if err != nil || len(oms) != 1 {
		t.Fatalf("exported oMarkers: %v %v", oms, err)
	}
}

func TestIsSamnPath(t *testing.T) {
	if !IsSamnPath("a.samn") || IsSamnPath("a.funscript") {
		t.Fatal("ext check")
	}
	if CompanionSamnPath("x.funscript") != "x.samn" {
		t.Fatal(CompanionSamnPath("x.funscript"))
	}
}

func contains(s, sub string) bool {
	return len(s) >= len(sub) && (s == sub || len(sub) == 0 ||
		(len(s) > 0 && findSub(s, sub)))
}

func findSub(s, sub string) bool {
	for i := 0; i+len(sub) <= len(s); i++ {
		if s[i:i+len(sub)] == sub {
			return true
		}
	}
	return false
}
