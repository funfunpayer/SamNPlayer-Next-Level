package samn

import (
	"encoding/json"
	"os"
	"path/filepath"
	"testing"

	"github.com/funfunpayer/SamNPlayer/funscript"
)

// Improve, Review, Kontakt-Einstellungen und "Bake" schreiben die
// Begleit-.funscript neu. Vorher ersetzte der Export die Datei komplett:
// audio_check (die Speech-Hold/Feel-Segmente, die Play für .samn genau aus
// dieser Datei liest), ai_opinion und alle OFS-Angaben (title, tags,
// performers, inverted, range, ...) gingen verloren.
func TestExportFunscriptKeepsForeignMetadata(t *testing.T) {
	p := filepath.Join(t.TempDir(), "x.funscript")
	orig := `{"actions":[{"at":0,"pos":0},{"at":500,"pos":100}],"inverted":false,"range":90,"version":"1.0",
	 "metadata":{"title":"My scene","tags":["a"],"performers":["p"],"script_url":"u","duration":1.0,
	  "audio_check":{"script_hz":1.5,"audio_hz":1.4,"warnings":[],"speech_hold_ms":500,
	   "segments":[{"label":"gentle","start_ms":0,"end_ms":400}]},
	  "ai_opinion":{"verdict":"ok","reason":"r"},
	  "tracking_gaps":[{"start_ms":100,"end_ms":200}],
	  "samn_axes":{"vibration":[{"at":0,"pos":10}]}}}`
	if err := os.WriteFile(p, []byte(orig), 0o644); err != nil {
		t.Fatal(err)
	}
	d := &Document{General: []Point{{At: 0, Pos: 5}, {At: 1000, Pos: 95}}}
	if err := d.ExportFunscript(p); err != nil {
		t.Fatal(err)
	}
	b, err := os.ReadFile(p)
	if err != nil {
		t.Fatal(err)
	}
	var top map[string]json.RawMessage
	if err := json.Unmarshal(b, &top); err != nil {
		t.Fatal(err)
	}
	for _, k := range []string{"range", "version"} {
		if _, ok := top[k]; !ok {
			t.Errorf("top-level %q lost", k)
		}
	}
	var meta map[string]json.RawMessage
	if err := json.Unmarshal(top["metadata"], &meta); err != nil {
		t.Fatal(err)
	}
	for _, k := range []string{"title", "tags", "performers", "script_url", "audio_check", "ai_opinion"} {
		if _, ok := meta[k]; !ok {
			t.Errorf("metadata %q lost", k)
		}
	}
	// Was der Export selbst verwaltet, kommt aus dem Dokument - auch das
	// Weglassen: keine Lücken mehr, keine Neo-Achsen im Community-Export.
	for _, k := range []string{"tracking_gaps", "samn_axes"} {
		if _, ok := meta[k]; ok {
			t.Errorf("metadata %q must follow the document (dropped)", k)
		}
	}
	// Die exportierten Punkte sind schon so, wie sie gespielt werden - ein
	// übernommenes "inverted" würde andere Player doppelt drehen lassen.
	if _, ok := top["inverted"]; ok {
		t.Error(`top-level "inverted" must not survive the export`)
	}
	s, err := funscript.Load(p)
	if err != nil {
		t.Fatal(err)
	}
	if len(s.Actions) != 2 || s.Actions[1] != (funscript.Action{At: 1000, Pos: 95}) {
		t.Errorf("actions not replaced: %v", s.Actions)
	}
	if s.Metadata.AudioCheck == nil || len(s.Metadata.AudioCheck.Segments) != 1 || s.Metadata.Duration != 1000 {
		t.Errorf("audio_check %+v duration %d", s.Metadata.AudioCheck, s.Metadata.Duration)
	}
}

// Ohne Zieldatei (oder mit kaputter) bleibt es der reine Export.
func TestExportFunscriptWithoutUsableTarget(t *testing.T) {
	dir := t.TempDir()
	d := &Document{General: []Point{{At: 0, Pos: 5}, {At: 1000, Pos: 95}}}
	for _, name := range []string{"new.funscript", "broken.funscript"} {
		p := filepath.Join(dir, name)
		if name == "broken.funscript" {
			if err := os.WriteFile(p, []byte("{not json"), 0o644); err != nil {
				t.Fatal(err)
			}
		}
		if err := d.ExportFunscript(p); err != nil {
			t.Fatalf("%s: %v", name, err)
		}
		if _, err := funscript.Load(p); err != nil {
			t.Fatalf("%s: %v", name, err)
		}
		if _, err := os.Stat(p + ".tmp"); !os.IsNotExist(err) {
			t.Errorf("%s: temp file left behind", name)
		}
	}
}

// Eine .funscript mit OFS "inverted": true wird so in .samn übernommen, wie
// Play sie abspielt - .samn kennt kein Flag, vorher ging die Drehung verloren.
func TestFromFunscriptBakesInverted(t *testing.T) {
	s, err := funscript.Parse([]byte(`{"actions":[{"at":0,"pos":10},{"at":1000,"pos":80}],"inverted":true}`))
	if err != nil {
		t.Fatal(err)
	}
	d := FromFunscript(s, "")
	if d.General[0].Pos != 90 || d.General[1].Pos != 20 {
		t.Fatalf("general %v, want played positions 90/20", d.General)
	}
}
