package device

import (
	"bytes"
	"context"
	"fmt"
	"strings"
	"sync"
	"time"

	"tinygo.org/x/bluetooth"

	"github.com/funfunpayer/SamNPlayer/logging"
)

// keepaliveInterval: Buttplug wiederholt bei Bedarf alle 5s das zuletzt
// gesendete Kommando (Kommentar im Quellcode: "iOS Bluetooth default",
// dort nur für iOS-Hintergrundbetrieb als *erforderlich* dokumentiert, nicht
// generell für jede Plattform). Ob die Sam-Neo-2-Firmware selbst auch ohne
// aktives BLE-Backgrounding eine Inaktivitäts-Abschaltung hat, ist aus dem
// Quellcode nicht ersichtlich - wird hier trotzdem sicherheitshalber
// nachgebildet (schadet nicht, verhindert im Zweifel ein Verstummen des
// Geräts bei langen Extended-O-Haltephasen).
const keepaliveInterval = 4 * time.Second

// SamNeo2 spricht per BLE mit dem Gerät, über ein austauschbares Protocol
// (siehe protocol.go). Die Transportschicht selbst (Scannen, Verbinden,
// GATT-Write, Keepalive) ist geräteunabhängig vom Byte-Format.
type SamNeo2 struct {
	adapter  *bluetooth.Adapter
	protocol Protocol

	device DeviceRef
	char   gattWriter

	// NamePrefix wird beim Scan zum Filtern der Advertisements genutzt.
	NamePrefix  string
	ScanTimeout time.Duration

	mu           sync.Mutex
	lastWriteAt  time.Time
	keepaliveEnd chan struct{}

	// Zustand BEIDER Kanäle, nicht nur des zuletzt gesendeten Pakets.
	//
	// Vorher merkte sich das Keepalive genau ein Paket. War der letzte
	// Schreibvorgang Sog, wurde die Vibration nicht gehalten - und
	// umgekehrt. Ausgerechnet in Pausen und beim Extended-O, also genau
	// dort, wo das Keepalive überhaupt greifen soll, fiel damit ein Kanal
	// still weg.
	//
	// Die Stufen werden zusätzlich verglichen, bevor gesendet wird: das
	// Gerät kennt nur ganzzahlige Stufen, aufeinanderfolgende Intensitäten
	// landen häufig auf derselben. Ein Schreibvorgang, der nichts ändert,
	// kostet trotzdem einen Roundtrip mit Bestätigung - bei zwei Kanälen
	// alle 50ms sind das bis zu 40 pro Sekunde.
	lastVibrationPacket []byte
	lastSuctionPacket   []byte

	// Angaben zum tatsächlich verbundenen Gerät - damit die Oberfläche
	// zeigen kann, WAS gefunden wurde, statt nur "verbunden". Ein falsch
	// erkanntes Gerät ist sonst nicht von einem richtigen zu unterscheiden.
	connectedName    string
	connectedAddress string
	connectedRSSI    int

	// Optionaler Standard-GATT-Akku (0x180F). -1 = unbekannt/nicht unterstützt.
	batteryPct     int
	batteryOK      bool
	batteryChecked bool
	batteryAt      time.Time
}

// ConnectionInfo beschreibt die aktuell bestehende Verbindung.
type ConnectionInfo struct {
	Connected  bool
	Name       string
	Address    string
	RSSI       int
	BatteryPct int  // 0–100 wenn BatteryOK
	BatteryOK  bool // false = Gerät meldet keinen Akku / nicht auslesbar
}

// Info liefert den aktuellen Verbindungszustand samt erkanntem Gerätenamen.
func (s *SamNeo2) Info() ConnectionInfo {
	s.mu.Lock()
	defer s.mu.Unlock()
	return ConnectionInfo{
		Connected:  s.keepaliveEnd != nil,
		Name:       s.connectedName,
		Address:    s.connectedAddress,
		RSSI:       s.connectedRSSI,
		BatteryPct: s.batteryPct,
		BatteryOK:  s.batteryOK,
	}
}

// BatteryProbed ist true, sobald mindestens einmal versucht wurde, den
// Standard-GATT-Akku zu lesen (Erfolg oder „Service fehlt“).
func (s *SamNeo2) BatteryProbed() bool {
	s.mu.Lock()
	defer s.mu.Unlock()
	return s.batteryChecked
}

// BatteryLevel versucht den Standard-GATT-Akku zu lesen (wenn vorhanden).
// Ergebnis wird kurz gecacht, damit der 2s-Status-Poll die BLE-Verbindung
// nicht mit Dauer-Lesevorgängen belastet.
func (s *SamNeo2) BatteryLevel() (int, bool) {
	s.mu.Lock()
	connected := s.keepaliveEnd != nil
	fresh := s.batteryChecked && time.Since(s.batteryAt) < batteryCacheTTL
	if !connected {
		s.mu.Unlock()
		return 0, false
	}
	if fresh {
		pct, ok := s.batteryPct, s.batteryOK
		s.mu.Unlock()
		return pct, ok
	}
	dev := s.device
	s.mu.Unlock()

	pct, ok := readStandardBatteryLevel(dev)

	s.mu.Lock()
	s.batteryPct = pct
	s.batteryOK = ok
	s.batteryChecked = true
	s.batteryAt = time.Now()
	s.mu.Unlock()
	if ok {
		logging.Info("samneo2: battery read", "percent", pct)
	}
	return pct, ok
}

// gattWriter ist der Teil der BLE-Characteristic, den SamNeo2 braucht
// (bluetooth.DeviceCharacteristic erfüllt es auf allen Plattformen) - so
// lässt sich die Kanal-Buchführung ohne echtes Gerät testen.
type gattWriter interface {
	Write(p []byte) (int, error)
	WriteWithoutResponse(p []byte) (int, error)
}

// DeviceRef hält die verbundene BLE-Geräte-Referenz.
type DeviceRef = bluetooth.Device

// NewSamNeo2 erstellt eine neue Anbindung. protocol ist typischerweise
// device.SamNeo2Protocol{}.
func NewSamNeo2(protocol Protocol) *SamNeo2 {
	return &SamNeo2{
		adapter:     bluetooth.DefaultAdapter,
		protocol:    protocol,
		NamePrefix:  "Sam Neo 2", // matcht auch "Sam Neo 2 Pro", NICHT den originalen "Sam Neo"
		ScanTimeout: 15 * time.Second,
	}
}

func (s *SamNeo2) Connect(ctx context.Context) error {
	s.mu.Lock()
	alreadyConnected := s.keepaliveEnd != nil
	s.mu.Unlock()
	if alreadyConnected {
		return fmt.Errorf("samneo2: already connected — call Disconnect() first")
	}

	logging.Info("samneo2: connecting", "name_prefix", s.NamePrefix, "scan_timeout", s.ScanTimeout)
	if err := s.adapter.Enable(); err != nil {
		logging.Error("samneo2: BLE adapter enable failed", "error", err)
		return fmt.Errorf("samneo2: BLE adapter could not be enabled: %w", err)
	}

	found := make(chan bluetooth.ScanResult, 1)
	scanErr := make(chan error, 1)

	go func() {
		err := s.adapter.Scan(func(adapter *bluetooth.Adapter, result bluetooth.ScanResult) {
			name := result.LocalName()
			if name == "" {
				return
			}
			if strings.HasPrefix(name, s.NamePrefix) {
				adapter.StopScan()
				// Nicht blockierend: nach StopScan können noch weitere
				// Advertisements eintreffen, und nach Timeout/Abbruch liest
				// niemand mehr - ein blockierender Send würde den Scan-
				// Callback (und damit Scan()) für immer hängen lassen.
				select {
				case found <- result:
				default:
				}
			}
		})
		if err != nil {
			scanErr <- err
		}
	}()

	var result bluetooth.ScanResult
	select {
	case result = <-found:
		logging.Info("samneo2: device found", "address", result.Address.String(), "name", result.LocalName(), "rssi", result.RSSI)
	case err := <-scanErr:
		logging.Error("samneo2: scan failed", "error", err)
		return fmt.Errorf("samneo2: scan failed: %w", err)
	case <-time.After(s.ScanTimeout):
		s.adapter.StopScan()
		logging.Warn("samneo2: scan timeout, no device found", "name_prefix", s.NamePrefix, "timeout", s.ScanTimeout)
		return fmt.Errorf("samneo2: no device with name prefix %q found within %s", s.NamePrefix, s.ScanTimeout)
	case <-ctx.Done():
		s.adapter.StopScan()
		logging.Warn("samneo2: scan cancelled (context)")
		return ctx.Err()
	}

	dev, err := s.adapter.Connect(result.Address, bluetooth.ConnectionParams{})
	if err != nil {
		return fmt.Errorf("samneo2: connection failed: %w", err)
	}
	s.device = dev
	// Ab hier ist die BLE-Verbindung offen - jeder folgende Fehlerpfad muss
	// sie wieder trennen, sonst bleibt eine verwaiste Verbindung offen
	// (auf manchen Systemen belegt das den Adapter, bis das Programm beendet
	// wird).
	connected := true
	defer func() {
		if connected {
			s.device.Disconnect() //nolint:errcheck // best effort im Fehlerfall
		}
	}()

	svcUUID, err := bluetooth.ParseUUID(s.protocol.ServiceUUID())
	if err != nil {
		return fmt.Errorf("samneo2: invalid service UUID: %w", err)
	}
	services, err := dev.DiscoverServices([]bluetooth.UUID{svcUUID})
	if err != nil || len(services) == 0 {
		return fmt.Errorf("samneo2: service not found: %w", err)
	}

	charUUID, err := bluetooth.ParseUUID(s.protocol.CharacteristicUUID())
	if err != nil {
		return fmt.Errorf("samneo2: invalid characteristic UUID: %w", err)
	}
	chars, err := services[0].DiscoverCharacteristics([]bluetooth.UUID{charUUID})
	if err != nil || len(chars) == 0 {
		logging.Error("samneo2: characteristic not found", "error", err)
		return fmt.Errorf("samneo2: characteristic not found: %w", err)
	}
	s.char = chars[0]

	s.mu.Lock()
	s.connectedName = result.LocalName()
	s.connectedAddress = result.Address.String()
	s.connectedRSSI = int(result.RSSI)
	s.batteryPct = 0
	s.batteryOK = false
	s.batteryChecked = false
	s.batteryAt = time.Time{}
	s.keepaliveEnd = make(chan struct{})
	s.mu.Unlock()
	go s.runKeepalive()

	connected = false // Verbindung steht - der defer oben soll sie NICHT mehr trennen
	logging.Info("samneo2: connected", "address", result.Address.String())
	// Akku optional nachziehen (fehlende Battery Service = still ok).
	_, _ = s.BatteryLevel()
	return nil
}

func (s *SamNeo2) Disconnect() error {
	logging.Info("samneo2: disconnecting")
	s.mu.Lock()
	if s.keepaliveEnd != nil {
		close(s.keepaliveEnd)
		s.keepaliveEnd = nil
	}
	s.batteryOK = false
	s.batteryChecked = false
	s.mu.Unlock()
	_ = s.Stop()
	err := s.device.Disconnect()
	if err != nil {
		logging.Warn("samneo2: disconnect error", "error", err)
	}
	return err
}

func (s *SamNeo2) SetVibration(intensity float64) error {
	return s.writeChannel(s.protocol.EncodeVibration(intensity), true)
}

// writeChannel sendet ein Kanalpaket und merkt es sich für das Keepalive.
// Ein Paket, das mit dem zuletzt gesendeten dieses Kanals identisch ist,
// wird übersprungen - es würde am Gerät nichts ändern und kostet doch einen
// vollen Roundtrip.
func (s *SamNeo2) writeChannel(packet []byte, vibration bool) error {
	s.mu.Lock()
	defer s.mu.Unlock()

	previous := s.lastSuctionPacket
	if vibration {
		previous = s.lastVibrationPacket
	}
	if previous != nil && bytes.Equal(previous, packet) {
		// Unverändert: nicht senden, aber die Zeit NICHT zurücksetzen -
		// sonst würde eine lange Folge gleicher Werte das Keepalive
		// verhindern, und genau dann wird es gebraucht.
		return nil
	}

	if err := s.writeLocked(packet); err != nil {
		return err
	}
	if vibration {
		s.lastVibrationPacket = packet
	} else {
		s.lastSuctionPacket = packet
	}
	return nil
}

// SetVibrationRaw und SetSuctionRaw umgehen die Quantisierung und schreiben
// den Stufenwert direkt. Nur für den Funktionstest im Geräte-Tab gedacht:
// die Wertebereiche 0-10 bzw. 0-5 stammen aus der Buttplug-Gerätekonfi-
// guration, nicht aus einer Untersuchung der Firmware. Ob das Gerät feinere
// oder höhere Werte annimmt, lässt sich nur am echten Gerät feststellen.
//
// Das Protocol-Interface bleibt bewusst schlank: der Rohzugriff wird per
// Typprüfung geholt, damit nicht jedes künftige Protokoll ihn anbieten muss.
type rawEncoder interface {
	EncodeVibrationRaw(byte) []byte
	EncodeSuctionRaw(byte) []byte
}

// Rohwerte schreiben immer (kein "unverändert -> überspringen") und merken
// sich das Paket als Kanalzustand - wie Stop(). Vorher liefen sie über
// write() an der Buchführung vorbei: das Keepalive spielte nach ~4s den
// zuletzt gemerkten *normalen* Wert erneut ab. Der Diagnose-Sweep endet mit
// Rohwert 0 - danach lief das Gerät von selbst wieder an; und ein folgender
// SetVibration() mit dem alten Wert wurde als "unverändert" verschluckt.
func (s *SamNeo2) SetVibrationRaw(speed byte) error {
	enc, ok := s.protocol.(rawEncoder)
	if !ok {
		return fmt.Errorf("device: this protocol does not support raw values")
	}
	return s.writeChannelAlways(enc.EncodeVibrationRaw(speed), true)
}

func (s *SamNeo2) SetSuctionRaw(level byte) error {
	enc, ok := s.protocol.(rawEncoder)
	if !ok {
		return fmt.Errorf("device: this protocol does not support raw values")
	}
	return s.writeChannelAlways(enc.EncodeSuctionRaw(level), false)
}

// writeChannelAlways sendet ein Kanalpaket ohne Unverändert-Prüfung und
// merkt es sich für Keepalive und die nächste Unverändert-Prüfung.
func (s *SamNeo2) writeChannelAlways(packet []byte, vibration bool) error {
	s.mu.Lock()
	defer s.mu.Unlock()
	if err := s.writeLocked(packet); err != nil {
		return err
	}
	if vibration {
		s.lastVibrationPacket = packet
	} else {
		s.lastSuctionPacket = packet
	}
	return nil
}

// SetSuction setzt den Sog-Level direkt, genau wie SetVibration - laut
// Buttplug-Rust-Quellcode (nachgeprüft, nicht nur angenommen: "Constrict"
// nutzt exakt denselben generischen Skalar-Werttyp wie "Vibrate", siehe
// crates/buttplug_core/src/message/v4/output_cmd.rs) ist der Sog-Kanal
// genauso eine stufenlos gemeinte Intensität wie die Vibration, nur mit
// weniger Auflösung (0-5 statt 0-10 Stufen laut svakom-sam2.yml) - keine
// Auswahl zwischen festen Rhythmus-Mustern. Frühere Vermutung hier war
// falsch (beruhte auf einem Forenbericht zur *offiziellen SVAKOM-App*,
// die eine eigene Preset-Auswahl-UI hat - nicht auf dem rohen Protokoll,
// das wir direkt ansteuern) und wurde darum wieder entfernt.
func (s *SamNeo2) SetSuction(intensity float64) error {
	return s.writeChannel(s.protocol.EncodeSuction(intensity), false)
}

// Stop schaltet beide Kanäle sofort ab. Schreibt IMMER, anders als
// writeChannel() (kein "unverändert -> überspringen"): Stop muss auch dann
// wirken, wenn der zuletzt gemerkte Zustand zufällig schon Null war.
//
// Aktualisiert lastVibrationPacket/lastSuctionPacket - vorher schrieb Stop()
// direkt über write()/writeLocked() an writeChannel() vorbei, sodass beide
// Felder weiter das zuletzt gesendete NICHT-Null-Paket enthielten. Zwei
// sichtbare Folgen: (1) ein SetVibration()/SetSuction()-Aufruf mit exakt
// demselben Wert wie vor dem Stop wurde von writeChannel()'s
// Unverändert-Prüfung fälschlich als "keine Änderung" übersprungen - das
// Gerät blieb still, obwohl der Aufruf ohne Fehler zurückkam. (2) schwerer:
// runKeepalive() sendet nach keepaliveInterval ungefragt den zuletzt
// gemerkten Zustand erneut - blieb die Verbindung nach Stop() offen (z.B.
// "Test Stop" im Geräte-Tab, das NICHT trennt), setzte sich das Gerät nach
// wenigen Sekunden von selbst wieder in Bewegung. Gefunden beim Testen mit
// echten Werten über den Geräte-Tab-Pfad.
func (s *SamNeo2) Stop() error {
	vibrationOff := s.protocol.EncodeVibration(0)
	suctionOff := s.protocol.EncodeSuction(0)

	s.mu.Lock()
	defer s.mu.Unlock()
	if err := s.writeLocked(vibrationOff); err != nil {
		return err
	}
	s.lastVibrationPacket = vibrationOff
	if err := s.writeLocked(suctionOff); err != nil {
		return err
	}
	s.lastSuctionPacket = suctionOff
	return nil
}

// writeLocked führt den eigentlichen GATT-Write aus. Aufrufer MUSS s.mu
// bereits halten: s.mu schützt nicht nur die Buchführung (Kanalzustand/
// lastWriteAt), sondern den gesamten Schreibvorgang - sonst könnten der
// Wiedergabe-Pfad und die Keepalive-Goroutine (runKeepalive) gleichzeitig
// auf dieselbe BLE-Characteristic schreiben, was der Treiber nicht als
// nebenläufig-sicher garantiert.
func (s *SamNeo2) writeLocked(packet []byte) error {
	var err error
	if s.protocol.WriteWithResponse() {
		_, err = s.char.Write(packet)
	} else {
		_, err = s.char.WriteWithoutResponse(packet)
	}
	if err != nil {
		logging.Error("samneo2: GATT write failed", "error", err, "bytes", fmt.Sprintf("% x", packet))
		return err
	}
	logging.Debug("samneo2: GATT-Write", "bytes", fmt.Sprintf("% x", packet))
	s.lastWriteAt = time.Now()
	return nil
}

// runKeepalive wiederholt den zuletzt gesendeten Zustand BEIDER Kanäle, wenn seit
// keepaliveInterval nichts Neues gesendet wurde (siehe Kommentar bei der
// Konstante). Läuft bis Disconnect() den Kanal schließt.
func (s *SamNeo2) runKeepalive() {
	ticker := time.NewTicker(keepaliveInterval / 2)
	defer ticker.Stop()
	for {
		s.mu.Lock()
		endCh := s.keepaliveEnd
		s.mu.Unlock()
		if endCh == nil {
			return
		}
		select {
		case <-endCh:
			return
		case <-ticker.C:
			s.mu.Lock()
			if (s.lastVibrationPacket != nil || s.lastSuctionPacket != nil) &&
				time.Since(s.lastWriteAt) >= keepaliveInterval {
				logging.Debug("samneo2: keepalive repeat", "idle", time.Since(s.lastWriteAt))
				// Direkter Write statt writeLocked(): ein Keepalive-Replay
				// ist kein inhaltlich neues Kommando, lastWriteAt soll darum
				// NICHT aktualisiert werden - sonst würde ein Dauerstrom aus
				// Keepalives den Idle-Timer immer wieder zurücksetzen und so
				// verhindern, dass er je wieder anschlägt.
				// BEIDE Kanäle wiederholen, nicht nur den zuletzt
				// gesendeten - sonst fällt der andere still weg.
				for _, packet := range [][]byte{s.lastVibrationPacket, s.lastSuctionPacket} {
					if packet == nil {
						continue
					}
					if s.protocol.WriteWithResponse() {
						_, _ = s.char.Write(packet)
					} else {
						_, _ = s.char.WriteWithoutResponse(packet)
					}
				}
			}
			s.mu.Unlock()
		}
	}
}

var _ Device = (*SamNeo2)(nil)
