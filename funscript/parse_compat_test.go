package funscript

import (
	"encoding/json"
	"os"
	"strings"
	"testing"
)

// OFS schreibt metadata.duration in Sekunden als Kommazahl (631.8). Vorher
// scheiterte das ganze Skript am JSON-Typ (int64) - jede solche Datei war
// in Play/Improve nicht ladbar.
func TestParseOFSStyleMetadata(t *testing.T) {
	for name, tc := range map[string]struct {
		json string
		want int64
	}{
		"ofs seconds float":   {`{"actions":[{"at":0,"pos":0},{"at":631000,"pos":100}],"inverted":false,"metadata":{"creator":"","duration":631.8,"performers":[],"tags":[],"type":"basic"},"range":100,"version":"1.0"}`, 631800},
		"ofs seconds integer": {`{"actions":[{"at":0,"pos":0},{"at":631000,"pos":100}],"metadata":{"duration":631}}`, 631000},
		"ms stays ms":         {`{"actions":[{"at":0,"pos":0},{"at":631000,"pos":100}],"metadata":{"duration":631800}}`, 631800},
		"clock string":        {`{"actions":[{"at":0,"pos":0},{"at":1000,"pos":100}],"metadata":{"duration":"10:31"}}`, 631000},
		"string ignored":      {`{"actions":[{"at":0,"pos":0},{"at":1000,"pos":100}],"metadata":{"duration":"soon"}}`, 0},
		"null":                {`{"actions":[{"at":0,"pos":0},{"at":1000,"pos":100}],"metadata":{"duration":null}}`, 0},
	} {
		s, err := Parse([]byte(tc.json))
		if err != nil {
			t.Errorf("%s: %v", name, err)
			continue
		}
		if int64(s.Metadata.Duration) != tc.want {
			t.Errorf("%s: duration %d, want %d", name, s.Metadata.Duration, tc.want)
		}
	}
}

// Manche Werkzeuge schreiben at/pos als Kommazahlen ("at": 500.4,
// "pos": 99.6) - vorher ebenfalls ein Ladefehler fürs ganze Skript.
func TestParseFloatActions(t *testing.T) {
	s, err := Parse([]byte(`{"actions":[{"at":500.4,"pos":99.6},{"at":0.0,"pos":0.5},{"at":1000,"pos":50}]}`))
	if err != nil {
		t.Fatal(err)
	}
	want := []Action{{0, 1}, {500, 100}, {1000, 50}}
	for i, a := range s.Actions {
		if a != want[i] {
			t.Fatalf("actions %v, want %v", s.Actions, want)
		}
	}
	// Geschrieben wird weiter mit ganzen Zahlen.
	b, _ := json.Marshal(s.Actions[1])
	if string(b) != `{"at":500,"pos":100}` {
		t.Errorf("marshal %s", b)
	}
	if _, err := Parse([]byte(`{"actions":[{"at":"x","pos":1}]}`)); err == nil || !strings.Contains(err.Error(), "ungültiges JSON") {
		t.Errorf("non-number at must still fail: %v", err)
	}
}

// OFS 3 schreibt Kapitel-/Bookmark-Zeiten als Zeitstempel-Text
// ("00:01:23.456"). Vorher: "chapters ungültig" - und weil der Aufrufer
// den Fehler schluckt, löschte ein anschließender Export die Kapitel.
func TestLoadChaptersAndBookmarksTimestampStrings(t *testing.T) {
	p := t.TempDir() + "/x.funscript"
	body := `{"actions":[{"at":0,"pos":0},{"at":200000,"pos":100}],"metadata":{
	 "chapters":[{"name":"Intro","startTime":"00:00:00.000","endTime":"00:01:23.456"},
	             {"name":"Main","startTime":"1:23.456","endTime":"01:02:03"}],
	 "bookmarks":[{"name":"b","time":"00:00:12.5"}]}}`
	if err := os.WriteFile(p, []byte(body), 0o644); err != nil {
		t.Fatal(err)
	}
	ch, err := LoadChapters(p)
	if err != nil {
		t.Fatal(err)
	}
	want := []ChapterMark{{Name: "Intro", StartTime: 0, EndTime: 83456}, {Name: "Main", StartTime: 83456, EndTime: 3723000}}
	if len(ch) != 2 || ch[0] != want[0] || ch[1] != want[1] {
		t.Fatalf("chapters %+v, want %+v", ch, want)
	}
	bm, err := LoadBookmarks(p)
	if err != nil || len(bm) != 1 || bm[0].Time != 12500 {
		t.Fatalf("bookmarks %+v err %v", bm, err)
	}
	// Unlesbarer Text bleibt ein Fehler.
	if err := os.WriteFile(p, []byte(`{"actions":[{"at":0,"pos":0}],"metadata":{"bookmarks":[{"name":"b","time":"soon"}]}}`), 0o644); err != nil {
		t.Fatal(err)
	}
	if _, err := LoadBookmarks(p); err == nil {
		t.Error("garbage time must still error")
	}
}
