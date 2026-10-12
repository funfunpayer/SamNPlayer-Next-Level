package device

import (
	"context"
	"fmt"
)

// Mock protokolliert Steuerbefehle auf stdout statt sie per BLE zu senden.
// Nützlich, um den Player und das funscript-Timing zu testen, ohne das
// Gerät griffbereit zu haben oder BLE-Rechte zu benötigen.
type Mock struct {
	Verbose bool
}

func NewMock(verbose bool) *Mock {
	return &Mock{Verbose: verbose}
}

func (m *Mock) Connect(ctx context.Context) error {
	if m.Verbose {
		fmt.Println("[mock] verbunden")
	}
	return nil
}

func (m *Mock) Disconnect() error {
	if m.Verbose {
		fmt.Println("[mock] getrennt")
	}
	return nil
}

func (m *Mock) SetVibration(intensity float64) error {
	if m.Verbose {
		fmt.Printf("[mock] vibration=%.2f\n", intensity)
	}
	return nil
}

func (m *Mock) SetSuction(intensity float64) error {
	if m.Verbose {
		fmt.Printf("[mock] suction=%.2f\n", intensity)
	}
	return nil
}

func (m *Mock) SetVibrationRaw(speed byte) error {
	if m.Verbose {
		fmt.Printf("[mock] vibration_raw=%d\n", speed)
	}
	return nil
}

func (m *Mock) SetSuctionRaw(level byte) error {
	if m.Verbose {
		fmt.Printf("[mock] suction_raw=%d\n", level)
	}
	return nil
}

func (m *Mock) Stop() error {
	if m.Verbose {
		fmt.Println("[mock] stop")
	}
	return nil
}
