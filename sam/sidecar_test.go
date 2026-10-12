package sam

import (
	"os"
	"path/filepath"
	"testing"

	"github.com/funfunpayer/SamNPlayer/funscript"
)

func TestWriteAndLoadSidecar(t *testing.T) {
	dir := t.TempDir()
	fsPath := filepath.Join(dir, "clip.funscript")
	raw := []byte(`{"actions":[
		{"at":0,"pos":20},{"at":500,"pos":90},{"at":1000,"pos":20}
	],"metadata":{"profile":"tj",
		"device_recipe":{"sync":"suction_position","min_suction":0.2,
			"tick_ms":50,"max_speed":0.5,"smoothing":0.22,"contact_vibration":true}}}`)
	if err := os.WriteFile(fsPath, raw, 0644); err != nil {
		t.Fatal(err)
	}
	fs, err := funscript.Load(fsPath)
	if err != nil {
		t.Fatal(err)
	}
	if err := WriteEnrichedSidecar(fsPath, fs); err != nil {
		t.Fatal(err)
	}
	samPath := SidecarPath(fsPath)
	if _, err := os.Stat(samPath); err != nil {
		t.Fatalf("sidecar fehlt: %v", err)
	}
	s, err := LoadSidecarIfPresent(fsPath)
	if err != nil || s == nil {
		t.Fatalf("LoadSidecarIfPresent: %v %v", err, s)
	}
	if s.Metadata.Source != "funscript-sidecar" {
		t.Errorf("Source=%q", s.Metadata.Source)
	}
	var maxI float64
	for _, f := range s.Frames {
		if f.Motion.Intensity > maxI {
			maxI = f.Motion.Intensity
		}
	}
	if maxI < 0.5 {
		t.Errorf("erwartete Contact-Intensity im Sidecar, max=%.3f", maxI)
	}
}

func TestLoadSidecarMissing(t *testing.T) {
	s, err := LoadSidecarIfPresent(filepath.Join(t.TempDir(), "x.funscript"))
	if err != nil || s != nil {
		t.Fatalf("erwartet nil,nil got %v %v", s, err)
	}
}
