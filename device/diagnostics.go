package device

import (
	"context"
	"time"
)

// RunDiagnostics implementiert die in docs/SAM_NEO_2_RESEARCH.md §11/§12
// geforderte Messreihe, soweit sie ohne einen physischen Sensor am Gerät
// überhaupt objektiv messbar ist: welche Rohwerte das Gerät ohne Fehler
// annimmt, wie sich die Schreib-Latenz bei steigender Update-Rate verhält,
// und ob beide Kanäle sich gegenseitig beeinflussen (im Sinne von
// Schreibfehlern/Latenz-Ausreißern - NICHT im Sinne von gefühlter Intensität,
// das kann nur der Nutzer selbst beurteilen).
//
// Bewusst NICHT gemessen (weil ohne Sensor am Gerät nicht messbar): echte
// Anstiegs-/Abfallzeit der Vibration/des Sogs, gefühlte Intensität, gefühlte
// Kanalüberlagerung. Der Bericht sagt das explizit in Notes, statt eine
// Zahl vorzutäuschen, die niemand gemessen hat.
//
// dev muss bereits verbunden sein (siehe app_device.go's testDeviceOrErr) -
// RunDiagnostics baut selbst keine Verbindung auf und trennt keine, das
// bleibt Aufgabe des Geräte-Tabs.
type RawCapable interface {
	SetVibrationRaw(byte) error
	SetSuctionRaw(byte) error
}

// DiagnosticsEntry ist eine einzelne Transport-Log-Zeile: pro Kommando
// monotone Zeit (seit Laufbeginn), Phase, Kanal, gewünschter und
// tatsächlich gesendeter Wert, Latenz des Schreibvorgangs, Fehler.
type DiagnosticsEntry struct {
	TimeOffsetMs int64   `json:"timeOffsetMs"`
	Phase        string  `json:"phase"`
	Channel      string  `json:"channel,omitempty"`
	WantedValue  float64 `json:"wantedValue,omitempty"`
	SentRaw      *int    `json:"sentRaw,omitempty"`
	LatencyMs    float64 `json:"latencyMs"`
	Error        string  `json:"error,omitempty"`
}

// DiagnosticsPhaseSummary fasst eine Testphase zusammen (Kommandos, Fehler,
// Latenz-Statistik) - das ist die knappe Sicht für die Oberfläche, das volle
// Log bleibt für spätere Detailanalyse erhalten.
type DiagnosticsPhaseSummary struct {
	Phase         string  `json:"phase"`
	Commands      int     `json:"commands"`
	Errors        int     `json:"errors"`
	MeanLatencyMs float64 `json:"meanLatencyMs"`
	MaxLatencyMs  float64 `json:"maxLatencyMs"`
}

// DiagnosticsReport ist das Gesamtergebnis eines Diagnoselaufs.
type DiagnosticsReport struct {
	StartedAt   time.Time                 `json:"startedAt"`
	DurationMs  int64                     `json:"durationMs"`
	RawCapable  bool                      `json:"rawCapable"`
	Phases      []DiagnosticsPhaseSummary `json:"phases"`
	Log         []DiagnosticsEntry        `json:"log"`
	Notes       []string                  `json:"notes"`
	Interrupted bool                      `json:"interrupted"`
}

// rawSweepValues: eine Stichprobe über 0-255 statt aller 256 Werte - genug,
// um Annahme-Grenzen und Fehler zu finden, ohne bei jedem Lauf hunderte
// GATT-Writes gegen echte Hardware zu schicken.
func rawSweepValues() []int {
	values := make([]int, 0, 33)
	for v := 0; v <= 255; v += 8 {
		values = append(values, v)
	}
	if values[len(values)-1] != 255 {
		values = append(values, 255)
	}
	return values
}

// updateRateIntervals: sinkende Abstände zwischen Kommandos, um die maximal
// stabile Update-Rate zu finden (§11 "maximale stabile Update-Rate").
var updateRateIntervals = []time.Duration{
	200 * time.Millisecond,
	100 * time.Millisecond,
	50 * time.Millisecond,
	20 * time.Millisecond,
	10 * time.Millisecond,
}

const commandsPerRateStep = 8

// offsetChangeDelay/rapidSwitchDelay: Pausen innerhalb der Kanalinteraktions-
// Phase (siehe unten). Als Variablen statt Literale, damit Tests sie
// verkürzen können, ohne echte Sekunden zu warten.
var (
	offsetChangeDelay = 100 * time.Millisecond
	rapidSwitchDelay  = 30 * time.Millisecond
)

func RunDiagnostics(ctx context.Context, dev Device, connectLatencyMs float64, onEntry func(DiagnosticsEntry)) DiagnosticsReport {
	start := time.Now()
	report := DiagnosticsReport{StartedAt: start}

	phaseCommands := map[string]int{}
	phaseErrors := map[string]int{}
	phaseLatencySum := map[string]float64{}
	phaseLatencyMax := map[string]float64{}
	var phaseOrder []string

	record := func(e DiagnosticsEntry) {
		e.TimeOffsetMs = time.Since(start).Milliseconds()
		report.Log = append(report.Log, e)
		if _, seen := phaseCommands[e.Phase]; !seen {
			phaseOrder = append(phaseOrder, e.Phase)
		}
		phaseCommands[e.Phase]++
		if e.Error != "" {
			phaseErrors[e.Phase]++
		}
		phaseLatencySum[e.Phase] += e.LatencyMs
		if e.LatencyMs > phaseLatencyMax[e.Phase] {
			phaseLatencyMax[e.Phase] = e.LatencyMs
		}
		if onEntry != nil {
			onEntry(e)
		}
	}

	if connectLatencyMs > 0 {
		record(DiagnosticsEntry{Phase: "connection", LatencyMs: connectLatencyMs})
	}

	raw, rawOK := dev.(RawCapable)
	report.RawCapable = rawOK

	interrupted := func() bool {
		select {
		case <-ctx.Done():
			return true
		default:
			return false
		}
	}

	// Phase 1: Rohwert-Annahme, pro Kanal - welche Werte nimmt das Gerät
	// ohne Fehler an (§11 "tatsächlich akzeptierte Werte/Stufen").
	if rawOK {
		for _, ch := range []struct {
			name string
			set  func(byte) error
		}{
			{"vibration", raw.SetVibrationRaw},
			{"suction", raw.SetSuctionRaw},
		} {
			phase := "raw_sweep_" + ch.name
			for _, v := range rawSweepValues() {
				if interrupted() {
					report.Interrupted = true
					break
				}
				value := v
				t0 := time.Now()
				err := ch.set(byte(value))
				latency := time.Since(t0)
				entry := DiagnosticsEntry{
					Phase:       phase,
					Channel:     ch.name,
					WantedValue: float64(value) / 255.0,
					SentRaw:     &value,
					LatencyMs:   float64(latency.Microseconds()) / 1000.0,
				}
				if err != nil {
					entry.Error = err.Error()
				}
				record(entry)
			}
			_ = ch.set(0)
			if report.Interrupted {
				break
			}
		}
	} else {
		report.Notes = append(report.Notes,
			"Rohwert-Sweep übersprungen: dieser Transport unterstützt keine Rohwerte (nur die echte BLE-Verbindung und Mock bieten das).")
	}

	// Phase 2: maximale stabile Update-Rate (nur Vibration, per Rohwert -
	// wenn nicht verfügbar, über den regulären 0.0-1.0-Kanal).
	if !report.Interrupted {
		phase := "update_rate"
		for _, interval := range updateRateIntervals {
			for i := 0; i < commandsPerRateStep; i++ {
				if interrupted() {
					report.Interrupted = true
					break
				}
				value := 0.0
				if i%2 == 0 {
					value = 1.0
				}
				t0 := time.Now()
				var err error
				if rawOK {
					v := 0
					if i%2 == 0 {
						v = 255
					}
					err = raw.SetVibrationRaw(byte(v))
				} else {
					err = dev.SetVibration(value)
				}
				latency := time.Since(t0)
				entry := DiagnosticsEntry{
					Phase:       phase + "_" + interval.String(),
					Channel:     "vibration",
					WantedValue: value,
					LatencyMs:   float64(latency.Microseconds()) / 1000.0,
				}
				if err != nil {
					entry.Error = err.Error()
				}
				record(entry)
				time.Sleep(interval)
			}
			if report.Interrupted {
				break
			}
		}
		if rawOK {
			_ = raw.SetVibrationRaw(0)
		} else {
			_ = dev.SetVibration(0)
		}
	}

	// Phase 3: Kanalinteraktion (§11 "Kanalinteraktion") - Vibration allein,
	// Sog allein, beide gleichzeitig, zeitversetzt, schnelle Wechsel. Über
	// die reguläre 0.0-1.0-Schnittstelle, weil das dem echten Wiedergabepfad
	// entspricht (nicht dem Rohwert-Test).
	if !report.Interrupted {
		runSetter := func(phase, channel string, set func(float64) error, value float64) {
			if interrupted() {
				report.Interrupted = true
				return
			}
			t0 := time.Now()
			err := set(value)
			latency := time.Since(t0)
			entry := DiagnosticsEntry{
				Phase:       phase,
				Channel:     channel,
				WantedValue: value,
				LatencyMs:   float64(latency.Microseconds()) / 1000.0,
			}
			if err != nil {
				entry.Error = err.Error()
			}
			record(entry)
		}

		ramp := []float64{0.25, 0.5, 0.75, 1.0, 0.5, 0}

		for _, v := range ramp {
			runSetter("vibration_alone", "vibration", dev.SetVibration, v)
		}
		for _, v := range ramp {
			runSetter("suction_alone", "suction", dev.SetSuction, v)
		}
		for _, v := range []float64{0.25, 0.5, 0.75, 1.0, 0} {
			runSetter("simultaneous", "vibration", dev.SetVibration, v)
			runSetter("simultaneous", "suction", dev.SetSuction, v)
		}
		for _, v := range []float64{0.5, 0} {
			runSetter("offset_change", "vibration", dev.SetVibration, v)
			time.Sleep(offsetChangeDelay)
			runSetter("offset_change", "suction", dev.SetSuction, v)
			time.Sleep(offsetChangeDelay)
		}
		for i := 0; i < 10 && !report.Interrupted; i++ {
			if i%2 == 0 {
				runSetter("rapid_switching", "vibration", dev.SetVibration, 0.8)
				runSetter("rapid_switching", "suction", dev.SetSuction, 0)
			} else {
				runSetter("rapid_switching", "vibration", dev.SetVibration, 0)
				runSetter("rapid_switching", "suction", dev.SetSuction, 0.8)
			}
			time.Sleep(rapidSwitchDelay)
		}
	}

	_ = dev.Stop()
	record(DiagnosticsEntry{Phase: "stop", LatencyMs: 0})

	for _, phase := range phaseOrder {
		n := phaseCommands[phase]
		mean := 0.0
		if n > 0 {
			mean = phaseLatencySum[phase] / float64(n)
		}
		report.Phases = append(report.Phases, DiagnosticsPhaseSummary{
			Phase:         phase,
			Commands:      n,
			Errors:        phaseErrors[phase],
			MeanLatencyMs: mean,
			MaxLatencyMs:  phaseLatencyMax[phase],
		})
	}

	report.Notes = append(report.Notes,
		"Nicht gemessen (ohne Sensor am Gerät nicht objektiv feststellbar): "+
			"tatsächliche physische Anstiegs-/Abfallzeit, gefühlte Intensität, "+
			"gefühlte Kanalüberlagerung. Latenzwerte hier sind Software-Round-Trip-"+
			"Zeiten des Schreibvorgangs, keine physische Reaktionszeit des Geräts.")
	report.DurationMs = time.Since(start).Milliseconds()
	return report
}
