package main

import (
	"os"
	"path/filepath"
	"testing"

	"github.com/funfunpayer/SamNPlayer/funscript"
)

func TestAppScriptActionsRoundtrip(t *testing.T) {
	path := filepath.Join(t.TempDir(), "test.funscript")
	if err := os.WriteFile(path, []byte(`{"actions":[{"at":0,"pos":0},{"at":1000,"pos":100}],"metadata":{}}`), 0644); err != nil {
		t.Fatalf("Testskript anlegen: %v", err)
	}
	a := NewApp()

	if _, err := a.GetScriptActions(); err == nil {
		t.Error("erwartet Fehler ohne geladenes Skript")
	}
	if err := a.SaveScriptActions([]funscript.Action{{At: 0, Pos: 0}}); err == nil {
		t.Error("erwartet Fehler beim Speichern ohne geladenes Skript")
	}

	if _, err := a.LoadFunscript(path); err != nil {
		t.Fatalf("LoadFunscript: %v", err)
	}

	got, err := a.GetScriptActions()
	if err != nil {
		t.Fatalf("GetScriptActions: %v", err)
	}
	if len(got) != 2 {
		t.Fatalf("erwartet 2 Punkte, bekommen %d: %+v", len(got), got)
	}

	edited := []funscript.Action{
		{At: 0, Pos: 0},
		{At: 500, Pos: 40}, // neuer Punkt aus dem Editor
		{At: 1000, Pos: 100},
	}
	if err := a.SaveScriptActions(edited); err != nil {
		t.Fatalf("SaveScriptActions: %v", err)
	}

	// a.currentScript muss sofort den neuen Stand widerspiegeln, ohne dass
	// die GUI die Datei erst erneut über LoadFunscript öffnen muss - sonst
	// zeigen Kurve/Heatmap nach dem Speichern kurz einen veralteten Stand.
	got, err = a.GetScriptActions()
	if err != nil {
		t.Fatalf("GetScriptActions nach Save: %v", err)
	}
	if len(got) != 3 || got[1].At != 500 || got[1].Pos != 40 {
		t.Errorf("Punkte nach dem Speichern falsch: %+v", got)
	}

	// Und die Datei auf der Platte muss denselben Stand haben.
	s, err := funscript.Load(path)
	if err != nil {
		t.Fatalf("Load: %v", err)
	}
	if len(s.Actions) != 3 {
		t.Errorf("Datei wurde nicht aktualisiert: %+v", s.Actions)
	}
}
