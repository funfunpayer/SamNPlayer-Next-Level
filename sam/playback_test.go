package sam

import (
	"testing"

	"github.com/funfunpayer/SamNPlayer/funscript"
)

func TestPlaybackFramesUsesSAMIntensity(t *testing.T) {
	fs, err := funscript.Parse([]byte(`{"actions":[
		{"at":0,"pos":20},{"at":500,"pos":20},
		{"at":600,"pos":90},{"at":700,"pos":90},{"at":900,"pos":90},
		{"at":1000,"pos":20},{"at":1500,"pos":20}
	],"metadata":{
		"profile":"tj",
		"device_recipe":{"sync":"suction_position","min_suction":0.2,
			"tick_ms":50,"max_speed":0.5,"smoothing":0.22,"contact_vibration":true},
		"tracking_gaps":[{"start_ms":650,"end_ms":850}]
	}}`))
	if err != nil {
		t.Fatal(err)
	}
	opts := funscript.RecipeFor("tj")
	opts.ContactVibration = true
	opts.ContactVibrationEnvelope = -1
	opts.Smoothing = 0
	opts.TrackingGaps = fs.Metadata.TrackingGaps

	frames := PlaybackFramesFromFunscript(fs, opts)
	if len(frames) == 0 {
		t.Fatal("keine Frames")
	}
	var vibFar, vibContact, vibGap float64
	for _, f := range frames {
		if f.At >= 400 && f.At <= 500 && f.Vibration > vibFar {
			vibFar = f.Vibration
		}
		if f.At >= 600 && f.At <= 640 && f.Vibration > vibContact {
			vibContact = f.Vibration
		}
		if f.At >= 700 && f.At <= 800 && f.Vibration > vibGap {
			vibGap = f.Vibration
		}
	}
	if vibFar > 0.001 {
		t.Errorf("fern: vib=%.3f", vibFar)
	}
	if vibContact < 0.5 {
		t.Errorf("Kontakt über SAM-Intensity: vib=%.3f", vibContact)
	}
	if vibGap > 0.001 {
		t.Errorf("Gap muss Vib 0 halten: vib=%.3f", vibGap)
	}
}

func TestPlaybackFramesFallsBackWithoutContact(t *testing.T) {
	fs, err := funscript.Parse([]byte(`{"actions":[
		{"at":0,"pos":20},{"at":500,"pos":90},{"at":1000,"pos":20}
	],"metadata":{"profile":"tj"}}`))
	if err != nil {
		t.Fatal(err)
	}
	opts := funscript.RecipeFor("tj")
	opts.Smoothing = 0
	frames := PlaybackFramesFromFunscript(fs, opts)
	for _, f := range frames {
		if f.Vibration > 0.001 {
			t.Fatalf("ohne Contact sollte Vib 0 sein, ist %.3f", f.Vibration)
		}
		if f.Suction < 0.1 {
			t.Fatalf("Sog sollte Positionsignal folgen")
		}
	}
}

func TestPlaybackFramesHonorsOptsOnlyTrackingGaps(t *testing.T) {
	// Gaps nur in MapOptions, nicht in Funscript-Metadata — SAM muss wie Classic muten.
	fs, err := funscript.Parse([]byte(`{"actions":[
		{"at":0,"pos":20},{"at":500,"pos":20},
		{"at":600,"pos":90},{"at":700,"pos":90},{"at":900,"pos":90},
		{"at":1000,"pos":20},{"at":1500,"pos":20}
	],"metadata":{"profile":"tj",
		"device_recipe":{"sync":"suction_position","min_suction":0.2,
			"tick_ms":50,"max_speed":0.5,"smoothing":0.22,"contact_vibration":true}}}`))
	if err != nil {
		t.Fatal(err)
	}
	opts := funscript.RecipeFor("tj")
	opts.ContactVibration = true
	opts.ContactVibrationEnvelope = -1
	opts.Smoothing = 0
	opts.MinVibration = 0
	opts.TrackingGaps = []funscript.TrackingGap{{StartMs: 650, EndMs: 850}}

	classic := fs.ToIntensityCurve(opts)
	samFrames := PlaybackFramesFromFunscript(fs, opts)
	var classicGap, samGap, samContact float64
	for _, f := range classic {
		if f.At >= 700 && f.At <= 800 && f.Vibration > classicGap {
			classicGap = f.Vibration
		}
	}
	for _, f := range samFrames {
		if f.At >= 700 && f.At <= 800 && f.Vibration > samGap {
			samGap = f.Vibration
		}
		if f.At >= 600 && f.At <= 640 && f.Vibration > samContact {
			samContact = f.Vibration
		}
	}
	if classicGap > 0.001 {
		t.Fatalf("classic Gap-Mute broken: %.3f", classicGap)
	}
	if samGap > 0.001 {
		t.Errorf("SAM ignoriert opts.TrackingGaps: gap vib=%.3f", samGap)
	}
	if samContact < 0.5 {
		t.Errorf("Kontakt außerhalb Gap verloren: vib=%.3f", samContact)
	}
}

func TestToDeviceFramesMutesOnConfidenceZero(t *testing.T) {
	fs, err := funscript.Parse([]byte(`{"actions":[
		{"at":0,"pos":20},{"at":500,"pos":20},
		{"at":600,"pos":90},{"at":700,"pos":90},{"at":1000,"pos":20}
	],"metadata":{"profile":"tj",
		"device_recipe":{"sync":"suction_position","min_suction":0.2,
			"tick_ms":50,"max_speed":0.5,"smoothing":0.22,"contact_vibration":true}}}`))
	if err != nil {
		t.Fatal(err)
	}
	s := FromFunscriptEnriched(fs)
	for i := range s.Frames {
		if s.Frames[i].Time == 600 || s.Frames[i].Time == 700 {
			s.Frames[i].Motion.Confidence = 0
			// Intensity absichtlich hoch lassen — Confidence allein muss muten.
		}
	}
	s.Metadata.TrackingGaps = nil
	opts := funscript.RecipeFor("tj")
	opts.ContactVibration = true
	opts.ContactVibrationEnvelope = -1
	opts.Smoothing = 0
	opts.MinVibration = 0
	frames := ToDeviceFrames(s, opts)
	for _, f := range frames {
		if f.At >= 600 && f.At <= 700 && f.Vibration > 0.001 {
			t.Fatalf("Confidence=0 muss Vib muten, at=%d vib=%.3f", f.At, f.Vibration)
		}
	}
}

func TestHasContactIntensityRejectsThin(t *testing.T) {
	fs, err := funscript.Parse([]byte(`{"actions":[
		{"at":0,"pos":20},{"at":500,"pos":90},{"at":1000,"pos":20}
	],"metadata":{"profile":"tj",
		"device_recipe":{"sync":"suction_position","min_suction":0.2,
			"tick_ms":50,"max_speed":0.5,"smoothing":0.22,"contact_vibration":true}}}`))
	if err != nil {
		t.Fatal(err)
	}
	if HasContactIntensity(FromFunscript(fs)) {
		t.Fatal("dünner Import darf keine Contact-Intensity melden")
	}
	if !HasContactIntensity(FromFunscriptEnriched(fs)) {
		t.Fatal("Enrich muss Contact-Intensity setzen")
	}
}

func TestToDeviceFramesNoVibBelowContactThreshold(t *testing.T) {
	// Intensity-Lerp zwischen fern (0) und Kontakt (1) darf unter contactMin
	// nicht vorvibrieren — Schwelle wird an interpolierter Pos geprüft.
	fs, err := funscript.Parse([]byte(`{"actions":[
		{"at":0,"pos":20},{"at":500,"pos":20},
		{"at":1000,"pos":90},{"at":1500,"pos":90}
	],"metadata":{"profile":"tj",
		"device_recipe":{"sync":"suction_position","min_suction":0.2,
			"tick_ms":50,"max_speed":0.5,"smoothing":0.22,
			"contact_vibration":true,"contact_vibration_span":0.5}}}`))
	if err != nil {
		t.Fatal(err)
	}
	opts := funscript.RecipeFor("tj")
	opts.ContactVibration = true
	opts.ContactVibrationSpan = 0.5
	opts.ContactVibrationEnvelope = -1
	opts.Smoothing = 0
	opts.MinVibration = 0
	// Span 0.5 → contactMin = 20 + 0.5*70 = 55
	frames := PlaybackFramesFromFunscript(fs, opts)
	for _, f := range frames {
		// Pos steigt linear 20→90 von 500→1000; bei t=700 ist pos≈48 < 55
		if f.At >= 650 && f.At <= 720 && f.Vibration > 0.001 {
			t.Fatalf("unter contactMin vorvibriert: at=%d vib=%.3f", f.At, f.Vibration)
		}
	}
	var contact float64
	for _, f := range frames {
		if f.At >= 1000 && f.At <= 1200 && f.Vibration > contact {
			contact = f.Vibration
		}
	}
	if contact < 0.5 {
		t.Fatalf("über Schwelle erwartet vib, got %.3f", contact)
	}
}
