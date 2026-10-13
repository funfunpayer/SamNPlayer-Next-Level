package main

import (
	"os"
	"path/filepath"
	"testing"

	"github.com/funfunpayer/SamNPlayer/funscript"
	"github.com/funfunpayer/SamNPlayer/samn"
)

func TestSamnOMarkersAndContactRoundTrip(t *testing.T) {
	dir := t.TempDir()
	path := filepath.Join(dir, "clip.samn")
	doc := &samn.Document{
		Version:        samn.CurrentVersion,
		Kind:           samn.Kind,
		Profile:        funscript.ProfileTJ,
		PlaybackSource: samn.PlaybackRecipe,
		General:        []funscript.Action{{At: 0, Pos: 20}, {At: 2000, Pos: 90}},
		Recipe:         funscript.RecipeMeta(funscript.ProfileTJ),
		OMarkers: []funscript.OMarker{{
			StartMs: 500, EndMs: 1500, Kind: funscript.OMarkerPrimary, Intensity: 1,
		}},
	}
	doc.ApplyContactRecipe(true, 0.55, "soft")
	if err := samn.Save(path, doc); err != nil {
		t.Fatal(err)
	}

	a := NewApp()
	if _, err := a.LoadFunscript(path); err != nil {
		t.Fatal(err)
	}
	got, err := a.GetOMarkers(path)
	if err != nil || len(got) != 1 || got[0].Kind != funscript.OMarkerPrimary {
		t.Fatalf("GetOMarkers: %+v %v", got, err)
	}
	got = append(got, funscript.OMarker{
		StartMs: 100, EndMs: 300, Kind: funscript.OMarkerSecondary, Intensity: 0.4,
	})
	if err := a.SaveOMarkers(path, got); err != nil {
		t.Fatal(err)
	}
	again, err := a.GetOMarkers(path)
	if err != nil || len(again) != 2 {
		t.Fatalf("after save: %+v %v", again, err)
	}

	if err := a.SaveContactSettings(true, 0.6, "peak"); err != nil {
		t.Fatal(err)
	}
	re, err := samn.Load(path)
	if err != nil {
		t.Fatal(err)
	}
	if !re.Recipe.ContactVibration || re.Recipe.ContactVibrationCurve != "peak" {
		t.Fatalf("contact recipe: %+v", re.Recipe)
	}
	// Switch to axes and save contact again → bake updates vibration.
	if err := a.SetPlaybackSource(samn.PlaybackAxes); err != nil {
		t.Fatal(err)
	}
	if err := a.SaveContactSettings(true, 0.5, "linear"); err != nil {
		t.Fatal(err)
	}
	re, err = samn.Load(path)
	if err != nil {
		t.Fatal(err)
	}
	if len(re.Vibration) < 2 {
		t.Fatalf("expected baked vibration after contact save in axes mode")
	}
	if _, err := os.Stat(samn.CompanionFunscriptPath(path)); err != nil {
		t.Fatalf("expected funscript export sync: %v", err)
	}
}
