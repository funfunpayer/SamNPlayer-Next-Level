package main

import (
	"os"
	"path/filepath"
	"testing"

	"github.com/funfunpayer/SamNPlayer/funscript"
	"github.com/funfunpayer/SamNPlayer/samn"
)

func TestLoadSamnAndExportFunscript(t *testing.T) {
	dir := t.TempDir()
	fsPath := filepath.Join(dir, "clip7776_tf.funscript")
	src := filepath.Join("frontend", "test", "fixtures", "clip7776_tf.funscript")
	data, err := os.ReadFile(src)
	if err != nil {
		t.Skip("clip7776 fixture missing:", err)
	}
	if err := os.WriteFile(fsPath, data, 0o644); err != nil {
		t.Fatal(err)
	}

	a := NewApp()
	info, err := a.LoadFunscript(fsPath)
	if err != nil {
		t.Fatal(err)
	}
	if info.ActionCount < 2 {
		t.Fatalf("actions: %d", info.ActionCount)
	}

	out, err := a.BakeNeoAxesOnLoaded()
	if err != nil {
		t.Fatal(err)
	}
	if !samn.IsSamnPath(out) {
		t.Fatalf("expected .samn path, got %s", out)
	}
	info2, err := a.LoadFunscript(out)
	if err != nil {
		t.Fatal(err)
	}
	if !info2.NativeFormat || !info2.HasNeoAxes {
		t.Fatalf("info: %+v", info2)
	}
	if info2.PlaybackSource != funscript.PlaybackSourceAxes {
		t.Fatalf("source=%s", info2.PlaybackSource)
	}

	exported, err := a.ExportLoadedFunscript("")
	if err != nil {
		t.Fatal(err)
	}
	raw, err := os.ReadFile(exported)
	if err != nil {
		t.Fatal(err)
	}
	if containsBytes(raw, []byte("samn_axes")) {
		t.Fatal("export must not embed samn_axes")
	}
}

func containsBytes(b, sub []byte) bool {
	return len(sub) == 0 || (len(b) >= len(sub) && stringIndex(string(b), string(sub)) >= 0)
}

func stringIndex(s, sub string) int {
	for i := 0; i+len(sub) <= len(s); i++ {
		if s[i:i+len(sub)] == sub {
			return i
		}
	}
	return -1
}
