package funscript

import (
	"math"
	"testing"
)

// Der Mapper ist der hör- und fühlbare Kern: er entscheidet, was am Gerät
// ankommt. Bis hierher hatte er keine Tests - ein Fehler darin hätte sich
// nur als "fühlt sich falsch an" gezeigt, also als etwas, das man nicht
// debuggen kann.

func scriptFrom(points ...Action) *Script {
	return &Script{Actions: points}
}

// Ein Skript ohne Bewegung darf kein Tempo vortäuschen.
func TestMapperStandstill(t *testing.T) {
	script := scriptFrom(
		Action{At: 0, Pos: 50},
		Action{At: 1000, Pos: 50},
		Action{At: 2000, Pos: 50},
	)
	opts := DefaultMapOptions()
	opts.MinVibration = 0
	opts.Smoothing = 0

	frames := script.ToIntensityCurve(opts)
	if len(frames) == 0 {
		t.Fatal("keine Frames erzeugt")
	}
	for _, f := range frames {
		if f.Vibration > 0.001 {
			t.Fatalf("Stillstand ergibt Vibration %.3f bei %dms", f.Vibration, f.At)
		}
	}
}

// Ein schneller Hub muss die volle Intensität erreichen. Täte er das nicht,
// bliebe das Gerät dauerhaft unter seinen Möglichkeiten.
func TestMapperFastStrokeReachesFull(t *testing.T) {
	// 0 -> 100 in 150ms entspricht Geschwindigkeit 0.67, über MaxSpeed 0.6.
	script := scriptFrom(
		Action{At: 0, Pos: 0},
		Action{At: 150, Pos: 100},
		Action{At: 300, Pos: 0},
	)
	opts := DefaultMapOptions()
	opts.Smoothing = 0

	frames := script.ToIntensityCurve(opts)
	var max float64
	for _, f := range frames {
		if f.Vibration > max {
			max = f.Vibration
		}
	}
	if max < 0.99 {
		t.Errorf("schneller Hub erreicht nur %.3f statt voller Intensität", max)
	}
}

// Der entscheidende Punkt: die Grundintensität wird HINEINSKALIERT, nicht
// abgeschnitten. Ein Abschneiden hätte jede Abstufung darunter eingeebnet -
// Stillstand, langsame und mittlere Bewegung ergäben denselben Wert, und
// gerade in ruhigen Passagen wäre kein Tempo mehr zu spüren.
func TestMapperFloorPreservesGradations(t *testing.T) {
	opts := DefaultMapOptions()
	opts.MinVibration = 0.15
	opts.Smoothing = 0
	opts.Sync = SyncVibrationOnly

	intensity := func(deltaPos int, deltaMs int64) float64 {
		script := scriptFrom(
			Action{At: 0, Pos: 0},
			Action{At: deltaMs, Pos: deltaPos},
			Action{At: deltaMs * 2, Pos: 0},
		)
		frames := script.ToIntensityCurve(opts)
		if len(frames) == 0 {
			t.Fatal("keine Frames")
		}
		return frames[0].Vibration
	}

	// MaxSpeed 0.6 bedeutet 0.6 Positionseinheiten je Millisekunde. Über
	// 200ms sind 10% davon also 12 Einheiten, 20% sind 24.
	still := intensity(0, 200)
	slow := intensity(12, 200)
	medium := intensity(24, 200)

	if math.Abs(still-0.15) > 0.01 {
		t.Errorf("Stillstand sollte genau auf der Grundintensität liegen, ist %.3f", still)
	}
	// Erwartet: 0.15, 0.15+0.10*0.85=0.235, 0.15+0.20*0.85=0.32
	if math.Abs(slow-0.235) > 0.02 {
		t.Errorf("10%% der Höchstgeschwindigkeit sollte auf rund 0.235 abbilden, ist %.3f", slow)
	}
	if math.Abs(medium-0.32) > 0.02 {
		t.Errorf("20%% sollte auf rund 0.32 abbilden, ist %.3f", medium)
	}
	if slow <= still+0.01 {
		t.Errorf("langsame Bewegung (%.3f) hebt sich nicht vom Stillstand (%.3f) ab - "+
			"die Grundintensität wird abgeschnitten statt skaliert", slow, still)
	}
	if medium <= slow+0.01 {
		t.Errorf("mittlere Bewegung (%.3f) hebt sich nicht von langsamer (%.3f) ab",
			medium, slow)
	}
}

// Die Grundintensität darf die Obergrenze nicht verschieben.
func TestMapperFloorKeepsCeiling(t *testing.T) {
	opts := DefaultMapOptions()
	opts.MinVibration = 0.4
	opts.Smoothing = 0
	opts.Sync = SyncVibrationOnly

	script := scriptFrom(
		Action{At: 0, Pos: 0},
		Action{At: 100, Pos: 100},
		Action{At: 200, Pos: 0},
	)
	for _, f := range script.ToIntensityCurve(opts) {
		if f.Vibration > 1.0001 {
			t.Fatalf("Vibration über 1.0: %.4f", f.Vibration)
		}
	}
}

// Ein Kanal, der in einem Modus bewusst auf null liegt, darf nicht durch
// die Grundintensität angehoben werden - sonst liefe das Gerät in einem
// Modus, den der Nutzer ausdrücklich abgewählt hat.
func TestMapperFloorRespectsChannelModes(t *testing.T) {
	script := scriptFrom(
		Action{At: 0, Pos: 0},
		Action{At: 200, Pos: 100},
		Action{At: 400, Pos: 0},
	)

	opts := DefaultMapOptions()
	opts.MinVibration = 0.3
	opts.MinSuction = 0.3
	opts.Smoothing = 0

	opts.Sync = SyncVibrationOnly
	for _, f := range script.ToIntensityCurve(opts) {
		if f.Suction > 0.001 {
			t.Fatalf("SyncVibrationOnly liefert Sog %.3f", f.Suction)
		}
	}

	opts.Sync = SyncSuctionOnly
	for _, f := range script.ToIntensityCurve(opts) {
		if f.Vibration > 0.001 {
			t.Fatalf("SyncSuctionOnly liefert Vibration %.3f", f.Vibration)
		}
	}
}

// Im unabhängigen Modus folgt der Sog der POSITION, nicht der
// Geschwindigkeit. Das ist der Unterschied zu SyncSynchronized und der
// Grund, warum es beide Modi gibt.
func TestMapperIndependentUsesPositionForSuction(t *testing.T) {
	// Gleichmäßige Bewegung: die Geschwindigkeit ist konstant, die Position
	// steigt. Sog und Vibration müssen sich also unterscheiden.
	script := scriptFrom(
		Action{At: 0, Pos: 0},
		Action{At: 2000, Pos: 100},
	)
	opts := DefaultMapOptions()
	opts.Smoothing = 0
	opts.MinVibration = 0
	opts.MinSuction = 0
	opts.Sync = SyncIndependent

	frames := script.ToIntensityCurve(opts)
	if len(frames) < 10 {
		t.Fatalf("zu wenige Frames: %d", len(frames))
	}
	first, last := frames[0], frames[len(frames)-2]

	if math.Abs(first.Vibration-last.Vibration) > 0.05 {
		t.Errorf("bei gleichmäßiger Bewegung sollte die Vibration konstant bleiben: "+
			"%.3f -> %.3f", first.Vibration, last.Vibration)
	}
	if last.Suction <= first.Suction+0.5 {
		t.Errorf("der Sog sollte der steigenden Position folgen: %.3f -> %.3f",
			first.Suction, last.Suction)
	}
}

// Glättung darf die Kurve verzögern, aber nicht ihren Wertebereich sprengen.
func TestMapperSmoothingStaysInRange(t *testing.T) {
	script := scriptFrom(
		Action{At: 0, Pos: 0},
		Action{At: 100, Pos: 100},
		Action{At: 200, Pos: 0},
		Action{At: 300, Pos: 100},
	)
	opts := DefaultMapOptions()
	opts.Smoothing = 0.8

	frames := script.ToIntensityCurve(opts)
	for _, f := range frames {
		if f.Vibration < 0 || f.Vibration > 1 || f.Suction < 0 || f.Suction > 1 {
			t.Fatalf("Wert außerhalb 0-1 bei %dms: vib %.3f suc %.3f",
				f.At, f.Vibration, f.Suction)
		}
	}

	// Mit Glättung darf die Spitze nicht höher sein als ohne.
	opts.Smoothing = 0
	var rawMax float64
	for _, f := range script.ToIntensityCurve(opts) {
		if f.Vibration > rawMax {
			rawMax = f.Vibration
		}
	}
	opts.Smoothing = 0.8
	var smoothMax float64
	for _, f := range script.ToIntensityCurve(opts) {
		if f.Vibration > smoothMax {
			smoothMax = f.Vibration
		}
	}
	if smoothMax > rawMax+0.001 {
		t.Errorf("Glättung erhöht die Spitze: %.3f gegen %.3f", smoothMax, rawMax)
	}
}

// Randfälle dürfen nicht abstürzen.
func TestMapperEdgeCases(t *testing.T) {
	opts := DefaultMapOptions()

	if frames := scriptFrom().ToIntensityCurve(opts); frames != nil {
		t.Errorf("leeres Skript sollte nil liefern, lieferte %d Frames", len(frames))
	}
	if frames := scriptFrom(Action{At: 0, Pos: 50}).ToIntensityCurve(opts); frames != nil {
		t.Errorf("ein einzelnes Action sollte nil liefern, lieferte %d Frames", len(frames))
	}

	// Doppelte Zeitstempel dürfen keine Division durch null ergeben.
	duplicate := scriptFrom(
		Action{At: 0, Pos: 0},
		Action{At: 0, Pos: 100},
		Action{At: 500, Pos: 0},
	)
	for _, f := range duplicate.ToIntensityCurve(opts) {
		if math.IsNaN(f.Vibration) || math.IsInf(f.Vibration, 0) {
			t.Fatalf("doppelte Zeitstempel ergeben %v bei %dms", f.Vibration, f.At)
		}
	}

	// Unsinnige Optionen werden auf brauchbare Werte zurückgesetzt.
	broken := DefaultMapOptions()
	broken.TickMs = 0
	broken.MaxSpeed = 0
	frames := scriptFrom(
		Action{At: 0, Pos: 0}, Action{At: 1000, Pos: 100},
	).ToIntensityCurve(broken)
	if len(frames) == 0 {
		t.Fatal("mit ungültigen Optionen wurden keine Frames erzeugt")
	}
	for _, f := range frames {
		if math.IsNaN(f.Vibration) {
			t.Fatal("ungültige Optionen ergeben NaN")
		}
	}
}

// Die Frames müssen den gesamten Skriptbereich abdecken und zeitlich
// aufsteigen - sonst liefe die Wiedergabe rückwärts oder bräche früh ab.
func TestMapperFrameTimeline(t *testing.T) {
	script := scriptFrom(
		Action{At: 0, Pos: 0},
		Action{At: 1000, Pos: 100},
		Action{At: 2000, Pos: 0},
	)
	opts := DefaultMapOptions()
	opts.TickMs = 50

	frames := script.ToIntensityCurve(opts)
	if len(frames) < 40 {
		t.Fatalf("2 Sekunden bei 50ms sollten rund 41 Frames ergeben, sind %d", len(frames))
	}
	if frames[0].At != 0 {
		t.Errorf("erster Frame bei %dms statt 0", frames[0].At)
	}
	for i := 1; i < len(frames); i++ {
		if frames[i].At <= frames[i-1].At {
			t.Fatalf("Zeitstempel nicht aufsteigend bei %d: %d nach %d",
				i, frames[i].At, frames[i-1].At)
		}
	}
	if last := frames[len(frames)-1].At; last < 1950 {
		t.Errorf("letzter Frame bei %dms - das Skriptende fehlt", last)
	}
}
