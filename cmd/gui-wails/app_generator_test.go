package main

import (
	"encoding/json"
	"os"
	"path/filepath"
	"testing"

	"github.com/funfunpayer/SamNPlayer/funscript"
)

func writeTestScript(t *testing.T, path string, actions []funscript.Action) {
	t.Helper()
	raw, err := json.Marshal(funscript.Script{Actions: actions})
	if err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(path, raw, 0644); err != nil {
		t.Fatal(err)
	}
}

func TestApplyAutoOZoneMarkerWritesPrimaryMarker(t *testing.T) {
	path := filepath.Join(t.TempDir(), "g.funscript")
	actions := make([]funscript.Action, 0, 101)
	for i := 0; i <= 100; i++ {
		pos := 20
		if i >= 90 {
			pos = 90
		}
		actions = append(actions, funscript.Action{At: int64(i * 100), Pos: pos})
	}
	writeTestScript(t, path, actions)

	zone, err := applyAutoOZoneMarker(path, actions)
	if err != nil {
		t.Fatal(err)
	}
	if !zone.OK {
		t.Fatalf("erwartete Zone bei hohem Ende: %+v", zone)
	}

	markers, err := funscript.LoadOMarkers(path)
	if err != nil {
		t.Fatal(err)
	}
	if len(markers) != 1 {
		t.Fatalf("erwartete genau einen Marker, got %d", len(markers))
	}
	if markers[0].Kind != funscript.OMarkerPrimary {
		t.Fatalf("erwartete primary, got %s", markers[0].Kind)
	}
	if markers[0].StartMs != zone.StartMs || markers[0].EndMs != zone.EndMs {
		t.Fatalf("Marker-Zeiten weichen von der Suggestion ab: %+v vs %+v", markers[0], zone)
	}
}

func TestApplyAutoOZoneMarkerWritesSecondaryMarkerWhenPresent(t *testing.T) {
	path := filepath.Join(t.TempDir(), "g2.funscript")
	actions := make([]funscript.Action, 0, 1001)
	for i := 0; i <= 1000; i++ {
		at := int64(i * 100) // 0..100000ms
		pos := 10
		switch {
		case at >= 90000:
			pos = 90 // Hauptmarker im letzten Achtel
		case at >= 20000 && at < 25000:
			pos = 55 // schwächere, frühere Erhebung
		}
		actions = append(actions, funscript.Action{At: at, Pos: pos})
	}
	writeTestScript(t, path, actions)

	zone, err := applyAutoOZoneMarker(path, actions)
	if err != nil {
		t.Fatal(err)
	}
	if !zone.OK {
		t.Fatalf("erwartete Zone bei hohem Ende: %+v", zone)
	}

	markers, err := funscript.LoadOMarkers(path)
	if err != nil {
		t.Fatal(err)
	}
	if len(markers) < 2 {
		t.Fatalf("erwartete Haupt- plus mindestens einen sekundären Marker, got %d: %+v", len(markers), markers)
	}
	var primaryCount, secondaryCount int
	for _, m := range markers {
		switch m.Kind {
		case funscript.OMarkerPrimary:
			primaryCount++
		case funscript.OMarkerSecondary:
			secondaryCount++
			if m.Intensity <= 0 || m.Intensity >= 1 {
				t.Fatalf("sekundärer Marker sollte schwächer als der Hauptmarker sein (Intensität in (0,1)): %+v", m)
			}
			if m.EndMs > zone.StartMs {
				t.Fatalf("sekundärer Marker darf nicht in/nach den Hauptmarker reichen: %+v vs primary %+v", m, zone)
			}
		}
	}
	if primaryCount != 1 {
		t.Fatalf("erwartete genau einen primary-Marker, got %d", primaryCount)
	}
	if secondaryCount == 0 {
		t.Fatalf("erwartete mindestens einen secondary-Marker bei der markierten Erhebung")
	}
}

func TestApplyAutoOZoneMarkerFlatScriptWritesNothing(t *testing.T) {
	path := filepath.Join(t.TempDir(), "flat.funscript")
	actions := make([]funscript.Action, 0, 51)
	for i := 0; i <= 50; i++ {
		actions = append(actions, funscript.Action{At: int64(i * 100), Pos: 20})
	}
	writeTestScript(t, path, actions)

	zone, err := applyAutoOZoneMarker(path, actions)
	if err != nil {
		t.Fatal(err)
	}
	if zone.OK {
		t.Fatalf("flaches Ende darf keine Zone liefern: %+v", zone)
	}

	markers, err := funscript.LoadOMarkers(path)
	if err != nil {
		t.Fatal(err)
	}
	if len(markers) != 0 {
		t.Fatalf("ohne Vorschlag darf kein Marker geschrieben werden, got %d", len(markers))
	}
}
