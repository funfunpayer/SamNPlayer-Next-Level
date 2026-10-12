package device

import "math"

// Protocol kapselt ausschließlich die Byte-Kodierung der Steuerbefehle.
// Die BLE-Transportschicht (samneo2.go) weiß nichts über das Byteformat -
// so lässt sich das Protokoll austauschen, ohne den Rest anzufassen.
type Protocol interface {
	ServiceUUID() string
	CharacteristicUUID() string
	// WriteWithResponse legt fest, ob GATT Write-With-Response (true) oder
	// Write-Without-Response (false) genutzt wird. Ist gerätespezifisch.
	WriteWithResponse() bool
	EncodeVibration(intensity float64) []byte // intensity 0.0-1.0
	EncodeSuction(intensity float64) []byte   // intensity 0.0-1.0
	EncodeStop() []byte
}

// SamNeo2Protocol ist das BLE-Protokoll des SVAKOM Sam Neo 2 / Sam Neo 2 Pro.
//
// Quelle: offizieller, gepflegter Buttplug-Rust-Server (github.com/buttplugio/buttplug),
// verifiziert am Originalcode, nicht geschätzt:
//   - crates/buttplug_server/src/device/protocol_impl/svakom/svakom_sam2.rs
//     (Byte-Layout der Vibrate-/Constrict-Kommandos)
//   - crates/buttplug_server_device_config/device-config/protocols/svakom-sam2.yml
//     (BLE-Name, Service-/Characteristic-UUIDs, Wertebereiche)
//
// BLE Advertising-Name: "Sam Neo 2" bzw. "Sam Neo 2 Pro"
// Service UUID:  0000ffe0-0000-1000-8000-00805f9b34fb
// TX Char UUID:  0000ffe1-0000-1000-8000-00805f9b34fb  (Kommandos hierhin schreiben)
// RX Char UUID:  0000ffe2-0000-1000-8000-00805f9b34fb  (Notify, geräteeigenes
//
//	Statusformat — hier nicht ausgewertet). Akku wird zusätzlich über den
//	Standard-GATT-Battery-Service (0x180F/0x2A19) versucht, falls vorhanden.
//
// Kein Initialisierungs-Handshake nötig (im Gegensatz zum originalen Sam Neo,
// der vor dem ersten Befehl auf RX subscriben muss) - laut Quellcode wird
// SvakomSam2 direkt per Default-Konstruktor verwendet.
//
// Beide Kommandos gehen als 7-Byte-Pakete an dieselbe TX-Characteristic,
// unterschieden durch das zweite Byte ("kind": 0x03 = Vibrate, 0x09 = Constrict).
// Schreibmodus: GATT Write WITH Response (write_with_response=true im Quellcode) -
// wichtig, WriteWithoutResponse funktioniert hier laut Quelle nicht.
type SamNeo2Protocol struct{}

func (SamNeo2Protocol) ServiceUUID() string        { return "0000ffe0-0000-1000-8000-00805f9b34fb" }
func (SamNeo2Protocol) CharacteristicUUID() string { return "0000ffe1-0000-1000-8000-00805f9b34fb" }
func (SamNeo2Protocol) WriteWithResponse() bool    { return true }

// EncodeVibration: Wertebereich 0-10 (laut svakom-sam2.yml), ganzzahlig.
// Paket: [0x55, 0x03, 0x00, 0x00, pattern, speed, 0x00]
// pattern ist 0x00 bei speed=0 (aus), sonst 0x05 (konstante Vibration).
func (SamNeo2Protocol) EncodeVibration(intensity float64) []byte {
	speed := scaleToRange(intensity, 10)
	pattern := byte(0x05)
	if speed == 0 {
		pattern = 0x00
	}
	return []byte{0x55, 0x03, 0x00, 0x00, pattern, speed, 0x00}
}

// EncodeSuction: Wertebereich 0-5 (laut svakom-sam2.yml), ganzzahlig.
// Paket: [0x55, 0x09, 0x00, 0x00, flag, level, 0x00]
// flag ist 0x00 bei level=0 (aus), sonst 0x01 (an).
func (SamNeo2Protocol) EncodeSuction(intensity float64) []byte {
	level := scaleToRange(intensity, 5)
	flag := byte(0x01)
	if level == 0 {
		flag = 0x00
	}
	return []byte{0x55, 0x09, 0x00, 0x00, flag, level, 0x00}
}

// EncodeVibrationRaw und EncodeSuctionRaw schreiben den Stufenwert direkt ins
// Paket, ohne die Quantisierung über scaleToRange.
//
// Hintergrund: die Wertebereiche 0-10 (Vibration) und 0-5 (Sog) stammen aus
// svakom-sam2.yml, also aus der StepCount-Angabe der Buttplug-Gerätekonfi-
// guration. Das ist der Wert, auf den BUTTPLUG quantisiert - nicht
// notwendigerweise eine Grenze der Firmware. Das Byte im Paket kann 0-255
// tragen, und ob das Gerät Zwischen- oder höhere Werte annimmt, ist nirgends
// geprüft; solche StepCounts stammen häufig aus der Hersteller-App statt aus
// einer Untersuchung der Firmware.
//
// Diese Funktionen existieren, um genau das an echter Hardware zu klären.
// Für den Normalbetrieb bleibt EncodeVibration/EncodeSuction zuständig.
func (SamNeo2Protocol) EncodeVibrationRaw(speed byte) []byte {
	pattern := byte(0x05)
	if speed == 0 {
		pattern = 0x00
	}
	return []byte{0x55, 0x03, 0x00, 0x00, pattern, speed, 0x00}
}

func (SamNeo2Protocol) EncodeSuctionRaw(level byte) []byte {
	flag := byte(0x01)
	if level == 0 {
		flag = 0x00
	}
	return []byte{0x55, 0x09, 0x00, 0x00, flag, level, 0x00}
}

func (p SamNeo2Protocol) EncodeStop() []byte {
	// Nutzt das Vibrate-Kommando mit speed=0. Sog wird separat über
	// EncodeSuction(0) gestoppt (device.Device.Stop() ruft beide auf).
	return p.EncodeVibration(0)
}

// LegacySamProtocol ist das Protokoll des *originalen* Sam Neo (nicht Neo 2!).
// Nur als Referenz/Testziel enthalten, falls mal Zugriff auf das Vorgängergerät
// besteht - NICHT identisch mit dem Neo 2 (andere UUIDs, andere Bytes, andere
// Wertebereiche, andere Write-Response-Einstellung).
//
// Quelle: crates/buttplug_server/src/device/protocol_impl/svakom/svakom_sam.rs
// und svakom-sam.yml. Der Originalcode unterscheidet zusätzlich zwischen zwei
// Hardware-Revisionen ("gen2" per Endpoint-Erkennung, Pattern-Byte 0x04 vs 0x05);
// hier ist der Einfachheit halber nur die nicht-gen2-Variante abgebildet.
type LegacySamProtocol struct{}

func (LegacySamProtocol) ServiceUUID() string        { return "0000ae00-0000-1000-8000-00805f9b34fb" }
func (LegacySamProtocol) CharacteristicUUID() string { return "0000ae01-0000-1000-8000-00805f9b34fb" }
func (LegacySamProtocol) WriteWithResponse() bool    { return false }

func (LegacySamProtocol) EncodeVibration(intensity float64) []byte {
	speed := scaleToRange(intensity, 10)
	pattern := byte(5)
	if speed == 0 {
		pattern = 0
	}
	return []byte{18, 1, 3, 0, pattern, speed}
}

func (LegacySamProtocol) EncodeSuction(intensity float64) []byte {
	// Original: Wertebereich nur 0-1 (im Grunde an/aus, kein feines Level).
	level := scaleToRange(intensity, 1)
	return []byte{18, 6, 1, level}
}

func (p LegacySamProtocol) EncodeStop() []byte {
	return p.EncodeVibration(0)
}

// scaleToRange rechnet eine 0.0-1.0-Intensität in einen ganzzahligen
// Gerätewert 0..max um (kaufmännisch gerundet, nicht abgeschnitten -
// bei den kleinen Wertebereichen dieser Geräte (0-10, 0-5) macht das einen
// spürbaren Unterschied).
func scaleToRange(intensity float64, max int) byte {
	return byte(math.Round(clamp01(intensity) * float64(max)))
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
