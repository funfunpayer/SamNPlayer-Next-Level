package main

import (
	"os"
	"path/filepath"
	"testing"

	"github.com/funfunpayer/SamNPlayer/funscript"
)

// D4: companion axis files beside the loaded script are readable lanes.
func TestGetScriptAxisActionsReadsCompanionFiles(t *testing.T) {
	dir := t.TempDir()
	path := filepath.Join(dir, "clip.funscript")
	if err := os.WriteFile(path, []byte(`{"actions":[{"at":0,"pos":0},{"at":1000,"pos":100}]}`), 0o644); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(dir, "clip.twist.funscript"),
		[]byte(`{"actions":[{"at":0,"pos":30},{"at":1000,"pos":70}]}`), 0o644); err != nil {
		t.Fatal(err)
	}
	a := &App{}
	if _, err := a.LoadFunscript(path); err != nil {
		t.Fatal(err)
	}
	twist, err := a.GetScriptAxisActions("Twist")
	if err != nil || len(twist) != 2 || twist[1].Pos != 70 {
		t.Fatalf("twist = %+v, %v", twist, err)
	}
	surge, err := a.GetScriptAxisActions("surge")
	if err != nil || surge != nil {
		t.Fatalf("no surge file → nil, nil; got %+v, %v", surge, err)
	}
	if err := a.SaveScriptAxisActions("twist", []funscript.Action{{At: 0, Pos: 1}, {At: 10, Pos: 2}}); err == nil {
		t.Fatal("companion axes are read-only")
	}
	if _, err := a.GetScriptAxisActions("banana"); err == nil {
		t.Fatal("unknown axis still errors")
	}
}

func TestCompanionAxesForPlayback(t *testing.T) {
	dir := t.TempDir()
	path := filepath.Join(dir, "clip.funscript")
	write := func(name, body string) {
		if err := os.WriteFile(filepath.Join(dir, name), []byte(body), 0o644); err != nil {
			t.Fatal(err)
		}
	}
	write("clip.funscript", `{"actions":[{"at":0,"pos":0},{"at":1000,"pos":100}]}`)
	write("clip.roll.funscript", `{"actions":[{"at":0,"pos":40},{"at":1000,"pos":60}]}`)
	write("clip.pitch.funscript", `broken`)
	got := companionAxesFor(path)
	if len(got) != 1 || len(got["roll"]) != 2 {
		t.Fatalf("roll only (pitch unreadable, others missing): %+v", got)
	}
	if len(companionAxesFor("")) != 0 {
		t.Fatal("no script → no axes")
	}
}
