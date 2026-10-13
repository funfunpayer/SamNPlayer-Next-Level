package main

import (
	"os"
	"path/filepath"
	"testing"
)

func writeSceneProposalsJSON(t *testing.T, dir, name, body string) string {
	t.Helper()
	p := filepath.Join(dir, name)
	if err := os.WriteFile(p, []byte(body), 0o644); err != nil {
		t.Fatal(err)
	}
	return p
}

const sampleSceneJSON = `{"version":1,"tool":"scene_roles","width":1280,"height":720,"windows":[],
 "proposals":[
  {"t_ms":4000,"start_ms":0,"end_ms":8000,"scene_type":"blowjob","confidence":0.8,
   "primary":{"x":560,"y":400,"w":160,"h":80,"score":0.8,"class":"mouth"},
   "partner":{"x":560,"y":480,"w":80,"h":240,"score":0.8,"class":"penis"}},
  {"t_ms":14000,"start_ms":10000,"end_ms":18000,"scene_type":"titjob","confidence":0.55,
   "primary":{"x":480,"y":320,"w":240,"h":240,"score":0.55,"class":"breasts"}}]}`

func TestSceneProposalsPathBesideVideo(t *testing.T) {
	if got := SceneProposalsPathBesideVideo("/tmp/clip.mp4"); got != "/tmp/clip.scene.json" {
		t.Fatalf("got %q", got)
	}
	if SceneProposalsPathBesideVideo("  ") != "" {
		t.Fatal("empty path")
	}
}

func TestLoadSceneProposalAt(t *testing.T) {
	a := &App{}
	p := writeSceneProposalsJSON(t, t.TempDir(), "clip.scene.json", sampleSceneJSON)
	got, err := a.LoadSceneProposalAt(p, 2000)
	if err != nil {
		t.Fatal(err)
	}
	if !got.Found || got.Count != 2 || got.Proposal.SceneType != "blowjob" {
		t.Fatalf("got %+v", got)
	}
	if got.RegionClass != "mouth" {
		t.Fatalf("regionClass=%q", got.RegionClass)
	}
	if got.Proposal.Partner == nil || got.Proposal.Partner.Class != "penis" {
		t.Fatalf("partner %+v", got.Proposal.Partner)
	}
	got, err = a.LoadSceneProposalAt(p, 12000)
	if err != nil || !got.Found || got.Proposal.SceneType != "titjob" || got.RegionClass != "breasts" {
		t.Fatalf("At(12000)=%+v err=%v", got, err)
	}
	if _, err := a.LoadSceneProposalAt("", 0); err == nil {
		t.Fatal("empty path want error")
	}
}

func TestLoadSceneProposalsBesideVideo(t *testing.T) {
	a := &App{}
	dir := t.TempDir()
	video := filepath.Join(dir, "clip.mp4")
	if err := os.WriteFile(video, []byte("x"), 0o644); err != nil {
		t.Fatal(err)
	}
	missing, err := a.LoadSceneProposalsBesideVideo(video, 0)
	if err != nil || missing.Found {
		t.Fatalf("missing: %+v err=%v", missing, err)
	}
	if missing.Path != filepath.Join(dir, "clip.scene.json") {
		t.Fatalf("path %q", missing.Path)
	}
	writeSceneProposalsJSON(t, dir, "clip.scene.json", sampleSceneJSON)
	got, err := a.LoadSceneProposalsBesideVideo(video, 0)
	if err != nil || !got.Found || got.Proposal.SceneType != "blowjob" {
		t.Fatalf("beside: %+v err=%v", got, err)
	}
}
