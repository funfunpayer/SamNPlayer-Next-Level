package sam

import (
	"testing"

	"github.com/funfunpayer/SamNPlayer/funscript"
)

func TestFromFunscriptPreservesTimingAndPosition(t *testing.T) {
	fs, err := funscript.Parse([]byte(`{"actions":[
		{"at":0,"pos":20},{"at":500,"pos":80},{"at":1000,"pos":10}
	],"metadata":{"creator":"test-creator"}}`))
	if err != nil {
		t.Fatalf("funscript.Parse fehlgeschlagen: %v", err)
	}
	s := FromFunscript(fs)
	if s.Version != ScriptVersion {
		t.Errorf("erwartet Version %q, ist %q", ScriptVersion, s.Version)
	}
	if len(s.Frames) != len(fs.Actions) {
		t.Fatalf("Frame-Anzahl weicht ab: %d vs %d", len(s.Frames), len(fs.Actions))
	}
	for i, a := range fs.Actions {
		if s.Frames[i].Time != a.At {
			t.Errorf("Frame %d: Time weicht ab: %d vs %d", i, s.Frames[i].Time, a.At)
		}
		if int(s.Frames[i].Motion.Position) != a.Pos {
			t.Errorf("Frame %d: Position weicht ab: %v vs %d", i, s.Frames[i].Motion.Position, a.Pos)
		}
		if s.Frames[i].Motion.Type != MotionUnknown {
			t.Errorf("Frame %d: Type sollte nicht geraten werden, ist %q", i, s.Frames[i].Motion.Type)
		}
	}
	if s.Metadata.Creator != "test-creator" {
		t.Errorf("Creator sollte übernommen werden, ist %q", s.Metadata.Creator)
	}
	if s.Metadata.Source != "funscript-import" {
		t.Errorf("Source sollte 'funscript-import' sein, ist %q", s.Metadata.Source)
	}
}

func TestFromFunscriptHonorsInverted(t *testing.T) {
	fs, err := funscript.Parse([]byte(`{"actions":[
		{"at":0,"pos":10},{"at":500,"pos":90}
	],"inverted":true}`))
	if err != nil {
		t.Fatal(err)
	}
	s := FromFunscript(fs)
	if int(s.Frames[0].Motion.Position) != 90 || int(s.Frames[1].Motion.Position) != 10 {
		t.Fatalf("SAM import must flip OFS inverted: %+v / disk %+v",
			s.Frames[0].Motion.Position, fs.Actions[0].Pos)
	}
	if fs.Actions[0].Pos != 10 {
		t.Fatal("disk Actions must stay unflipped")
	}
}

func TestToFunscriptPreservesTimingAndPosition(t *testing.T) {
	s := &Script{
		Version: ScriptVersion,
		Frames: []Frame{
			{Time: 0, Motion: Motion{Position: 20, Type: MotionRhythmic, Energy: 0.5}},
			{Time: 500, Motion: Motion{Position: 80}},
			{Time: 1000, Motion: Motion{Position: 10}},
		},
		Metadata: Metadata{Creator: "sam-creator"},
	}
	fs := ToFunscript(s)
	if len(fs.Actions) != len(s.Frames) {
		t.Fatalf("Action-Anzahl weicht ab: %d vs %d", len(fs.Actions), len(s.Frames))
	}
	for i, f := range s.Frames {
		if fs.Actions[i].At != f.Time {
			t.Errorf("Action %d: At weicht ab: %d vs %d", i, fs.Actions[i].At, f.Time)
		}
		if fs.Actions[i].Pos != int(f.Motion.Position) {
			t.Errorf("Action %d: Pos weicht ab: %d vs %v", i, fs.Actions[i].Pos, f.Motion.Position)
		}
	}
	if fs.Metadata.Creator != "sam-creator" {
		t.Errorf("Creator sollte übernommen werden, ist %q", fs.Metadata.Creator)
	}
	if fs.Metadata.Duration != 1000 {
		t.Errorf("Duration sollte 1000 sein, ist %d", fs.Metadata.Duration)
	}
}

// ToFunscript rundet statt abzuschneiden: ein reiner funscript-Import hat
// immer ganzzahlige Positionen (siehe FromFunscript), aber ein künftiger
// SAM-Producer könnte echte Zwischenwerte schreiben - Abschneiden würde die
// dann systematisch nach unten verzerren statt zum nächsten Punkt zu runden.
func TestToFunscriptRoundsFractionalPosition(t *testing.T) {
	s := &Script{
		Version: ScriptVersion,
		Frames: []Frame{
			{Time: 0, Motion: Motion{Position: 20.4}},
			{Time: 100, Motion: Motion{Position: 20.6}},
			{Time: 200, Motion: Motion{Position: 99.5}},
		},
	}
	fs := ToFunscript(s)
	want := []int{20, 21, 100}
	for i, w := range want {
		if fs.Actions[i].Pos != w {
			t.Errorf("Action %d: erwartet gerundet %d, ist %d", i, w, fs.Actions[i].Pos)
		}
	}
}

// Roundtrip: .funscript -> SAM -> .funscript muss Timing und Positionen
// exakt erhalten (docs/SAM_ARCHITECTURE.md, Meilenstein-1-Akzeptanzkriterium
// "bestehende Player, Skripte funktionieren weiter, ohne SAM zu kennen").
func TestRoundtripFunscriptSAMFunscript(t *testing.T) {
	original, err := funscript.Parse([]byte(`{"actions":[
		{"at":0,"pos":50},{"at":133,"pos":20},{"at":316,"pos":95},
		{"at":650,"pos":33},{"at":883,"pos":48},{"at":1033,"pos":0},
		{"at":1150,"pos":100}
	],"metadata":{"creator":"roundtrip-test"}}`))
	if err != nil {
		t.Fatalf("funscript.Parse fehlgeschlagen: %v", err)
	}

	samScript := FromFunscript(original)
	roundtripped := ToFunscript(samScript)

	if len(roundtripped.Actions) != len(original.Actions) {
		t.Fatalf("Action-Anzahl weicht nach Roundtrip ab: %d vs %d",
			len(roundtripped.Actions), len(original.Actions))
	}
	for i, a := range original.Actions {
		if roundtripped.Actions[i].At != a.At {
			t.Errorf("Action %d: Timing nach Roundtrip verändert: %d vs %d",
				i, roundtripped.Actions[i].At, a.At)
		}
		if roundtripped.Actions[i].Pos != a.Pos {
			t.Errorf("Action %d: Position nach Roundtrip verändert: %d vs %d",
				i, roundtripped.Actions[i].Pos, a.Pos)
		}
	}
	if roundtripped.Metadata.Creator != original.Metadata.Creator {
		t.Errorf("Creator nach Roundtrip verändert: %q vs %q",
			roundtripped.Metadata.Creator, original.Metadata.Creator)
	}
}

// Roundtrip mit einem echten tj-Skript (Ausschnitt eines tatsächlich mit
// generate_funscript.py --profile tj --contact-vibration erzeugten Skripts
// aus diesem Projekt, nicht ausgedacht): Profile und DeviceRecipe müssen
// erhalten bleiben, sonst würde die Wiedergabe nach einem SAM-Roundtrip
// stillschweigend auf die Hub-statt-Abstand-Zuordnung zurückfallen
// (funscript.IsDistanceProfile prüft genau dieses Feld) und die
// Kontakt-Vibration verlieren - beides ohne jede Fehlermeldung.
func TestRoundtripPreservesProfileAndDeviceRecipe(t *testing.T) {
	original, err := funscript.Parse([]byte(`{"actions":[
		{"at":0,"pos":82},{"at":350,"pos":90},{"at":833,"pos":77},{"at":983,"pos":76}
	],"metadata":{
		"creator":"SamNPlayer generate_funscript.py",
		"profile":"tj",
		"device_recipe":{"sync":"suction_position","min_suction":0.2,"tick_ms":50,
			"max_speed":0.5,"smoothing":0.22,"contact_vibration":true}
	}}`))
	if err != nil {
		t.Fatalf("funscript.Parse fehlgeschlagen: %v", err)
	}
	if !funscript.IsDistanceProfile(original.Metadata.Profile) {
		t.Fatalf("Testdaten-Voraussetzung verletzt: Original sollte ein Distanzprofil sein")
	}

	roundtripped := ToFunscript(FromFunscript(original))

	if roundtripped.Metadata.Profile != original.Metadata.Profile {
		t.Errorf("Profile nach Roundtrip verändert: %q vs %q",
			roundtripped.Metadata.Profile, original.Metadata.Profile)
	}
	if !funscript.IsDistanceProfile(roundtripped.Metadata.Profile) {
		t.Error("Skript ist nach dem Roundtrip kein Distanzprofil mehr - " +
			"die Wiedergabe würde auf die falsche (Hub- statt Abstands-) Zuordnung zurückfallen")
	}
	dr := roundtripped.Metadata.DeviceRecipe
	if dr == nil {
		t.Fatal("DeviceRecipe ist nach dem Roundtrip nil")
	}
	if *dr != *original.Metadata.DeviceRecipe {
		t.Errorf("DeviceRecipe nach Roundtrip verändert: %+v vs %+v", *dr, *original.Metadata.DeviceRecipe)
	}
	if !dr.ContactVibration {
		t.Error("ContactVibration ist nach dem Roundtrip verloren gegangen")
	}
}

// Großes Funscript (10000 Actions, ~ein langes Video bei dichter Abtastung)
// - eigenes Akzeptanzkriterium aus docs/SAM_ARCHITECTURE.md ("große Dateien
// testen"), nicht nur eine Handvoll Punkte.
func TestRoundtripLargeFunscript(t *testing.T) {
	const n = 10000
	actions := make([]funscript.Action, n)
	for i := 0; i < n; i++ {
		pos := i % 101 // deterministisches, aber abwechslungsreiches Muster
		actions[i] = funscript.Action{At: int64(i) * 20, Pos: pos}
	}
	original := &funscript.Script{Actions: actions}
	original.Metadata.Creator = "large-file-test"

	samScript := FromFunscript(original)
	if len(samScript.Frames) != n {
		t.Fatalf("erwartet %d Frames, sind %d", n, len(samScript.Frames))
	}
	roundtripped := ToFunscript(samScript)
	if len(roundtripped.Actions) != n {
		t.Fatalf("erwartet %d Actions nach Roundtrip, sind %d", n, len(roundtripped.Actions))
	}
	for i := 0; i < n; i += 137 { // stichprobenartig prüfen, nicht jeden einzelnen Punkt
		if roundtripped.Actions[i] != original.Actions[i] {
			t.Fatalf("Action %d weicht nach Roundtrip ab: %+v vs %+v",
				i, roundtripped.Actions[i], original.Actions[i])
		}
	}
	// Randpunkte (erster/letzter) immer explizit prüfen.
	if roundtripped.Actions[0] != original.Actions[0] {
		t.Fatalf("erste Action weicht ab: %+v vs %+v", roundtripped.Actions[0], original.Actions[0])
	}
	if roundtripped.Actions[n-1] != original.Actions[n-1] {
		t.Fatalf("letzte Action weicht ab: %+v vs %+v", roundtripped.Actions[n-1], original.Actions[n-1])
	}
}
