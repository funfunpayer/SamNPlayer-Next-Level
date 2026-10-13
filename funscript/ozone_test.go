package funscript

import "testing"

func TestSuggestOZonePicksLatePeak(t *testing.T) {
	var actions []Action
	for i := 0; i <= 100; i++ {
		pos := 20
		if i >= 90 {
			pos = 90
		}
		actions = append(actions, Action{At: int64(i * 100), Pos: pos})
	}
	s := SuggestOZone(actions)
	if !s.OK {
		t.Fatalf("erwartete Zone, got %+v", s)
	}
	if s.StartMs < 8000 {
		t.Fatalf("Zone sollte im letzten Achtel liegen, start=%d", s.StartMs)
	}
	if s.EndMs <= s.StartMs {
		t.Fatalf("Ende muss nach Start liegen: %+v", s)
	}
}

func TestSuggestOZoneRejectsFlatTail(t *testing.T) {
	var actions []Action
	for i := 0; i <= 50; i++ {
		actions = append(actions, Action{At: int64(i * 100), Pos: 20})
	}
	s := SuggestOZone(actions)
	if s.OK {
		t.Fatalf("flaches Ende darf keine Zone liefern: %+v", s)
	}
}

func TestSuggestOZoneShortScript(t *testing.T) {
	s := SuggestOZone([]Action{{At: 0, Pos: 0}, {At: 100, Pos: 100}, {At: 200, Pos: 0}, {At: 300, Pos: 50}})
	if s.OK {
		t.Fatalf("kurzes Skript: %+v", s)
	}
}

// buildScriptWithBumps erzeugt ein Skript mit einer flachen Grundlinie und
// erhöhten Abschnitten bei den angegebenen [von,bis)-Fensterpaaren
// (Millisekunden) - für SuggestSecondaryOZones-Tests, die mehrere
// unterschiedlich starke Erhebungen an bekannten Stellen brauchen.
func buildScriptWithBumps(totalMs int64, baseline int, bumps map[[2]int64]int) []Action {
	var actions []Action
	for at := int64(0); at <= totalMs; at += 100 {
		pos := baseline
		for win, bumpPos := range bumps {
			if at >= win[0] && at < win[1] {
				pos = bumpPos
			}
		}
		actions = append(actions, Action{At: at, Pos: pos})
	}
	return actions
}

func TestSuggestSecondaryOZonesFindsEarlierWeakerPeak(t *testing.T) {
	// Hauptmarker im letzten Achtel (90), eine deutlich schwächere,
	// aber klar über der Grundlinie liegende Erhebung bei 20-25s (55).
	actions := buildScriptWithBumps(100000, 10, map[[2]int64]int{
		{90000, 100001}: 90,
		{20000, 25000}:  55,
	})
	primary := SuggestOZone(actions)
	if !primary.OK {
		t.Fatalf("erwartete Hauptmarker-Zone, got %+v", primary)
	}
	secondary := SuggestSecondaryOZones(actions, primary, 2)
	if len(secondary) == 0 {
		t.Fatalf("erwartete mindestens eine sekundäre Zone, got keine (primary=%+v)", primary)
	}
	s := secondary[0]
	if s.StartMs >= primary.StartMs {
		t.Fatalf("sekundäre Zone muss vor dem Hauptmarker liegen: %+v vs primary %+v", s, primary)
	}
	if s.Mean >= primary.Mean {
		t.Fatalf("sekundäre Zone darf nicht so stark wie der Hauptmarker sein: %.1f >= %.1f", s.Mean, primary.Mean)
	}
	if s.StartMs >= 25000 || s.EndMs <= 20000 {
		t.Fatalf("sekundäre Zone sollte die markierte Erhebung (20-25s) überlappen, got %+v", s)
	}
}

func TestSuggestSecondaryOZonesRespectsMaxCount(t *testing.T) {
	actions := buildScriptWithBumps(120000, 10, map[[2]int64]int{
		{100000, 110001}: 90,
		{10000, 15000}:   55,
		{40000, 45000}:   60,
		{70000, 75000}:   50,
	})
	primary := SuggestOZone(actions)
	if !primary.OK {
		t.Fatalf("erwartete Hauptmarker-Zone, got %+v", primary)
	}
	secondary := SuggestSecondaryOZones(actions, primary, 1)
	if len(secondary) > 1 {
		t.Fatalf("maxCount=1 überschritten: %d Zonen", len(secondary))
	}
	secondaryAll := SuggestSecondaryOZones(actions, primary, 2)
	if len(secondaryAll) > 2 {
		t.Fatalf("maxCount=2 überschritten: %d Zonen", len(secondaryAll))
	}
}

func TestSuggestSecondaryOZonesNoOverlap(t *testing.T) {
	actions := buildScriptWithBumps(120000, 10, map[[2]int64]int{
		{100000, 110001}: 90,
		{10000, 20000}:   60, // breite Erhebung - mehrere Schiebefenster darin überlappen sich
	})
	primary := SuggestOZone(actions)
	secondary := SuggestSecondaryOZones(actions, primary, 3)
	for i := 0; i < len(secondary); i++ {
		for j := i + 1; j < len(secondary); j++ {
			a, b := secondary[i], secondary[j]
			if a.StartMs < b.EndMs && b.StartMs < a.EndMs {
				t.Fatalf("überlappende sekundäre Zonen: %+v und %+v", a, b)
			}
		}
	}
}

func TestSuggestSecondaryOZonesNoPrimaryNoSecondary(t *testing.T) {
	primary := OZoneSuggestion{OK: false}
	actions := buildScriptWithBumps(50000, 10, nil)
	if got := SuggestSecondaryOZones(actions, primary, 2); got != nil {
		t.Fatalf("ohne gültigen Hauptmarker dürfen keine sekundären Zonen entstehen: %+v", got)
	}
}

func TestSuggestSecondaryOZonesFlatScriptFindsNone(t *testing.T) {
	// Durchgehend flaches Skript mit nur der Hauptmarker-Erhebung - keine
	// sekundäre Erhebung vorhanden, also darf auch keine erfunden werden.
	actions := buildScriptWithBumps(100000, 10, map[[2]int64]int{
		{90000, 100001}: 90,
	})
	primary := SuggestOZone(actions)
	if !primary.OK {
		t.Fatalf("erwartete Hauptmarker-Zone, got %+v", primary)
	}
	if got := SuggestSecondaryOZones(actions, primary, 2); len(got) != 0 {
		t.Fatalf("flaches Skript vor dem Hauptmarker darf keine sekundäre Zone liefern: %+v", got)
	}
}

func TestSuggestSecondaryOZonesMaxCountZero(t *testing.T) {
	actions := buildScriptWithBumps(100000, 10, map[[2]int64]int{
		{90000, 100001}: 90,
		{20000, 25000}:  55,
	})
	primary := SuggestOZone(actions)
	if got := SuggestSecondaryOZones(actions, primary, 0); got != nil {
		t.Fatalf("maxCount=0 muss nil liefern: %+v", got)
	}
}
