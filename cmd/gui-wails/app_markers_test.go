package main

import (
	"os"
	"path/filepath"
	"testing"

	"github.com/funfunpayer/SamNPlayer/funscript"
)

func TestAppOMarkersRoundtrip(t *testing.T) {
	path := filepath.Join(t.TempDir(), "test.funscript")
	if err := os.WriteFile(path, []byte(`{"actions":[{"at":0,"pos":0}],"metadata":{}}`), 0644); err != nil {
		t.Fatalf("Testskript anlegen: %v", err)
	}
	a := NewApp()

	got, err := a.GetOMarkers(path)
	if err != nil {
		t.Fatalf("GetOMarkers (leer): %v", err)
	}
	if len(got) != 0 {
		t.Fatalf("erwartet keine Marker, bekommen: %+v", got)
	}

	want := []funscript.OMarker{
		{StartMs: 1000, EndMs: 2000, Kind: funscript.OMarkerPrimary, Intensity: 1.0},
	}
	if err := a.SaveOMarkers(path, want); err != nil {
		t.Fatalf("SaveOMarkers: %v", err)
	}
	got, err = a.GetOMarkers(path)
	if err != nil {
		t.Fatalf("GetOMarkers (nach Save): %v", err)
	}
	if len(got) != 1 || got[0].Kind != funscript.OMarkerPrimary || got[0].StartMs != 1000 {
		t.Errorf("Marker nach dem Speichern falsch: %+v", got)
	}

	if err := a.SaveOMarkers(path, []funscript.OMarker{
		{StartMs: 5000, EndMs: 1000, Kind: funscript.OMarkerPrimary, Intensity: 1.0},
	}); err == nil {
		t.Error("ungültiger Marker (Ende vor Start) muss abgelehnt werden")
	}
}
