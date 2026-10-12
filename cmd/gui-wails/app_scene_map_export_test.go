//go:build suite

package main

import (
	"os"
	"path/filepath"
	"testing"

	"github.com/funfunpayer/SamNPlayer/funscript"
	"github.com/funfunpayer/SamNPlayer/generator"
	"github.com/funfunpayer/SamNPlayer/samn"
)

func TestResolveSamnForLearning(t *testing.T) {
	got, err := resolveSamnForLearning("clip.mp4")
	if err != nil {
		t.Fatal(err)
	}
	if got != samn.CompanionSamnPath("clip.mp4") {
		t.Fatalf("got %q", got)
	}
	got, err = resolveSamnForLearning("/tmp/x.SAMN")
	if err != nil || got != "/tmp/x.SAMN" {
		t.Fatalf("got %q err %v", got, err)
	}
	if _, err := resolveSamnForLearning("  "); err == nil {
		t.Fatal("expected empty path error")
	}
}

func TestExportSceneMapLearningRequiresOptIn(t *testing.T) {
	dir := t.TempDir()
	store := &settingsStore{path: filepath.Join(dir, "settings.json"), data: map[string]any{}}
	a := &App{settings: store}
	samnPath := writeMinimalSceneMapSamn(t, dir)
	if _, err := a.ExportSceneMapLearning(samnPath); err == nil {
		t.Fatal("expected opt-in error")
	} else if err != generator.ErrLearningExportNotOptedIn {
		t.Fatalf("want ErrLearningExportNotOptedIn, got %v", err)
	}
	if err := a.SetSetting(prefCollectLearningData, true); err != nil {
		t.Fatal(err)
	}
	outRoot := filepath.Join(dir, "dataset")
	if err := a.SetSetting(prefRoiDatasetDir, outRoot); err != nil {
		t.Fatal(err)
	}
	res, err := a.ExportSceneMapLearning(samnPath)
	if err != nil {
		t.Fatal(err)
	}
	if res.OutDir == "" || res.Windows < 1 {
		t.Fatalf("unexpected result: %+v", res)
	}
	if err := a.DeleteSceneMapLearningData(); err != nil {
		t.Fatal(err)
	}
	learn := filepath.Join(outRoot, generator.SceneMapLearningSubdir)
	if _, err := os.Stat(learn); !os.IsNotExist(err) {
		t.Fatalf("learning dir should be gone, err=%v", err)
	}
}

func writeMinimalSceneMapSamn(t *testing.T, dir string) string {
	t.Helper()
	score := make([]byte, 16*9)
	score[0] = 200
	doc := &samn.Document{
		Version: samn.CurrentVersion,
		Kind:    samn.Kind,
		General: []samn.Point{{At: 0, Pos: 50}, {At: 1000, Pos: 60}},
		SceneMap: &funscript.SceneMapData{
			Version: 1,
			Grid:    funscript.SceneMapGrid{Cols: 16, Rows: 9},
			Video:   funscript.SceneMapVideo{Width: 640, Height: 360, DurationMs: 8000},
			Windows: []funscript.SceneMapWindow{{
				StartMs: 0, EndMs: 8000, TempoHz: 1.0,
				ScoreB64: funscript.EncodeSceneMapScore(score), Chosen: 0,
			}},
			Marks: []funscript.SceneMapMark{{
				ID: "m1", Kind: "exclude", FromMs: 0, ToMs: 8000,
				Rect: []int{10, 10, 80, 80}, Author: "user",
			}},
		},
	}
	path := filepath.Join(dir, "clip.samn")
	if err := samn.Save(path, doc); err != nil {
		t.Fatal(err)
	}
	return path
}
