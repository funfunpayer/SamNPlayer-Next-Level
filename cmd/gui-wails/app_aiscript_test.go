//go:build suite

package main

import (
	"bytes"
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/funfunpayer/SamNPlayer/funscript"
	"github.com/funfunpayer/SamNPlayer/generator"
	"github.com/funfunpayer/SamNPlayer/generator/aiscript"
)

func TestExportAIScriptImitation(t *testing.T) {
	dir := t.TempDir()
	scriptPath := filepath.Join(dir, "clip.funscript")
	body := []byte(`{"actions":[{"at":0,"pos":10},{"at":500,"pos":90},{"at":1000,"pos":20}],"metadata":{"quality_passed":true,"quality_score":0.8}}`)
	if err := os.WriteFile(scriptPath, body, 0o644); err != nil {
		t.Fatal(err)
	}
	_ = generator.DefaultRoiDatasetDir()
	a := &App{}
	res, err := a.ExportAIScriptImitation(scriptPath, filepath.Join(dir, "clip.mp4"), 10, 20, 80, 90)
	if err != nil {
		t.Fatal(err)
	}
	if res.Path == "" {
		t.Fatal("empty path")
	}
	if _, err := os.Stat(res.Path); err != nil {
		t.Fatal(err)
	}
	raw, err := os.ReadFile(res.Path)
	if err != nil {
		t.Fatal(err)
	}
	if len(raw) < 20 {
		t.Fatalf("short file: %s", raw)
	}
	if !bytes.Contains(raw, []byte(`"tipX": 10`)) && !bytes.Contains(raw, []byte(`"tipX":10`)) {
		t.Fatalf("expected tipX in sample: %s", raw)
	}
	_ = funscript.Action{}
}

func TestDraftAndKeepAIScript(t *testing.T) {
	dir := t.TempDir()
	scriptPath := filepath.Join(dir, "clip.funscript")
	body := []byte(`{"actions":[{"at":0,"pos":10},{"at":400,"pos":90},{"at":800,"pos":30}],"metadata":{"quality_passed":true,"quality_score":0.75}}`)
	if err := os.WriteFile(scriptPath, body, 0o644); err != nil {
		t.Fatal(err)
	}
	a := &App{}
	if _, err := a.ExportAIScriptImitation(scriptPath, filepath.Join(dir, "clip.mp4"), 0, 0, 0, 0); err != nil {
		t.Fatal(err)
	}
	st := a.AIScriptWriterStatus()
	if !st.Available {
		t.Fatalf("status not available: %+v", st)
	}
	if st.Stage != "S2-imitation" {
		t.Fatalf("stage=%q", st.Stage)
	}
	draft, err := a.DraftAIScript(filepath.Join(dir, "clip.mp4"), 0, 0, 0, 0, 1600)
	if err != nil {
		t.Fatal(err)
	}
	if len(draft.Actions) < 2 {
		t.Fatal("no draft actions")
	}
	if draft.Engine != "imitation-stretch" {
		t.Fatalf("engine=%q", draft.Engine)
	}
	if draft.QDScore == nil {
		t.Fatal("expected QD score on draft")
	}
	video := filepath.Join(dir, "kept.mp4")
	if err := os.WriteFile(video, []byte("fake"), 0o644); err != nil {
		t.Fatal(err)
	}
	kept, err := a.KeepAIScriptDraft(video, draft.Actions)
	if err != nil {
		t.Fatal(err)
	}
	if kept.Path == "" {
		t.Fatal("empty keep path")
	}
	if _, err := os.Stat(kept.Path); err != nil {
		t.Fatal(err)
	}
	funPath := strings.TrimSuffix(video, filepath.Ext(video)) + ".funscript"
	if _, err := os.Stat(funPath); err != nil {
		t.Fatal(err)
	}
	_ = aiscript.Action{}
}
