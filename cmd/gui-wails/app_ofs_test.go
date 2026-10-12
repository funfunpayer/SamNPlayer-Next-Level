package main

import (
	"os"
	"path/filepath"
	"testing"
)

func TestEditScaleRangeOnVibrationAxis(t *testing.T) {
	dir := t.TempDir()
	path := filepath.Join(dir, "clip.funscript")
	body := []byte(`{
  "actions":[{"at":0,"pos":10},{"at":1000,"pos":90},{"at":2000,"pos":10}],
  "metadata":{
    "samn_axes":{
      "vibration":[{"at":0,"pos":80},{"at":1000,"pos":80},{"at":2000,"pos":80}],
      "suction":[{"at":0,"pos":20},{"at":2000,"pos":20}]
    }
  }
}`)
	if err := os.WriteFile(path, body, 0o644); err != nil {
		t.Fatal(err)
	}
	a := &App{}
	if _, err := a.LoadFunscript(path); err != nil {
		t.Fatal(err)
	}
	if err := a.EditScaleRange("vibration", 0, 1000, 0.5, false); err != nil {
		t.Fatal(err)
	}
	vib, err := a.GetScriptAxisActions("vibration")
	if err != nil {
		t.Fatal(err)
	}
	if len(vib) < 2 {
		t.Fatalf("vib=%+v", vib)
	}
	// 80 scaled ×0.5 around 50 → 65
	if vib[0].Pos != 65 || vib[1].Pos != 65 {
		t.Fatalf("vib after scale=%+v", vib)
	}
	gen, err := a.GetScriptAxisActions("general")
	if err != nil {
		t.Fatal(err)
	}
	if gen[0].Pos != 10 || gen[1].Pos != 90 {
		t.Fatalf("general must be untouched: %+v", gen)
	}
}

func TestEditDeleteRangeDefaultsToGeneral(t *testing.T) {
	dir := t.TempDir()
	path := filepath.Join(dir, "clip.funscript")
	body := []byte(`{"actions":[{"at":0,"pos":0},{"at":100,"pos":50},{"at":200,"pos":100},{"at":300,"pos":50}],"metadata":{}}`)
	if err := os.WriteFile(path, body, 0o644); err != nil {
		t.Fatal(err)
	}
	a := &App{}
	if _, err := a.LoadFunscript(path); err != nil {
		t.Fatal(err)
	}
	if err := a.EditDeleteRange("", 100, 200); err != nil {
		t.Fatal(err)
	}
	got, err := a.GetScriptAxisActions("general")
	if err != nil {
		t.Fatal(err)
	}
	if len(got) != 2 || got[0].At != 0 || got[1].At != 300 {
		t.Fatalf("got %+v", got)
	}
}
