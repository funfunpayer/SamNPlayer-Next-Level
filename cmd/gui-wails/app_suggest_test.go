package main

import (
	"encoding/json"
	"os"
	"path/filepath"
	"testing"

	"github.com/funfunpayer/SamNPlayer/funscript"
	"github.com/funfunpayer/SamNPlayer/samn"
)

func TestSuggestPolarityAndInvertRoundtrip(t *testing.T) {
	path := filepath.Join(t.TempDir(), "p.funscript")
	body := `{"actions":[{"at":0,"pos":10},{"at":100,"pos":15},{"at":200,"pos":12},{"at":300,"pos":80},{"at":400,"pos":90}],"inverted":true}`
	if err := os.WriteFile(path, []byte(body), 0644); err != nil {
		t.Fatal(err)
	}
	t.Setenv("XDG_CONFIG_HOME", t.TempDir())
	a := NewApp()
	if _, err := a.LoadFunscript(path); err != nil {
		t.Fatal(err)
	}
	if !a.currentScript.Inverted {
		t.Fatal("expected loaded inverted flag")
	}
	// Gespielt wird 90,85,88,20,10 - das Flag hat die Richtung schon
	// korrigiert, also keine Empfehlung. (Vorher wurden die Rohwerte
	// 10..90 bewertet und Invertieren empfohlen.)
	hint, err := a.SuggestPolarity()
	if err != nil {
		t.Fatal(err)
	}
	if hint.SuggestInvert {
		t.Fatalf("played curve already has the right direction: %+v", hint)
	}
	if got := a.currentScript.PlaybackActions()[0].Pos; got != 90 {
		t.Fatalf("played first position before invert: %d", got)
	}
	// Invertieren dreht das, was gespielt wird: 90 -> 10. Vorher wurden
	// die Rohwerte gedreht und das Flag gelöscht - die gespielte Kurve
	// blieb dieselbe (90), der Knopf wirkte nicht.
	if err := a.InvertLoadedScript(); err != nil {
		t.Fatal(err)
	}
	if got := a.currentScript.PlaybackActions()[0].Pos; got != 10 {
		t.Fatalf("played first position after invert: %d, want 10", got)
	}
	if a.currentScript.Inverted {
		t.Fatal("Invert bake must clear top-level inverted so Play does not flip twice")
	}
	raw, err := os.ReadFile(path)
	if err != nil {
		t.Fatal(err)
	}
	var doc map[string]json.RawMessage
	if err := json.Unmarshal(raw, &doc); err != nil {
		t.Fatal(err)
	}
	if _, ok := doc["inverted"]; ok {
		t.Fatalf("disk inverted should be cleared, got %s", doc["inverted"])
	}
}

func TestSuggestOZoneOnLoadedScript(t *testing.T) {
	path := filepath.Join(t.TempDir(), "o.funscript")
	actions := make([]funscript.Action, 0, 101)
	for i := 0; i <= 100; i++ {
		pos := 25
		if i >= 90 {
			pos = 88
		}
		actions = append(actions, funscript.Action{At: int64(i * 100), Pos: pos})
	}
	script := funscript.Script{Actions: actions}
	raw, err := json.Marshal(script)
	if err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(path, raw, 0644); err != nil {
		t.Fatal(err)
	}
	a := NewApp()
	if _, err := a.LoadFunscript(path); err != nil {
		t.Fatal(err)
	}
	zone, err := a.SuggestOZone()
	if err != nil {
		t.Fatal(err)
	}
	if !zone.OK || zone.StartMs < 8000 {
		t.Fatalf("unerwartete Zone: %+v", zone)
	}
}

func TestApplyRingDownOnLoadedScript(t *testing.T) {
	path := filepath.Join(t.TempDir(), "r.funscript")
	body := `{"actions":[{"at":0,"pos":20},{"at":1000,"pos":85},{"at":2000,"pos":90}]}`
	if err := os.WriteFile(path, []byte(body), 0644); err != nil {
		t.Fatal(err)
	}
	a := NewApp()
	if _, err := a.LoadFunscript(path); err != nil {
		t.Fatal(err)
	}
	nBefore := len(a.currentScript.Actions)
	if err := a.ApplyRingDown(2000, 2); err != nil {
		t.Fatal(err)
	}
	if len(a.currentScript.Actions) <= nBefore {
		t.Fatalf("erwartete zusätzliche Punkte, vorher %d nachher %d", nBefore, len(a.currentScript.Actions))
	}
	last := a.currentScript.Actions[len(a.currentScript.Actions)-1]
	if last.Pos != 0 {
		t.Fatalf("Ring-down muss mit 0 enden: %+v", last)
	}
	if last.At <= 2000 {
		t.Fatalf("Ring-down muss nach atMs liegen: %+v", last)
	}
}

// Schalter "inverted beachten" aus: die Datei spielt, wie die Punkte
// dastehen; das Umschalten lädt das offene Skript sofort neu.
func TestHonorInvertedSetting(t *testing.T) {
	t.Setenv("XDG_CONFIG_HOME", t.TempDir())
	path := filepath.Join(t.TempDir(), "inv.funscript")
	if err := os.WriteFile(path, []byte(`{"actions":[{"at":0,"pos":10},{"at":1000,"pos":80}],"inverted":true}`), 0o644); err != nil {
		t.Fatal(err)
	}
	a := NewApp()
	if !a.GetSettings().PlaybackHonorInverted {
		t.Fatal("default must stay on (behavior since v0.5.44)")
	}
	if _, err := a.LoadFunscript(path); err != nil {
		t.Fatal(err)
	}
	if got := a.loadedScript().PlaybackActions()[0].Pos; got != 90 {
		t.Fatalf("on: played %d, want 90", got)
	}
	if err := a.SetSetting(prefPlaybackHonorInverted, false); err != nil {
		t.Fatal(err)
	}
	if got := a.loadedScript().PlaybackActions()[0].Pos; got != 10 {
		t.Fatalf("off: played %d, want 10 (file as written)", got)
	}
	// Invertieren dreht auch hier das Gespielte: 10 -> 90, Flag weg.
	if err := a.InvertLoadedScript(); err != nil {
		t.Fatal(err)
	}
	if got := a.loadedScript().PlaybackActions()[0].Pos; got != 90 {
		t.Fatalf("off + invert: played %d, want 90", got)
	}
	if err := a.SetSetting(prefPlaybackHonorInverted, true); err != nil {
		t.Fatal(err)
	}
	if got := a.loadedScript().PlaybackActions()[0].Pos; got != 90 {
		t.Fatalf("flag cleared by invert, so on/off no longer matter: %d", got)
	}
}

// After Create, Play usually loads .samn (preferSamnCompanion). Invert must
// rewrite the companion .funscript actions too — clearing "inverted" alone
// left Share/other apps on the pre-invert curve.
func TestInvertLoadedScriptExportsSamnCompanion(t *testing.T) {
	t.Setenv("XDG_CONFIG_HOME", t.TempDir())
	dir := t.TempDir()
	samnPath := filepath.Join(dir, "clip.samn")
	companion := filepath.Join(dir, "clip.funscript")

	doc := &samn.Document{
		Version: 1,
		General: []funscript.Action{{At: 0, Pos: 10}, {At: 1000, Pos: 80}},
	}
	if err := samn.Save(samnPath, doc); err != nil {
		t.Fatal(err)
	}
	if err := doc.ExportFunscript(companion); err != nil {
		t.Fatal(err)
	}
	// Simulate an OFS-style leftover flag on the companion.
	if err := funscript.WriteInverted(companion, true); err != nil {
		t.Fatal(err)
	}

	a := NewApp()
	if _, err := a.LoadFunscript(samnPath); err != nil {
		t.Fatal(err)
	}
	if !samn.IsSamnPath(a.loadedScriptPath()) {
		t.Fatalf("expected .samn loaded, got %s", a.loadedScriptPath())
	}
	if got := a.loadedScript().PlaybackActions()[0].Pos; got != 10 {
		t.Fatalf("before invert played %d", got)
	}
	if err := a.InvertLoadedScript(); err != nil {
		t.Fatal(err)
	}
	if got := a.loadedScript().PlaybackActions()[0].Pos; got != 90 {
		t.Fatalf("after invert played %d, want 90", got)
	}

	fs, err := funscript.Load(companion)
	if err != nil {
		t.Fatal(err)
	}
	if fs.Inverted {
		t.Fatal("companion inverted flag must be cleared")
	}
	if got := fs.PlaybackActions()[0].Pos; got != 90 {
		t.Fatalf("companion played %d, want 90 (actions rewritten, not only flag)", got)
	}
}
