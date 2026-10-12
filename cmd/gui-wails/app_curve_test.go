package main

import (
	"testing"

	"github.com/funfunpayer/SamNPlayer/funscript"
)

// Die Ausdünnung darf die Amplitude nicht verlieren: ein Funscript besteht
// fast nur aus Hoch- und Tiefpunkten, jeden n-ten Punkt zu nehmen würde die
// Kurve flacher aussehen lassen, als sie ist.
func TestGetScriptCurveKeepsExtremes(t *testing.T) {
	a := NewApp()
	var actions []funscript.Action
	for i := 0; i < 5000; i++ {
		pos := 0
		if i%2 == 0 {
			pos = 100
		}
		actions = append(actions, funscript.Action{At: int64(i * 100), Pos: pos})
	}
	a.currentScript = &funscript.Script{Actions: actions}

	pts, err := a.GetScriptCurve(200)
	if err != nil {
		t.Fatalf("GetScriptCurve: %v", err)
	}
	if len(pts) > 200 {
		t.Errorf("zu viele Punkte: %d", len(pts))
	}
	if len(pts) < 50 {
		t.Fatalf("zu wenige Punkte: %d", len(pts))
	}

	var sawMin, sawMax bool
	for _, p := range pts {
		if p.Pos == 0 {
			sawMin = true
		}
		if p.Pos == 100 {
			sawMax = true
		}
	}
	if !sawMin || !sawMax {
		t.Error("Ausdünnung hat die Extremwerte verloren - die Kurve würde flacher wirken")
	}

	// Zeitlich aufsteigend, sonst zickzackt die Linie rückwärts.
	for i := 1; i < len(pts); i++ {
		if pts[i].AtMs < pts[i-1].AtMs {
			t.Fatalf("Punkte nicht zeitlich sortiert bei %d: %d < %d", i, pts[i].AtMs, pts[i-1].AtMs)
		}
	}
}

func TestGetScriptCurveShortScriptUnchanged(t *testing.T) {
	a := NewApp()
	a.currentScript = &funscript.Script{Actions: []funscript.Action{
		{At: 0, Pos: 10}, {At: 500, Pos: 90}, {At: 1000, Pos: 20},
	}}
	pts, err := a.GetScriptCurve(100)
	if err != nil {
		t.Fatalf("GetScriptCurve: %v", err)
	}
	if len(pts) != 3 {
		t.Errorf("kurzes Skript muss unverändert durchgereicht werden, bekam %d Punkte", len(pts))
	}
}

func TestGetScriptCurveWithoutScript(t *testing.T) {
	a := NewApp()
	if _, err := a.GetScriptCurve(100); err == nil {
		t.Error("ohne geladenes Skript muss ein Fehler kommen")
	}
}

func TestGetScriptCurveHonorsInverted(t *testing.T) {
	a := NewApp()
	a.currentScript = &funscript.Script{
		Actions:  []funscript.Action{{At: 0, Pos: 10}, {At: 500, Pos: 90}, {At: 1000, Pos: 20}},
		Inverted: true,
	}
	pts, err := a.GetScriptCurve(100)
	if err != nil {
		t.Fatal(err)
	}
	if len(pts) != 3 {
		t.Fatalf("got %d points", len(pts))
	}
	if pts[0].Pos != 90 || pts[1].Pos != 10 || pts[2].Pos != 80 {
		t.Fatalf("curve must use PlaybackActions: %+v", pts)
	}
}

func TestGetVibrationCurveContactRecipe(t *testing.T) {
	a := NewApp()
	script := &funscript.Script{Actions: []funscript.Action{
		{At: 0, Pos: 20}, {At: 500, Pos: 20},
		{At: 600, Pos: 90}, {At: 900, Pos: 90},
		{At: 1000, Pos: 20}, {At: 1500, Pos: 20},
	}}
	script.Metadata.Profile = "tj"
	script.Metadata.DeviceRecipe = &funscript.DeviceRecipe{
		Sync: "suction_position", ContactVibration: true,
		ContactVibrationCurve: "peak", ContactVibrationSpan: 0.5,
	}
	a.currentScript = script

	pts, err := a.GetVibrationCurve(100)
	if err != nil {
		t.Fatalf("GetVibrationCurve: %v", err)
	}
	if len(pts) < 2 {
		t.Fatal("erwartete Vibrationsspur")
	}
	var max float64
	for _, p := range pts {
		if p.Vibration > max {
			max = p.Vibration
		}
	}
	if max < 0.5 {
		t.Errorf("Kontaktfenster sollte spürbare Vibration liefern, max=%.3f", max)
	}
}

func TestGetVibrationCurveAbsentWithoutFlag(t *testing.T) {
	a := NewApp()
	script := &funscript.Script{Actions: []funscript.Action{
		{At: 0, Pos: 20}, {At: 500, Pos: 90}, {At: 1000, Pos: 20},
	}}
	script.Metadata.Profile = "tj"
	script.Metadata.DeviceRecipe = &funscript.DeviceRecipe{Sync: "suction_position"}
	a.currentScript = script
	pts, err := a.GetVibrationCurve(100)
	if err != nil {
		t.Fatalf("GetVibrationCurve: %v", err)
	}
	if pts != nil {
		t.Errorf("ohne contact_vibration sollte keine Spur kommen, bekam %d Punkte", len(pts))
	}
}

func TestGetVibrationCurvePreviewLiveScaleAndMute(t *testing.T) {
	a := NewApp()
	script := &funscript.Script{Actions: []funscript.Action{
		{At: 0, Pos: 20}, {At: 500, Pos: 20},
		{At: 600, Pos: 90}, {At: 900, Pos: 90},
		{At: 1000, Pos: 20}, {At: 1500, Pos: 20},
	}}
	script.Metadata.Profile = "tj"
	script.Metadata.DeviceRecipe = &funscript.DeviceRecipe{
		Sync: "suction_position", ContactVibration: true,
		ContactVibrationCurve: "linear", ContactVibrationSpan: 0.5,
	}
	a.currentScript = script

	base, err := a.GetVibrationCurvePreview(ContactPreviewOptions{
		MaxPoints: 100, ContactIntensityScale: 1,
	})
	if err != nil || len(base) < 2 {
		t.Fatalf("base: %v n=%d", err, len(base))
	}
	half, err := a.GetVibrationCurvePreview(ContactPreviewOptions{
		MaxPoints: 100, ContactIntensityScale: 0.5,
	})
	if err != nil {
		t.Fatal(err)
	}
	var maxBase, maxHalf float64
	for i := range base {
		if base[i].Vibration > maxBase {
			maxBase = base[i].Vibration
		}
		if half[i].Vibration > maxHalf {
			maxHalf = half[i].Vibration
		}
	}
	if maxHalf > maxBase*0.6+0.01 {
		t.Errorf("scale 0.5 sollte Peak halbieren: base=%.3f half=%.3f", maxBase, maxHalf)
	}
	muted, err := a.GetVibrationCurvePreview(ContactPreviewOptions{
		MaxPoints: 100, ContactIntensityScale: 1, MuteContact: true,
	})
	if err != nil {
		t.Fatal(err)
	}
	if muted != nil {
		t.Errorf("mute sollte keine Spur liefern, got %d", len(muted))
	}
}

func TestGetVibrationCurvePreviewSpanOverride(t *testing.T) {
	a := NewApp()
	script := &funscript.Script{Actions: []funscript.Action{
		{At: 0, Pos: 20}, {At: 250, Pos: 50}, {At: 500, Pos: 70},
		{At: 750, Pos: 90}, {At: 1000, Pos: 20},
	}}
	script.Metadata.Profile = "tj"
	script.Metadata.DeviceRecipe = &funscript.DeviceRecipe{
		Sync: "suction_position", ContactVibration: true,
		ContactVibrationSpan: 0.9, ContactVibrationCurve: "linear",
	}
	a.currentScript = script

	tight, _ := a.GetVibrationCurvePreview(ContactPreviewOptions{
		MaxPoints: 80, ContactVibrationSpan: 0.9, ContactIntensityScale: 1,
	})
	wide, _ := a.GetVibrationCurvePreview(ContactPreviewOptions{
		MaxPoints: 80, ContactVibrationSpan: 0.4, ContactIntensityScale: 1,
	})
	count := func(pts []VibrationCurvePoint) int {
		n := 0
		for _, p := range pts {
			if p.Vibration > 0.05 {
				n++
			}
		}
		return n
	}
	if count(wide) <= count(tight) {
		t.Errorf("span 0.4 sollte mehr Kontaktfenster öffnen als 0.9: wide=%d tight=%d",
			count(wide), count(tight))
	}
}
