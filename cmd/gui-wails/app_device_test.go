package main

import (
	"strings"
	"testing"
)

func TestDeviceTabFlow(t *testing.T) {
	a := NewApp()

	if st := a.GetDeviceStatus(); st.Connected {
		t.Fatal("frisch gestartet darf nichts verbunden sein")
	}
	if err := a.TestVibration(0.5); err == nil {
		t.Fatal("Test ohne Verbindung muss abgelehnt werden")
	}

	st, err := a.ConnectDevice(true)
	if err != nil || !st.Connected {
		t.Fatalf("Mock-Verbindung fehlgeschlagen: %v %+v", err, st)
	}
	if !st.Mock {
		t.Error("Mock-Flag nicht gesetzt")
	}

	if _, err := a.ConnectDevice(true); err == nil {
		t.Error("doppeltes Verbinden muss abgelehnt werden")
	}
	if err := a.TestVibration(0.5); err != nil {
		t.Errorf("Vibrationstest: %v", err)
	}
	if err := a.TestSuction(1.0); err != nil {
		t.Errorf("Sogtest: %v", err)
	}
	if err := a.TestStop(); err != nil {
		t.Errorf("Stop: %v", err)
	}

	// Kern: eine Session darf die bestehende Testverbindung wiederverwenden
	// (claimSessionDevice), statt dass man vorher im Geräte-Tab trennen muss.
	if _, err := a.tryStartSession(); err != nil {
		t.Errorf("Session-Start bei offener Testverbindung sollte die Verbindung wiederverwenden: %v", err)
	}
	dev, reused := a.claimSessionDevice(false)
	if !reused {
		t.Error("claimSessionDevice sollte die Testverbindung wiederverwenden (reused=true)")
	}
	if dev != a.testDevice {
		t.Error("claimSessionDevice sollte GENAU das testDevice-Objekt liefern, keine Kopie/neues Gerät")
	}

	// Trennen während eine Session diese Verbindung nutzt muss abgelehnt
	// werden - sonst würde die laufende Wiedergabe/das Training mitten im
	// Senden abgewürgt.
	if _, err := a.DisconnectDevice(); err == nil {
		t.Error("Trennen bei laufender Session (wiederverwendete Verbindung) muss abgelehnt werden")
	}
	if st := a.GetDeviceStatus(); !st.Connected {
		t.Error("abgelehntes Trennen darf die Verbindung nicht trennen")
	}
	a.endSession()

	// Nach Sessionende ist die Testverbindung weiterhin verbunden (sie
	// gehört dem Geräte-Tab, nicht der Session) und lässt sich jetzt trennen.
	if st := a.GetDeviceStatus(); !st.Connected {
		t.Error("nach Sessionende sollte die Testverbindung noch bestehen")
	}
	if _, err := a.DisconnectDevice(); err != nil {
		t.Errorf("Trennen nach Sessionende: %v", err)
	}
	if st := a.GetDeviceStatus(); st.Connected {
		t.Error("nach dem Trennen darf nichts verbunden sein")
	}

	// Ohne Testverbindung erzeugt eine Session ihr eigenes Gerät.
	if _, err := a.tryStartSession(); err != nil {
		t.Errorf("Session nach Trennen: %v", err)
	}
	if _, reused := a.claimSessionDevice(true); reused {
		t.Error("ohne Testverbindung sollte claimSessionDevice ein neues Gerät erzeugen (reused=false)")
	}
	// Und dann sperrt die Session umgekehrt den Gerätetest.
	if _, err := a.ConnectDevice(true); err == nil {
		t.Error("Verbinden bei laufender Session muss abgelehnt werden")
	}
	a.endSession()
}

// TestClaimTestDeviceConnectSerializes prüft die eigentliche Absicherung
// hinter ConnectDevice/ConnectDeviceVia direkt: solange ein Connect-Versuch
// als laufend markiert ist (testDevice selbst ist bis zum Erfolg noch nil),
// muss ein zweiter claimTestDeviceConnect()-Aufruf abgelehnt werden - sonst
// könnten zwei nahezu gleichzeitige Connect-Aufrufe beide die alte
// Nil-Prüfung bestehen und am Ende überschreibt der zuletzt fertige den
// anderen, dessen Verbindung dann offen, aber unerreichbar bliebe.
func TestClaimTestDeviceConnectSerializes(t *testing.T) {
	a := NewApp()

	if err := a.claimTestDeviceConnect(); err != nil {
		t.Fatalf("erster Claim muss gelingen: %v", err)
	}
	if err := a.claimTestDeviceConnect(); err == nil {
		t.Error("zweiter Claim während des ersten muss abgelehnt werden")
	}
	a.releaseTestDeviceConnect()
	if err := a.claimTestDeviceConnect(); err != nil {
		t.Errorf("Claim nach dem Freigeben muss wieder gelingen: %v", err)
	}
	a.releaseTestDeviceConnect()
}

func TestDeviceStatusCarriesCapabilities(t *testing.T) {
	a := NewApp()
	if st := a.GetDeviceStatus(); len(st.Capabilities.Features) != 0 {
		t.Fatalf("no device → no channels, got %+v", st.Capabilities)
	}
	st, err := a.ConnectDevice(true)
	if err != nil {
		t.Fatal(err)
	}
	defer a.DisconnectDevice()
	got := strings.Join(st.Capabilities.Channels(), ",")
	if got != "vibration,suction" {
		t.Fatalf("mock channels %q, want vibration,suction", got)
	}
}
