package generator

import (
	"os"
	"path/filepath"
	"testing"
)

func writeTemp(t *testing.T, body string) string {
	t.Helper()
	p := filepath.Join(t.TempDir(), "clip.contact.json")
	if err := os.WriteFile(p, []byte(body), 0o644); err != nil {
		t.Fatal(err)
	}
	return p
}

func TestLoadContactPointsFilterAndSort(t *testing.T) {
	p := writeTemp(t, `{"version":1,"points":[
		{"t_ms":1500,"x":0.5,"y":0.7,"agree":2,"source":"consensus"},
		{"t_ms":500,"x":0.4,"y":0.6,"agree":1,"source":"nudenet"},
		{"t_ms":1000,"x":0.45,"y":0.65,"source":"vlm"}]}`)
	all, err := LoadContactPoints(p, 0)
	if err != nil {
		t.Fatal(err)
	}
	if len(all) != 3 || all[0].Ms != 500 || all[2].Ms != 1500 {
		t.Fatalf("want 3 points sorted by time, got %+v", all)
	}
	agreed, err := LoadContactPoints(p, 2)
	if err != nil {
		t.Fatal(err)
	}
	if len(agreed) != 1 || agreed[0].Ms != 1500 || agreed[0].X != 0.5 {
		t.Fatalf("min agree 2: got %+v", agreed)
	}
}

func TestLoadContactPointsRejectsBadInput(t *testing.T) {
	for name, body := range map[string]string{
		"version":  `{"version":2,"points":[]}`,
		"range":    `{"version":1,"points":[{"t_ms":0,"x":1.5,"y":0.5}]}`,
		"negative": `{"version":1,"points":[{"t_ms":-1,"x":0.5,"y":0.5}]}`,
		"not json": `nope`,
	} {
		if _, err := LoadContactPoints(writeTemp(t, body), 0); err == nil {
			t.Errorf("%s: want error", name)
		}
	}
	if _, err := LoadContactPoints(filepath.Join(t.TempDir(), "missing.json"), 0); err == nil {
		t.Error("missing file: want error")
	}
}
