package device

import (
	"context"
	"time"
)

// Device ist die minimale Steuerschnittstelle, die der Player benötigt.
// Intensitäten sind immer 0.0 (aus) bis 1.0 (voll).
type Device interface {
	// Connect baut die Verbindung zum Gerät auf (z.B. BLE-Scan + Connect).
	Connect(ctx context.Context) error
	// Disconnect trennt die Verbindung sauber.
	Disconnect() error
	// SetVibration setzt die Vibrationsintensität.
	SetVibration(intensity float64) error
	// SetSuction setzt die Sog-/Saugintensität.
	SetSuction(intensity float64) error
	// Stop schaltet beide Kanäle sofort aus (z.B. bei Pause/Abbruch).
	Stop() error
}

// LinearDevice is a device that moves to a position (strokers: The Handy,
// OSR2 …; docs/DEVICE_SUPPORT.md D2). The player sends the script's stroke
// positions to it in addition to the vibration / suction channels.
type LinearDevice interface {
	// HasLinear reports whether the connected device has a driven linear
	// channel (known after Connect).
	HasLinear() bool
	// SetPosition moves to pos (0 = bottom, 1 = top) within duration.
	SetPosition(pos float64, duration time.Duration) error
}

// MultiAxisDevice moves extra axes besides the stroke (SR6 / OSR2 over
// TCode: surge, sway, twist, roll, pitch; docs/DEVICE_SUPPORT.md D4).
type MultiAxisDevice interface {
	// HasAxis reports whether the device takes this axis ("twist" …).
	HasAxis(axis string) bool
	// SetAxis moves axis to pos (0–1, 0.5 = centre) within duration.
	SetAxis(axis string, pos float64, duration time.Duration) error
}

// MotorLevelSetter scales single motors of a multi-motor device (LELO F1S
// V3: two vibration motors) on top of the channel level. channel is the
// feature's channel id ("vibration", "vibration2" …); 1 = full.
type MotorLevelSetter interface {
	SetMotorLevel(channel string, level float64)
}
