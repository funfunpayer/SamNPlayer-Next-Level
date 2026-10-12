package aiscript

import (
	"encoding/json"
	"os"
	"path/filepath"
	"testing"
)

func TestExportImitationSample(t *testing.T) {
	dir := t.TempDir()
	path, err := ExportImitationSample(dir, ImitationSample{
		VideoPath: "clip.mp4",
		Actions: []Action{
			{At: 0, Pos: 10},
			{At: 500, Pos: 90},
			{At: 1000, Pos: 20},
		},
		QDPassed: boolPtr(true),
	})
	if err != nil {
		t.Fatal(err)
	}
	raw, err := os.ReadFile(path)
	if err != nil {
		t.Fatal(err)
	}
	var got ImitationSample
	if err := json.Unmarshal(raw, &got); err != nil {
		t.Fatal(err)
	}
	if len(got.Actions) != 3 || got.Engine != "csrt" {
		t.Fatalf("got %+v", got)
	}
	if filepath.Dir(path) != dir {
		t.Fatalf("path %s not under %s", path, dir)
	}
}

func TestExportImitationSampleRejectsShort(t *testing.T) {
	_, err := ExportImitationSample(t.TempDir(), ImitationSample{
		Actions: []Action{{At: 0, Pos: 50}},
	})
	if err == nil {
		t.Fatal("expected error")
	}
}

func boolPtr(v bool) *bool { return &v }
