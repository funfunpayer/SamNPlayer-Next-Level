package main

import "testing"

// Effects presets (Player → Inspector → Effects): the GUI writes a JSON text
// under playback.effect_presets; GetSettings hands it back unchanged, and a
// fresh install has none.
func TestEffectPresetsRoundTrip(t *testing.T) {
	t.Setenv("XDG_CONFIG_HOME", t.TempDir())
	a := NewApp()
	if got := a.GetSettings().PlaybackEffectPresets; got != "" {
		t.Fatalf("fresh install should have no presets, got %q", got)
	}
	const presets = `{"Night":{"intensity":40,"smoothing":0.8,"maxSpeed":0.4,"tickMs":80,"softStartMs":1500,"reducePeaks":true}}`
	if err := a.SetSetting(prefPlaybackEffectPresets, presets); err != nil {
		t.Fatal(err)
	}
	if got := a.GetSettings().PlaybackEffectPresets; got != presets {
		t.Fatalf("presets not returned unchanged: %q", got)
	}
	// A new App reads the same settings file.
	if got := NewApp().GetSettings().PlaybackEffectPresets; got != presets {
		t.Fatalf("presets not persisted: %q", got)
	}
}
