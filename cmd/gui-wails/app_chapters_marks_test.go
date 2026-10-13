package main

import (
	"encoding/json"
	"os"
	"path/filepath"
	"testing"

	"github.com/funfunpayer/SamNPlayer/funscript"
)

func TestGetSaveScriptChapterMarksFunscript(t *testing.T) {
	dir := t.TempDir()
	path := filepath.Join(dir, "clip.funscript")
	body := []byte(`{"actions":[{"at":0,"pos":10},{"at":1000,"pos":90}],"metadata":{}}`)
	if err := os.WriteFile(path, body, 0o644); err != nil {
		t.Fatal(err)
	}
	a := &App{}
	if _, err := a.LoadFunscript(path); err != nil {
		t.Fatal(err)
	}
	if err := a.SaveScriptChapterMarks([]funscript.ChapterMark{
		{Name: "Build", StartTime: 100, EndTime: 400},
		{Name: "Peak", StartTime: 500, EndTime: 900},
	}); err != nil {
		t.Fatal(err)
	}
	got, err := a.GetScriptChapterMarks()
	if err != nil {
		t.Fatal(err)
	}
	if len(got) != 2 || got[0].Name != "Build" || got[0].StartTime != 100 || got[0].EndTime != 400 {
		t.Fatalf("got %#v", got)
	}
	raw, err := os.ReadFile(path)
	if err != nil {
		t.Fatal(err)
	}
	var doc map[string]any
	if err := json.Unmarshal(raw, &doc); err != nil {
		t.Fatal(err)
	}
	meta, _ := doc["metadata"].(map[string]any)
	if meta == nil || meta["chapters"] == nil {
		t.Fatalf("metadata.chapters missing: %s", raw)
	}
}
