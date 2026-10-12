package main

import (
	"os"
	"path/filepath"
	"testing"

	"github.com/funfunpayer/SamNPlayer/funscript"
)

func TestBakeScriptOffsetMovesPointsAndResetsOffset(t *testing.T) {
	t.Setenv("XDG_CONFIG_HOME", t.TempDir())
	path := filepath.Join(t.TempDir(), "s.funscript")
	body := `{"actions":[{"at":0,"pos":10},{"at":500,"pos":90},{"at":1000,"pos":10}],` +
		`"metadata":{"bookmarks":[{"name":"b","time":800}],"chapters":[{"name":"c","startTime":100,"endTime":900}]}}`
	if err := os.WriteFile(path, []byte(body), 0o644); err != nil {
		t.Fatal(err)
	}
	a := NewApp()
	if _, err := a.LoadFunscript(path); err != nil {
		t.Fatal(err)
	}
	if res, err := a.BakeScriptOffset(); err != nil || res.ShiftedMs != 0 {
		t.Fatalf("offset 0 must be a no-op: %+v %v", res, err)
	}
	a.SetScriptOffset(250)
	res, err := a.BakeScriptOffset()
	if err != nil {
		t.Fatal(err)
	}
	if res.ShiftedMs != 250 || res.Dropped != 0 {
		t.Fatalf("result %+v", res)
	}
	if a.GetScriptOffset() != 0 {
		t.Fatalf("offset must be 0 after baking, got %d", a.GetScriptOffset())
	}
	disk, err := funscript.Load(path)
	if err != nil {
		t.Fatal(err)
	}
	if disk.Actions[0].At != 250 || disk.Actions[2].At != 1250 {
		t.Fatalf("file actions not shifted: %+v", disk.Actions)
	}
	bms, _ := funscript.LoadBookmarks(path)
	if len(bms) != 1 || bms[0].Time != 1050 {
		t.Fatalf("bookmarks not shifted: %+v", bms)
	}
	// Reloading the script must not bring the old offset back.
	if _, err := a.LoadFunscript(path); err != nil {
		t.Fatal(err)
	}
	if a.GetScriptOffset() != 0 {
		t.Fatalf("offset came back after reload: %d", a.GetScriptOffset())
	}

	// Negative offset: points before 0 are dropped and counted.
	a.SetScriptOffset(-500)
	res, err = a.BakeScriptOffset()
	if err != nil {
		t.Fatal(err)
	}
	if res.Dropped != 1 { // the action at 250 → -250
		t.Fatalf("want 1 dropped, got %+v", res)
	}
	disk, _ = funscript.Load(path)
	if len(disk.Actions) != 2 || disk.Actions[0].At != 250 {
		t.Fatalf("after -500: %+v", disk.Actions)
	}
}
