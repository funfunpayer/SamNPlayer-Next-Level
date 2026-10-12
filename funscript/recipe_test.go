package funscript

import "testing"

func TestNormalizeProfile(t *testing.T) {
	if NormalizeProfile("TF") != ProfileTJ {
		t.Fatalf("tf sollte auf tj fallen, ist %q", NormalizeProfile("TF"))
	}
	if NormalizeProfile("tj") != ProfileTJ {
		t.Fatalf("tj kanonisch, ist %q", NormalizeProfile("tj"))
	}
	if !IsDistanceProfile("tf") || !IsDistanceProfile("TJ") {
		t.Fatal("tf/tj müssen Distanzprofile sein")
	}
	if IsDistanceProfile("standard") {
		t.Fatal("standard ist kein Distanzprofil")
	}
	if !IsStrokeProfile("standard") || !IsStrokeProfile("weich") || !IsStrokeProfile("autotune") {
		t.Fatal("standard/weich/autotune müssen Stroke-Profile sein")
	}
	if IsStrokeProfile("tj") || IsStrokeProfile("tf") {
		t.Fatal("tf/tj sind keine Stroke-Profile")
	}
	if !AllowsContactSettings("standard") || !AllowsContactSettings("tj") {
		t.Fatal("Stroke und Tf/Tj dürfen Contact-Settings erlauben")
	}
	if DefaultSyncForProfile("tj") != SyncSuctionPosition.String() {
		t.Fatal("tj DefaultSync muss suction_position sein")
	}
	if DefaultSyncForProfile("standard") != SyncIndependent.String() {
		t.Fatal("stroke DefaultSync muss independent sein")
	}
}

func TestRecipeTJSuctionOnlyNoVibration(t *testing.T) {
	script := scriptFrom(
		Action{At: 0, Pos: 20},
		Action{At: 800, Pos: 90},
		Action{At: 1600, Pos: 20},
	)
	opts := RecipeFor("tj")
	opts.Smoothing = 0
	frames := script.ToIntensityCurve(opts)
	if len(frames) == 0 {
		t.Fatal("keine Frames")
	}
	var maxSuc, maxVib float64
	var minSuc = 1.0
	for _, f := range frames {
		if f.Vibration > maxVib {
			maxVib = f.Vibration
		}
		if f.Suction > maxSuc {
			maxSuc = f.Suction
		}
		if f.Suction < minSuc {
			minSuc = f.Suction
		}
	}
	if maxVib > 0.001 {
		t.Errorf("tf/tj darf nicht vibrieren, max %.3f", maxVib)
	}
	if maxSuc < 0.7 {
		t.Errorf("Sog muss der hohen Position folgen, max %.3f", maxSuc)
	}
	// Floor comes from the 20–90 script clamp (pos 20 → suc 0.20), not from
	// a second liftFloor on MinSuction — see TestRecipeTJSuctionNoDoubleFloor.
	if minSuc < 0.15 || minSuc > 0.25 {
		t.Errorf("Sog-Boden sollte ~0.20 aus dem Pos-Clamp sein, ist %.3f", minSuc)
	}
	if maxSuc > 0.95 {
		t.Errorf("Sog-Maximum sollte ~0.90 aus Pos 90 sein (kein Double-Floor), ist %.3f", maxSuc)
	}
}

// GEMESSEN/geprüft (docs/FINDINGS_TIMING_TF.md): Pos 20 + MinSuction 0.20
// darf NICHT über liftFloor zu ~0.36 werden — der 20–90-Clamp ist bereits
// der Boden. Sonst liegt Dauer-Sog unnötig hoch und die Dynamik schrumpft.
func TestRecipeTJSuctionNoDoubleFloor(t *testing.T) {
	script := scriptFrom(
		Action{At: 0, Pos: 20},
		Action{At: 500, Pos: 20},
		Action{At: 1000, Pos: 90},
		Action{At: 1500, Pos: 90},
	)
	opts := RecipeFor("tj")
	opts.Smoothing = 0
	frames := script.ToIntensityCurve(opts)
	var atLow, atHigh float64
	var sawLow, sawHigh bool
	for _, f := range frames {
		if f.At <= 400 {
			atLow = f.Suction
			sawLow = true
		}
		if f.At >= 1200 && f.At <= 1400 {
			atHigh = f.Suction
			sawHigh = true
		}
	}
	if !sawLow || !sawHigh {
		t.Fatal("expected both low and high windows")
	}
	if atLow < 0.18 || atLow > 0.22 {
		t.Fatalf("pos=20 should map to ~0.20 suction, got %.3f (double-floor would be ~0.36)", atLow)
	}
	if atHigh < 0.88 || atHigh > 0.92 {
		t.Fatalf("pos=90 should map to ~0.90 suction, got %.3f (double-floor would be ~0.92)", atHigh)
	}
	// Recipe still documents MinSuction=0.20 as the script-space floor.
	if opts.MinSuction != 0.20 {
		t.Fatalf("recipe MinSuction should stay 0.20 in metadata, got %.2f", opts.MinSuction)
	}
}

// Ohne ContactVibration bleibt tf/tj wie bisher stumm, auch bei tiefem
// Kontakt (Pos nahe Maximum) - reine Opt-in-Erweiterung, kein geändertes
// Standardverhalten.
func TestRecipeTJContactVibrationOffByDefault(t *testing.T) {
	script := scriptFrom(
		Action{At: 0, Pos: 20},
		Action{At: 500, Pos: 90},
		Action{At: 1000, Pos: 90},
		Action{At: 1500, Pos: 20},
	)
	opts := RecipeFor("tj")
	opts.Smoothing = 0
	frames := script.ToIntensityCurve(opts)
	for _, f := range frames {
		if f.Vibration > 0.001 {
			t.Fatalf("ContactVibration nicht gesetzt, aber Vibration %.3f bei %dms", f.Vibration, f.At)
		}
	}
}

// Mit ContactVibration steigt die Vibration nur, wenn die Position nahe ihr
// eigenes, im Skript beobachtetes Maximum kommt (simulierter Kontakt), und
// bleibt davor/danach bei 0 - Dauer/Stärke kommen aus dem Positionssignal
// selbst, nicht aus einem festen Impuls.
func TestRecipeTJContactVibrationTracksPosition(t *testing.T) {
	script := scriptFrom(
		Action{At: 0, Pos: 20}, // fern
		Action{At: 500, Pos: 20},
		Action{At: 600, Pos: 90},  // Kontakt: nahe am beobachteten Maximum
		Action{At: 900, Pos: 90},  // Kontakt hält an - lange Berührung
		Action{At: 1000, Pos: 20}, // wieder fern
		Action{At: 1500, Pos: 20},
	)
	opts := RecipeFor("tj")
	opts.ContactVibration = true
	opts.ContactVibrationEnvelope = -1 // präzise Assertions ohne Envelope
	opts.Smoothing = 0
	frames := script.ToIntensityCurve(opts)
	if len(frames) == 0 {
		t.Fatal("keine Frames")
	}
	var vibDuringFar, vibDuringContact, maxVib float64
	var contactFrames int
	for _, f := range frames {
		if f.At < 400 || (f.At > 1100 && f.At < 1500) {
			if f.Vibration > vibDuringFar {
				vibDuringFar = f.Vibration
			}
		}
		if f.At >= 700 && f.At <= 850 {
			contactFrames++
			if f.Vibration > vibDuringContact {
				vibDuringContact = f.Vibration
			}
		}
		if f.Vibration > maxVib {
			maxVib = f.Vibration
		}
	}
	if vibDuringFar > 0.001 {
		t.Errorf("fern von Kontakt sollte Vibration 0 sein, ist %.3f", vibDuringFar)
	}
	if contactFrames == 0 {
		t.Fatal("Testfenster für Kontakt leer - Test defekt")
	}
	if vibDuringContact < 0.5 {
		t.Errorf("während anhaltendem Kontakt sollte Vibration deutlich steigen, max %.3f", vibDuringContact)
	}
	if maxVib > 1.0001 {
		t.Errorf("Vibration über 1.0: %.4f", maxVib)
	}
}

// docs/NEXT.md Priorität 5 nannte das offen: bisher nur an einem Clip
// geprüft, dessen Rhythmus Kontakt über einen großen Teil der Laufzeit hält
// - ungetestet, ob die Hüllkurve auch bei einer kurzen, flüchtigen Berührung
// (ein rascher Antippen-und-Loslassen statt eines anhaltenden Kontakts)
// noch natürlich wirkt. Da die Hüllkurve rein aus der Position abgeleitet
// wird, nicht aus einer festen Impulsdauer, ist die Erwartung: derselbe
// Spitzenwert (Position erreicht dasselbe beobachtete Maximum) soll
// dieselbe Spitzen-Vibration ergeben, aber über deutlich weniger Frames -
// "Dauer/Stärke kommen aus dem Signal selbst" muss für kurze Berührungen
// genauso gelten wie für lange, nicht nur für lange.
func TestRecipeTJContactVibrationBriefGraze(t *testing.T) {
	sustained := scriptFrom(
		Action{At: 0, Pos: 20},
		Action{At: 500, Pos: 20},
		Action{At: 600, Pos: 90},
		Action{At: 900, Pos: 90}, // anhaltender Kontakt: 300ms auf dem Maximum
		Action{At: 1000, Pos: 20},
		Action{At: 1500, Pos: 20},
	)
	brief := scriptFrom(
		Action{At: 0, Pos: 20},
		Action{At: 500, Pos: 20},
		Action{At: 600, Pos: 90}, // kurzes Antippen: sofort wieder los
		Action{At: 650, Pos: 20},
		Action{At: 1500, Pos: 20},
	)

	countAndPeak := func(script *Script) (framesAboveHalf int, peak float64) {
		opts := RecipeFor("tj")
		opts.ContactVibration = true
		opts.ContactVibrationEnvelope = -1 // präzise Assertions ohne Envelope
		opts.Smoothing = 0
		for _, f := range script.ToIntensityCurve(opts) {
			if f.Vibration > peak {
				peak = f.Vibration
			}
			if f.Vibration > 0.5 {
				framesAboveHalf++
			}
		}
		return
	}

	sustainedFrames, sustainedPeak := countAndPeak(sustained)
	briefFrames, briefPeak := countAndPeak(brief)

	if briefPeak < 0.9 {
		t.Errorf("kurzes Antippen erreicht dasselbe beobachtete Maximum wie der anhaltende Kontakt - "+
			"Spitzen-Vibration sollte trotzdem hoch sein, ist %.3f", briefPeak)
	}
	if sustainedPeak < 0.9 {
		t.Fatalf("Testannahme verletzt: anhaltender Kontakt sollte nahe 1.0 erreichen, ist %.3f", sustainedPeak)
	}
	if briefFrames == 0 {
		t.Error("kurzes Antippen löst gar keine spürbare Vibration aus - Feature reagiert nicht auf kurze Berührungen")
	}
	if briefFrames >= sustainedFrames {
		t.Errorf("kurzes Antippen (%d Frames über 0.5) sollte deutlich kürzer nachwirken als der anhaltende "+
			"Kontakt (%d Frames) - die Hüllkurve darf eine flüchtige Berührung nicht künstlich in die Länge ziehen",
			briefFrames, sustainedFrames)
	}
}

// Ein Skript ohne nennenswerte Positions-Spannweite (z.B. nur ein kurzer,
// flacher Ausschlag) darf nicht dauerhaft vibrieren - sonst würde jede
// normale Bewegung als "Kontakt" fehlinterpretiert.
func TestRecipeTJContactVibrationNeedsSpan(t *testing.T) {
	script := scriptFrom(
		Action{At: 0, Pos: 50},
		Action{At: 500, Pos: 52},
		Action{At: 1000, Pos: 50},
	)
	opts := RecipeFor("tj")
	opts.ContactVibration = true
	opts.ContactVibrationEnvelope = -1 // präzise Assertions ohne Envelope
	opts.Smoothing = 0
	frames := script.ToIntensityCurve(opts)
	for _, f := range frames {
		if f.Vibration > 0.001 {
			t.Fatalf("zu geringe Spannweite sollte keinen Kontakt auslösen, Vibration %.3f bei %dms", f.Vibration, f.At)
		}
	}
}

// Niedrigerer ContactVibrationSpan = früher an: bei derselben Position
// mittig im Spektrum muss soft-span Vib auslösen, default-span noch nicht.
func TestRecipeTJContactVibrationSpanEarlier(t *testing.T) {
	script := scriptFrom(
		Action{At: 0, Pos: 20},
		Action{At: 500, Pos: 55}, // mittig: bei span=0.75 unter Schwelle, bei 0.45 darüber
		Action{At: 1000, Pos: 55},
		Action{At: 1500, Pos: 90},
	)
	atMid := func(span float64) float64 {
		opts := RecipeFor("tj")
		opts.ContactVibration = true
		opts.ContactVibrationEnvelope = -1 // präzise Assertions ohne Envelope
		opts.ContactVibrationSpan = span
		opts.Smoothing = 0
		var max float64
		for _, f := range script.ToIntensityCurve(opts) {
			if f.At >= 500 && f.At <= 1000 && f.Vibration > max {
				max = f.Vibration
			}
		}
		return max
	}
	if v := atMid(0.75); v > 0.001 {
		t.Errorf("Default-Span 0.75 sollte bei Pos 55 noch still sein, vib=%.3f", v)
	}
	if v := atMid(0.45); v < 0.05 {
		t.Errorf("Span 0.45 (früher an) sollte bei Pos 55 schon vibrieren, vib=%.3f", v)
	}
}

// soft (t²) bleibt unter linear, peak (√t) darüber - bei gleicher Position
// im Kontaktfenster, damit die Kurvenwahl spürbar und video-treu bleibt.
func TestRecipeTJContactVibrationCurves(t *testing.T) {
	script := scriptFrom(
		Action{At: 0, Pos: 20},
		Action{At: 500, Pos: 80}, // im Kontaktfenster, aber nicht am Peak
		Action{At: 800, Pos: 80},
		Action{At: 1200, Pos: 90},
		Action{At: 1500, Pos: 20},
	)
	vibAt := func(curve string) float64 {
		opts := RecipeFor("tj")
		opts.ContactVibration = true
		opts.ContactVibrationEnvelope = -1 // präzise Assertions ohne Envelope
		opts.ContactVibrationCurve = curve
		opts.Smoothing = 0
		var max float64
		for _, f := range script.ToIntensityCurve(opts) {
			if f.At >= 550 && f.At <= 750 && f.Vibration > max {
				max = f.Vibration
			}
		}
		return max
	}
	linear, soft, peak := vibAt("linear"), vibAt("soft"), vibAt("peak")
	if linear < 0.1 {
		t.Fatalf("Testannahme: Pos 80 sollte im Kontaktfenster liegen, linear=%.3f", linear)
	}
	if soft >= linear {
		t.Errorf("soft (weicher Einstieg) sollte unter linear liegen: soft=%.3f linear=%.3f", soft, linear)
	}
	if peak <= linear {
		t.Errorf("peak (stärkerer Peak) sollte über linear liegen: peak=%.3f linear=%.3f", peak, linear)
	}
	impulse := vibAt("impulse")
	// Mid-window (pos 80 of 20–90, span 0.75 → contactMin=72.5, t≈0.43) must
	// stay quiet under impulse; soft still has fill there.
	if impulse > 0.05 {
		t.Errorf("impulse should suppress mid-window fill: impulse=%.3f (soft=%.3f)", impulse, soft)
	}
}

func TestApplyContactCurveImpulsePeaks(t *testing.T) {
	if applyContactCurve(0.5, ContactCurveImpulse) != 0 {
		t.Fatalf("impulse below threshold must be 0, got %v", applyContactCurve(0.5, ContactCurveImpulse))
	}
	near := applyContactCurve(0.9, ContactCurveImpulse)
	top := applyContactCurve(1.0, ContactCurveImpulse)
	if near <= 0 || near >= top {
		t.Fatalf("impulse should rise sharply near peak: near=%.3f top=%.3f", near, top)
	}
	if top < 0.999 {
		t.Fatalf("impulse at t=1 want ~1, got %v", top)
	}
	if NormalizeContactCurve("events") != ContactCurveImpulse {
		t.Fatalf("events alias → impulse")
	}
}

// Tracker-Verlustfenster: Vibration muss aus, auch wenn die gehaltene
// Position noch im Kontaktbereich liegt (sonst brummt es weiter).
func TestRecipeTJContactVibrationMutedInTrackingGap(t *testing.T) {
	script := scriptFrom(
		Action{At: 0, Pos: 20},
		Action{At: 500, Pos: 90},
		Action{At: 1000, Pos: 90},
		Action{At: 1500, Pos: 20},
	)
	opts := RecipeFor("tj")
	opts.ContactVibration = true
	opts.ContactVibrationEnvelope = -1
	opts.Smoothing = 0
	opts.TrackingGaps = []TrackingGap{{StartMs: 600, EndMs: 900}}
	var vibInGap, vibOutside float64
	for _, f := range script.ToIntensityCurve(opts) {
		if f.At >= 650 && f.At <= 850 {
			if f.Vibration > vibInGap {
				vibInGap = f.Vibration
			}
		}
		if f.At >= 950 && f.At <= 1000 {
			if f.Vibration > vibOutside {
				vibOutside = f.Vibration
			}
		}
	}
	if vibInGap > 0.001 {
		t.Errorf("im Tracking-Gap sollte Vibration 0 sein, ist %.3f", vibInGap)
	}
	if vibOutside < 0.5 {
		t.Errorf("außerhalb des Gaps bei Pos 90 sollte Vibration hoch sein, ist %.3f", vibOutside)
	}
}

func TestRecipeTJContactGapWinsOverEnvelopeAndSmoothing(t *testing.T) {
	// Envelope/Smoothing dürfen prevVib nicht in den Gap tragen.
	script := scriptFrom(
		Action{At: 0, Pos: 90},
		Action{At: 200, Pos: 90},
		Action{At: 800, Pos: 90},
		Action{At: 1000, Pos: 20},
	)
	opts := RecipeFor("tj")
	opts.ContactVibration = true
	opts.ContactVibrationEnvelope = 0.45
	opts.Smoothing = 0.3
	opts.TrackingGaps = []TrackingGap{{StartMs: 250, EndMs: 700}}
	for _, f := range script.ToIntensityCurve(opts) {
		if f.At >= 350 && f.At <= 650 && f.Vibration > 0.001 {
			t.Fatalf("Gap-Mute muss Envelope/Smoothing schlagen: at=%d vib=%.4f", f.At, f.Vibration)
		}
	}
}

func TestRecipeStandardContactVibrationTracksPosition(t *testing.T) {
	// Stroke profile (SyncIndependent): contact vib owns vibe only in the
	// deep slice; far from peak, speed-based vibe may still be non-zero on
	// transitions — assert deep peak is high and a flat far window is low.
	script := scriptFrom(
		Action{At: 0, Pos: 20},
		Action{At: 800, Pos: 20}, // flat far — intensity ~0
		Action{At: 900, Pos: 90},
		Action{At: 1200, Pos: 90}, // flat deep — contact on, intensity ~0
		Action{At: 1300, Pos: 20},
		Action{At: 1800, Pos: 20},
	)
	opts := DefaultMapOptions()
	opts.ContactVibration = true
	opts.ContactVibrationEnvelope = -1
	opts.Smoothing = 0
	opts.MinVibration = 0
	frames := script.ToIntensityCurve(opts)
	var vibFar, vibDeep float64
	for _, f := range frames {
		if f.At >= 200 && f.At <= 700 && f.Vibration > vibFar {
			vibFar = f.Vibration
		}
		if f.At >= 1000 && f.At <= 1150 && f.Vibration > vibDeep {
			vibDeep = f.Vibration
		}
	}
	if vibFar > 0.05 {
		t.Errorf("flat far window should stay near 0 vibe, got %.3f", vibFar)
	}
	if vibDeep < 0.5 {
		t.Errorf("flat deep window should drive contact vibe, got %.3f", vibDeep)
	}
}

func TestRecipeTFEqualsTJ(t *testing.T) {
	a, b := RecipeFor("tf"), RecipeFor("tj")
	if a.Sync != b.Sync || a.MinSuction != b.MinSuction {
		t.Fatalf("tf und tj müssen dasselbe Rezept sein: %+v vs %+v", a, b)
	}
	if a.Sync != SyncSuctionPosition {
		t.Fatalf("erwartet suction_position, ist %s", a.Sync)
	}
}
