package sam

import (
	"math"
	"testing"

	"github.com/funfunpayer/SamNPlayer/funscript"
)

func TestRuntimeAdjustIdentity(t *testing.T) {
	frames := []funscript.Frame{{At: 0, Vibration: 0.5, Suction: 0.4}}
	out := AdjustDeviceFrames(frames, RuntimeAdjust{})
	if out[0].Vibration != 0.5 || out[0].Suction != 0.4 {
		t.Fatalf("zero adjust changed frames: %+v", out[0])
	}
}

func TestRuntimeAdjustScaleAndMute(t *testing.T) {
	frames := []funscript.Frame{
		{At: 0, Vibration: 0.8, Suction: 0.5},
		{At: 50, Vibration: 0.8, Suction: 0.5},
	}
	half := AdjustDeviceFrames(frames, RuntimeAdjust{IntensityScale: 0.5})
	if math.Abs(half[0].Vibration-0.4) > 1e-9 {
		t.Errorf("scale 0.5: vib=%.3f", half[0].Vibration)
	}
	muted := AdjustDeviceFrames(frames, RuntimeAdjust{MuteContact: true})
	if muted[0].Vibration != 0 {
		t.Errorf("mute: vib=%.3f", muted[0].Vibration)
	}
	if muted[0].Suction != 0.5 {
		t.Errorf("mute must not touch suction: %.3f", muted[0].Suction)
	}
}

func TestRuntimeAdjustBoostClamp(t *testing.T) {
	frames := []funscript.Frame{{At: 0, Vibration: 0.9, Suction: 1}}
	out := AdjustDeviceFrames(frames, RuntimeAdjust{IntensityScale: 1, IntensityBoost: 0.5})
	if out[0].Vibration != 1 {
		t.Errorf("boost must clamp to 1, got %.3f", out[0].Vibration)
	}
}

func TestDensifyMatchesClassicLinear(t *testing.T) {
	fs, err := funscript.Parse([]byte(`{"actions":[
		{"at":0,"pos":20},{"at":200,"pos":40},{"at":400,"pos":70},
		{"at":600,"pos":90},{"at":800,"pos":55},{"at":1000,"pos":20},
		{"at":1200,"pos":85},{"at":1400,"pos":30},{"at":1600,"pos":20}
	],"metadata":{"profile":"tj",
		"device_recipe":{"sync":"suction_position","min_suction":0.2,
			"tick_ms":50,"max_speed":0.5,"smoothing":0.22,
			"contact_vibration":true,"contact_vibration_span":0.5},
		"tracking_gaps":[{"start_ms":750,"end_ms":950}]}}`))
	if err != nil {
		t.Fatal(err)
	}
	opts := funscript.RecipeFor("tj")
	opts.ContactVibration = true
	opts.ContactVibrationSpan = 0.5
	opts.ContactVibrationCurve = "linear"
	opts.ContactVibrationEnvelope = -1
	opts.Smoothing = 0
	opts.MinVibration = 0
	opts.TrackingGaps = fs.Metadata.TrackingGaps

	classic := fs.ToIntensityCurve(opts)
	samFrames := PlaybackFramesFromFunscript(fs, opts)
	if len(classic) != len(samFrames) {
		t.Fatalf("len classic=%d sam=%d", len(classic), len(samFrames))
	}
	var maxDiff float64
	for i := range classic {
		d := math.Abs(classic[i].Vibration - samFrames[i].Vibration)
		if d > maxDiff {
			maxDiff = d
		}
		if classic[i].At >= 750 && classic[i].At <= 950 && samFrames[i].Vibration > 0.001 {
			t.Fatalf("gap leak at %d vib=%.3f", classic[i].At, samFrames[i].Vibration)
		}
	}
	if maxDiff > 0.02 {
		t.Errorf("Densify sollte Classic eng folgen, maxVibDiff=%.4f", maxDiff)
	}
}

func TestDensifySourceAndFrameCount(t *testing.T) {
	fs, err := funscript.Parse([]byte(`{"actions":[
		{"at":0,"pos":20},{"at":500,"pos":90},{"at":1000,"pos":20}
	],"metadata":{"profile":"tj",
		"device_recipe":{"sync":"suction_position","min_suction":0.2,
			"tick_ms":50,"max_speed":0.5,"smoothing":0.22,"contact_vibration":true}}}`))
	if err != nil {
		t.Fatal(err)
	}
	opts := funscript.RecipeFor("tj")
	opts.ContactVibration = true
	s := Densify(FromFunscriptEnriched(fs), 50, opts)
	if s.Metadata.Source != "funscript-dense" {
		t.Errorf("Source=%q", s.Metadata.Source)
	}
	if len(s.Frames) < 20 {
		t.Errorf("erwartet tick-dichte Frames, got %d", len(s.Frames))
	}
}
