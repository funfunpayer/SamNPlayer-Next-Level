package device

import (
	"fmt"
	"time"

	"tinygo.org/x/bluetooth"

	"github.com/funfunpayer/SamNPlayer/logging"
)

// BatteryReader ist optional: Geräte, die den Akku auslesen können,
// implementieren es. GetDeviceStatus fragt das ab und zeigt den Wert nur,
// wenn ok=true — sonst bleibt die Anzeige weg (kein „Akku ?“-Platzhalter).
type BatteryReader interface {
	// BatteryLevel liefert 0–100 und ok=true, wenn das Gerät den Stand meldet.
	BatteryLevel() (pct int, ok bool)
}

const batteryCacheTTL = 30 * time.Second

// readStandardBatteryLevel liest den Standard-GATT-Akku (Service 0x180F,
// Characteristic 0x2A19). Viele Toys exponieren das; Sam Neo 2 tut es
// möglicherweise nicht — dann ist fehlendes Service kein Fehler, nur „nicht
// unterstützt“.
func readStandardBatteryLevel(dev bluetooth.Device) (int, bool) {
	services, err := dev.DiscoverServices([]bluetooth.UUID{bluetooth.ServiceUUIDBattery})
	if err != nil || len(services) == 0 {
		return 0, false
	}
	chars, err := services[0].DiscoverCharacteristics([]bluetooth.UUID{
		bluetooth.CharacteristicUUIDBatteryLevel,
	})
	if err != nil || len(chars) == 0 {
		return 0, false
	}
	buf := make([]byte, 1)
	n, err := chars[0].Read(buf)
	if err != nil || n < 1 {
		logging.Debug("battery: read failed", "error", err)
		return 0, false
	}
	pct := int(buf[0])
	if pct > 100 {
		pct = 100
	}
	return pct, true
}

// FormatBatteryPct formats a battery percentage for status lines (English UI).
func FormatBatteryPct(pct int) string {
	return fmt.Sprintf("Battery %d%%", pct)
}
