package main

import (
	"encoding/json"
	"os"
	"path/filepath"
	"testing"

	"github.com/funfunpayer/SamNPlayer/samn"
)

func TestOptimizeLoadedForNeo2FromFunscript(t *testing.T) {
	dir := t.TempDir()
	path := filepath.Join(dir, "imported.funscript")
	body := map[string]any{
		"actions": []map[string]any{
			{"at": 0, "pos": 0},
			{"at": 100, "pos": 20},
			{"at": 4000, "pos": 80},
			{"at": 4100, "pos": 100},
		},
		"metadata": map[string]any{"creator": "other-tool"},
	}
	raw, _ := json.Marshal(body)
	if err := os.WriteFile(path, raw, 0o644); err != nil {
		t.Fatal(err)
	}

	a := NewApp()
	if _, err := a.LoadFunscript(path); err != nil {
		t.Fatal(err)
	}

	res, err := a.OptimizeLoadedForNeo2(true)
	if err != nil {
		t.Fatal(err)
	}
	if !res.Baked || !res.ContactOn || !samn.IsSamnPath(res.Path) {
		t.Fatalf("result %+v", res)
	}
	doc, err := samn.Load(res.Path)
	if err != nil {
		t.Fatal(err)
	}
	if len(doc.General) < 2 {
		t.Fatalf("general empty")
	}
	if len(doc.Vibration) < 2 || len(doc.Suction) < 2 {
		t.Fatalf("neo axes missing vib=%d suc=%d", len(doc.Vibration), len(doc.Suction))
	}
	if !doc.Recipe.ContactVibration {
		t.Fatal("contact recipe expected")
	}
}
