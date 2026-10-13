// Package player: Trainingsmodus - unabhängig von einem funscript, erzeugt
// eigene Auf/Ab-Zyklen zur Ausdauer-/Kontrollübung, angelehnt an die
// klinisch beschriebene "Stop-Start"-Methode (Semans, 1950er, ursprünglich
// zur Behandlung vorzeitiger Ejakulation entwickelt, wird heute auch
// allgemein zum Ausdauertraining genutzt) sowie die "Edging/Plateau"-
// Variante (Intensität wird nicht auf 0 gefahren, sondern knapp unter dem
// Höhepunkt gehalten). Steuert Vibration und/oder Sog, je nach Channel -
// beide Kanäle sind laut Buttplug-Rust-Quellcode (nachgeprüft, siehe
// device/samneo2.go) stufenlose Intensitäten, kein Unterschied mehr in der
// Ansteuerbarkeit.
package player

import (
	"context"
	"fmt"
	"math/rand"
	"sync"
	"time"
)

// TrainingTechnique wählt das Grundmuster eines Zyklus.
type TrainingTechnique string

const (
	// TechniqueStopStart: hochfahren, kurz halten, komplett auf ~0 fahren,
	// Pause, von vorn. Die klassische, klinisch beschriebene Methode.
	TechniqueStopStart TrainingTechnique = "stopstart"
	// TechniquePlateau: hochfahren, dann nicht auf 0 sondern auf einem
	// Plateau knapp unter dem Höhepunkt halten ("Edging"/"Surfing"),
	// erst danach etwas absenken - kein vollständiges Abbrechen.
	TechniquePlateau TrainingTechnique = "plateau"
)

// TrainingChannel wählt, welche(r) Kanal/Kanäle die Auf/Ab-Zyklen fahren.
type TrainingChannel string

const (
	ChannelVibration TrainingChannel = "vibration"
	ChannelSuction   TrainingChannel = "suction"
	ChannelBoth      TrainingChannel = "both"
)

// TrainingOptions konfiguriert eine Trainings-Session.
type TrainingOptions struct {
	Technique TrainingTechnique
	Channel   TrainingChannel

	Cycles int // Anzahl Zyklen in dieser Session

	RampUpMs int // Zeit zum Hochfahren auf PeakIntensity
	HoldMs   int // Haltezeit auf dem Höhepunkt (bzw. Plateau)
	RestMs   int // Pause/Absenkzeit zwischen den Zyklen

	PeakIntensity   float64 // Ziel-Intensität am Höhepunkt, 0-1
	PlateauFraction float64 // nur TechniquePlateau: Plateau-Level als Anteil von PeakIntensity, 0-1 (z.B. 0.7 = 70% des Höhepunkts)

	// ProgressionPerCycle: 0-1, erhöht PeakIntensity und HoldMs über die
	// Zyklen hinweg linear um diesen Anteil bis zum letzten Zyklus (0 = alle
	// Zyklen identisch, 0.3 = letzter Zyklus 30% intensiver/länger als der erste).
	ProgressionPerCycle float64
}

// TrainingControl erlaubt Eingriffe während einer laufenden Session.
//
// Das ist der Kern der Stop-Start-Methode und fehlte bisher: RunTraining
// fuhr eine vorher festgelegte Zeitkette ab, ohne jede Rückmeldung. Bei
// Semans entscheidet aber der Anwender, wann gestoppt wird - nämlich beim
// Herannahen des Punktes ohne Wiederkehr - und nicht eine Stoppuhr. Ohne
// diesen Eingriff ist es Intervalltraining, nicht Stop-Start.
type TrainingControl struct {
	mu            sync.Mutex
	stopRequested bool // s.u. requested()/drain() - bewusst kein Channel mehr
	arousal       int  // zuletzt gemeldeter Wert, 0 = nichts gemeldet
	// onLevel is optional live intensity for the GUI ring/clip (per write).
	onLevel func(vibration, suction float64)
}

// ArousalTarget ist der Zielbereich der Skala 1-10: nahe an der Grenze, aber
// mit Abstand. Meldungen darüber führen zu kürzeren, sanfteren Zyklen mit
// längerer Pause, Meldungen darunter zu längeren.
const ArousalTarget = 7

// SetOnLevel registers a live per-channel intensity callback. Fired on every
// successful SetVibration/SetSuction while a session runs (via levelMirror).
// Nil clears the listener. Safe to call before RunTraining*.
func (c *TrainingControl) SetOnLevel(fn func(vibration, suction float64)) {
	if c == nil {
		return
	}
	c.mu.Lock()
	c.onLevel = fn
	c.mu.Unlock()
}

// ReportArousal nimmt eine Rückmeldung auf der Skala 1-10 entgegen. Sie wirkt
// auf den NÄCHSTEN Zyklus, nicht auf den laufenden - in den laufenden greift
// stattdessen StopCycle ein.
//
// Ohne diese Rückmeldung ist der Ablauf gesteuert, aber nicht geregelt: die
// Zyklen laufen nach Stoppuhr ab, unabhängig davon, wie es tatsächlich
// gerade ist. Werte außerhalb 1-10 werden ignoriert statt geklemmt - eine
// versehentliche 0 aus einem leeren Eingabefeld soll nicht als "sehr
// entspannt" durchgehen und die Intensität hochtreiben.
func (c *TrainingControl) ReportArousal(level int) {
	if c == nil || level < 1 || level > 10 {
		return
	}
	c.mu.Lock()
	c.arousal = level
	c.mu.Unlock()
}

// takeArousal liest die Meldung und setzt sie zurück: jede Rückmeldung wirkt
// genau auf einen Zyklus. Sonst würde eine einmalige hohe Meldung den Rest
// der Session dämpfen, auch wenn sich die Lage längst geändert hat.
func (c *TrainingControl) takeArousal() int {
	if c == nil {
		return 0
	}
	c.mu.Lock()
	defer c.mu.Unlock()
	level := c.arousal
	c.arousal = 0
	return level
}

// adjustForArousal passt Höhepunkt, Haltezeit und Pause an die letzte
// Rückmeldung an.
//
// Bewusst asymmetrisch: nach oben (bei hoher Erregung) wird deutlich
// stärker gegengesteuert als nach unten. Zu früh zu weit zu gehen ist der
// Fehler, der eine Session beendet; zu vorsichtig zu sein kostet nur Zeit.
func adjustForArousal(arousal int, peak float64, holdMs, restMs int) (float64, int, int) {
	if arousal < 1 {
		return peak, holdMs, restMs
	}
	deviation := float64(arousal - ArousalTarget)

	// Richtungsabhängige Koeffizienten. Ein gemeinsamer Faktor mit
	// anschließender Klemmung sah zwar asymmetrisch aus, kehrte das
	// Verhältnis an den Rändern aber um: bei Erregung 10 wurde um 45%
	// gedämpft, bei Erregung 2 um 50% verlängert - also mehr Risiko statt
	// weniger. Zu früh zu weit zu gehen ist der Fehler, der eine Session
	// beendet; zu vorsichtig zu sein kostet nur Zeit.
	holdFactor, restFactor, peakFactor := 1.0, 1.0, 1.0
	if deviation > 0 {
		holdFactor = clampRange(1.0-0.15*deviation, 0.35, 1.0)
		restFactor = clampRange(1.0+0.20*deviation, 1.0, 2.5)
		peakFactor = clampRange(1.0-0.07*deviation, 0.5, 1.0)
	} else if deviation < 0 {
		// Nach unten deutlich zurückhaltender, und die Pause wird nie
		// unter die Einstellung gekürzt.
		holdFactor = clampRange(1.0-0.07*deviation, 1.0, 1.3)
	}

	return clamp01(peak * peakFactor),
		int(float64(holdMs) * holdFactor),
		int(float64(restMs) * restFactor)
}

func clampRange(v, lo, hi float64) float64 {
	if v < lo {
		return lo
	}
	if v > hi {
		return hi
	}
	return v
}

func NewTrainingControl() *TrainingControl {
	return &TrainingControl{}
}

// StopCycle bricht den laufenden Zyklus sofort ab und geht in die Pause
// über. Die Session läuft danach mit dem nächsten Zyklus weiter - im
// Unterschied zum Abbruch der gesamten Session.
func (c *TrainingControl) StopCycle() {
	if c == nil {
		return
	}
	c.mu.Lock()
	c.stopRequested = true
	c.mu.Unlock()
}

// requested ist absichtlich zustandslos-lesend (kein Channel-Receive mehr):
// ein Multi-Phasen-Zyklus lässt Vibration und Sog in zwei eigenen
// Goroutinen laufen (runPhaseRepeat), die beide currently() abfragen -
// ein Channel mit einem Platz hätte nur die zuerst lesende Goroutine
// bedient und die zweite den Druck nie sehen lassen. drain() setzt den
// Zustand explizit zurück statt ihn implizit beim Lesen zu verbrauchen.
func (c *TrainingControl) requested() bool {
	if c == nil {
		return false
	}
	c.mu.Lock()
	defer c.mu.Unlock()
	return c.stopRequested
}

// drain verwirft einen vorgemerkten Druck - zu Beginn jedes Zyklus, damit
// ein Druck aus dem vorherigen Zyklus nicht den nächsten sofort abbricht.
func (c *TrainingControl) drain() {
	if c == nil {
		return
	}
	c.mu.Lock()
	c.stopRequested = false
	c.mu.Unlock()
}

// TrainingCycleResult wird nach jedem abgeschlossenen Zyklus gemeldet -
// Grundlage für Session-Logging und Live-Anzeige im Frontend.
type TrainingCycleResult struct {
	CycleIndex    int // 0-basiert
	PeakIntensity float64
	HoldMs        int
	StartedAt     time.Time
	EndedAt       time.Time

	// StoppedByUser: der Zyklus wurde vorzeitig per StopCycle beendet.
	// Für das Sessionprotokoll die wichtigste Angabe überhaupt - wie oft
	// und wie früh gestoppt werden musste, ist das eigentliche Maß des
	// Trainingsfortschritts.
	StoppedByUser bool
	// ReachedPeakAfterMs: Zeit vom Zyklusbeginn bis zum Erreichen des
	// Höhepunkts bzw. bis zum vorzeitigen Stopp.
	ReachedPeakAfterMs int
	// RestMs: die für diesen Zyklus tatsächlich verwendete Pause (kann von
	// der Einstellung abweichen, wenn eine Rückmeldung vorlag).
	RestMs int
	// ArousalBefore: die Rückmeldung, die in diesen Zyklus eingeflossen ist
	// (0 = keine). Fürs Protokoll wichtiger als der reine Zyklusindex.
	ArousalBefore int
}

// trainingDevice ist die minimale Schnittstelle, die RunTraining braucht -
// unabhängig vom vollen device.Device-Interface, damit sich das Paket
// leicht ohne echtes Gerät testen lässt.
type trainingDevice interface {
	SetVibration(float64) error
	SetSuction(float64) error
	Stop() error
}

// levelMirror tracks last vibration/suction and fires TrainingControl.onLevel
// after each successful write so the GUI can drive a live meter (not just
// completed-cycle peak summaries).
type levelMirror struct {
	inner   trainingDevice
	control *TrainingControl
	mu      sync.Mutex
	vib     float64
	suc     float64
}

func mirrorLevels(dev trainingDevice, control *TrainingControl) trainingDevice {
	if control == nil {
		return dev
	}
	return &levelMirror{inner: dev, control: control}
}

func (m *levelMirror) notify() {
	m.mu.Lock()
	vib, suc := m.vib, m.suc
	m.mu.Unlock()
	m.control.mu.Lock()
	fn := m.control.onLevel
	m.control.mu.Unlock()
	if fn != nil {
		fn(vib, suc)
	}
}

func (m *levelMirror) SetVibration(v float64) error {
	if err := m.inner.SetVibration(v); err != nil {
		return err
	}
	m.mu.Lock()
	m.vib = v
	m.mu.Unlock()
	m.notify()
	return nil
}

func (m *levelMirror) SetSuction(v float64) error {
	if err := m.inner.SetSuction(v); err != nil {
		return err
	}
	m.mu.Lock()
	m.suc = v
	m.mu.Unlock()
	m.notify()
	return nil
}

func (m *levelMirror) Stop() error {
	return m.inner.Stop()
}

// RunTraining führt die konfigurierten Zyklen aus, bis sie abgeschlossen
// sind oder ctx abgebrochen wird (z.B. Nutzer klickt Stop - "man muss die
// Funktion auch ausschlagen können"). onCycle wird nach jedem Zyklus mit
// dessen Ergebnis aufgerufen (fürs Session-Log/UI), darf nil sein.
func RunTraining(ctx context.Context, dev trainingDevice, opts TrainingOptions, onCycle func(TrainingCycleResult)) error {
	return RunTrainingWithControl(ctx, dev, opts, nil, onCycle)
}

// RunTrainingWithControl ist RunTraining mit der Möglichkeit, einzelne Zyklen
// vorzeitig zu beenden (siehe TrainingControl).
func RunTrainingWithControl(ctx context.Context, dev trainingDevice, opts TrainingOptions,
	control *TrainingControl, onCycle func(TrainingCycleResult)) error {
	dev = mirrorLevels(dev, control)
	if opts.Cycles <= 0 {
		return fmt.Errorf("player: Training braucht mindestens 1 Zyklus")
	}
	if opts.Channel == "" {
		opts.Channel = ChannelVibration
	}
	defer dev.Stop() //nolint:errcheck // best effort beim Beenden

	// level: wo der Kanal gerade steht. Jeder Zyklus fährt von hier aus
	// hoch - bei Stop-Start ist das immer 0, bei Plateau der Plateau-Wert
	// des vorigen Zyklus (vorher sprang er dort auf 0 und begann von
	// unten; Owner 29 Sep), nach einem Stopp wieder 0.
	level := 0.0
	for i := 0; i < opts.Cycles; i++ {
		frac := 0.0
		if opts.Cycles > 1 {
			frac = float64(i) / float64(opts.Cycles-1)
		}
		progress := 1.0 + opts.ProgressionPerCycle*frac
		peak := clamp01(opts.PeakIntensity * progress)
		holdMs := int(float64(opts.HoldMs) * progress)
		restMs := opts.RestMs

		// Rückmeldung des vorherigen Zyklus einarbeiten. Sie überschreibt
		// die Progression bewusst NICHT, sondern wirkt darauf: die geplante
		// Steigerung bleibt, wird aber gedämpft, wenn es zu viel war.
		arousal := control.takeArousal()
		peak, holdMs, restMs = adjustForArousal(arousal, peak, holdMs, restMs)

		control.drain()
		result := TrainingCycleResult{CycleIndex: i, PeakIntensity: peak, HoldMs: holdMs,
			RestMs: restMs, ArousalBefore: arousal, StartedAt: time.Now()}

		// Hochfahren
		stopped, err := rampChannel(ctx, dev, opts.Channel, level, peak, opts.RampUpMs, control)
		if err != nil {
			return err
		}
		// Halten - hier wird in kleinen Schritten gewartet, damit ein Druck
		// auf "Stopp" sofort wirkt statt erst am Ende der Haltezeit.
		if !stopped {
			var err error
			stopped, err = waitInterruptible(ctx, time.Duration(holdMs)*time.Millisecond, control)
			if err != nil {
				return err
			}
		}
		result.ReachedPeakAfterMs = int(time.Since(result.StartedAt).Milliseconds())
		result.StoppedByUser = stopped

		if stopped {
			// Auf Wunsch abgebrochen: sofort auf 0 und volle Pause, egal
			// welche Technik eingestellt ist. Ein Plateau zu halten, nachdem
			// jemand um Unterbrechung gebeten hat, wäre das Gegenteil des
			// Gewünschten.
			if err := setChannel(dev, opts.Channel, 0); err != nil {
				return err
			}
			level = 0
			if err := waitOrDone(ctx, time.Duration(restMs)*time.Millisecond); err != nil {
				return err
			}
		} else {
			target := 0.0 // TechniqueStopStart
			if opts.Technique == TechniquePlateau {
				target = clamp01(peak * opts.PlateauFraction)
			}
			stoppedDown, err := rampChannel(ctx, dev, opts.Channel, peak, target, restMs, control)
			if err != nil {
				return err
			}
			level = target
			// Ein Stopp während der Abwärtsrampe wirkt wie jeder andere:
			// sofort auf 0 und volle Pause. Vorher wurde er verworfen und
			// der Kanal blieb die ganze Pause auf dem Zwischenwert stehen.
			if stoppedDown {
				result.StoppedByUser = true
				if err := setChannel(dev, opts.Channel, 0); err != nil {
					return err
				}
				level = 0
			}
			if stoppedDown || opts.Technique != TechniquePlateau {
				if err := waitOrDone(ctx, time.Duration(restMs)*time.Millisecond); err != nil {
					return err
				}
			}
		}

		result.EndedAt = time.Now()
		if onCycle != nil {
			onCycle(result)
		}
	}
	return nil
}

// rampChannel rampt den/die gewählten Kanal/Kanäle in kleinen Schritten von
// "from" nach "to". Bei ChannelBoth bekommen Vibration und Sog denselben
// Wert - eine per-Kanal-unterschiedliche Kurve wäre auch denkbar, ist aber
// für den Trainings-Anwendungsfall (ein gemeinsames Auf/Ab-Gefühl) nicht
// nötig.
// setChannel setzt den/die gewählten Kanal/Kanäle auf einen Wert.
func setChannel(dev trainingDevice, channel TrainingChannel, v float64) error {
	if channel == ChannelVibration || channel == ChannelBoth {
		if err := dev.SetVibration(v); err != nil {
			return err
		}
	}
	if channel == ChannelSuction || channel == ChannelBoth {
		if err := dev.SetSuction(v); err != nil {
			return err
		}
	}
	return nil
}

// rampChannel rampt den/die gewählten Kanal/Kanäle in kleinen Schritten von
// "from" nach "to". Bei ChannelBoth bekommen Vibration und Sog denselben
// Wert - eine per-Kanal-unterschiedliche Kurve wäre auch denkbar, ist aber
// für den Trainings-Anwendungsfall (ein gemeinsames Auf/Ab-Gefühl) nicht
// nötig.
//
// Gibt zurück, ob der Nutzer währenddessen um Unterbrechung gebeten hat.
// Die Rampe bricht dann sofort ab, statt erst zu Ende zu laufen.
//
// Die Schrittweite ist zeitbasiert statt fest: eine feste Anzahl Schritte
// bedeutete bei langen Rampen große zeitliche Lücken (bei 8 Sekunden und 20
// Schritten alle 400ms eine Änderung), und in genau diesen Lücken hätte ein
// Druck auf "Stopp" nicht gewirkt.
func rampChannel(ctx context.Context, dev trainingDevice, channel TrainingChannel,
	from, to float64, durationMs int, control *TrainingControl) (bool, error) {
	if durationMs <= 0 {
		return false, setChannel(dev, channel, to)
	}

	const stepMs = 50
	steps := durationMs / stepMs
	if steps < 1 {
		steps = 1
	}
	stepDur := time.Duration(durationMs) * time.Millisecond / time.Duration(steps)

	for i := 1; i <= steps; i++ {
		if control.requested() {
			return true, nil
		}
		v := from + (to-from)*float64(i)/float64(steps)
		if err := setChannel(dev, channel, v); err != nil {
			return false, err
		}
		if err := waitOrDone(ctx, stepDur); err != nil {
			return false, err
		}
	}
	return control.requested(), nil
}

// waitInterruptible wartet, prüft dabei aber regelmäßig auf einen
// Stopp-Wunsch. Ein einfaches time.After würde den Druck erst nach Ablauf
// der gesamten Haltezeit bemerken - bei mehreren Sekunden Haltezeit wäre
// der Knopf damit praktisch wirkungslos.
//
// Gibt den ctx-Fehler zurück statt ihn zu verschlucken: der Aufrufer hat
// sonst keine Möglichkeit zu erkennen, dass die Wartezeit wegen eines
// Context-Abbruchs (nicht wegen StopCycle) vorzeitig endete, und würde in
// den normalen Rampe-Pfad weiterlaufen - der dort noch mindestens einen
// weiteren Gerätebefehl absetzt, bevor der Fehler eine Ebene später doch
// noch bemerkt wird (gefunden über TestContextCancelDuringHoldStopsImmediately).
func waitInterruptible(ctx context.Context, d time.Duration, control *TrainingControl) (bool, error) {
	const tick = 50 * time.Millisecond
	deadline := time.Now().Add(d)
	for time.Now().Before(deadline) {
		if control.requested() {
			return true, nil
		}
		remaining := time.Until(deadline)
		if remaining > tick {
			remaining = tick
		}
		if err := waitOrDone(ctx, remaining); err != nil {
			return false, err
		}
	}
	return control.requested(), nil
}

func waitOrDone(ctx context.Context, d time.Duration) error {
	select {
	case <-time.After(d):
		return nil
	case <-ctx.Done():
		return ctx.Err()
	}
}

func clamp01(v float64) float64 {
	if v < 0 {
		return 0
	}
	if v > 1 {
		return 1
	}
	return v
}

// ---- Multi-phase, per-channel training scripts ----
//
// TrainingOptions above can only shape ONE curve, applied identically to
// whichever channel(s) are selected - "both" sends vibration and suction
// the same number every step (setChannel). That can't express "light
// vibration ramping up then down, followed by a suction-focused phase
// with light vibration alongside it" - one script, two channels doing
// different things at different times. See docs/TRAINING_MODE_RESEARCH.md
// ("Follow-up design: multi-phase, per-channel scripts") for the full
// rationale and the researched massage/suction patterns this is built
// from. TrainingOptions/RunTraining/RunTrainingWithControl are untouched
// and keep working exactly as before - this is additive, not a rewrite.

// ChannelCurve shapes one physical channel's level across one repeat of
// a phase: ramp from StartLevel to PeakLevel, hold, ramp to EndLevel.
// EndLevel > 0 lets a channel carry a constant level into the next
// repeat/phase (e.g. vibration staying "light" through a suction-focused
// phase) instead of always returning to 0 the way the legacy techniques
// do.
type ChannelCurve struct {
	// Channel: ChannelVibration or ChannelSuction - never ChannelBoth here,
	// each channel gets its own curve. A caller-supplied value is never
	// trusted (see NormalizeTrainingScript) - present in the JSON mainly
	// so a round-tripped script (save -> load -> re-save) looks complete.
	Channel    TrainingChannel `json:"channel"`
	StartLevel float64         `json:"startLevel"`
	PeakLevel  float64         `json:"peakLevel"`
	EndLevel   float64         `json:"endLevel"`
	RampUpMs   int             `json:"rampUpMs"`
	HoldMs     int             `json:"holdMs"`
	RampDownMs int             `json:"rampDownMs"`

	// RandomJitterFraction, if >0, perturbs PeakLevel by up to this
	// fraction (either direction, result clamped to 0-1) on each repeat -
	// the "Variable/Random" pattern researched products use specifically
	// to reduce sensory adaptation. 0 (the default) reproduces a fully
	// predictable ramp.
	RandomJitterFraction float64 `json:"randomJitterFraction"`

	// StartFromCurrent: the ramp up starts where the channel actually is
	// (the previous repeat's EndLevel, 0 on the first repeat or after an
	// interrupt) instead of at StartLevel. Plateau/edging sets it so the
	// next repeat climbs from the plateau floor instead of dropping to 0
	// first (Owner 29 Sep). Off keeps the curve exactly as written.
	StartFromCurrent bool `json:"startFromCurrent,omitempty"`
}

// TrainingPhase is one named segment of a script: an optional curve per
// channel (nil = that channel is not touched this phase and stays
// wherever the previous phase left it - the mechanism a "light constant
// vibration" carries through an unrelated suction-focused phase without
// repeating itself), repeated RepeatCycles times with RestMs between
// repeats and before the next phase.
type TrainingPhase struct {
	Name         string        `json:"name"`
	Vibration    *ChannelCurve `json:"vibration,omitempty"`
	Suction      *ChannelCurve `json:"suction,omitempty"`
	RepeatCycles int           `json:"repeatCycles"`
	RestMs       int           `json:"restMs"`
}

// TrainingScript is an ordered list of phases plus the same
// cross-session progression legacy sessions have, applied once across
// the script's total repeats rather than per phase.
type TrainingScript struct {
	Name                string          `json:"name"`
	Description         string          `json:"description"`
	Phases              []TrainingPhase `json:"phases"`
	ProgressionPerCycle float64         `json:"progressionPerCycle"`
}

// TrainingScriptCycleResult is reported after each phase repeat - the
// script-engine equivalent of TrainingCycleResult, with the per-phase,
// per-channel detail a single-curve session doesn't have.
type TrainingScriptCycleResult struct {
	PhaseIndex   int
	PhaseName    string
	PhasesTotal  int
	RepeatIndex  int // 0-basiert innerhalb dieser Phase
	RepeatsTotal int

	VibrationPeak float64 // tatsächlich angefahrener Höhepunkt, 0 wenn diese Wiederholung Vibration nicht anfasst
	SuctionPeak   float64 // tatsächlich angefahrener Höhepunkt, 0 wenn diese Wiederholung Sog nicht anfasst
	RestMs        int     // tatsächlich verwendete Pause (kann von der Phase abweichen, siehe Rückmeldung)

	StartedAt time.Time
	EndedAt   time.Time

	StoppedByUser bool
	// ReachedPeakAfterMs: Zeit vom Beginn dieser Wiederholung bis sie
	// fertig war (Rampe hoch, halten, Rampe runter) bzw. bis zum Stopp.
	// Anders als bei TrainingCycleResult schließt das hier die Abwärts-
	// rampe ein - zwei parallel laufende Kanalkurven haben keinen
	// gemeinsamen "Höhepunkt erreicht"-Zeitpunkt, den man hier festhalten
	// könnte, ohne die beiden Goroutinen zusätzlich zu synchronisieren.
	ReachedPeakAfterMs int
	ArousalBefore      int

	// PeakFactorApplied/RestFactorApplied machen die Rückmeldungs-Wirkung
	// sichtbar statt nur aus Vorher/Nachher-Werten erschließbar - die
	// GUI-Anzeige ("Feedback 9 -> Höhepunkt -30%, Pause +65%") liest diese
	// direkt statt das Verhältnis selbst nachzurechnen.
	PeakFactorApplied float64
	RestFactorApplied float64
}

// arousalFactors drückt adjustForArousals bereits getestete Anpassung als
// reine Multiplikationsfaktoren aus, die sich auf mehrere unabhängige
// Kanal-Kurven gleichzeitig anwenden lassen - adjustForArousal selbst
// wurde für genau eine Kurve geschrieben (player/training_test.go prüft
// dessen Signatur direkt, daher unverändert gelassen). Ruft dieselbe,
// bereits getestete Funktion mit neutralen 1.0/1000/1000-Basiswerten auf
// und liest die Verhältnisse zurück - die Anpassungslogik selbst wird
// nicht verdoppelt.
func arousalFactors(arousal int) (peakFactor, holdFactor, restFactor float64) {
	peak, hold, rest := adjustForArousal(arousal, 1.0, 1000, 1000)
	return peak, float64(hold) / 1000.0, float64(rest) / 1000.0
}

// scaledCurve wendet Fortschritt (über die Zyklen des Scripts), die
// Rückmeldungs-Faktoren und optionalen Zufalls-Jitter auf eine
// Kurven-Vorlage an. base bleibt unverändert (Vorlagen werden wiederholt
// verwendet); nil bleibt nil, damit "diese Phase fasst den Kanal nicht
// an" erhalten bleibt.
func scaledCurve(base *ChannelCurve, progress, peakFactor, holdFactor float64) *ChannelCurve {
	if base == nil {
		return nil
	}
	peak := clamp01(base.PeakLevel * progress * peakFactor)
	if base.RandomJitterFraction > 0 {
		delta := (rand.Float64()*2 - 1) * base.RandomJitterFraction
		peak = clamp01(peak * (1 + delta))
	}
	end := base.EndLevel
	start := base.StartLevel
	if base.PeakLevel > 0 {
		// StartLevel and EndLevel stay proportional to the SCALED peak so
		// feedback damping (and progression) actually lowers the whole
		// curve — not just Peak/End while Start stays at the original
		// high (flat 0.8→0.8→0.8 + high arousal was the failure mode).
		ratio := peak / base.PeakLevel
		start = clamp01(base.StartLevel * ratio)
		end = clamp01(base.EndLevel * ratio)
	}
	return &ChannelCurve{
		Channel:    base.Channel,
		StartLevel: start,
		PeakLevel:  peak,
		EndLevel:   end,
		RampUpMs:   base.RampUpMs,
		HoldMs:     int(float64(base.HoldMs) * holdFactor),
		RampDownMs: base.RampDownMs,

		StartFromCurrent: base.StartFromCurrent,
	}
}

// runChannelCurve fährt eine einzelne Kanal-Kurve einmal durch:
// hoch, halten, runter auf EndLevel. Gibt zurück, ob währenddessen ein
// Stopp-Wunsch gemeldet wurde.
func runChannelCurve(ctx context.Context, dev trainingDevice, curve *ChannelCurve, control *TrainingControl) (bool, error) {
	stopped, err := rampChannel(ctx, dev, curve.Channel, curve.StartLevel, curve.PeakLevel, curve.RampUpMs, control)
	if err != nil || stopped {
		return stopped, err
	}
	stopped, err = waitInterruptible(ctx, time.Duration(curve.HoldMs)*time.Millisecond, control)
	if err != nil || stopped {
		return stopped, err
	}
	return rampChannel(ctx, dev, curve.Channel, curve.PeakLevel, curve.EndLevel, curve.RampDownMs, control)
}

// runPhaseRepeat fährt Vibration und Sog EINER Wiederholung parallel statt
// nacheinander - beide Kurven laufen über dieselbe Zeitachse. device.go's
// SamNeo2/Intiface schützen ihren Zustand bereits mit einem eigenen Mutex
// (writeChannel), gleichzeitige SetVibration/SetSuction-Aufrufe aus zwei
// Goroutinen sind also sicher.
//
// Ein eigener, ableitbarer Kontext sorgt dafür, dass diese Funktion NIE
// zurückkehrt, während eine der beiden Goroutinen noch am Gerät schreibt:
// meldet ein Kanal einen echten Fehler, bricht cancel() den jeweils
// anderen sofort ab (rampChannel/waitInterruptible/waitOrDone prüfen
// ctx.Done() bereits überall), statt ihn seine volle Rampe zu Ende laufen
// zu lassen. Vorher konnte ein früher Fehler-Return die zweite Goroutine
// unbeaufsichtigt weiterlaufen lassen - kein Goroutine-Leak im
// Laufzeit-Sinn (der gepufferte Kanal verhindert das), aber ein
// Gerät, das nach dem scheinbaren Sessionende weiter angesteuert wird,
// potenziell über den defer dev.Stop() hinweg oder in eine inzwischen
// neu beanspruchte Session hinein.
func runPhaseRepeat(ctx context.Context, dev trainingDevice, vib, suc *ChannelCurve, control *TrainingControl) (bool, error) {
	type outcome struct {
		stopped bool
		err     error
	}
	innerCtx, cancel := context.WithCancel(ctx)
	defer cancel()

	outcomes := make(chan outcome, 2)
	n := 0
	if vib != nil {
		n++
		go func() {
			stopped, err := runChannelCurve(innerCtx, dev, vib, control)
			outcomes <- outcome{stopped, err}
		}()
	}
	if suc != nil {
		n++
		go func() {
			stopped, err := runChannelCurve(innerCtx, dev, suc, control)
			outcomes <- outcome{stopped, err}
		}()
	}
	stoppedAny := false
	var firstErr error
	for i := 0; i < n; i++ {
		o := <-outcomes
		if o.err != nil {
			if firstErr == nil {
				firstErr = o.err
			}
			cancel() // die andere Kurve muss nicht mehr zu Ende laufen
		}
		if o.stopped {
			stoppedAny = true
		}
	}
	if firstErr != nil {
		return false, firstErr
	}
	return stoppedAny, nil
}

// stopBothChannels zeros BOTH physical channels immediately — same reaction
// as a user Interrupt. Always clears vibration AND suction, even when the
// current phase's curve for a channel is nil: nil means "preserve previous
// output", so a carried level from an earlier phase can still be active.
// Attempts the second channel even if the first write errors.
func stopBothChannels(dev trainingDevice) error {
	var firstErr error
	if err := setChannel(dev, ChannelVibration, 0); err != nil {
		firstErr = err
	}
	if err := setChannel(dev, ChannelSuction, 0); err != nil && firstErr == nil {
		firstErr = err
	}
	return firstErr
}

// NormalizeTrainingScript sets each phase's ChannelCurve.Channel field
// from which slot it is stored in (Vibration always becomes
// ChannelVibration, Suction always ChannelSuction), ignoring whatever
// value was set there. A hand-built script (the GUI script editor, or
// any future caller) has no reason to set this field itself - and a
// mismatch (e.g. phase.Suction's curve carrying Channel:
// ChannelVibration) would make two curves silently race for the SAME
// physical channel inside runPhaseRepeat's two goroutines instead of
// each owning its own. RunTrainingScript calls this itself, so every
// script - built-in or custom - gets the guarantee; exported so the GUI
// can normalize before saving/previewing a user-edited script too.
// Returns a copy; does not mutate script.
func NormalizeTrainingScript(script TrainingScript) TrainingScript {
	out := script
	out.Phases = make([]TrainingPhase, len(script.Phases))
	for i, p := range script.Phases {
		if p.Vibration != nil {
			v := *p.Vibration
			v.Channel = ChannelVibration
			p.Vibration = &v
		}
		if p.Suction != nil {
			s := *p.Suction
			s.Channel = ChannelSuction
			p.Suction = &s
		}
		out.Phases[i] = p
	}
	return out
}

// ScaleTrainingScript multiplies every curve's StartLevel/PeakLevel/
// EndLevel by factor (each result clamped to 0-1), leaving timing
// (ramp/hold durations, RepeatCycles, RestMs) untouched - a single knob
// for "make this whole profile gentler/stronger" without reshaping it.
// factor <= 0 or == 1 returns script unchanged (0 would zero out every
// curve, never the intent of "no adjustment" - see app_training.go's
// TrainingRequest.IntensityFactor for the caller-side convention). Two
// independent inputs are meant to combine into one shared factor before
// calling this: an explicit difficulty tier the user picks, and an
// implicit nudge from how the same profile's recent sessions went (see
// docs/TRAINING_MODE_RESEARCH.md proposal A). Returns a copy; does not
// mutate script.
func ScaleTrainingScript(script TrainingScript, factor float64) TrainingScript {
	if factor <= 0 || factor == 1 {
		return script
	}
	out := script
	out.Phases = make([]TrainingPhase, len(script.Phases))
	for i, p := range script.Phases {
		if p.Vibration != nil {
			v := scaleCurveLevels(*p.Vibration, factor)
			p.Vibration = &v
		}
		if p.Suction != nil {
			s := scaleCurveLevels(*p.Suction, factor)
			p.Suction = &s
		}
		out.Phases[i] = p
	}
	return out
}

func scaleCurveLevels(c ChannelCurve, factor float64) ChannelCurve {
	c.StartLevel = clamp01(c.StartLevel * factor)
	c.PeakLevel = clamp01(c.PeakLevel * factor)
	c.EndLevel = clamp01(c.EndLevel * factor)
	return c
}

// ValidateTrainingScript checks a script's shape before it runs (or
// before it's offered in the GUI) - cheap, no device access, no waiting.
// RunTrainingScript calls this itself; exported so the GUI/tests can
// check a script (including a user-edited one, once that exists) without
// actually running it.
func ValidateTrainingScript(script TrainingScript) error {
	if len(script.Phases) == 0 {
		return fmt.Errorf("player: script braucht mindestens 1 Phase")
	}
	for _, p := range script.Phases {
		if p.RepeatCycles <= 0 {
			return fmt.Errorf("player: Phase %q braucht mindestens 1 Wiederholung", p.Name)
		}
		if p.Vibration == nil && p.Suction == nil {
			return fmt.Errorf("player: Phase %q hat keinen aktiven Kanal", p.Name)
		}
	}
	return nil
}

// RunTrainingScript führt die Phasen eines Scripts der Reihe nach aus.
// Pro Wiederholung wird EINMAL aus der letzten Rückmeldung ein
// gemeinsamer Faktor berechnet (arousalFactors) und auf JEDEN aktiven
// Kanal sowie die gemeinsame Pause angewendet - hohe Erregung macht damit
// wirklich beide Kanäle schwächer und die Pause länger, nicht nur den
// einen Kanal, den eine einzelne geteilte Kurve zufällig traf.
func RunTrainingScript(ctx context.Context, dev trainingDevice, script TrainingScript,
	control *TrainingControl, onCycle func(TrainingScriptCycleResult)) error {
	dev = mirrorLevels(dev, control)
	if err := ValidateTrainingScript(script); err != nil {
		return err
	}
	script = NormalizeTrainingScript(script)
	totalRepeats := 0
	for _, p := range script.Phases {
		totalRepeats += p.RepeatCycles
	}
	defer dev.Stop() //nolint:errcheck // best effort beim Beenden

	globalIdx := 0
	// lastVib/lastSuc track where each channel's level actually sits after
	// the most recently run curve - a nil phase.Vibration/.Suction means
	// "don't touch this channel", not "it's at 0" (see TrainingPhase's doc
	// comment), so a script whose last phase only drives one channel can
	// still finish with the OTHER channel sitting wherever an earlier
	// phase left it. Used below to wind both channels down gracefully
	// instead of leaving them elevated for the deferred dev.Stop() to cut
	// abruptly.
	var lastVib, lastSuc float64
	for pi := range script.Phases {
		phase := script.Phases[pi]
		for ri := 0; ri < phase.RepeatCycles; ri++ {
			frac := 0.0
			if totalRepeats > 1 {
				frac = float64(globalIdx) / float64(totalRepeats-1)
			}
			progress := 1.0 + script.ProgressionPerCycle*frac

			arousal := control.takeArousal()
			peakFactor, holdFactor, restFactor := arousalFactors(arousal)

			vib := scaledCurve(phase.Vibration, progress, peakFactor, holdFactor)
			suc := scaledCurve(phase.Suction, progress, peakFactor, holdFactor)
			if vib != nil && vib.StartFromCurrent {
				vib.StartLevel = lastVib
			}
			if suc != nil && suc.StartFromCurrent {
				suc.StartLevel = lastSuc
			}
			restMs := int(float64(phase.RestMs) * restFactor)

			control.drain()
			result := TrainingScriptCycleResult{
				PhaseIndex: pi, PhaseName: phase.Name, PhasesTotal: len(script.Phases),
				RepeatIndex: ri, RepeatsTotal: phase.RepeatCycles,
				RestMs: restMs, ArousalBefore: arousal,
				PeakFactorApplied: peakFactor, RestFactorApplied: restFactor,
				StartedAt: time.Now(),
			}
			if vib != nil {
				result.VibrationPeak = vib.PeakLevel
			}
			if suc != nil {
				result.SuctionPeak = suc.PeakLevel
			}

			stopped, err := runPhaseRepeat(ctx, dev, vib, suc, control)
			if err != nil {
				return err
			}
			result.ReachedPeakAfterMs = int(time.Since(result.StartedAt).Milliseconds())
			result.StoppedByUser = stopped

			if stopped {
				if err := stopBothChannels(dev); err != nil {
					return err
				}
				lastVib, lastSuc = 0, 0
			} else {
				if vib != nil {
					lastVib = vib.EndLevel
				}
				if suc != nil {
					lastSuc = suc.EndLevel
				}
			}
			rest := time.Duration(restMs) * time.Millisecond
			if lastVib > 0 || lastSuc > 0 {
				// Ein Kanal steht in der Pause noch an (EndLevel > 0 oder
				// aus einer früheren Phase mitgetragen): ein Stopp muss
				// hier genauso wirken wie in der Rampe. Vorher sah ihn
				// niemand, und das drain() des nächsten Zyklus verwarf ihn.
				stoppedInRest, err := waitInterruptible(ctx, rest, control)
				if err != nil {
					return err
				}
				if stoppedInRest {
					result.StoppedByUser = true
					if err := stopBothChannels(dev); err != nil {
						return err
					}
					lastVib, lastSuc = 0, 0
					if err := waitOrDone(ctx, rest); err != nil {
						return err
					}
				}
			} else if err := waitOrDone(ctx, rest); err != nil {
				return err
			}

			result.EndedAt = time.Now()
			if onCycle != nil {
				onCycle(result)
			}
			globalIdx++
		}
	}

	// A script that finishes with a channel still above 0 (a user
	// Interrupt already hard-cut to 0 above and reset lastVib/lastSuc, so
	// only repeats that ran after the last Interrupt count) - either
	// its last phase's EndLevel is nonzero by design (e.g. "plateau"'s
	// edging floor), or a phase later in the script never touched a
	// channel an earlier one raised. Without this, the only thing that
	// followed was the deferred dev.Stop() - an abrupt cut, not a
	// ramp-down, that read as the level "just staying" until it suddenly
	// dropped. Ramp whichever channel(s) are still elevated back to 0
	// instead, same as the felt shape of every other ramp-down in the
	// script.
	if lastVib > 0 || lastSuc > 0 {
		if _, err := runPhaseRepeat(ctx, dev, closingRamp(ChannelVibration, lastVib), closingRamp(ChannelSuction, lastSuc), control); err != nil {
			return err
		}
	}
	return nil
}

// curve is a small constructor helper so the built-in scripts below read
// as a table of levels/durations instead of repeated struct literals.
func curve(channel TrainingChannel, start, peak, end float64, rampUpMs, holdMs, rampDownMs int) *ChannelCurve {
	return &ChannelCurve{Channel: channel, StartLevel: start, PeakLevel: peak, EndLevel: end,
		RampUpMs: rampUpMs, HoldMs: holdMs, RampDownMs: rampDownMs}
}

// fromCurrent sets StartFromCurrent on a builtin curve.
func fromCurrent(c *ChannelCurve) *ChannelCurve {
	c.StartFromCurrent = true
	return c
}

// closingRampMs is how long RunTrainingScript takes to wind an elevated
// channel back down to 0 after a script finishes naturally - short enough
// not to drag out the ending, long enough to still read as a ramp instead
// of another abrupt cut.
const closingRampMs = 2000

// closingRamp builds the "wind this channel down to 0" curve
// RunTrainingScript runs once after a script's last phase, for whichever
// channel(s) it left above 0. nil for a channel already at (or below) 0,
// so runPhaseRepeat only touches the channel(s) that actually need it.
func closingRamp(channel TrainingChannel, level float64) *ChannelCurve {
	if level <= 0 {
		return nil
	}
	return &ChannelCurve{Channel: channel, StartLevel: level, PeakLevel: level, EndLevel: 0,
		RampDownMs: closingRampMs}
}

// BuiltinTrainingScripts returns the named scripts shipped with the app,
// composed from the researched suction/vibration pattern table in
// docs/TRAINING_MODE_RESEARCH.md. All levels here are nominal 0-1 floats
// sent to Buttplug's quantization (see HANDOFF.md "Known limitations" -
// actual device resolution is unmeasured), not calibrated physical
// intensities - starting guesses to revise once real hardware (NEXT.md
// priority 1) shows what these floats actually do.
func BuiltinTrainingScripts() []TrainingScript {
	// Defaults match the Training tab "Custom" form so classic methods and
	// the named presets stay one family — improve, don't replace.
	const (
		classicPeak   = 0.8
		classicRampUp = 8000
		classicHold   = 3000
		classicRest   = 10000
		classicCycles = 5
		plateauFrac   = 0.7
		// lightSuctionFrac scales a vibration-led classic technique's peak
		// down into a light accompanying suction layer - every script gets
		// a suction curve from the start (owner, 23 Sep 2026: a script with
		// no suction at all wasn't intentional), but stop-start/plateau are
		// still vibration-led methods, so suction rides alongside at a
		// clearly lower intensity rather than matching it 1:1.
		lightSuctionFrac = 0.4
	)
	return []TrainingScript{
		{
			Name: "stop-start",
			Description: "Classic Stop-Start (Semans): ramp to peak, hold, fully to 0, rest, with a lighter " +
				"suction layer riding along. Same method as Custom → Technique Stop-start; Vibration. Fine-tune timings under Custom.",
			Phases: []TrainingPhase{
				{
					Name:         "Stop-Start cycle",
					Vibration:    curve(ChannelVibration, 0, classicPeak, 0, classicRampUp, classicHold, classicRampUp),
					Suction:      curve(ChannelSuction, 0, classicPeak*lightSuctionFrac, 0, classicRampUp, classicHold, classicRampUp),
					RepeatCycles: classicCycles,
					RestMs:       classicRest,
				},
			},
			ProgressionPerCycle: 0.15,
		},
		{
			Name: "plateau",
			Description: "Classic Plateau/edging: ramp to peak, hold, down to a high floor (not 0), rest, with a lighter " +
				"suction layer that (unlike vibration) fully releases each cycle. Same method as Custom → Technique Plateau; Vibration. Fine-tune under Custom.",
			Phases: []TrainingPhase{
				{
					Name: "Plateau cycle",
					// StartFromCurrent: each next repeat climbs from the
					// plateau floor instead of dropping to 0 first.
					Vibration: fromCurrent(curve(ChannelVibration, 0, classicPeak, classicPeak*plateauFrac, classicRampUp, classicHold, classicRampUp)),
					// Suction ends at 0 even though Vibration deliberately
					// holds its high floor - the edging feel belongs to
					// vibration; suction is just an accent, not a second
					// channel stuck above 0 between reps.
					Suction:      curve(ChannelSuction, 0, classicPeak*lightSuctionFrac, 0, classicRampUp, classicHold, classicRampUp),
					RepeatCycles: classicCycles,
					RestMs:       classicRest,
				},
			},
			ProgressionPerCycle: 0.15,
		},
		{
			Name:        "vibration-wave-suction-focus",
			Description: "Light vibration ramps up then back down, then a suction-focused phase carries a light constant vibration alongside it.",
			Phases: []TrainingPhase{
				{
					Name:         "Vibration wave",
					Vibration:    curve(ChannelVibration, 0.2, 0.8, 0.2, 6000, 2000, 6000),
					RepeatCycles: 3,
					RestMs:       4000,
				},
				{
					Name: "Suction focus",
					// Vibration bleibt unangetastet (nil) - der Kanal
					// bleibt auf dem 0.2, das Phase 1 zuletzt gesetzt hat.
					Suction:      curve(ChannelSuction, 0.2, 0.75, 0.3, 5000, 3000, 5000),
					RepeatCycles: 3,
					RestMs:       5000,
				},
			},
		},
		{
			Name:        "tissue-massage",
			Description: "Suction-only, cupping-inspired: fast pumping, a long static hold, then a slow gliding sweep. Vibration stays off.",
			Phases: []TrainingPhase{
				{
					Name:         "Pumping",
					Suction:      curve(ChannelSuction, 0, 0.6, 0, 400, 200, 400),
					RepeatCycles: 8,
					RestMs:       300,
				},
				{
					// Was HoldMs=20000 at PeakLevel=0.7 - real-hardware
					// feedback (owner, 23 Sep 2026): 20s of continuous
					// suction at 70% was too much for the device. Every
					// other phase across all scripts holds its peak for at
					// most a few seconds (Pumping: instant; Gliding sweep:
					// 0ms, it's a transient rise/fall, not a held plateau)
					// - 20s was the one real outlier. Cut hold to 6s and
					// peak to 0.6 (matching Pumping's already-used level)
					// rather than guessing a single "which one" fix without
					// hardware to test further adjustments against.
					Name:         "Static hold",
					Suction:      curve(ChannelSuction, 0, 0.6, 0.6, 3000, 6000, 0),
					RepeatCycles: 1,
					RestMs:       3000,
				},
				{
					Name:         "Gliding sweep",
					Suction:      curve(ChannelSuction, 0.2, 0.8, 0.2, 15000, 0, 15000),
					RepeatCycles: 1,
					RestMs:       0,
				},
			},
		},
		{
			Name:        "vibration-massage",
			Description: "Vibration-led: a gentle escalating warm-up, then a wave rhythm with no hard hold, with a light suction layer under both.",
			Phases: []TrainingPhase{
				{
					Name:         "Escalating warm-up",
					Vibration:    curve(ChannelVibration, 0.15, 0.5, 0.3, 4000, 1500, 3000),
					Suction:      curve(ChannelSuction, 0.05, 0.25, 0.15, 4000, 1500, 3000),
					RepeatCycles: 4,
					RestMs:       2000,
				},
				{
					Name:         "Wave",
					Vibration:    curve(ChannelVibration, 0.2, 0.85, 0.2, 3000, 0, 3000),
					Suction:      curve(ChannelSuction, 0.1, 0.35, 0.1, 3000, 0, 3000),
					RepeatCycles: 6,
					RestMs:       1000,
				},
			},
			ProgressionPerCycle: 0.2,
		},
		{
			Name: "variable",
			Description: "Wave rhythm with randomized peak per repeat - reduces sensory adaptation instead of a fully predictable " +
				"rhythm - with a light, steadier suction layer alongside.",
			Phases: []TrainingPhase{
				{
					Name:         "Randomized wave",
					Vibration:    withJitter(curve(ChannelVibration, 0.2, 0.75, 0.2, 3000, 500, 3000), 0.25),
					Suction:      curve(ChannelSuction, 0.1, 0.3, 0.1, 3000, 500, 3000),
					RepeatCycles: 8,
					RestMs:       2000,
				},
			},
		},
		{
			// The one named shape from docs/TRAINING_MODE_RESEARCH.md's
			// pattern table ("Pulse | Either | RampUpMs/RampDownMs ≈ 0")
			// that none of the scripts above used yet - near-instant on/off
			// instead of a smooth ramp, a distinctly different feel from
			// every wave/hold/sweep shape above. Alternates a vibration
			// pulse phase with a suction pulse phase rather than mixing
			// both channels into every repeat, so each phase's rhythm
			// stays sharp and readable instead of blurring together.
			Name:        "pulse-rhythm",
			Description: "Sharp on/off pulses (near-instant ramps): a fast vibration pulse phase, then a fast suction pulse phase - punchier and more staccato than the wave-based scripts above.",
			Phases: []TrainingPhase{
				{
					Name:         "Vibration pulses",
					Vibration:    curve(ChannelVibration, 0, 0.7, 0, 80, 150, 80),
					RepeatCycles: 6,
					RestMs:       200,
				},
				{
					Name:         "Suction pulses",
					Suction:      curve(ChannelSuction, 0, 0.65, 0, 100, 150, 100),
					RepeatCycles: 6,
					RestMs:       200,
				},
			},
		},
	}
}

func withJitter(c *ChannelCurve, fraction float64) *ChannelCurve {
	c.RandomJitterFraction = fraction
	return c
}

// BuiltinTrainingScript looks a script up by name - the GUI sends the
// name a user picked from ListTrainingScripts back verbatim.
func BuiltinTrainingScript(name string) (TrainingScript, bool) {
	for _, s := range BuiltinTrainingScripts() {
		if s.Name == name {
			return s, true
		}
	}
	return TrainingScript{}, false
}
