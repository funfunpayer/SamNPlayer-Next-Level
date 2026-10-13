package sam

import (
	"testing"

	"github.com/funfunpayer/SamNPlayer/funscript"
)

func TestEnrichContactIntensityAndGaps(t *testing.T) {
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
	s := FromFunscriptEnriched(fs)
	if s.Metadata.Source != "funscript-enriched" {
		t.Errorf("Source=%q", s.Metadata.Source)
	}
	if len(s.Metadata.TrackingGaps) != 1 {
		t.Fatalf("TrackingGaps=%d", len(s.Metadata.TrackingGaps))
	}

	var far, contact, inGap *Motion
	for i := range s.Frames {
		f := &s.Frames[i]
		switch f.Time {
		case 500:
			far = &f.Motion
		case 600:
			contact = &f.Motion
		case 700:
			inGap = &f.Motion
		}
	}
	if far == nil || contact == nil || inGap == nil {
		t.Fatal("erwartete Frames bei 500/600/700 fehlen")
	}
	if far.Intensity > 0.001 {
		t.Errorf("fern von Kontakt: Intensity=%.3f", far.Intensity)
	}
	if far.Confidence != 1 {
		t.Errorf("außerhalb Gap: Confidence=%.3f", far.Confidence)
	}
	if contact.Intensity < 0.5 {
		t.Errorf("Kontakt Pos 90: Intensity=%.3f", contact.Intensity)
	}
	if contact.Range < 0.8 {
		t.Errorf("Kontakt Range (pos/100)=%.3f", contact.Range)
	}
	if inGap.Confidence != 0 {
		t.Errorf("im Gap: Confidence=%.3f, erwartet 0", inGap.Confidence)
	}
	if inGap.Intensity > 0.001 {
		t.Errorf("im Gap: Intensity=%.3f, erwartet 0", inGap.Intensity)
	}
}

func TestEnrichVelocity(t *testing.T) {
	fs, err := funscript.Parse([]byte(`{"actions":[
		{"at":0,"pos":0},{"at":100,"pos":50},{"at":200,"pos":50}
	]}`))
	if err != nil {
		t.Fatal(err)
	}
	s := FromFunscriptEnriched(fs)
	if s.Frames[1].Motion.Velocity != 0.5 { // 50 pos / 100 ms
		t.Errorf("Velocity=%.4f, erwartet 0.5", s.Frames[1].Motion.Velocity)
	}
	if s.Frames[2].Motion.Velocity != 0 {
		t.Errorf("plateau Velocity=%.4f", s.Frames[2].Motion.Velocity)
	}
}

func TestFromFunscriptStillThin(t *testing.T) {
	// FromFunscript bleibt dünn — Enrich ist opt-in.
	fs, err := funscript.Parse([]byte(`{"actions":[
		{"at":0,"pos":20},{"at":500,"pos":90}
	],"metadata":{"profile":"tj",
		"device_recipe":{"sync":"suction_position","min_suction":0.2,
			"tick_ms":50,"max_speed":0.5,"smoothing":0.22,"contact_vibration":true}}}`))
	if err != nil {
		t.Fatal(err)
	}
	s := FromFunscript(fs)
	if s.Frames[1].Motion.Intensity != 0 {
		t.Error("FromFunscript soll Intensity nicht setzen")
	}
	if s.Frames[1].Motion.Confidence != 0 {
		t.Error("FromFunscript soll Confidence nicht setzen (omitempty zero)")
	}
}

func TestRoundtripPreservesTrackingGaps(t *testing.T) {
	fs, err := funscript.Parse([]byte(`{"actions":[
		{"at":0,"pos":20},{"at":500,"pos":90}
	],"metadata":{"profile":"tj",
		"tracking_gaps":[{"start_ms":100,"end_ms":200}],
		"device_recipe":{"sync":"suction_position","min_suction":0.2,
			"tick_ms":50,"max_speed":0.5,"smoothing":0.22,"contact_vibration":true}}}`))
	if err != nil {
		t.Fatal(err)
	}
	back := ToFunscript(FromFunscript(fs))
	if len(back.Metadata.TrackingGaps) != 1 ||
		back.Metadata.TrackingGaps[0].StartMs != 100 ||
		back.Metadata.TrackingGaps[0].EndMs != 200 {
		t.Errorf("TrackingGaps verloren: %+v", back.Metadata.TrackingGaps)
	}
}
