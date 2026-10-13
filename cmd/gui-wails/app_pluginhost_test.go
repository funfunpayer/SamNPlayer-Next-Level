package main

import (
	"os"
	"path/filepath"
	"testing"

	"github.com/funfunpayer/SamNPlayer/pluginhost"
)

func TestPluginsDirHintWhenUnset(t *testing.T) {
	a := NewApp()
	dir := a.PluginsDir()
	if dir == "" {
		t.Fatal("PluginsDir must return a path or hint")
	}
}

func TestListInstalledPluginsEmpty(t *testing.T) {
	a := NewApp()
	packs, err := a.ListInstalledPlugins()
	if err != nil {
		t.Fatal(err)
	}
	_ = packs
}

func TestDiscoverInstalledPackRoundTrip(t *testing.T) {
	root := t.TempDir()
	packDir := filepath.Join(root, "sample_pack")
	if err := os.MkdirAll(packDir, 0o755); err != nil {
		t.Fatal(err)
	}
	raw := `{
  "apiVersion": 1,
  "id": "sample_pack",
  "name": "Sample Pack",
  "version": "0.1.0",
  "entry": {"kind": "asset_pack", "webRoot": "web"}
}`
	if err := os.WriteFile(filepath.Join(packDir, pluginhost.ManifestFile), []byte(raw), 0o644); err != nil {
		t.Fatal(err)
	}
	packs, err := pluginhost.DiscoverPacks(root)
	if err != nil || len(packs) != 1 {
		t.Fatalf("discover: %v packs=%d", err, len(packs))
	}
	if packs[0].Manifest.ID != "sample_pack" {
		t.Fatalf("%+v", packs[0])
	}
}
