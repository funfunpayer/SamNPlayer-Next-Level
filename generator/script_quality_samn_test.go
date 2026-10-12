package generator_test

import (
	"os"
	"path/filepath"
	"testing"

	"github.com/funfunpayer/SamNPlayer/generator"
	"github.com/funfunpayer/SamNPlayer/samn"
)

func TestScriptQualityAcceptsSamnGeneral(t *testing.T) {
	dir := t.TempDir()
	path := filepath.Join(dir, "clip.samn")
	doc := &samn.Document{
		Version: samn.CurrentVersion,
		Kind:    samn.Kind,
		General: []samn.Point{{At: 0, Pos: 10}, {At: 1000, Pos: 90}, {At: 2000, Pos: 20}},
	}
	if err := samn.Save(path, doc); err != nil {
		t.Fatal(err)
	}
	got, err := generator.ScriptQuality(path)
	if err != nil {
		t.Fatal(err)
	}
	if got.Score <= 0 && len(got.Warnings) == 0 {
		// Either a score or warnings is fine; empty+error would be wrong.
		t.Fatalf("unexpected empty quality result: %+v", got)
	}
	_ = os.Remove(path)
}
