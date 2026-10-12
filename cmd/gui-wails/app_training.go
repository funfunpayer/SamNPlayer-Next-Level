package main

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"time"

	"github.com/wailsapp/wails/v2/pkg/runtime"

	"github.com/funfunpayer/SamNPlayer/logging"
	"github.com/funfunpayer/SamNPlayer/player"
)

// maxTrainingSessionDuration is a generous safety ceiling on a session's
// TOTAL wall-clock time - not a training-technique setting. It guards
// against a mistake (an extra zero on restMs, cycles in the hundreds)
// running far longer than intended without the user noticing, not
// against a legitimate long session. var, not const, so tests can shrink
// it instead of actually waiting hours.
var maxTrainingSessionDuration = 3 * time.Hour

// highArousalStopsCurrentCycle: a report at or above this level also
// interrupts the CURRENTLY RUNNING cycle (StopCycle), not just the next
// one adjustForArousal shapes. Two mechanisms existed for "this is too
// much" - a soft report (next cycle only) and a hard stop (immediate) -
// with a gap between them for the case where they clearly agree. See
// docs/TRAINING_MODE_RESEARCH.md proposal (B). Kept at the GUI layer
// (not inside player.TrainingControl.ReportArousal itself) deliberately:
// baking it into the shared primitive would race player's own
// drain()-at-cycle-start against a synthetic same-instant report in
// tests exactly like the one PR #176 fixed a flake in; a real button
// click happens at an arbitrary moment during a running cycle and
// doesn't share that race.
const highArousalStopsCurrentCycle = 9

// trainingDoneMessages decides what to tell the user when a training
// run function returns - a pure function (no ctx/runtime dependency) so
// the one new distinction it makes (hit the safety ceiling vs. any other
// failure) is unit-testable without a real Wails event sink. At most one
// of the two return values is non-empty; both empty means "say nothing,
// just training:done" (the ctx.Canceled/nil-error case).
func trainingDoneMessages(runErr error) (logMsg, errMsg string) {
	if errors.Is(runErr, context.DeadlineExceeded) {
		return fmt.Sprintf("Session safety limit (%s) reached — ended automatically.", maxTrainingSessionDuration), ""
	}
	if runErr != nil && runErr != context.Canceled {
		return "", runErr.Error()
	}
	return "", ""
}

// TrainingRequest kommt als JSON vom Frontend-Formular.
type TrainingRequest struct {
	Mock bool `json:"mock"`

	// ScriptName wählt eines der Multi-Phasen-Scripts aus
	// player.BuiltinTrainingScripts() (siehe ListTrainingScripts) - wenn
	// gesetzt, werden Technique/Channel/... unten ignoriert und
	// StartTraining läuft über player.RunTrainingScript statt über die
	// einfache Technik/Kanal-Form. Siehe docs/TRAINING_MODE_RESEARCH.md
	// ("Follow-up design: multi-phase, per-channel scripts").
	ScriptName string `json:"scriptName"`

	// IntensityFactor, if > 0 and != 1, scales every curve level (not
	// timing) of the selected script by this factor before running it -
	// see player.ScaleTrainingScript. Only applies with ScriptName set.
	// The frontend combines two independent sources into this one number
	// before sending it: an explicit difficulty tier the user picks, and
	// an implicit nudge from how the SAME script's recent sessions went
	// (docs/TRAINING_MODE_RESEARCH.md proposal A - previously only shown
	// as suggestion text, now actually applied, with an opt-out checkbox)
	// - StartTraining itself doesn't know or care which. <= 0 (including
	// the zero value from an omitted field) means "no adjustment",
	// matching player.ScaleTrainingScript's own no-op convention.
	IntensityFactor float64 `json:"intensityFactor,omitempty"`

	Technique           string  `json:"technique"` // "stopstart" | "plateau"
	Channel             string  `json:"channel"`   // "vibration" | "suction" | "both"
	Cycles              int     `json:"cycles"`
	RampUpMs            int     `json:"rampUpMs"`
	HoldMs              int     `json:"holdMs"`
	RestMs              int     `json:"restMs"`
	PeakIntensity       float64 `json:"peakIntensity"`
	PlateauFraction     float64 `json:"plateauFraction"`
	ProgressionPerCycle float64 `json:"progressionPerCycle"`
}

// TrainingScriptInfo ist die GUI-taugliche Kurzform eines
// player.TrainingScript für ListTrainingScripts - die Preisgabe der
// vollen Kurvendaten (für die Vorschau-Anzeige) geschieht separat, siehe
// TrainingScriptPreview.
type TrainingScriptInfo struct {
	Name        string `json:"name"`
	Description string `json:"description"`
	// Custom marks a user-saved script (cmd/gui-wails/app_training_scripts.go)
	// as opposed to one of player.BuiltinTrainingScripts() - the GUI only
	// offers "edit"/"delete" for these.
	Custom bool `json:"custom"`
}

// ListTrainingScripts füllt das Preset-Dropdown im Trainings-Tab: erst
// die eingebauten Scripts, dann die vom Nutzer im Editor gespeicherten
// (siehe app_training_scripts.go). Ein Fehler beim Lesen der
// Custom-Scripts (z.B. Verzeichnis nicht lesbar) lässt die eingebauten
// trotzdem durch, statt das ganze Dropdown leer zu lassen.
func (a *App) ListTrainingScripts() []TrainingScriptInfo {
	scripts := player.BuiltinTrainingScripts()
	out := make([]TrainingScriptInfo, len(scripts))
	for i, s := range scripts {
		out[i] = TrainingScriptInfo{Name: s.Name, Description: s.Description}
	}
	custom, err := listCustomTrainingScripts()
	if err != nil {
		logging.Warn("app: custom training scripts could not be listed", "error", err)
		return out
	}
	for _, s := range custom {
		out = append(out, TrainingScriptInfo{Name: s.Name, Description: s.Description, Custom: true})
	}
	return out
}

// TrainingScriptCurvePoint ist eine Ecke im Vorschau-/Live-Kurvenzug: ein
// Zeitpunkt (ab Script-Beginn) mit dem Pegel, den dieser Kanal DANN hat -
// das Frontend verbindet diese Punkte zu einer Linie statt selbst die
// Rampenform nachzubauen.
type TrainingScriptCurvePoint struct {
	AtMs  int     `json:"atMs"`
	Level float64 `json:"level"`
}

// TrainingScriptPreview liefert die volle Kurve eines Scripts (Vibration
// und Sog getrennt) für die Plan-Vorschau, BEVOR eine Session startet -
// siehe docs/TRAINING_MODE_RESEARCH.md's "Plan preview". Reine
// Vorschau-Nennung, läuft nicht über das Gerät. intensityFactor wendet
// dieselbe Skalierung an, die gleich beim echten Start gilt (siehe
// TrainingRequest.IntensityFactor) - sonst würde die Vorschau eine andere
// Kurve zeigen als die, die tatsächlich läuft.
func (a *App) TrainingScriptPreview(scriptName string, intensityFactor float64) (TrainingScriptPreviewResult, error) {
	script, err := resolveTrainingScript(scriptName, intensityFactor)
	if err != nil {
		return TrainingScriptPreviewResult{}, err
	}
	return buildScriptPreview(script), nil
}

// resolveTrainingScript lädt ein benanntes Script (built-in oder eigenes)
// und wendet optional eine Intensitäts-Anpassung an (siehe
// TrainingRequest.IntensityFactor) - aus StartTraining herausgezogen,
// damit es ohne die nil-ctx-Event-Goroutine drumherum testbar ist.
func resolveTrainingScript(scriptName string, intensityFactor float64) (player.TrainingScript, error) {
	script, err := loadAnyTrainingScript(scriptName)
	if err != nil {
		return player.TrainingScript{}, err
	}
	return player.ScaleTrainingScript(script, intensityFactor), nil
}

// TrainingScriptPreviewResult trägt beide Kanäle getrennt, damit das
// Frontend zwei unterschiedlich eingefärbte Linien zeichnen kann.
type TrainingScriptPreviewResult struct {
	Vibration    []TrainingScriptCurvePoint `json:"vibration"`
	Suction      []TrainingScriptCurvePoint `json:"suction"`
	TotalMs      int                        `json:"totalMs"`
	PhaseMarkers []TrainingPhaseMarker      `json:"phaseMarkers"`
	// HasRandomJitter: true if any curve in the script randomizes its
	// peak per repeat (player.ChannelCurve.RandomJitterFraction > 0).
	// The plotted curve above is the NOMINAL, unjittered plan - without
	// this flag a "Variable" script's preview looks like a plain fixed
	// wave even though the actual run varies every repeat.
	HasRandomJitter bool `json:"hasRandomJitter"`
}

// TrainingPhaseMarker markiert, wo im Zeitstrahl eine neue Phase beginnt
// - fürs Beschriften der Vorschau ("Vibration wave" | "Suction focus").
type TrainingPhaseMarker struct {
	AtMs int    `json:"atMs"`
	Name string `json:"name"`
}

// buildScriptPreview rechnet ein Script (ohne Rückmeldungs-/
// Fortschritts-Anpassung - das ist der NOMINALE Plan) in Zeitpunkte um.
// Ein Kanal, den eine Phase nicht anfasst (nil), hält einfach den
// zuletzt bekannten Pegel bis zur nächsten Phase, die ihn wieder anfasst
// - dieselbe "unberührt bleibt unberührt"-Regel wie im Player selbst.
func buildScriptPreview(script player.TrainingScript) TrainingScriptPreviewResult {
	var result TrainingScriptPreviewResult
	t := 0
	lastVib, lastSuc := 0.0, 0.0

	appendCurve := func(points *[]TrainingScriptCurvePoint, curve *player.ChannelCurve, startAt int, last *float64) int {
		if curve == nil {
			return startAt
		}
		at := startAt
		start := curve.StartLevel
		if curve.StartFromCurrent {
			start = *last // wie im Player: vom aktuellen Pegel aus hoch
		}
		*points = append(*points, TrainingScriptCurvePoint{AtMs: at, Level: start})
		at += curve.RampUpMs
		*points = append(*points, TrainingScriptCurvePoint{AtMs: at, Level: curve.PeakLevel})
		at += curve.HoldMs
		*points = append(*points, TrainingScriptCurvePoint{AtMs: at, Level: curve.PeakLevel})
		at += curve.RampDownMs
		*points = append(*points, TrainingScriptCurvePoint{AtMs: at, Level: curve.EndLevel})
		*last = curve.EndLevel
		return at
	}

	hasJitter := func(c *player.ChannelCurve) bool { return c != nil && c.RandomJitterFraction > 0 }

	for _, phase := range script.Phases {
		if hasJitter(phase.Vibration) || hasJitter(phase.Suction) {
			result.HasRandomJitter = true
		}
		result.PhaseMarkers = append(result.PhaseMarkers, TrainingPhaseMarker{AtMs: t, Name: phase.Name})
		for i := 0; i < phase.RepeatCycles; i++ {
			vibEnd := appendCurve(&result.Vibration, phase.Vibration, t, &lastVib)
			sucEnd := appendCurve(&result.Suction, phase.Suction, t, &lastSuc)
			repeatEnd := vibEnd
			if sucEnd > repeatEnd {
				repeatEnd = sucEnd
			}
			if phase.Vibration == nil {
				result.Vibration = append(result.Vibration, TrainingScriptCurvePoint{AtMs: repeatEnd, Level: lastVib})
			}
			if phase.Suction == nil {
				result.Suction = append(result.Suction, TrainingScriptCurvePoint{AtMs: repeatEnd, Level: lastSuc})
			}
			t = repeatEnd + phase.RestMs
		}
	}
	result.TotalMs = t
	return result
}

// sessionLogEntry ist eine Zeile im Trainings-Sitzungsprotokoll (JSONL -
// ein JSON-Objekt pro Zeile, leicht später auszuwerten/zu importieren).
// Das ist die "alle Daten mitschreiben"-Grundlage, um die Ansteuerung des
// Sam Neo 2 später anhand echter Sitzungsverläufe weiter feinabstimmen zu
// können.
type sessionLogEntry struct {
	Timestamp     string  `json:"timestamp"`
	Technique     string  `json:"technique"`
	Channel       string  `json:"channel"`
	CycleIndex    int     `json:"cycleIndex"`
	CyclesTotal   int     `json:"cyclesTotal"`
	PeakIntensity float64 `json:"peakIntensity"`
	HoldMs        int     `json:"holdMs"`
	DurationMs    int64   `json:"durationMs"`

	// StoppedByUser und ReachedPeakAfterMs sind die eigentlich
	// aussagekräftigen Werte für den Trainingsfortschritt: wie oft und wie
	// früh musste unterbrochen werden. Ein Protokoll ohne diese Angaben
	// zeigt nur, dass die Zyklen abgelaufen sind - nicht, wie sie verliefen.
	StoppedByUser      bool `json:"stoppedByUser"`
	ReachedPeakAfterMs int  `json:"reachedPeakAfterMs"`

	// ArousalBefore und RestMs machen im Protokoll nachvollziehbar, WARUM
	// ein Zyklus so verlief - ohne sie sähe eine gedämpfte Haltezeit wie
	// eine falsche Einstellung aus statt wie eine Reaktion.
	ArousalBefore int `json:"arousalBefore"`
	RestMs        int `json:"restMs"`

	// Die folgenden Felder sind nur bei Script-Sessions gesetzt (siehe
	// player.RunTrainingScript) - bei der einfachen Technik/Kanal-Form
	// bleiben sie auf ihrem Nullwert und tauchen im JSON als 0/"" auf,
	// bestehende Auswertung (summarizeSessionLog) liest sie nicht.
	// PeakIntensity oben trägt bei Script-Sessions den GRÖSSEREN der
	// beiden Kanal-Höhepunkte, damit MeanPeakIntensity ein sinnvolles
	// "wie intensiv insgesamt" bleibt, auch ohne diese Felder zu kennen.
	PhaseName         string  `json:"phaseName,omitempty"`
	VibrationPeak     float64 `json:"vibrationPeak,omitempty"`
	SuctionPeak       float64 `json:"suctionPeak,omitempty"`
	PeakFactorApplied float64 `json:"peakFactorApplied,omitempty"`
	RestFactorApplied float64 `json:"restFactorApplied,omitempty"`
}

// StartTraining startet eine Trainings-Session in einer eigenen Goroutine.
// Fortschritt kommt über Events ("training:cycle", "training:done",
// "training:error") - derselbe Aufbau wie StartPlayback.
func (a *App) StartTraining(req TrainingRequest) error {
	var script player.TrainingScript
	if req.ScriptName != "" {
		var err error
		script, err = resolveTrainingScript(req.ScriptName, req.IntensityFactor)
		if err != nil {
			return err
		}
	} else if req.Cycles <= 0 {
		return fmt.Errorf("at least 1 cycle required")
	}

	dev, reusedDevice := a.claimSessionDevice(req.Mock)

	opts := player.TrainingOptions{
		Technique:           player.TrainingTechnique(req.Technique),
		Channel:             player.TrainingChannel(req.Channel),
		Cycles:              req.Cycles,
		RampUpMs:            req.RampUpMs,
		HoldMs:              req.HoldMs,
		RestMs:              req.RestMs,
		PeakIntensity:       req.PeakIntensity,
		PlateauFraction:     req.PlateauFraction,
		ProgressionPerCycle: req.ProgressionPerCycle,
	}

	// Claim the session slot first — otherwise a rejected start would still
	// overwrite activeDevice and point shutdown at the wrong device.
	ctx, err := a.tryStartSession()
	if err != nil {
		return err
	}
	// Erst nach dem Belegen des Slots: ein abgelehnter Start ließ sonst
	// eine leere Protokolldatei samt offenem Handle zurück.
	logName := req.Technique
	if req.ScriptName != "" {
		logName = req.ScriptName
	}
	sessionFile, sessionErr := a.openSessionLog(logName)
	if sessionErr != nil {
		logging.Warn("app: session log could not be created", "error", sessionErr)
	}
	ctx, cancelSafetyTimeout := context.WithTimeout(ctx, maxTrainingSessionDuration)
	a.stateMu.Lock()
	a.activeDevice = dev
	a.stateMu.Unlock()

	go func() {
		defer a.endSession()
		defer cancelSafetyTimeout()
		if sessionFile != nil {
			defer sessionFile.Close()
		}

		if reusedDevice {
			// Bereits über den Geräte-Tab verbunden - nicht erneut
			// verbinden und am Ende NICHT trennen, das bleibt dessen
			// Sache (siehe claimSessionDevice).
			runtime.EventsEmit(a.ctx, "training:log", "Using existing connection from Tools → Device.")
		} else {
			connectCtx, connectCancel := context.WithTimeout(ctx, 20*time.Second)
			defer connectCancel()
			runtime.EventsEmit(a.ctx, "training:log", "Connecting...")
			if err := dev.Connect(connectCtx); err != nil {
				logging.Error("app: connection failed (training)", "error", err)
				runtime.EventsEmit(a.ctx, "training:error", err.Error())
				runtime.EventsEmit(a.ctx, "training:done")
				return
			}
			defer dev.Disconnect()
		}

		runtime.EventsEmit(a.ctx, "training:log", "Training starting...")

		control := player.NewTrainingControl()
		control.SetOnLevel(func(vib, suc float64) {
			runtime.EventsEmit(a.ctx, "training:levels", map[string]any{
				"vibration": vib,
				"suction":   suc,
			})
		})
		a.stateMu.Lock()
		a.trainingControl = control
		a.stateMu.Unlock()
		defer func() {
			a.stateMu.Lock()
			a.trainingControl = nil
			a.stateMu.Unlock()
		}()

		var runErr error
		if req.ScriptName != "" {
			runErr = player.RunTrainingScript(ctx, dev, script, control, func(result player.TrainingScriptCycleResult) {
				a.emitTrainingScriptCycle(req, sessionFile, result)
			})
		} else {
			runErr = player.RunTrainingWithControl(ctx, dev, opts, control, func(result player.TrainingCycleResult) {
				a.emitTrainingCycle(req, sessionFile, result)
			})
		}

		logMsg, errMsg := trainingDoneMessages(runErr)
		if logMsg != "" {
			runtime.EventsEmit(a.ctx, "training:log", logMsg)
		}
		if errMsg != "" {
			runtime.EventsEmit(a.ctx, "training:error", errMsg)
		}
		runtime.EventsEmit(a.ctx, "training:done")
	}()

	return nil
}

// emitTrainingCycle behandelt ein TrainingCycleResult der einfachen
// Technik/Kanal-Form (RunTrainingWithControl) - Event fürs Live-Update,
// Zeile fürs Sessionprotokoll.
func (a *App) emitTrainingCycle(req TrainingRequest, sessionFile *os.File, result player.TrainingCycleResult) {
	runtime.EventsEmit(a.ctx, "training:cycle", map[string]any{
		"cycleIndex":         result.CycleIndex,
		"cyclesTotal":        req.Cycles,
		"peakIntensity":      result.PeakIntensity,
		"holdMs":             result.HoldMs,
		"stoppedByUser":      result.StoppedByUser,
		"reachedPeakAfterMs": result.ReachedPeakAfterMs,
		"arousalBefore":      result.ArousalBefore,
		"restMs":             result.RestMs,
	})
	if sessionFile == nil {
		return
	}
	entry := sessionLogEntry{
		Timestamp:     result.EndedAt.Format(time.RFC3339),
		Technique:     req.Technique,
		Channel:       req.Channel,
		CycleIndex:    result.CycleIndex,
		CyclesTotal:   req.Cycles,
		PeakIntensity: result.PeakIntensity,
		HoldMs:        result.HoldMs,
		DurationMs:    result.EndedAt.Sub(result.StartedAt).Milliseconds(),

		StoppedByUser:      result.StoppedByUser,
		ReachedPeakAfterMs: result.ReachedPeakAfterMs,
		ArousalBefore:      result.ArousalBefore,
		RestMs:             result.RestMs,
	}
	writeSessionLogEntry(sessionFile, entry)
}

// emitTrainingScriptCycle ist emitTrainingCycle's Gegenstück für
// player.RunTrainingScript - eigenes Event ("training:scriptCycle" statt
// "training:cycle"), weil eine Script-Wiederholung zwei Kanal-Höhepunkte
// und eine Phase trägt statt einer einzelnen Kurve, und das Frontend
// beide Formen unterschiedlich anzeigt (siehe training.js).
func (a *App) emitTrainingScriptCycle(req TrainingRequest, sessionFile *os.File, result player.TrainingScriptCycleResult) {
	runtime.EventsEmit(a.ctx, "training:scriptCycle", map[string]any{
		"phaseIndex":        result.PhaseIndex,
		"phaseName":         result.PhaseName,
		"phasesTotal":       result.PhasesTotal,
		"repeatIndex":       result.RepeatIndex,
		"repeatsTotal":      result.RepeatsTotal,
		"vibrationPeak":     result.VibrationPeak,
		"suctionPeak":       result.SuctionPeak,
		"restMs":            result.RestMs,
		"stoppedByUser":     result.StoppedByUser,
		"arousalBefore":     result.ArousalBefore,
		"peakFactorApplied": result.PeakFactorApplied,
		"restFactorApplied": result.RestFactorApplied,
	})
	if sessionFile == nil {
		return
	}
	peak := result.VibrationPeak
	if result.SuctionPeak > peak {
		peak = result.SuctionPeak
	}
	entry := sessionLogEntry{
		Timestamp:     result.EndedAt.Format(time.RFC3339),
		Technique:     req.ScriptName,
		Channel:       "script",
		CycleIndex:    result.PhaseIndex*1000 + result.RepeatIndex, // grob, nur fürs Protokoll lesbar geordnet
		PeakIntensity: peak,
		DurationMs:    result.EndedAt.Sub(result.StartedAt).Milliseconds(),

		StoppedByUser:      result.StoppedByUser,
		ReachedPeakAfterMs: result.ReachedPeakAfterMs,
		ArousalBefore:      result.ArousalBefore,
		RestMs:             result.RestMs,

		PhaseName:         result.PhaseName,
		VibrationPeak:     result.VibrationPeak,
		SuctionPeak:       result.SuctionPeak,
		PeakFactorApplied: result.PeakFactorApplied,
		RestFactorApplied: result.RestFactorApplied,
	}
	writeSessionLogEntry(sessionFile, entry)
}

func writeSessionLogEntry(sessionFile *os.File, entry sessionLogEntry) {
	b, err := json.Marshal(entry)
	if err != nil {
		logging.Warn("app: training cycle could not be encoded for the session log", "error", err)
		return
	}
	if _, writeErr := sessionFile.Write(append(b, '\n')); writeErr != nil {
		logging.Warn("app: training cycle could not be written to session log", "error", writeErr)
	}
}

// StopTrainingCycle bricht den laufenden Zyklus ab und geht in die Pause -
// die Session läuft danach weiter. Das ist der eigentliche Kern der
// Stop-Start-Methode: nicht eine Stoppuhr entscheidet, wann unterbrochen
// wird, sondern der Anwender.
func (a *App) StopTrainingCycle() error {
	a.stateMu.RLock()
	control := a.trainingControl
	a.stateMu.RUnlock()
	if control == nil {
		return fmt.Errorf("no training is running")
	}
	control.StopCycle()
	logging.Info("training: cycle interrupted on request")
	return nil
}

// ReportArousal nimmt eine Rückmeldung auf der Skala 1-10 entgegen. Sie
// wirkt auf den nächsten Zyklus: hohe Werte führen zu kürzeren, sanfteren
// Zyklen mit längerer Pause. Damit wird aus dem gesteuerten Ablauf ein
// geregelter.
func (a *App) ReportArousal(level int) error {
	a.stateMu.RLock()
	control := a.trainingControl
	a.stateMu.RUnlock()
	if control == nil {
		return fmt.Errorf("no training is running")
	}
	if level < 1 || level > 10 {
		return fmt.Errorf("value must be between 1 and 10 (got %d)", level)
	}
	control.ReportArousal(level)
	if level >= highArousalStopsCurrentCycle {
		// Reporting 9-10 clearly means "this is too much right now", not
		// just "shape the next cycle a bit gentler" - close the gap
		// between reporting it and the ramp actually responding instead
		// of making the user reach for a second, separate button too.
		control.StopCycle()
		logging.Info("training: high feedback also interrupted the running cycle", "arousal", level)
	}
	logging.Info("training: feedback", "arousal", level)
	return nil
}

func (a *App) StopTraining() {
	a.stopSession()
}

// openSessionLog legt (falls nötig) den Sessions-Unterordner neben der
// normalen Logdatei an und öffnet eine neue JSONL-Datei für diese Session.
func (a *App) openSessionLog(technique string) (*os.File, error) {
	dir, err := logging.Dir()
	if err != nil {
		return nil, err
	}
	sessionsDir := filepath.Join(dir, "sessions")
	if err := os.MkdirAll(sessionsDir, 0755); err != nil {
		return nil, err
	}
	// Eigene Scripts tragen frei getippte Namen ("Abend / kurz", "Edge:
	// 10 min?") - roh im Dateinamen ließ "/" (Windows auch ":" und "?")
	// os.Create scheitern, und die Session fehlte in Verlauf und Anpassung.
	// Die Zuordnung zum Script läuft über Technique IM Protokoll, nicht
	// über den Dateinamen.
	slug, err := scriptFileSlug(technique)
	if err != nil {
		slug = "session"
	}
	name := fmt.Sprintf("training-%s-%s.jsonl", slug, time.Now().Format("20060102-150405"))
	return os.Create(filepath.Join(sessionsDir, name))
}
