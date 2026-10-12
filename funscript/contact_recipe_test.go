package funscript

import (
	"os"
	"path/filepath"
	"testing"
)

func TestDefaultSyncForProfile(t *testing.T) {
	if got := DefaultSyncForProfile("tj"); got != SyncSuctionPosition.String() {
		t.Fatalf("tj: got %q", got)
	}
	if got := DefaultSyncForProfile("standard"); got != SyncIndependent.String() {
		t.Fatalf("standard: got %q want independent", got)
	}
	if got := DefaultSyncForProfile("autotune"); got != SyncIndependent.String() {
		t.Fatalf("autotune: got %q want independent", got)
	}
}

func TestSaveContactRecipeStrokeEmptySyncStaysIndependent(t *testing.T) {
	dir := t.TempDir()
	path := filepath.Join(dir, "stroke.funscript")
	// Older stroke scripts often omit sync entirely.
	body := `{"actions":[{"at":0,"pos":20},{"at":800,"pos":90}],"metadata":{"profile":"standard","device_recipe":{"min_suction":0.1,"tick_ms":50}}}`
	if err := os.WriteFile(path, []byte(body), 0644); err != nil {
		t.Fatal(err)
	}
	if err := SaveContactRecipe(path, true, 0.5, "soft"); err != nil {
		t.Fatalf("SaveContactRecipe: %v", err)
	}
	s, err := Load(path)
	if err != nil {
		t.Fatal(err)
	}
	dr := s.Metadata.DeviceRecipe
	if dr == nil {
		t.Fatal("device_recipe fehlt")
	}
	if dr.Sync != SyncIndependent.String() {
		t.Fatalf("stroke empty sync became %q — must stay independent (not suction_position)", dr.Sync)
	}
	if !dr.ContactVibration {
		t.Error("ContactVibration nicht gesetzt")
	}
}

func TestSaveContactRecipeTjEmptySyncUsesSuctionPosition(t *testing.T) {
	dir := t.TempDir()
	path := filepath.Join(dir, "tj.funscript")
	body := `{"actions":[{"at":0,"pos":20},{"at":800,"pos":90}],"metadata":{"profile":"tj","device_recipe":{"min_suction":0.2}}}`
	if err := os.WriteFile(path, []byte(body), 0644); err != nil {
		t.Fatal(err)
	}
	if err := SaveContactRecipe(path, true, 0.5, "soft"); err != nil {
		t.Fatalf("SaveContactRecipe: %v", err)
	}
	s, err := Load(path)
	if err != nil {
		t.Fatal(err)
	}
	if s.Metadata.DeviceRecipe.Sync != SyncSuctionPosition.String() {
		t.Fatalf("tj empty sync: got %q", s.Metadata.DeviceRecipe.Sync)
	}
}

func TestSaveContactRecipeRoundtrip(t *testing.T) {
	dir := t.TempDir()
	path := filepath.Join(dir, "clip.funscript")
	body := `{"actions":[{"at":0,"pos":20},{"at":1000,"pos":90}],"metadata":{"profile":"tj","device_recipe":{"sync":"suction_position","min_suction":0.2,"tick_ms":50,"max_speed":0.5,"smoothing":0.22}}}`
	if err := os.WriteFile(path, []byte(body), 0644); err != nil {
		t.Fatal(err)
	}
	if err := SaveContactRecipe(path, true, 0.55, "soft"); err != nil {
		t.Fatalf("SaveContactRecipe: %v", err)
	}
	s, err := Load(path)
	if err != nil {
		t.Fatal(err)
	}
	dr := s.Metadata.DeviceRecipe
	if dr == nil {
		t.Fatal("device_recipe fehlt")
	}
	if !dr.ContactVibration {
		t.Error("ContactVibration nicht gesetzt")
	}
	if dr.ContactVibrationSpan != 0.55 {
		t.Errorf("span=%v", dr.ContactVibrationSpan)
	}
	if dr.ContactVibrationCurve != ContactCurveSoft {
		t.Errorf("curve=%q", dr.ContactVibrationCurve)
	}
	if dr.Sync != SyncSuctionPosition.String() {
		t.Errorf("sync verloren: %q", dr.Sync)
	}
	if len(s.Actions) != 2 {
		t.Errorf("actions verändert: %d", len(s.Actions))
	}
}
