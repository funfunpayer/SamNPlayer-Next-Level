package player

import (
	"context"
	"errors"
	"sync"
	"testing"
	"time"
)

// recordingDevice hält fest, welche Intensitäten gesetzt wurden - damit
// prüfbar ist, dass nach einem Stopp-Wunsch tatsächlich auf 0 gefahren wird
// und nicht nur die Buchführung stimmt.
type recordingDevice struct {
	mu         sync.Mutex
	vibrations []float64
	suctions   []float64
	stops      int
}

func (d *recordingDevice) SetVibration(v float64) error {
	d.mu.Lock()
	defer d.mu.Unlock()
	d.vibrations = append(d.vibrations, v)
	return nil
}

func (d *recordingDevice) SetSuction(v float64) error {
	d.mu.Lock()
	defer d.mu.Unlock()
	d.suctions = append(d.suctions, v)
	return nil
}

func (d *recordingDevice) lastSuction() float64 {
	d.mu.Lock()
	defer d.mu.Unlock()
	if len(d.suctions) == 0 {
		return -1
	}
	return d.suctions[len(d.suctions)-1]
}

func (d *recordingDevice) maxSuction() float64 {
	d.mu.Lock()
	defer d.mu.Unlock()
	m := 0.0
	for _, v := range d.suctions {
		if v > m {
			m = v
		}
	}
	return m
}

func (d *recordingDevice) Stop() error {
	d.mu.Lock()
	defer d.mu.Unlock()
	d.stops++
	return nil
}

func (d *recordingDevice) last() float64 {
	d.mu.Lock()
	defer d.mu.Unlock()
	if len(d.vibrations) == 0 {
		return -1
	}
	return d.vibrations[len(d.vibrations)-1]
}

func (d *recordingDevice) max() float64 {
	d.mu.Lock()
	defer d.mu.Unlock()
	m := 0.0
	for _, v := range d.vibrations {
		if v > m {
			m = v
		}
	}
	return m
}

func (d *recordingDevice) count() int {
	d.mu.Lock()
	defer d.mu.Unlock()
	return len(d.vibrations)
}

// vibrationsContainApprox reports whether any recorded vibration write is
// within tol of want - used where a value must have been reached and held
// at some point, but isn't necessarily (or shouldn't be, once a later
// rundown changes it) the LAST recorded value.
func (d *recordingDevice) vibrationsContainApprox(want, tol float64) bool {
	d.mu.Lock()
	defer d.mu.Unlock()
	for _, v := range d.vibrations {
		if v >= want-tol && v <= want+tol {
			return true
		}
	}
	return false
}

// suctionsContainApprox mirrors vibrationsContainApprox for the Suction channel.
func (d *recordingDevice) suctionsContainApprox(want, tol float64) bool {
	d.mu.Lock()
	defer d.mu.Unlock()
	for _, v := range d.suctions {
		if v >= want-tol && v <= want+tol {
			return true
		}
	}
	return false
}

func fastOptions(cycles int) TrainingOptions {
	return TrainingOptions{
		Technique:     TechniqueStopStart,
		Channel:       ChannelVibration,
		Cycles:        cycles,
		RampUpMs:      300,
		HoldMs:        2000,
		RestMs:        100,
		PeakIntensity: 1.0,
	}
}

// Der Kern der Stop-Start-Methode: der Anwender bricht ab, nicht die
// Stoppuhr. Vorher lief RunTraining eine feste Zeitkette ohne jede
// Eingriffsmöglichkeit - die einzige Steuerung war der Abbruch der ganzen
// Session.
func TestStopCycleInterruptsHoldAndContinues(t *testing.T) {
	dev := &recordingDevice{}
	control := NewTrainingControl()
	opts := fastOptions(2)

	var results []TrainingCycleResult
	var mu sync.Mutex
	done := make(chan error, 1)

	go func() {
		done <- RunTrainingWithControl(context.Background(), dev, opts, control,
			func(r TrainingCycleResult) {
				mu.Lock()
				results = append(results, r)
				mu.Unlock()
			})
	}()

	// Nach der Rampe (300ms) mitten in die Haltezeit (2000ms) hinein stoppen.
	time.Sleep(500 * time.Millisecond)
	started := time.Now()
	control.StopCycle()

	select {
	case err := <-done:
		if err != nil {
			t.Fatalf("RunTraining: %v", err)
		}
	case <-time.After(10 * time.Second):
		t.Fatal("Training ist nicht beendet worden - Stopp hat den Ablauf blockiert")
	}

	mu.Lock()
	defer mu.Unlock()
	if len(results) != 2 {
		t.Fatalf("erwartet 2 Zyklen, bekam %d", len(results))
	}
	if !results[0].StoppedByUser {
		t.Error("erster Zyklus muss als vom Nutzer gestoppt markiert sein")
	}
	if results[1].StoppedByUser {
		t.Error("zweiter Zyklus darf nicht als gestoppt gelten - ein Druck aus dem " +
			"vorherigen Zyklus darf nicht nachwirken")
	}
	// Der Stopp muss schnell wirken. Ohne unterbrechbares Warten würde erst
	// nach der vollen Haltezeit von 2000ms reagiert.
	if elapsed := results[0].EndedAt.Sub(started); elapsed > 1200*time.Millisecond {
		t.Errorf("Stopp wirkte erst nach %v - die Haltezeit wird nicht unterbrochen", elapsed)
	}
	if results[0].ReachedPeakAfterMs <= 0 {
		t.Error("ReachedPeakAfterMs muss gesetzt sein")
	}
	if dev.last() != 0 {
		t.Errorf("nach dem Training muss die Intensität 0 sein, ist %v", dev.last())
	}
}

// Ein Druck während der Hochfahrrampe muss diese sofort abbrechen, statt sie
// noch zu Ende laufen zu lassen.
func TestStopCycleInterruptsRamp(t *testing.T) {
	dev := &recordingDevice{}
	control := NewTrainingControl()
	opts := fastOptions(1)
	opts.RampUpMs = 2000

	done := make(chan error, 1)
	go func() {
		done <- RunTrainingWithControl(context.Background(), dev, opts, control, nil)
	}()

	time.Sleep(200 * time.Millisecond)
	control.StopCycle()

	select {
	case err := <-done:
		if err != nil {
			t.Fatalf("RunTraining: %v", err)
		}
	case <-time.After(10 * time.Second):
		t.Fatal("Rampe wurde nicht abgebrochen")
	}

	// Bei 2s Rampe und Abbruch nach 200ms darf der Höhepunkt nicht erreicht
	// worden sein.
	if m := dev.max(); m > 0.6 {
		t.Errorf("Rampe lief trotz Stopp bis %v weiter", m)
	}
}

// Ohne Stopp-Wunsch muss sich alles verhalten wie bisher.
func TestTrainingWithoutStopUnchanged(t *testing.T) {
	dev := &recordingDevice{}
	opts := fastOptions(2)
	opts.HoldMs = 50

	var results []TrainingCycleResult
	err := RunTraining(context.Background(), dev, opts, func(r TrainingCycleResult) {
		results = append(results, r)
	})
	if err != nil {
		t.Fatalf("RunTraining: %v", err)
	}
	if len(results) != 2 {
		t.Fatalf("erwartet 2 Zyklen, bekam %d", len(results))
	}
	for i, r := range results {
		if r.StoppedByUser {
			t.Errorf("Zyklus %d fälschlich als gestoppt markiert", i)
		}
	}
	if m := dev.max(); m < 0.9 {
		t.Errorf("Höhepunkt wurde nicht erreicht (max %v)", m)
	}
}

// Ein nil-Control darf nicht abstürzen - RunTraining reicht genau das durch.
func TestNilControlIsSafe(t *testing.T) {
	var control *TrainingControl
	control.StopCycle()
	if control.requested() {
		t.Error("nil-Control darf keinen Stopp melden")
	}
	control.drain()
}

// Die Rückmeldung soll den nächsten Zyklus regeln, nicht nur protokolliert
// werden. Ohne sie ist der Ablauf gesteuert, aber nicht geregelt.
func TestArousalAdjustsNextCycle(t *testing.T) {
	base := 0.8
	baseHold, baseRest := 4000, 1000

	// Am Ziel: nichts ändert sich.
	peak, hold, rest := adjustForArousal(ArousalTarget, base, baseHold, baseRest)
	if peak != base || hold != baseHold || rest != baseRest {
		t.Errorf("am Zielwert darf nichts angepasst werden: %v %d %d", peak, hold, rest)
	}

	// Hohe Erregung: kürzer, sanfter, längere Pause.
	peakHigh, holdHigh, restHigh := adjustForArousal(10, base, baseHold, baseRest)
	if holdHigh >= baseHold {
		t.Errorf("hohe Erregung muss die Haltezeit verkürzen (%d >= %d)", holdHigh, baseHold)
	}
	if restHigh <= baseRest {
		t.Errorf("hohe Erregung muss die Pause verlängern (%d <= %d)", restHigh, baseRest)
	}
	if peakHigh >= base {
		t.Errorf("hohe Erregung muss den Höhepunkt senken (%v >= %v)", peakHigh, base)
	}

	// Niedrige Erregung: länger, aber der Höhepunkt wird NICHT über die
	// Einstellung hinaus angehoben.
	peakLow, holdLow, restLow := adjustForArousal(2, base, baseHold, baseRest)
	if holdLow <= baseHold {
		t.Errorf("niedrige Erregung sollte die Haltezeit verlängern (%d <= %d)", holdLow, baseHold)
	}
	if restLow != baseRest {
		t.Errorf("Pause darf bei niedriger Erregung unverändert bleiben (%d > %d)", restLow, baseRest)
	}
	if peakLow > base {
		t.Errorf("Höhepunkt darf die Einstellung nie überschreiten (%v > %v)", peakLow, base)
	}

	// Gegensteuern nach oben muss stärker ausfallen als nach unten:
	// zu früh zu weit zu gehen ist der teurere Fehler.
	downShift := float64(baseHold-holdHigh) / float64(baseHold)
	upShift := float64(holdLow-baseHold) / float64(baseHold)
	if downShift <= upShift {
		t.Errorf("Dämpfung bei hoher Erregung (%.2f) muss stärker sein als "+
			"Verlängerung bei niedriger (%.2f)", downShift, upShift)
	}
}

// Ungültige Werte dürfen nicht als Rückmeldung durchgehen - eine
// versehentliche 0 aus einem leeren Eingabefeld wäre sonst "sehr entspannt"
// und würde die Intensität hochtreiben.
func TestArousalIgnoresInvalidValues(t *testing.T) {
	control := NewTrainingControl()
	for _, invalid := range []int{0, -3, 11, 999} {
		control.ReportArousal(invalid)
		if got := control.takeArousal(); got != 0 {
			t.Errorf("ungültiger Wert %d wurde übernommen (%d)", invalid, got)
		}
	}
	control.ReportArousal(8)
	if got := control.takeArousal(); got != 8 {
		t.Errorf("gültiger Wert ging verloren: %d", got)
	}
}

// Jede Rückmeldung wirkt auf genau einen Zyklus - sonst würde eine einmalige
// hohe Meldung den Rest der Session dämpfen.
func TestArousalAppliesToOneCycleOnly(t *testing.T) {
	control := NewTrainingControl()
	control.ReportArousal(9)
	if got := control.takeArousal(); got != 9 {
		t.Fatalf("erste Abfrage: %d", got)
	}
	if got := control.takeArousal(); got != 0 {
		t.Errorf("Rückmeldung wirkte ein zweites Mal: %d", got)
	}
}

// Ende-zu-Ende: eine hohe Meldung muss den folgenden Zyklus messbar dämpfen.
func TestArousalReachesRunningSession(t *testing.T) {
	dev := &recordingDevice{}
	control := NewTrainingControl()
	opts := fastOptions(2)
	opts.HoldMs = 600
	opts.RampUpMs = 100

	var results []TrainingCycleResult
	var mu sync.Mutex
	done := make(chan error, 1)
	// Report before the session starts so cycle 0 always sees it. Reporting
	// after go RunTraining raced takeArousal on CI (arousal landed on cycle 1).
	control.ReportArousal(10)
	go func() {
		done <- RunTrainingWithControl(context.Background(), dev, opts, control,
			func(r TrainingCycleResult) {
				mu.Lock()
				results = append(results, r)
				mu.Unlock()
			})
	}()

	select {
	case err := <-done:
		if err != nil {
			t.Fatalf("RunTraining: %v", err)
		}
	case <-time.After(15 * time.Second):
		t.Fatal("Training nicht beendet")
	}

	mu.Lock()
	defer mu.Unlock()
	if len(results) != 2 {
		t.Fatalf("erwartet 2 Zyklen, bekam %d", len(results))
	}
	if results[0].ArousalBefore != 10 {
		t.Errorf("Rückmeldung kam nicht im ersten Zyklus an: %d", results[0].ArousalBefore)
	}
	if results[0].HoldMs >= opts.HoldMs {
		t.Errorf("Haltezeit wurde nicht gedämpft: %d >= %d", results[0].HoldMs, opts.HoldMs)
	}
	if results[1].ArousalBefore != 0 {
		t.Errorf("Rückmeldung wirkte in den zweiten Zyklus nach: %d", results[1].ArousalBefore)
	}
}

// TestContextCancelDuringHoldStopsImmediately prüft, dass ein Context-
// Abbruch während der Haltezeit sofort wirkt - kein weiterer Gerätebefehl
// mehr, sobald ctx.Done() ausgelöst wurde. Anders als beim StopCycle-Pfad
// oben (der bewusst weiterläuft und geordnet auf 0 fährt) ist ein
// Context-Abbruch ein härterer Stopp (App schließt, Session wird
// verworfen) - waitInterruptible wartete zwar auch dabei kürzer als die
// volle Haltezeit, verschluckte den ctx-Fehler aber und ließ
// RunTrainingWithControl in den normalen Rampe-Pfad weiterlaufen, der dort
// noch einen SetVibration-Aufruf absetzte, bevor der Fehler eine Ebene
// später (in rampChannel) doch noch bemerkt wurde.
func TestContextCancelDuringHoldStopsImmediately(t *testing.T) {
	dev := &recordingDevice{}
	control := NewTrainingControl()
	opts := TrainingOptions{
		Technique:     TechniqueStopStart,
		Channel:       ChannelVibration,
		Cycles:        1,
		RampUpMs:      100,
		HoldMs:        5000,
		RestMs:        100,
		PeakIntensity: 1.0,
	}

	ctx, cancel := context.WithCancel(context.Background())
	done := make(chan error, 1)
	go func() {
		done <- RunTrainingWithControl(ctx, dev, opts, control, nil)
	}()

	// Warten, bis die Rampe fertig ist (Höhepunkt erreicht), dann mitten in
	// die Haltezeit hinein abbrechen.
	time.Sleep(300 * time.Millisecond)
	countAtCancel := dev.count()
	cancel()

	select {
	case err := <-done:
		if !errors.Is(err, context.Canceled) {
			t.Fatalf("erwartet context.Canceled, bekam: %v", err)
		}
	case <-time.After(5 * time.Second):
		t.Fatal("RunTrainingWithControl hat auf den Context-Abbruch nicht reagiert")
	}

	if got := dev.count(); got != countAtCancel {
		t.Errorf("nach dem Context-Abbruch während der Haltezeit wurden noch %d "+
			"weitere SetVibration-Aufrufe gemacht (erwartet 0) - waitInterruptible "+
			"hat den ctx-Fehler verschluckt statt ihn sofort weiterzugeben",
			got-countAtCancel)
	}
}

// ---- TrainingScript (Multi-Phasen, pro Kanal eigene Kurve) ----

// Zwei Phasen, jede mit genau einem aktiven Kanal: Phase 2 lässt Vibration
// unangetastet (nil) - der Kanal muss WÄHREND Phase 2 auf dem Wert bleiben,
// den Phase 1 zuletzt gesetzt hat (nicht sofort auf 0 zurückfallen), wird
// aber am Ende des GESAMTEN Skripts trotzdem sanft auf 0 heruntergefahren
// (siehe TestRunTrainingScriptRampsDownElevatedChannelsAtNaturalEnd) statt
// bis zum abrupten dev.Stop() erhöht stehen zu bleiben.
func TestRunTrainingScriptCarriesUntouchedChannelBetweenPhases(t *testing.T) {
	dev := &recordingDevice{}
	script := TrainingScript{
		Phases: []TrainingPhase{
			{
				Name:         "Vibration",
				Vibration:    curve(ChannelVibration, 0.2, 0.8, 0.2, 50, 50, 50),
				RepeatCycles: 1,
				RestMs:       10,
			},
			{
				Name:         "Suction only",
				Suction:      curve(ChannelSuction, 0, 0.6, 0, 50, 50, 50),
				RepeatCycles: 1,
				RestMs:       10,
			},
		},
	}

	var results []TrainingScriptCycleResult
	err := RunTrainingScript(context.Background(), dev, script, nil, func(r TrainingScriptCycleResult) {
		results = append(results, r)
	})
	if err != nil {
		t.Fatalf("RunTrainingScript: %v", err)
	}
	if len(results) != 2 {
		t.Fatalf("erwartet 2 Wiederholungen, bekam %d", len(results))
	}
	if results[0].PhaseName != "Vibration" || results[1].PhaseName != "Suction only" {
		t.Errorf("Phasennamen falsch: %q / %q", results[0].PhaseName, results[1].PhaseName)
	}
	// Phase 1 endet bei EndLevel 0.2 - Phase 2 fasst Vibration nicht an,
	// also muss dieser Wert irgendwann im Verlauf erreicht und gehalten
	// worden sein (Toleranz wegen der schrittweisen Rampe in rampChannel),
	// bevor der abschließende Rundown ihn wieder auf 0 fährt.
	if !dev.vibrationsContainApprox(0.2, 0.01) {
		t.Errorf("Vibration hat den aus Phase 1 übernommenen Wert 0.2 nie erreicht/gehalten: %v", dev.vibrations)
	}
	if got := dev.maxSuction(); got < 0.55 {
		t.Errorf("Sog-Höhepunkt aus Phase 2 wurde nicht erreicht: max=%v", got)
	}
	// Das Skript endet natürlich (kein Interrupt) mit Vibration noch bei
	// 0.2 (Phase 2 hat sie nie angefasst) - der abschließende Rundown muss
	// sie trotzdem auf 0 fahren, statt sie bis zum abrupten dev.Stop()
	// erhöht stehen zu lassen.
	if got := dev.last(); got != 0 {
		t.Errorf("Vibration wurde am Skriptende nicht auf 0 heruntergefahren: %v", got)
	}
	if got := dev.lastSuction(); got != 0 {
		t.Errorf("Sog wurde am Skriptende nicht auf 0 heruntergefahren: %v", got)
	}
}

// Der eigentliche Kern des Follow-up-Designs: eine hohe Rückmeldung muss
// BEIDE gleichzeitig aktiven Kanäle schwächen und die gemeinsame Pause
// verlängern - nicht nur den einen Kanal, den eine einzelne geteilte Kurve
// (wie bei TrainingOptions.Channel=Both) zufällig träfe.
func TestRunTrainingScriptArousalWeakensBothChannelsAndLengthensRest(t *testing.T) {
	dev := &recordingDevice{}
	control := NewTrainingControl()
	control.ReportArousal(10)

	script := TrainingScript{
		Phases: []TrainingPhase{
			{
				Name:         "Both channels",
				Vibration:    curve(ChannelVibration, 0, 0.8, 0, 50, 50, 50),
				Suction:      curve(ChannelSuction, 0, 0.6, 0, 50, 50, 50),
				RepeatCycles: 1,
				RestMs:       1000,
			},
		},
	}

	var results []TrainingScriptCycleResult
	err := RunTrainingScript(context.Background(), dev, script, control, func(r TrainingScriptCycleResult) {
		results = append(results, r)
	})
	if err != nil {
		t.Fatalf("RunTrainingScript: %v", err)
	}
	if len(results) != 1 {
		t.Fatalf("erwartet 1 Wiederholung, bekam %d", len(results))
	}
	r := results[0]
	if r.ArousalBefore != 10 {
		t.Fatalf("Rückmeldung kam nicht an: %d", r.ArousalBefore)
	}
	if r.VibrationPeak >= 0.8 {
		t.Errorf("Vibration wurde nicht gedämpft: %v >= 0.8", r.VibrationPeak)
	}
	if r.SuctionPeak >= 0.6 {
		t.Errorf("Sog wurde nicht gedämpft: %v >= 0.6", r.SuctionPeak)
	}
	if r.RestMs <= 1000 {
		t.Errorf("Pause wurde nicht verlängert: %d <= 1000", r.RestMs)
	}
	if dev.max() >= 0.8 {
		t.Errorf("dem Gerät wurde trotz Dämpfung der volle Vibrationswert geschickt: max=%v", dev.max())
	}
	if dev.maxSuction() >= 0.6 {
		t.Errorf("dem Gerät wurde trotz Dämpfung der volle Sog-Wert geschickt: max=%v", dev.maxSuction())
	}
}

// StopCycle muss, wenn zwei Kanalkurven parallel laufen, WIRKLICH beide
// treffen - nicht nur die zuerst lesende Goroutine (das war mit dem
// alten Channel-basierten TrainingControl der Fall, siehe dessen
// requested()-Kommentar).
func TestRunTrainingScriptStopCycleStopsBothChannels(t *testing.T) {
	dev := &recordingDevice{}
	control := NewTrainingControl()

	script := TrainingScript{
		Phases: []TrainingPhase{
			{
				Name:         "Both channels",
				Vibration:    curve(ChannelVibration, 0, 1.0, 0, 2000, 2000, 2000),
				Suction:      curve(ChannelSuction, 0, 1.0, 0, 2000, 2000, 2000),
				RepeatCycles: 1,
				RestMs:       10,
			},
		},
	}

	done := make(chan error, 1)
	go func() {
		done <- RunTrainingScript(context.Background(), dev, script, control, nil)
	}()

	time.Sleep(300 * time.Millisecond)
	control.StopCycle()

	select {
	case err := <-done:
		if err != nil {
			t.Fatalf("RunTrainingScript: %v", err)
		}
	case <-time.After(10 * time.Second):
		t.Fatal("RunTrainingScript ist nach StopCycle nicht beendet worden")
	}

	if got := dev.last(); got != 0 {
		t.Errorf("Vibration muss nach Stopp 0 sein, ist %v", got)
	}
	if got := dev.lastSuction(); got != 0 {
		t.Errorf("Sog muss nach Stopp 0 sein, ist %v", got)
	}
	// Beide Rampen liefen mit 2s Hochfahrzeit und wurden nach 300ms
	// gestoppt - keine darf den Höhepunkt 1.0 erreicht haben.
	if dev.max() > 0.5 {
		t.Errorf("Vibrationsrampe lief trotz Stopp bis %v weiter", dev.max())
	}
	if dev.maxSuction() > 0.5 {
		t.Errorf("Sogrampe lief trotz Stopp bis %v weiter", dev.maxSuction())
	}
}

// Die ausgelieferten Scripts müssen strukturell gültig sein - eine falsche
// Phase (0 Wiederholungen, kein aktiver Kanal) würde sonst erst beim
// ersten echten Start im Frontend auffallen. Läuft nur ValidateTrainingScript,
// nicht RunTrainingScript: die echten Scripts sind für den tatsächlichen
// Gebrauch temporiert (bis zu ~2 Minuten pro Script), nicht für einen
// Unit-Test.
func TestBuiltinTrainingScriptsAreValid(t *testing.T) {
	for _, script := range BuiltinTrainingScripts() {
		t.Run(script.Name, func(t *testing.T) {
			if err := ValidateTrainingScript(script); err != nil {
				t.Fatalf("Script %q ungültig: %v", script.Name, err)
			}
			if script.Description == "" {
				t.Errorf("Script %q hat keine Beschreibung fürs GUI-Dropdown", script.Name)
			}
		})
	}
}

// Real-hardware feedback (owner, 23 Sep 2026): "tissue-massage"'s
// "Static hold" phase held 20s continuous suction at PeakLevel=0.7 -
// too much for the device. Cut to 6s/0.6. Locks in a bound so nobody
// re-introduces a long sustained hold at a high level by accident -
// every phase across every built-in script should stay within a
// conservative sustained-load budget until real hardware says
// otherwise (see BuiltinTrainingScripts' own doc comment: these are
// starting guesses, not calibrated physical intensities).
func TestBuiltinTrainingScriptsNoLongHighSuctionHold(t *testing.T) {
	const maxHoldMsAtHighLevel = 10000
	const highLevel = 0.65
	for _, script := range BuiltinTrainingScripts() {
		for _, phase := range script.Phases {
			if phase.Suction == nil {
				continue
			}
			c := phase.Suction
			if c.HoldMs > maxHoldMsAtHighLevel && c.PeakLevel >= highLevel {
				t.Errorf("%s/%s: HoldMs=%d at PeakLevel=%.2f - sustained suction this long at this level was reported too much for real hardware",
					script.Name, phase.Name, c.HoldMs, c.PeakLevel)
			}
		}
	}
}

// Real-hardware feedback (owner, 23 Sep 2026): some built-in scripts never
// touched the Suction channel at all - that wasn't intentional design, every
// script should have a suction curve somewhere from the start. Locks in that
// every built-in script has at least one phase driving Suction, so a future
// addition can't silently ship vibration-only again.
func TestBuiltinTrainingScriptsHaveSuctionFromStart(t *testing.T) {
	for _, script := range BuiltinTrainingScripts() {
		hasSuction := false
		for _, phase := range script.Phases {
			if phase.Suction != nil {
				hasSuction = true
				break
			}
		}
		if !hasSuction {
			t.Errorf("%s: no phase drives the Suction channel", script.Name)
		}
	}
}

// Real-hardware feedback (owner, 23 Sep 2026): a script whose last active
// phase for a channel ends above 0 (a deliberate mid-script floor, like
// "plateau"'s edging hold, or simply a later phase that never touches a
// channel an earlier one raised) left that channel elevated until the
// deferred dev.Stop() cut it abruptly - felt like the level "just staying"
// instead of ramping down. RunTrainingScript must wind both channels back
// to 0 gracefully once the script completes naturally.
func TestRunTrainingScriptRampsDownElevatedChannelsAtNaturalEnd(t *testing.T) {
	dev := &recordingDevice{}
	script := TrainingScript{
		Phases: []TrainingPhase{
			{
				Name:         "Ends elevated",
				Vibration:    curve(ChannelVibration, 0, 0.8, 0.5, 50, 50, 50),
				Suction:      curve(ChannelSuction, 0, 0.6, 0.3, 50, 50, 50),
				RepeatCycles: 1,
				RestMs:       10,
			},
		},
	}

	if err := RunTrainingScript(context.Background(), dev, script, nil, nil); err != nil {
		t.Fatalf("RunTrainingScript: %v", err)
	}

	if !dev.vibrationsContainApprox(0.5, 0.01) {
		t.Errorf("Vibration hat ihr EndLevel 0.5 nie erreicht: %v", dev.vibrations)
	}
	if got := dev.last(); got != 0 {
		t.Errorf("Vibration wurde am Skriptende nicht auf 0 heruntergefahren: %v", got)
	}
	if !dev.suctionsContainApprox(0.3, 0.01) {
		t.Errorf("Sog hat sein EndLevel 0.3 nie erreicht: %v", dev.suctions)
	}
	if got := dev.lastSuction(); got != 0 {
		t.Errorf("Sog wurde am Skriptende nicht auf 0 heruntergefahren: %v", got)
	}
}

// Ein Interrupt fährt beide Kanäle schon per stopBothChannels() sofort auf
// 0 (bewusst abrupt, wie ein Nutzer-Stopp reagieren soll) - der zusätzliche
// sanfte Rundown am natürlichen Skriptende darf danach nicht noch einmal
// anlaufen und die Reaktion verzögern.
func TestRunTrainingScriptSkipsRampDownAfterInterrupt(t *testing.T) {
	dev := &recordingDevice{}
	control := NewTrainingControl()
	script := TrainingScript{
		Phases: []TrainingPhase{
			{
				Name:         "Long hold",
				Vibration:    curve(ChannelVibration, 0, 0.8, 0.5, 50, 2000, 50),
				RepeatCycles: 1,
				RestMs:       10,
			},
		},
	}

	done := make(chan error, 1)
	go func() {
		done <- RunTrainingScript(context.Background(), dev, script, control, nil)
	}()

	time.Sleep(100 * time.Millisecond)
	control.StopCycle()

	select {
	case err := <-done:
		if err != nil {
			t.Fatalf("RunTrainingScript: %v", err)
		}
	case <-time.After(5 * time.Second):
		t.Fatal("RunTrainingScript hat auf StopCycle nicht reagiert")
	}

	if got := dev.last(); got != 0 {
		t.Errorf("nach Interrupt sollte Vibration bei 0 stehen, ist aber: %v", got)
	}
}

// Ein leeres Script bzw. eine Phase ohne aktiven Kanal muss schon vor dem
// ersten Gerätebefehl scheitern, nicht mitten im Ablauf.
func TestRunTrainingScriptRejectsInvalidScript(t *testing.T) {
	dev := &recordingDevice{}
	if err := RunTrainingScript(context.Background(), dev, TrainingScript{}, nil, nil); err == nil {
		t.Error("leeres Script hätte abgelehnt werden müssen")
	}
	if err := RunTrainingScript(context.Background(), dev, TrainingScript{
		Phases: []TrainingPhase{{Name: "leer", RepeatCycles: 1}},
	}, nil, nil); err == nil {
		t.Error("Phase ohne aktiven Kanal hätte abgelehnt werden müssen")
	}
	if dev.count() != 0 || dev.stops != 0 {
		t.Error("ein abgelehntes Script darf keinen Gerätebefehl ausgelöst haben")
	}
}

// Ein Script mit einem falsch gesetzten Channel-Feld (z.B. aus dem
// GUI-Editor, der das Feld selbst nicht setzen sollte) darf die Kurve
// NICHT auf den falschen physischen Kanal schicken - sonst würden
// Vibration und Sog denselben Kanal ansteuern und sich überschreiben.
// Ohne NormalizeTrainingScript würde dieser Test fehlschlagen: die
// "Suction"-Kurve trägt hier absichtlich Channel: ChannelVibration.
func TestRunTrainingScriptNormalizesMismatchedChannelField(t *testing.T) {
	dev := &recordingDevice{}
	script := TrainingScript{
		Phases: []TrainingPhase{
			{
				Name:         "Mismatched",
				Vibration:    curve(ChannelVibration, 0, 0.5, 0, 50, 50, 50),
				Suction:      curve(ChannelVibration, 0, 0.9, 0, 50, 50, 50), // absichtlich falsch
				RepeatCycles: 1,
				RestMs:       10,
			},
		},
	}
	err := RunTrainingScript(context.Background(), dev, script, nil, nil)
	if err != nil {
		t.Fatalf("RunTrainingScript: %v", err)
	}
	if got := dev.maxSuction(); got < 0.85 {
		t.Errorf("Sog-Kurve wurde nicht auf den Sog-Kanal geschickt (Channel-Feld ignoriert werden muss): max=%v", got)
	}
	if got := dev.max(); got > 0.6 {
		t.Errorf("die Sog-Kurve (Höhepunkt 0.9) landete auf dem Vibrationskanal statt beim Sog: vib-max=%v", got)
	}
}

// ScaleTrainingScript is the shared mechanism behind both the difficulty
// tier a user picks and the history-based nudge (app_training.go's
// TrainingRequest.IntensityFactor) - it has to scale levels without
// touching timing, clamp instead of overflow, and leave factor<=0/==1
// (the "no adjustment" sentinels) as true no-ops.
func TestScaleTrainingScriptScalesLevelsNotTiming(t *testing.T) {
	script := TrainingScript{
		Phases: []TrainingPhase{{
			Name:         "p",
			Vibration:    curve(ChannelVibration, 0.2, 0.6, 0.3, 1000, 2000, 1000),
			Suction:      curve(ChannelSuction, 0.1, 0.5, 0.2, 500, 1500, 500),
			RepeatCycles: 3,
			RestMs:       400,
		}},
	}
	scaled := ScaleTrainingScript(script, 0.5)
	v := scaled.Phases[0].Vibration
	if v.StartLevel != 0.1 || v.PeakLevel != 0.3 || v.EndLevel != 0.15 {
		t.Errorf("Vibrations-Level nicht korrekt skaliert: %+v", v)
	}
	if v.RampUpMs != 1000 || v.HoldMs != 2000 || v.RampDownMs != 1000 {
		t.Errorf("Timing hätte unangetastet bleiben müssen: %+v", v)
	}
	s := scaled.Phases[0].Suction
	if s.StartLevel != 0.05 || s.PeakLevel != 0.25 || s.EndLevel != 0.1 {
		t.Errorf("Sog-Level nicht korrekt skaliert: %+v", s)
	}
	if scaled.Phases[0].RepeatCycles != 3 || scaled.Phases[0].RestMs != 400 {
		t.Errorf("Phasen-Struktur (Wiederholungen/Pause) hätte unangetastet bleiben müssen: %+v", scaled.Phases[0])
	}
}

func TestScaleTrainingScriptClampsToOne(t *testing.T) {
	script := TrainingScript{Phases: []TrainingPhase{{
		Name:         "p",
		Vibration:    curve(ChannelVibration, 0.5, 0.9, 0.5, 100, 100, 100),
		RepeatCycles: 1,
	}}}
	scaled := ScaleTrainingScript(script, 2.0)
	v := scaled.Phases[0].Vibration
	if v.StartLevel != 1 || v.PeakLevel != 1 || v.EndLevel != 1 {
		t.Errorf("Level hätten auf 1 gekappt werden müssen, nicht darüber: %+v", v)
	}
}

func TestScaleTrainingScriptNeutralFactorIsNoop(t *testing.T) {
	script := TrainingScript{Phases: []TrainingPhase{{
		Name:         "p",
		Vibration:    curve(ChannelVibration, 0.2, 0.6, 0.3, 100, 100, 100),
		RepeatCycles: 1,
	}}}
	for _, factor := range []float64{0, -1, 1} {
		scaled := ScaleTrainingScript(script, factor)
		if scaled.Phases[0].Vibration.PeakLevel != 0.6 {
			t.Errorf("factor=%v hätte ein No-op sein müssen, PeakLevel=%v", factor, scaled.Phases[0].Vibration.PeakLevel)
		}
	}
}

func TestScaleTrainingScriptDoesNotMutateInput(t *testing.T) {
	original := curve(ChannelVibration, 0.2, 0.6, 0.3, 100, 100, 100)
	script := TrainingScript{Phases: []TrainingPhase{{Name: "p", Vibration: original, RepeatCycles: 1}}}
	ScaleTrainingScript(script, 0.5)
	if original.PeakLevel != 0.6 {
		t.Errorf("das Original-Script wurde verändert: PeakLevel=%v", original.PeakLevel)
	}
}

// errorAfterFirstSuctionDevice fails the very first SetSuction call while
// still recording SetVibration calls - used to prove that runPhaseRepeat
// cancels the sibling channel's goroutine on error instead of letting it
// keep writing to the device unsupervised after runPhaseRepeat has already
// returned (see runPhaseRepeat's innerCtx/cancel doc comment).
type errorAfterFirstSuctionDevice struct {
	mu         sync.Mutex
	vibrations int
	suctionErr error
}

func (d *errorAfterFirstSuctionDevice) SetVibration(v float64) error {
	d.mu.Lock()
	defer d.mu.Unlock()
	d.vibrations++
	return nil
}

func (d *errorAfterFirstSuctionDevice) SetSuction(v float64) error {
	return d.suctionErr
}

func (d *errorAfterFirstSuctionDevice) Stop() error { return nil }

func (d *errorAfterFirstSuctionDevice) vibrationCount() int {
	d.mu.Lock()
	defer d.mu.Unlock()
	return d.vibrations
}

// Ohne den innerCtx/cancel-Fix in runPhaseRepeat würde diese Funktion beim
// ersten Fehler (hier: Sog-Kanal) sofort zurückkehren, während die
// Vibrations-Goroutine ihre eigene, viel längere Rampe unbeaufsichtigt zu
// Ende laufen lässt - sie teilt sich dann keinen abbrechbaren Kontext mit
// dem Fehlerpfad. Dieser Test schlägt ohne den Fix fehl: runPhaseRepeat
// bräuchte dann ~6s statt <500ms, und SetVibration würde nach der Rückkehr
// noch weiter aufgerufen.
func TestRunPhaseRepeatCancelsSiblingChannelOnError(t *testing.T) {
	dev := &errorAfterFirstSuctionDevice{suctionErr: errors.New("device write failed")}
	vib := &ChannelCurve{Channel: ChannelVibration, StartLevel: 0, PeakLevel: 1, EndLevel: 0,
		RampUpMs: 2000, HoldMs: 2000, RampDownMs: 2000}
	suc := &ChannelCurve{Channel: ChannelSuction, StartLevel: 0, PeakLevel: 1, EndLevel: 0,
		RampUpMs: 100, HoldMs: 100, RampDownMs: 100}

	start := time.Now()
	stopped, err := runPhaseRepeat(context.Background(), dev, vib, suc, nil)
	elapsed := time.Since(start)

	if err == nil {
		t.Fatal("erwartete einen Fehler vom fehlschlagenden Sog-Kanal, bekam nil")
	}
	if stopped {
		t.Error("stopped sollte bei einem echten Fehler false sein, das ist kein Nutzer-Stopp-Wunsch")
	}
	if elapsed > 500*time.Millisecond {
		t.Errorf("runPhaseRepeat brauchte %v bis zur Rückkehr nach dem Sog-Fehler - die 6s-Vibrationskurve wurde nicht abgebrochen", elapsed)
	}

	countAtReturn := dev.vibrationCount()
	time.Sleep(150 * time.Millisecond)
	countAfterWait := dev.vibrationCount()
	if countAfterWait > countAtReturn {
		t.Errorf("SetVibration wurde noch nach der Rückkehr von runPhaseRepeat aufgerufen (%d -> %d Aufrufe) - die Vibrations-Goroutine lief unbeaufsichtigt weiter", countAtReturn, countAfterWait)
	}
}

func TestBuiltinTrainingScriptLookup(t *testing.T) {
	if _, ok := BuiltinTrainingScript("does-not-exist"); ok {
		t.Error("unbekannter Name hätte nicht gefunden werden dürfen")
	}
	script, ok := BuiltinTrainingScript("vibration-wave-suction-focus")
	if !ok || len(script.Phases) != 2 {
		t.Errorf("bekannter Name nicht korrekt gefunden: ok=%v phases=%d", ok, len(script.Phases))
	}
	// Classic methods remain first-class presets (research: Stop-Start/Plateau
	// become built-in single-phase scripts — improve, don't remove).
	stop, ok := BuiltinTrainingScript("stop-start")
	if !ok || len(stop.Phases) != 1 || stop.Phases[0].Vibration == nil || stop.Phases[0].Vibration.EndLevel != 0 {
		t.Fatalf("stop-start builtin fehlt oder falsch: %+v", stop)
	}
	plat, ok := BuiltinTrainingScript("plateau")
	if !ok || len(plat.Phases) != 1 || plat.Phases[0].Vibration == nil || plat.Phases[0].Vibration.EndLevel <= 0 {
		t.Fatalf("plateau builtin fehlt oder falsch: %+v", plat)
	}
	all := BuiltinTrainingScripts()
	if len(all) < 6 || all[0].Name != "stop-start" || all[1].Name != "plateau" {
		t.Fatalf("Klassiker müssen BuiltinTrainingScripts anführen, got %d first=%q", len(all), all[0].Name)
	}
}

// Interrupt must zero a channel carried from an earlier phase even when the
// current phase's curve for that channel is nil (preserve-previous semantics).
// Reproduction: phase 1 leaves vibration at 0.2; phase 2 is suction-only;
// StopCycle during phase 2 must clear vibration too (shipped preset pattern).
func TestStopCycleClearsCarriedChannelFromEarlierPhase(t *testing.T) {
	dev := &recordingDevice{}
	control := NewTrainingControl()
	script := TrainingScript{
		Phases: []TrainingPhase{
			{
				Name:         "Vibration wave",
				Vibration:    curve(ChannelVibration, 0, 0.5, 0.2, 40, 10, 40),
				RepeatCycles: 1,
				RestMs:       10,
			},
			{
				Name:         "Suction focus",
				Suction:      curve(ChannelSuction, 0, 1.0, 0, 2000, 2000, 2000),
				RepeatCycles: 1,
				RestMs:       10,
			},
		},
	}

	var phase2Stopped bool
	done := make(chan error, 1)
	go func() {
		done <- RunTrainingScript(context.Background(), dev, script, control, func(r TrainingScriptCycleResult) {
			if r.PhaseIndex == 1 && r.StoppedByUser {
				phase2Stopped = true
			}
		})
	}()

	// Wait until phase 1 has finished writing (vibration ended near 0.2) and
	// phase 2 has begun suction writes, then interrupt.
	deadline := time.Now().Add(3 * time.Second)
	for time.Now().Before(deadline) {
		if dev.maxSuction() > 0.05 {
			break
		}
		time.Sleep(20 * time.Millisecond)
	}
	if dev.maxSuction() <= 0.05 {
		t.Fatal("phase 2 suction never started")
	}
	control.StopCycle()

	select {
	case err := <-done:
		if err != nil {
			t.Fatalf("RunTrainingScript: %v", err)
		}
	case <-time.After(10 * time.Second):
		t.Fatal("RunTrainingScript did not finish after StopCycle")
	}

	if !phase2Stopped {
		t.Fatal("expected phase-2 completion with StoppedByUser")
	}
	if got := dev.last(); got != 0 {
		t.Fatalf("carried vibration remains %v after Interrupt now (want 0)", got)
	}
	if got := dev.lastSuction(); got != 0 {
		t.Fatalf("suction remains %v after Interrupt now (want 0)", got)
	}
}

// Feedback damping must scale StartLevel too — flat 0.8→0.8→0.8 curves
// otherwise resume at the undamped start above the reported peak.
func TestScaledCurveScalesStartLevelWithFeedback(t *testing.T) {
	peakF, holdF, _ := arousalFactors(10)
	base := &ChannelCurve{
		Channel: ChannelVibration, StartLevel: 0.8, PeakLevel: 0.8, EndLevel: 0.8, HoldMs: 1000,
	}
	got := scaledCurve(base, 1.0, peakF, holdF)
	if got == nil {
		t.Fatal("scaledCurve returned nil")
	}
	if got.StartLevel > got.PeakLevel+1e-9 {
		t.Fatalf("StartLevel %v above PeakLevel %v after feedback 10", got.StartLevel, got.PeakLevel)
	}
	if absF(got.StartLevel-got.PeakLevel) > 1e-9 {
		t.Fatalf("flat curve: StartLevel should match PeakLevel after scale, got start=%v peak=%v",
			got.StartLevel, got.PeakLevel)
	}
	if got.PeakLevel >= 0.8-1e-9 {
		t.Fatalf("expected peak damped below 0.8, got %v (factor %v)", got.PeakLevel, peakF)
	}
}

func absF(v float64) float64 {
	if v < 0 {
		return -v
	}
	return v
}

func TestLevelMirrorReportsLiveWrites(t *testing.T) {
	dev := &recordingDevice{}
	control := NewTrainingControl()
	var lastVib, lastSuc float64
	var n int
	control.SetOnLevel(func(vib, suc float64) {
		lastVib, lastSuc = vib, suc
		n++
	})
	mirrored := mirrorLevels(dev, control)
	if err := mirrored.SetVibration(0.4); err != nil {
		t.Fatal(err)
	}
	if err := mirrored.SetSuction(0.7); err != nil {
		t.Fatal(err)
	}
	if n < 2 {
		t.Fatalf("expected >=2 level callbacks, got %d", n)
	}
	if lastVib != 0.4 || lastSuc != 0.7 {
		t.Fatalf("last levels vib=%v suc=%v", lastVib, lastSuc)
	}
}

// stopAfter startet fn im Hintergrund, drückt nach `after` StopCycle und
// liefert den Vibrationswert `settle` später - also den Stand, den der
// Nutzer nach dem Druck auf "Unterbrechen" tatsächlich spürt.
func stopAfter(t *testing.T, dev *recordingDevice, control *TrainingControl, after, settle time.Duration, fn func() error) (float64, float64, chan error) {
	t.Helper()
	done := make(chan error, 1)
	go func() { done <- fn() }()
	time.Sleep(after)
	control.StopCycle()
	time.Sleep(settle)
	return dev.last(), dev.lastSuction(), done
}

// Stopp während der Abwärtsrampe (Stop-Start) bzw. der Rampe aufs Plateau:
// vorher brach rampChannel die Rampe ab und der Rückgabewert wurde
// verworfen - der Kanal blieb für die ganze Pause auf dem Zwischenwert
// stehen, statt wie bei jedem anderen Stopp sofort auf 0 zu gehen.
func TestStopCycleDuringRampDownCutsToZero(t *testing.T) {
	for _, tech := range []TrainingTechnique{TechniqueStopStart, TechniquePlateau} {
		t.Run(string(tech), func(t *testing.T) {
			dev := &recordingDevice{}
			control := NewTrainingControl()
			opts := TrainingOptions{Technique: tech, Channel: ChannelVibration, Cycles: 1,
				RampUpMs: 50, HoldMs: 50, RestMs: 3000, PeakIntensity: 1.0, PlateauFraction: 0.7}
			var results []TrainingCycleResult
			vib, _, done := stopAfter(t, dev, control, 800*time.Millisecond, 200*time.Millisecond, func() error {
				return RunTrainingWithControl(context.Background(), dev, opts, control, func(r TrainingCycleResult) {
					results = append(results, r)
				})
			})
			if vib != 0 {
				t.Errorf("Stopp in der Abwärtsrampe: Vibration bleibt auf %v statt 0", vib)
			}
			if err := <-done; err != nil {
				t.Fatal(err)
			}
			if len(results) != 1 || !results[0].StoppedByUser {
				t.Errorf("Zyklus muss als vom Nutzer gestoppt gemeldet werden: %+v", results)
			}
		})
	}
}

// Script-Modus: in der Pause steht ein Kanal mit EndLevel > 0 (Plateau,
// "Suction focus") weiter an. Ein Stopp dort wurde bisher nicht gesehen und
// vom drain() des nächsten Zyklus verworfen - der Knopf wirkte nicht.
func TestRunTrainingScriptStopCycleDuringElevatedRest(t *testing.T) {
	dev := &recordingDevice{}
	control := NewTrainingControl()
	script := TrainingScript{Phases: []TrainingPhase{{
		Name:         "Plateau",
		Vibration:    curve(ChannelVibration, 0, 0.8, 0.5, 50, 50, 50),
		Suction:      curve(ChannelSuction, 0, 0.6, 0.3, 50, 50, 50),
		RepeatCycles: 1,
		RestMs:       3000,
	}}}
	var results []TrainingScriptCycleResult
	vib, suc, done := stopAfter(t, dev, control, 600*time.Millisecond, 200*time.Millisecond, func() error {
		return RunTrainingScript(context.Background(), dev, script, control, func(r TrainingScriptCycleResult) {
			results = append(results, r)
		})
	})
	if vib != 0 || suc != 0 {
		t.Errorf("Stopp in der Pause: Vibration %v, Sog %v - beide müssen auf 0", vib, suc)
	}
	if err := <-done; err != nil {
		t.Fatal(err)
	}
	if len(results) != 1 || !results[0].StoppedByUser {
		t.Errorf("Wiederholung muss als vom Nutzer gestoppt gemeldet werden: %+v", results)
	}
}

// Ein Stopp in einer frühen Wiederholung darf den sanften Rundown am
// natürlichen Skriptende nicht für den Rest der Session abschalten: die
// späteren Wiederholungen laufen normal und enden wieder erhöht.
func TestRunTrainingScriptRampsDownAtEndAfterEarlierStop(t *testing.T) {
	dev := &recordingDevice{}
	control := NewTrainingControl()
	script := TrainingScript{Phases: []TrainingPhase{
		{Name: "Stopped", Vibration: curve(ChannelVibration, 0, 0.8, 0, 50, 2000, 50), RepeatCycles: 1, RestMs: 10},
		{Name: "Ends elevated", Vibration: curve(ChannelVibration, 0, 0.8, 0.5, 50, 50, 50), RepeatCycles: 1, RestMs: 10},
	}}
	_, _, done := stopAfter(t, dev, control, 300*time.Millisecond, 0, func() error {
		return RunTrainingScript(context.Background(), dev, script, control, nil)
	})
	if err := <-done; err != nil {
		t.Fatal(err)
	}
	if !dev.vibrationsContainApprox(0.5, 0.01) {
		t.Fatalf("zweite Phase hat ihr EndLevel 0.5 nie erreicht: %v", dev.vibrations)
	}
	if got := dev.last(); got != 0 {
		t.Errorf("kein Rundown am Skriptende nach früherem Stopp, Vibration zuletzt %v", got)
	}
}

// noDropBetween: nachdem der Kanal einmal `floor` erreicht hat (nach dem
// ersten Höhepunkt `peak`), darf er bis zum nächsten Höhepunkt nicht unter
// den Boden fallen. Liefert false, wenn der zweite Höhepunkt fehlt.
func noDropBetween(t *testing.T, writes []float64, floor, peak float64) bool {
	t.Helper()
	const eps = 0.011
	i := 0
	for i < len(writes) && writes[i] < peak-eps {
		i++
	}
	for i < len(writes) && absF(writes[i]-floor) > eps {
		i++
	}
	if i == len(writes) {
		t.Fatalf("Plateau %v nach Höhepunkt %v nie erreicht: %v", floor, peak, writes)
	}
	for j := i + 1; j < len(writes); j++ {
		if writes[j] >= peak-eps {
			return true
		}
		if writes[j] < floor-eps {
			t.Errorf("fällt nach dem Plateau auf %v statt vom Plateau %v aus hochzufahren", writes[j], floor)
			return true
		}
	}
	return false
}

// Plateau/Edging: der nächste Zyklus fährt vom Plateau aus hoch. Vorher
// sprang er auf 0 und startete die Rampe von unten - ein kurzer Abbruch
// mitten in einer Methode, deren Sinn das Obenbleiben ist (Owner 29 Sep).
func TestPlateauNextCycleStartsFromPlateau(t *testing.T) {
	dev := &recordingDevice{}
	opts := TrainingOptions{Technique: TechniquePlateau, Channel: ChannelVibration, Cycles: 2,
		RampUpMs: 200, HoldMs: 50, RestMs: 200, PeakIntensity: 1.0, PlateauFraction: 0.7}
	if err := RunTraining(context.Background(), dev, opts, nil); err != nil {
		t.Fatal(err)
	}
	if !noDropBetween(t, dev.vibrations, 0.7, 1.0) {
		t.Fatalf("zweiter Höhepunkt fehlt: %v", dev.vibrations)
	}
}

// Script-Gegenstück: das eingebaute Plateau-Script setzt StartFromCurrent
// auf der Vibrationskurve.
func TestRunTrainingScriptStartFromCurrentKeepsPlateau(t *testing.T) {
	plateau, ok := BuiltinTrainingScript("plateau")
	if !ok || plateau.Phases[0].Vibration == nil || !plateau.Phases[0].Vibration.StartFromCurrent {
		t.Fatal("eingebautes Plateau-Script: Vibration muss StartFromCurrent setzen")
	}
	dev := &recordingDevice{}
	c := curve(ChannelVibration, 0, 0.8, 0.56, 150, 50, 150)
	c.StartFromCurrent = true
	script := TrainingScript{Phases: []TrainingPhase{{Name: "Plateau", Vibration: c, RepeatCycles: 2, RestMs: 50}}}
	if err := RunTrainingScript(context.Background(), dev, script, nil, nil); err != nil {
		t.Fatal(err)
	}
	if !noDropBetween(t, dev.vibrations, 0.56, 0.8) {
		t.Fatalf("zweiter Höhepunkt fehlt: %v", dev.vibrations)
	}
	// Skalieren (Schwierigkeit/Anpassung) darf das Flag nicht verlieren.
	if s := ScaleTrainingScript(script, 0.5); !s.Phases[0].Vibration.StartFromCurrent {
		t.Error("ScaleTrainingScript hat StartFromCurrent verloren")
	}
}
