package pluginhost

import (
	"os"
	"path/filepath"
	"testing"
)

func TestLoadManifestAndDiscover(t *testing.T) {
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
  "entry": {
    "kind": "asset_pack",
    "webRoot": "web",
    "assetsRoot": "assets"
  }
}`
	if err := os.WriteFile(filepath.Join(packDir, ManifestFile), []byte(raw), 0o644); err != nil {
		t.Fatal(err)
	}
	m, err := LoadManifest(packDir)
	if err != nil {
		t.Fatal(err)
	}
	if m.ID != "sample_pack" || m.APIVersion != 1 {
		t.Fatalf("manifest %+v", m)
	}
	packs, err := DiscoverPacks(root)
	if err != nil || len(packs) != 1 {
		t.Fatalf("discover: %v packs=%d", err, len(packs))
	}
	got, ok, err := FindPackByID(root, "sample_pack")
	if err != nil || !ok || got.Manifest.Version != "0.1.0" {
		t.Fatalf("find: ok=%v err=%v got=%+v", ok, err, got)
	}
}

func TestRejectBadAPIVersion(t *testing.T) {
	dir := t.TempDir()
	raw := `{"apiVersion":99,"id":"x","name":"X","version":"1","entry":{"kind":"asset_pack"}}`
	if err := os.WriteFile(filepath.Join(dir, ManifestFile), []byte(raw), 0o644); err != nil {
		t.Fatal(err)
	}
	if _, err := LoadManifest(dir); err == nil {
		t.Fatal("expected apiVersion error")
	}
}

func TestInstallPackDir(t *testing.T) {
	src := t.TempDir()
	raw := `{
  "apiVersion": 1,
  "id": "sample_pack",
  "name": "Sample Pack",
  "version": "0.2.0",
  "entry": {"kind": "asset_pack", "webRoot": "web"}
}`
	if err := os.WriteFile(filepath.Join(src, ManifestFile), []byte(raw), 0o644); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(src, "note.txt"), []byte("hi"), 0o644); err != nil {
		t.Fatal(err)
	}
	destRoot := t.TempDir()
	pack, err := InstallPackDir(destRoot, src)
	if err != nil {
		t.Fatal(err)
	}
	if pack.Manifest.Version != "0.2.0" {
		t.Fatalf("%+v", pack)
	}
	if _, err := os.Stat(filepath.Join(pack.Root, "note.txt")); err != nil {
		t.Fatal(err)
	}
}

func TestStatusReflectsPack(t *testing.T) {
	s := NewSlot()
	s.SetPluginsDir("/tmp/plugins")
	st := s.Status()
	if st.PackFound || st.Stage != Stage {
		t.Fatalf("%+v", st)
	}
	p := &Pack{
		Root: "/tmp/plugins/sample_pack",
		Manifest: Manifest{
			APIVersion: 1,
			ID:         "sample_pack",
			Name:       "Sample Pack",
			Version:    "0.1.0",
			Entry:      ManifestEntry{Kind: "asset_pack"},
		},
	}
	s.BindPack(p)
	st = s.Status()
	if !st.PackFound || st.PackVersion != "0.1.0" {
		t.Fatalf("%+v", st)
	}
	if err := s.Enable(); err != nil {
		t.Fatal(err)
	}
	st = s.Status()
	if !st.Running || st.Message == "" {
		t.Fatalf("%+v", st)
	}
}

func TestInstallPackDirRejectsSanitizedIDCollision(t *testing.T) {
	root := t.TempDir()
	writePack := func(dir, id, note string) {
		t.Helper()
		if err := os.MkdirAll(dir, 0o755); err != nil {
			t.Fatal(err)
		}
		raw := []byte(`{"apiVersion":1,"id":"` + id + `","name":"Pack","version":"1","entry":{"kind":"asset_pack"}}`)
		if err := os.WriteFile(filepath.Join(dir, ManifestFile), raw, 0o644); err != nil {
			t.Fatal(err)
		}
		if err := os.WriteFile(filepath.Join(dir, "note.txt"), []byte(note), 0o644); err != nil {
			t.Fatal(err)
		}
	}

	first := filepath.Join(t.TempDir(), "first")
	second := filepath.Join(t.TempDir(), "second")
	writePack(first, "foo/bar", "first")
	writePack(second, "foo_bar", "second")

	installed, err := InstallPackDir(root, first)
	if err != nil {
		t.Fatal(err)
	}
	if _, err := InstallPackDir(root, second); err == nil {
		t.Fatal("expected sanitized plugin ID collision to be rejected")
	}
	got, err := os.ReadFile(filepath.Join(installed.Root, "note.txt"))
	if err != nil {
		t.Fatal(err)
	}
	if string(got) != "first" {
		t.Fatalf("existing pack was modified: %q", got)
	}
}

func TestInstallPackDirAllowsSameIDUpgrade(t *testing.T) {
	root := t.TempDir()
	write := func(dir, version string) {
		t.Helper()
		raw := []byte(`{"apiVersion":1,"id":"same/id","name":"Pack","version":"` + version + `","entry":{"kind":"asset_pack"}}`)
		if err := os.WriteFile(filepath.Join(dir, ManifestFile), raw, 0o644); err != nil {
			t.Fatal(err)
		}
	}
	first, second := t.TempDir(), t.TempDir()
	write(first, "1")
	write(second, "2")
	if _, err := InstallPackDir(root, first); err != nil {
		t.Fatal(err)
	}
	upgraded, err := InstallPackDir(root, second)
	if err != nil {
		t.Fatal(err)
	}
	if upgraded.Manifest.Version != "2" {
		t.Fatalf("upgrade version=%q", upgraded.Manifest.Version)
	}
}

func TestInstallPackDirRejectsSourceDestinationOverlap(t *testing.T) {
	root := t.TempDir()
	dest := filepath.Join(root, sanitizeID("same/id"))
	if err := os.MkdirAll(dest, 0o755); err != nil {
		t.Fatal(err)
	}
	raw := []byte(`{"apiVersion":1,"id":"same/id","name":"Pack","version":"1","entry":{"kind":"asset_pack"}}`)
	if err := os.WriteFile(filepath.Join(dest, ManifestFile), raw, 0o644); err != nil {
		t.Fatal(err)
	}
	marker := filepath.Join(dest, "keep.txt")
	if err := os.WriteFile(marker, []byte("keep"), 0o644); err != nil {
		t.Fatal(err)
	}

	if _, err := InstallPackDir(root, dest); err == nil {
		t.Fatal("expected same source and destination to be rejected")
	}
	if got, err := os.ReadFile(marker); err != nil || string(got) != "keep" {
		t.Fatalf("installed pack was modified: got=%q err=%v", got, err)
	}

	nested := filepath.Join(dest, "nested")
	if err := os.MkdirAll(nested, 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(nested, ManifestFile), raw, 0o644); err != nil {
		t.Fatal(err)
	}
	if _, err := InstallPackDir(root, nested); err == nil {
		t.Fatal("expected nested source to be rejected")
	}
	if _, err := os.Stat(filepath.Join(nested, ManifestFile)); err != nil {
		t.Fatalf("nested source was removed: %v", err)
	}
}
