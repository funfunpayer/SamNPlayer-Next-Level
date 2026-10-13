package device

import (
	"context"
	"strings"
	"testing"
	"time"
)

// speedUpDiagnosticsForTest verkürzt die Warteschritte, damit der Test nicht
// mehrere Sekunden Echtzeit braucht - RunDiagnostics' Verhalten (welche
// Phasen laufen, was geloggt wird) hängt nicht von der Dauer der Pausen ab.
func speedUpDiagnosticsForTest(t *testing.T) {
	t.Helper()
	origIntervals := updateRateIntervals
	origOffset := offsetChangeDelay
	origRapid := rapidSwitchDelay
	updateRateIntervals = []time.Duration{time.Millisecond, time.Millisecond}
	offsetChangeDelay = time.Millisecond
	rapidSwitchDelay = time.Millisecond
	t.Cleanup(func() {
		updateRateIntervals = origIntervals
		offsetChangeDelay = origOffset
		rapidSwitchDelay = origRapid
	})
}

func TestRunDiagnosticsWithRawCapableDevice(t *testing.T) {
	speedUpDiagnosticsForTest(t)
	dev := NewMock(false)

	var streamed []DiagnosticsEntry
	report := RunDiagnostics(context.Background(), dev, 0, func(e DiagnosticsEntry) {
		streamed = append(streamed, e)
	})

	if !report.RawCapable {
		t.Fatal("Mock implementiert RawCapable, report.RawCapable sollte true sein")
	}
	if report.Interrupted {
		t.Fatal("unabgebrochener Kontext, report.Interrupted sollte false bleiben")
	}
	if len(report.Log) == 0 {
		t.Fatal("erwarte ein nicht-leeres Transport-Log")
	}
	if len(streamed) != len(report.Log) {
		t.Fatalf("onEntry sollte für jeden Log-Eintrag genau einmal aufgerufen werden: %d vs %d",
			len(streamed), len(report.Log))
	}

	wantPhases := map[string]bool{
		"raw_sweep_vibration": false,
		"raw_sweep_suction":   false,
		"vibration_alone":     false,
		"suction_alone":       false,
		"simultaneous":        false,
		"offset_change":       false,
		"rapid_switching":     false,
		"stop":                false,
	}
	for _, p := range report.Phases {
		if _, ok := wantPhases[p.Phase]; ok {
			wantPhases[p.Phase] = true
		}
		if p.Errors != 0 {
			t.Errorf("Mock sollte nie Fehler liefern, Phase %q hat %d", p.Phase, p.Errors)
		}
	}
	for phase, seen := range wantPhases {
		if !seen {
			t.Errorf("erwartete Phase %q fehlt im Bericht", phase)
		}
	}

	if len(report.Notes) == 0 {
		t.Fatal("erwarte mindestens die Nicht-gemessen-Hinweisnotiz")
	}
}

func TestRunDiagnosticsIncludesConnectLatency(t *testing.T) {
	speedUpDiagnosticsForTest(t)
	dev := NewMock(false)

	report := RunDiagnostics(context.Background(), dev, 1234.5, nil)

	if len(report.Log) == 0 || report.Log[0].Phase != "connection" {
		t.Fatalf("erwarte den ersten Log-Eintrag als 'connection', log leer oder falsch: %d Einträge", len(report.Log))
	}
	if report.Log[0].LatencyMs != 1234.5 {
		t.Errorf("Connect-Latenz nicht übernommen: %v", report.Log[0].LatencyMs)
	}
}

// plainDevice reicht nur die fünf Device-Methoden weiter, absichtlich ohne
// SetVibrationRaw/SetSuctionRaw - damit ein Typ ohne RawCapable existiert,
// so wie device.Intiface in echt.
type plainDevice struct{ inner Device }

func (p *plainDevice) Connect(ctx context.Context) error { return p.inner.Connect(ctx) }
func (p *plainDevice) Disconnect() error                 { return p.inner.Disconnect() }
func (p *plainDevice) SetVibration(v float64) error      { return p.inner.SetVibration(v) }
func (p *plainDevice) SetSuction(v float64) error        { return p.inner.SetSuction(v) }
func (p *plainDevice) Stop() error                       { return p.inner.Stop() }

func TestRunDiagnosticsSkipsRawSweepWithoutRawCapable(t *testing.T) {
	speedUpDiagnosticsForTest(t)
	dev := &plainDevice{inner: NewMock(false)}

	report := RunDiagnostics(context.Background(), dev, 0, nil)

	if report.RawCapable {
		t.Fatal("plainDevice bietet keine Rohwerte an, report.RawCapable sollte false sein")
	}
	for _, p := range report.Phases {
		if p.Phase == "raw_sweep_vibration" || p.Phase == "raw_sweep_suction" {
			t.Errorf("Rohwert-Sweep sollte übersprungen werden, fand aber Phase %q", p.Phase)
		}
	}
	foundSkipNote := false
	for _, n := range report.Notes {
		if strings.Contains(n, "Rohwert-Sweep übersprungen") {
			foundSkipNote = true
		}
	}
	if !foundSkipNote {
		t.Error("erwarte eine Notiz, dass der Rohwert-Sweep übersprungen wurde")
	}
}

func TestRunDiagnosticsStopsOnCanceledContext(t *testing.T) {
	speedUpDiagnosticsForTest(t)
	dev := NewMock(false)
	ctx, cancel := context.WithCancel(context.Background())
	cancel()

	report := RunDiagnostics(ctx, dev, 0, nil)

	if !report.Interrupted {
		t.Fatal("bereits abgebrochener Kontext sollte report.Interrupted=true ergeben")
	}
}
