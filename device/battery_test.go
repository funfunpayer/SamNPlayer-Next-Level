package device

import "testing"

func TestFormatBatteryPct(t *testing.T) {
	if got := FormatBatteryPct(73); got != "Battery 73%" {
		t.Fatalf("got %q", got)
	}
}

func TestConnectionInfoBatteryDefaults(t *testing.T) {
	var info ConnectionInfo
	if info.BatteryOK {
		t.Fatal("default BatteryOK should be false")
	}
}
