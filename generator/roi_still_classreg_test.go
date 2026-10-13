package generator

import (
	"encoding/json"
	"os"
	"path/filepath"
	"testing"
)

func TestEnsureClassIDsRegistersCanonicalLabels(t *testing.T) {
	dir := t.TempDir()
	if err := os.WriteFile(filepath.Join(dir, "classes.json"), []byte(`{"face":0}`), 0o644); err != nil {
		t.Fatal(err)
	}
	reg, err := EnsureClassIDs(dir, []string{"Face", "glans", "mouth", "nipples"})
	if err != nil {
		t.Fatal(err)
	}
	if _, ok := reg["face"]; !ok {
		t.Fatalf("face missing: %+v", reg)
	}
	for _, want := range []string{"glans", "mouth", "nipples"} {
		if _, ok := reg[want]; !ok {
			t.Fatalf("%s not registered: %+v", want, reg)
		}
	}
	raw, err := os.ReadFile(filepath.Join(dir, "classes.json"))
	if err != nil {
		t.Fatal(err)
	}
	var disk map[string]int
	if err := json.Unmarshal(raw, &disk); err != nil {
		t.Fatal(err)
	}
	if len(disk) < 4 {
		t.Fatalf("expected ≥4 classes on disk, got %v", disk)
	}
}
