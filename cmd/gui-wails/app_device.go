package main

import (
	"context"
	"fmt"
	"time"

	"github.com/funfunpayer/SamNPlayer/device"
	"github.com/funfunpayer/SamNPlayer/logging"
)

// Dieser Tab existiert, weil bisher nirgends sichtbar war, ob überhaupt ein
// Gerät gefunden und richtig erkannt wurde: das Gerät wurde ausschließlich
// innerhalb von StartPlayback/StartTraining erzeugt, verbunden und danach
// wieder verworfen. Ein Verbindungsfehler fiel damit erst mitten in einer
// Wiedergabe auf, und es gab keine Möglichkeit, die Ansteuerung isoliert zu
// prüfen.
//
// Bewusste Einschränkung: BLE erlaubt ohnehin nur eine Verbindung zum Gerät,
// deshalb schließen sich Testverbindung und laufende Session gegenseitig aus.
// Das wird explizit gemeldet, statt im Hintergrund die eine Verbindung durch
// die andere zu ersetzen - eine stillschweigende Übernahme wäre bei einem
// Gerät, das gerade läuft, die schlechtere Überraschung.

// TestConnectionTimeout begrenzt den BLE-Scan im Geräte-Tab.
const testConnectTimeout = 20 * time.Second

// DeviceStatus ist der Zustand, den der Geräte-Tab anzeigt.
type DeviceStatus struct {
	Connected     bool   `json:"connected"`
	Mock          bool   `json:"mock"`
	Name          string `json:"name"`
	Address       string `json:"address"`
	RSSI          int    `json:"rssi"`
	SessionActive bool   `json:"sessionActive"`
	Transport     string `json:"transport"` // ble | intiface | mock | ""
	// BatteryPct 0–100 wenn BatteryOK; sonst ignorieren (kein Platzhalter in der UI).
	BatteryPct int  `json:"batteryPct"`
	BatteryOK  bool `json:"batteryOk"`
	// Fähigkeiten des verbundenen Geräts (Anzeige „was geht“).
	CapVibration bool `json:"capVibration"`
	CapSuction   bool `json:"capSuction"`
	CapBattery   bool `json:"capBattery"`
	CapRaw       bool `json:"capRaw"`
	// Capabilities: every channel of the connected device, driven or not
	// (docs/DEVICE_SUPPORT.md D1). Empty while nothing is connected.
	Capabilities device.Capabilities `json:"capabilities"`
}

// GetDeviceStatus liefert den aktuellen Verbindungszustand der Testverbindung.
func (a *App) GetDeviceStatus() DeviceStatus {
	a.stateMu.RLock()
	dev := a.testDevice
	isMock := a.testDeviceMock
	transport := a.testDeviceTransport
	session := a.sessionActive
	a.stateMu.RUnlock()

	st := DeviceStatus{SessionActive: session, Mock: isMock, Transport: transport}
	if dev == nil {
		return st
	}
	st.Connected = true
	st.Capabilities = device.CapabilitiesOf(dev)
	if intiface, ok := dev.(*device.Intiface); ok {
		info := intiface.Info()
		st.Connected = info.Connected
		st.Name = info.Name
		st.Address = info.Address
		st.CapVibration, st.CapSuction, st.CapBattery = intiface.Capabilities()
		st.CapRaw = false
		if pct, ok := intiface.BatteryLevel(); ok {
			st.BatteryPct = pct
			st.BatteryOK = true
			st.CapBattery = true
		}
		return st
	}
	if tc, ok := dev.(*device.TCode); ok {
		info := tc.Info()
		st.Connected = info.Connected
		st.Name = info.Name
		st.Address = info.Address
		return st
	}
	if real, ok := dev.(*device.SamNeo2); ok {
		info := real.Info()
		st.Connected = info.Connected
		st.Name = info.Name
		st.Address = info.Address
		st.RSSI = info.RSSI
		st.CapVibration = true
		st.CapSuction = true
		st.CapRaw = true
		if pct, ok := real.BatteryLevel(); ok {
			st.BatteryPct = pct
			st.BatteryOK = true
			st.CapBattery = true
		} else if real.BatteryProbed() {
			st.CapBattery = false
		}
	} else {
		st.Name = "Mock device (no real hardware)"
		st.CapVibration = true
		st.CapSuction = true
		st.CapRaw = true
		st.CapBattery = false
	}
	return st
}

// ConnectDevice baut eine dauerhafte Testverbindung auf, unabhängig von
// Wiedergabe und Training.
// ConnectDevice verbindet mit dem Gerät. transport wählt den Weg:
// "ble" (Standard, eigener Bluetooth-Adapter), "intiface" (über einen
// laufenden Buttplug-Server, meist Intiface Central) oder "mock".
//
// Der Intiface-Weg braucht keinen eigenen Bluetooth-Adapter und funktioniert
// mit jedem von Buttplug unterstützten Gerät. Der direkte BLE-Weg bleibt
// daneben bestehen, weil er ohne Zusatzsoftware auskommt.
// claimTestDeviceConnect prüft und reserviert die Testverbindung atomar unter
// stateMu, bevor der (mehrere Sekunden dauernde) BLE-/Intiface-Connect
// beginnt. Ohne das bestünden ConnectDevice/ConnectDeviceVia aus Prüfen und
// spätem Setzen von a.testDevice mit einer unverriegelten Lücke dazwischen:
// zwei nahezu gleichzeitige Aufrufe (die Oberfläche verhindert das zwar
// schon durch das sofortige Deaktivieren des Verbinden-Knopfs, aber die
// Wails-Bindung selbst ist trotzdem direkt aufrufbar) würden beide die
// Prüfung bestehen, während der jeweils andere Connect noch läuft - danach
// überschreibt der zuletzt fertige a.testDevice, und die Verbindung des
// anderen bleibt offen, aber unerreichbar (Leak).
func (a *App) claimTestDeviceConnect() error {
	a.stateMu.Lock()
	defer a.stateMu.Unlock()
	if a.sessionActive {
		return fmt.Errorf("playback or training is in progress — " +
			"stop it first; the device can only hold one connection at a time")
	}
	if a.testDevice != nil || a.testDeviceConnecting {
		return fmt.Errorf("already connected — disconnect first")
	}
	a.testDeviceConnecting = true
	return nil
}

func (a *App) releaseTestDeviceConnect() {
	a.stateMu.Lock()
	a.testDeviceConnecting = false
	a.stateMu.Unlock()
}

func (a *App) ConnectDeviceVia(transport, url string) (DeviceStatus, error) {
	if err := a.claimTestDeviceConnect(); err != nil {
		return a.GetDeviceStatus(), err
	}
	defer a.releaseTestDeviceConnect()

	var dev device.Device
	switch transport {
	case "mock":
		dev = device.NewMock(true)
	case "intiface":
		dev = device.NewIntiface(url)
	case "tcode":
		// OSR2 / SR6 and other TCode devices over Wi-Fi (D5).
		dev = device.NewTCode(url)
	default:
		dev = device.NewSamNeo2(device.SamNeo2Protocol{})
	}

	// Intiface bekommt mehr Zeit: der Server sucht unter Umständen noch
	// selbst nach Geräten.
	timeout := testConnectTimeout
	if transport == "intiface" {
		timeout = 30 * time.Second
	}
	ctx, cancel := context.WithTimeout(context.Background(), timeout)
	defer cancel()
	connectStart := time.Now()
	if err := dev.Connect(ctx); err != nil {
		logging.Warn("device: connection failed", "transport", transport, "error", err)
		return a.GetDeviceStatus(), err
	}
	connectLatencyMs := float64(time.Since(connectStart).Microseconds()) / 1000.0

	a.stateMu.Lock()
	a.testDevice = dev
	a.testDeviceMock = transport == "mock"
	a.testDeviceTransport = transport
	a.testDeviceConnectLatencyMs = connectLatencyMs
	a.stateMu.Unlock()

	// Erfolgreiche Wahl merken - beim nächsten Start ist sie voreingestellt.
	_ = a.settings.Set(prefDeviceTransport, transport)
	if transport == "intiface" {
		_ = a.settings.Set(prefIntifaceURL, url)
	}
	if transport == "tcode" {
		_ = a.settings.Set(prefTCodeAddress, url)
	}

	a.applyMotorLevels(dev)
	a.maybeConnectSmokeTest(dev)

	st := a.GetDeviceStatus()
	logging.Info("device: connected", "transport", transport, "name", st.Name, "address", st.Address)
	return st, nil
}

func (a *App) ConnectDevice(mock bool) (DeviceStatus, error) {
	if err := a.claimTestDeviceConnect(); err != nil {
		return a.GetDeviceStatus(), err
	}
	defer a.releaseTestDeviceConnect()

	var dev device.Device
	if mock {
		dev = device.NewMock(true)
	} else {
		dev = device.NewSamNeo2(device.SamNeo2Protocol{})
	}

	ctx, cancel := context.WithTimeout(context.Background(), testConnectTimeout)
	defer cancel()
	connectStart := time.Now()
	if err := dev.Connect(ctx); err != nil {
		logging.Warn("device: connection failed", "mock", mock, "error", err)
		return a.GetDeviceStatus(), err
	}
	connectLatencyMs := float64(time.Since(connectStart).Microseconds()) / 1000.0

	a.stateMu.Lock()
	a.testDevice = dev
	a.testDeviceMock = mock
	if mock {
		a.testDeviceTransport = "mock"
	} else {
		a.testDeviceTransport = "ble"
	}
	a.testDeviceConnectLatencyMs = connectLatencyMs
	a.stateMu.Unlock()

	a.maybeConnectSmokeTest(dev)

	st := a.GetDeviceStatus()
	logging.Info("device: connected", "mock", mock, "name", st.Name, "address", st.Address, "rssi", st.RSSI)
	return st, nil
}

// maybeConnectSmokeTest sendet nach dem Verbinden einen kurzen Vib/Sog-
// Impuls und schaltet danach ab - nur wenn die Einstellung aktiv ist
// (Standard: aus). Damit lässt sich einmalig prüfen, ob die Verbindung
// wirklich Steuerbefehle durchlässt, ohne bei jedem Connect zu stören.
func (a *App) maybeConnectSmokeTest(dev device.Device) {
	if a.settings == nil || !a.settings.GetBool(prefDeviceConnectTest, false) {
		return
	}
	if dev == nil {
		return
	}
	logging.Info("device: post-connect connection check")
	_ = dev.SetVibration(0.3)
	time.Sleep(150 * time.Millisecond)
	_ = dev.SetSuction(0.3)
	time.Sleep(150 * time.Millisecond)
	if err := dev.Stop(); err != nil {
		logging.Warn("device: post-connect stop failed", "error", err)
	}
}

// DisconnectDevice trennt die Testverbindung wieder.
func (a *App) DisconnectDevice() (DeviceStatus, error) {
	a.stateMu.Lock()
	// Eine laufende Wiedergabe/ein laufendes Training kann diese Verbindung
	// gerade wiederverwenden (siehe claimSessionDevice) - a.testDevice bleibt
	// dabei bewusst gesetzt, damit der Geräte-Tab den Zustand weiter korrekt
	// anzeigt. Sie hier trotzdem zu trennen würde die laufende Sitzung mitten
	// im Senden abwürgen, ohne dass die Oberfläche das erwartet. Also
	// ablehnen, mit einer Meldung, die sagt, was zuerst zu tun ist -
	// symmetrisch zu claimTestDeviceConnect(), das aus demselben Grund keine
	// neue Verbindung während einer laufenden Sitzung zulässt.
	if a.sessionActive {
		a.stateMu.Unlock()
		return a.GetDeviceStatus(), fmt.Errorf(
			"playback or training is using this connection — stop it there first")
	}
	dev := a.testDevice
	a.testDevice = nil
	a.testDeviceMock = false
	a.testDeviceTransport = ""
	a.testDeviceConnectLatencyMs = 0
	a.stateMu.Unlock()

	if dev == nil {
		return a.GetDeviceStatus(), nil
	}
	// Vor dem Trennen immer ausschalten - sonst läuft ein Gerät weiter, das
	// gerade noch einen Testimpuls bekommen hat.
	if err := dev.Stop(); err != nil {
		logging.Warn("device: stop before disconnect failed", "error", err)
	}
	err := dev.Disconnect()
	logging.Info("device: disconnected", "error", err)
	return a.GetDeviceStatus(), err
}

// testDeviceOrErr liefert die aktive Testverbindung oder einen sprechenden Fehler.
func (a *App) testDeviceOrErr() (device.Device, error) {
	a.stateMu.RLock()
	dev := a.testDevice
	session := a.sessionActive
	a.stateMu.RUnlock()
	if session {
		return nil, fmt.Errorf("device test not available during playback or training")
	}
	if dev == nil {
		return nil, fmt.Errorf("no device connected — connect in Tools → Device first")
	}
	return dev, nil
}

// TestVibration setzt die Vibration direkt (0.0-1.0). Das Gerät kennt
// intern 11 Stufen (0-10), siehe device/protocol.go.
func (a *App) TestVibration(intensity float64) error {
	dev, err := a.testDeviceOrErr()
	if err != nil {
		return err
	}
	logging.Info("device: test vibration", "intensity", intensity)
	return dev.SetVibration(clamp01(intensity))
}

// TestSuction setzt den Sog direkt (0.0-1.0). Das Gerät kennt intern 6
// Stufen (0-5) und ist dabei stufenlos ansteuerbar, keine festen Muster.
func (a *App) TestSuction(intensity float64) error {
	dev, err := a.testDeviceOrErr()
	if err != nil {
		return err
	}
	logging.Info("device: test suction", "intensity", intensity)
	return dev.SetSuction(clamp01(intensity))
}

// TestStop schaltet beide Kanäle sofort ab.
func (a *App) TestStop() error {
	dev, err := a.testDeviceOrErr()
	if err != nil {
		return err
	}
	logging.Info("device: test stop")
	return dev.Stop()
}

// rawCapable ist die Schnittstelle für den Rohwert-Test. Nur das echte
// BLE-Gerät und das Mock bieten sie an.
type rawCapable interface {
	SetVibrationRaw(byte) error
	SetSuctionRaw(byte) error
}

// TestRawValue sendet einen Stufenwert DIREKT, ohne die Quantisierung auf
// 0-10 (Vibration) bzw. 0-5 (Sog).
//
// Diese Bereiche stammen aus der Buttplug-Gerätekonfiguration
// (svakom-sam2.yml) - das ist die StepCount, auf die Buttplug quantisiert,
// und nicht notwendigerweise eine Grenze der Firmware. Das Byte im Paket
// kann 0-255 tragen. Ob das Gerät Zwischenwerte oder höhere Werte annimmt,
// lässt sich ausschließlich am echten Gerät feststellen - und genau dafür
// ist diese Funktion da.
//
// Die Antwort hat Folgen über das Training hinaus: ist die Auflösung feiner,
// ist auch die Funscript-Wiedergabe unnötig grob gerastert.
func (a *App) TestRawValue(channel string, value int) error {
	dev, err := a.testDeviceOrErr()
	if err != nil {
		return err
	}
	raw, ok := dev.(rawCapable)
	if !ok {
		return fmt.Errorf("this device does not support raw values")
	}
	if value < 0 || value > 255 {
		return fmt.Errorf("raw value must be between 0 and 255 (got %d)", value)
	}
	logging.Info("device: raw value test", "channel", channel, "value", value)
	switch channel {
	case "vibration":
		return raw.SetVibrationRaw(byte(value))
	case "suction":
		return raw.SetSuctionRaw(byte(value))
	default:
		return fmt.Errorf("unknown channel %q", channel)
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

// SetMotorLevels sets the output level of single motors (channel id →
// 0–1, e.g. {"vibration": 1, "vibration2": 0.5}) on a multi-motor device,
// also while playing. Remembered for the next connect.
func (a *App) SetMotorLevels(levels map[string]float64) {
	a.stateMu.Lock()
	a.motorLevels = map[string]float64{}
	for ch, l := range levels {
		if !(l >= 0) {
			l = 0
		}
		if l > 1 {
			l = 1
		}
		a.motorLevels[ch] = l
	}
	dev := a.testDevice
	a.stateMu.Unlock()
	a.applyMotorLevels(dev)
}

func (a *App) applyMotorLevels(dev device.Device) {
	ms, ok := dev.(device.MotorLevelSetter)
	if !ok {
		return
	}
	a.stateMu.RLock()
	levels := make(map[string]float64, len(a.motorLevels))
	for ch, l := range a.motorLevels {
		levels[ch] = l
	}
	a.stateMu.RUnlock()
	for ch, l := range levels {
		ms.SetMotorLevel(ch, l)
	}
}
