package main

import (
	"os"
	"path/filepath"
	"testing"

	"github.com/funfunpayer/SamNPlayer/player"
)

func sampleCustomScript(name string) player.TrainingScript {
	return player.TrainingScript{
		Name:        name,
		Description: "test script",
		Phases: []player.TrainingPhase{
			{
				Name:         "Only phase",
				Vibration:    &player.ChannelCurve{PeakLevel: 0.5, RampUpMs: 100, HoldMs: 100, RampDownMs: 100},
				RepeatCycles: 1,
				RestMs:       100,
			},
		},
	}
}

func TestSaveTrainingScriptRoundTrips(t *testing.T) {
	t.Setenv("XDG_CONFIG_HOME", t.TempDir())
	a := NewApp()

	info, err := a.SaveTrainingScript(sampleCustomScript("My Routine"))
	if err != nil {
		t.Fatalf("SaveTrainingScript: %v", err)
	}
	if !info.Custom || info.Name != "My Routine" {
		t.Fatalf("unexpected info: %+v", info)
	}

	list := a.ListTrainingScripts()
	found := false
	for _, s := range list {
		if s.Name == "My Routine" {
			found = true
			if !s.Custom {
				t.Error("saved script should be marked Custom in ListTrainingScripts")
			}
		}
	}
	if !found {
		t.Fatalf("saved script missing from ListTrainingScripts: %+v", list)
	}

	loaded, err := a.LoadTrainingScriptForEditing("My Routine")
	if err != nil {
		t.Fatalf("LoadTrainingScriptForEditing: %v", err)
	}
	if len(loaded.Phases) != 1 || loaded.Phases[0].Name != "Only phase" {
		t.Fatalf("loaded script does not match what was saved: %+v", loaded)
	}

	found2, err := loadAnyTrainingScript("My Routine")
	if err != nil {
		t.Fatalf("loadAnyTrainingScript: %v", err)
	}
	if found2.Name != "My Routine" {
		t.Fatalf("loadAnyTrainingScript returned wrong script: %+v", found2)
	}
}

func TestSaveTrainingScriptRejectsInvalidOrBuiltinName(t *testing.T) {
	t.Setenv("XDG_CONFIG_HOME", t.TempDir())
	a := NewApp()

	if _, err := a.SaveTrainingScript(sampleCustomScript("")); err == nil {
		t.Error("empty name should be rejected")
	}
	if _, err := a.SaveTrainingScript(sampleCustomScript("   ")); err == nil {
		t.Error("whitespace-only name should be rejected")
	}
	if _, err := a.SaveTrainingScript(sampleCustomScript("vibration-wave-suction-focus")); err == nil {
		t.Error("a name colliding with a built-in script should be rejected")
	}

	empty := sampleCustomScript("no phases")
	empty.Phases = nil
	if _, err := a.SaveTrainingScript(empty); err == nil {
		t.Error("a script with no phases should be rejected")
	}
}

// Der Skriptname kommt direkt aus einem GUI-Textfeld - ein Name wie
// "../../evil" darf niemals außerhalb des eigenen Scripts-Ordners
// schreiben.
func TestSaveTrainingScriptSanitizesPathTraversal(t *testing.T) {
	configDir := t.TempDir()
	t.Setenv("XDG_CONFIG_HOME", configDir)
	a := NewApp()

	if _, err := a.SaveTrainingScript(sampleCustomScript("../../evil")); err != nil {
		t.Fatalf("SaveTrainingScript: %v", err)
	}

	scriptsDir, err := customScriptsDir()
	if err != nil {
		t.Fatal(err)
	}
	entries, err := os.ReadDir(scriptsDir)
	if err != nil {
		t.Fatalf("scripts dir not created as expected: %v", err)
	}
	if len(entries) != 1 {
		t.Fatalf("expected exactly 1 file inside the scripts dir, got %d: %v", len(entries), entries)
	}
	if dir := filepath.Dir(filepath.Join(scriptsDir, entries[0].Name())); dir != scriptsDir {
		t.Fatalf("file escaped the scripts dir: %s", dir)
	}
	// Außerhalb des Config-Verzeichnisbaums darf nichts entstanden sein.
	if _, err := os.Stat(filepath.Join(configDir, "..", "..", "evil.json")); !os.IsNotExist(err) {
		t.Fatalf("path traversal escaped the config directory: err=%v", err)
	}
}

func TestDeleteTrainingScript(t *testing.T) {
	t.Setenv("XDG_CONFIG_HOME", t.TempDir())
	a := NewApp()

	if _, err := a.SaveTrainingScript(sampleCustomScript("Temp")); err != nil {
		t.Fatalf("SaveTrainingScript: %v", err)
	}
	if err := a.DeleteTrainingScript("Temp"); err != nil {
		t.Fatalf("DeleteTrainingScript: %v", err)
	}
	if _, err := a.LoadTrainingScriptForEditing("Temp"); err == nil {
		t.Error("deleted script should no longer be loadable")
	}

	if err := a.DeleteTrainingScript("vibration-wave-suction-focus"); err == nil {
		t.Error("deleting a built-in script name should be rejected")
	}
	if err := a.DeleteTrainingScript("does-not-exist"); err == nil {
		t.Error("deleting a nonexistent script should error, not silently succeed")
	}
}

// SaveTrainingScript muss player.NormalizeTrainingScript anwenden - sonst
// könnte ein im Editor falsch verdrahtetes Channel-Feld (siehe
// player.TestRunTrainingScriptNormalizesMismatchedChannelField) unbemerkt
// gespeichert werden und beim Abspielen zwei Kurven auf denselben Kanal
// schicken.
func TestSaveTrainingScriptNormalizesChannelField(t *testing.T) {
	t.Setenv("XDG_CONFIG_HOME", t.TempDir())
	a := NewApp()

	script := sampleCustomScript("Mismatched")
	script.Phases[0].Suction = &player.ChannelCurve{
		Channel:   player.ChannelVibration, // absichtlich falsch
		PeakLevel: 0.4, RampUpMs: 100, HoldMs: 100, RampDownMs: 100,
	}
	if _, err := a.SaveTrainingScript(script); err != nil {
		t.Fatalf("SaveTrainingScript: %v", err)
	}

	loaded, err := a.LoadTrainingScriptForEditing("Mismatched")
	if err != nil {
		t.Fatalf("LoadTrainingScriptForEditing: %v", err)
	}
	if loaded.Phases[0].Suction.Channel != player.ChannelSuction {
		t.Errorf("saved script was not normalized: Suction curve has Channel=%q", loaded.Phases[0].Suction.Channel)
	}
}

// scriptFileSlug strips everything outside [a-z0-9-], so two different
// names can reduce to the same file. Without a collision check,
// "My Routine?" would silently overwrite "My Routine!"'s file on disk -
// no error, no warning, the first script just gone. Re-saving under the
// SAME name (editing a script in place) must still work unchanged.
func TestSaveTrainingScriptRejectsSlugCollisionWithDifferentName(t *testing.T) {
	t.Setenv("XDG_CONFIG_HOME", t.TempDir())
	a := NewApp()

	if _, err := a.SaveTrainingScript(sampleCustomScript("My Routine!")); err != nil {
		t.Fatalf("SaveTrainingScript(first): %v", err)
	}

	if _, err := a.SaveTrainingScript(sampleCustomScript("My Routine?")); err == nil {
		t.Fatal("a differently-named script that slugs to the same file name should be rejected, not silently overwrite the first one")
	}

	// The original script must still be there, untouched.
	loaded, err := a.LoadTrainingScriptForEditing("My Routine!")
	if err != nil {
		t.Fatalf("original script was lost after the rejected collision: %v", err)
	}
	if loaded.Name != "My Routine!" {
		t.Fatalf("original script content changed: %+v", loaded)
	}

	// Re-saving under the identical name (editing in place) must keep working.
	edited := sampleCustomScript("My Routine!")
	edited.Description = "edited"
	if _, err := a.SaveTrainingScript(edited); err != nil {
		t.Fatalf("re-saving under the same name should still work: %v", err)
	}
	loaded, err = a.LoadTrainingScriptForEditing("My Routine!")
	if err != nil {
		t.Fatalf("LoadTrainingScriptForEditing after edit-in-place: %v", err)
	}
	if loaded.Description != "edited" {
		t.Fatalf("edit-in-place did not take effect: %+v", loaded)
	}
}

func TestPreviewTrainingScriptDraftValidatesFirst(t *testing.T) {
	t.Setenv("XDG_CONFIG_HOME", t.TempDir())
	a := NewApp()

	if _, err := a.PreviewTrainingScriptDraft(player.TrainingScript{}); err == nil {
		t.Error("an empty draft should be rejected, not previewed")
	}

	preview, err := a.PreviewTrainingScriptDraft(sampleCustomScript("Draft"))
	if err != nil {
		t.Fatalf("PreviewTrainingScriptDraft: %v", err)
	}
	if preview.TotalMs <= 0 {
		t.Errorf("expected a positive TotalMs for a valid draft, got %d", preview.TotalMs)
	}
}

// The nominal preview curve is unjittered even for a script with random
// jitter on a curve - HasRandomJitter is the flag the GUI uses to say
// "this pattern varies each cycle" instead of the preview silently
// looking like a plain fixed wave that isn't what actually plays.
func TestPreviewTrainingScriptDraftFlagsRandomJitter(t *testing.T) {
	t.Setenv("XDG_CONFIG_HOME", t.TempDir())
	a := NewApp()

	plain := sampleCustomScript("Plain")
	preview, err := a.PreviewTrainingScriptDraft(plain)
	if err != nil {
		t.Fatalf("PreviewTrainingScriptDraft: %v", err)
	}
	if preview.HasRandomJitter {
		t.Error("a script with no jitter on any curve must not be flagged")
	}

	jittered := sampleCustomScript("Jittered")
	jittered.Phases[0].Vibration.RandomJitterFraction = 0.25
	preview, err = a.PreviewTrainingScriptDraft(jittered)
	if err != nil {
		t.Fatalf("PreviewTrainingScriptDraft: %v", err)
	}
	if !preview.HasRandomJitter {
		t.Error("a script with RandomJitterFraction > 0 on a curve must be flagged")
	}

	builtinPreview, err := a.TrainingScriptPreview("variable", 1)
	if err != nil {
		t.Fatalf("TrainingScriptPreview(variable): %v", err)
	}
	if !builtinPreview.HasRandomJitter {
		t.Error(`the built-in "variable" script uses RandomJitterFraction and must be flagged too`)
	}
	stopStartPreview, err := a.TrainingScriptPreview("vibration-wave-suction-focus", 1)
	if err != nil {
		t.Fatalf("TrainingScriptPreview(vibration-wave-suction-focus): %v", err)
	}
	if stopStartPreview.HasRandomJitter {
		t.Error("a built-in script without jitter must not be flagged")
	}
}
