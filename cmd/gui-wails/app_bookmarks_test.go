package main

import (
	"encoding/json"
	"os"
	"path/filepath"
	"testing"

	"github.com/funfunpayer/SamNPlayer/funscript"
)

func TestGetSaveScriptBookmarksFunscript(t *testing.T) {
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
	if err := a.SaveScriptBookmarks([]funscript.Bookmark{
		{Name: "A", Time: 100},
		{Name: "B", Time: 500},
	}); err != nil {
		t.Fatal(err)
	}
	got, err := a.GetScriptBookmarks()
	if err != nil {
		t.Fatal(err)
	}
	if len(got) != 2 || got[0].Name != "A" || got[0].Time != 100 || got[1].Name != "B" {
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
	if meta == nil || meta["bookmarks"] == nil {
		t.Fatalf("metadata.bookmarks missing: %s", raw)
	}
}
