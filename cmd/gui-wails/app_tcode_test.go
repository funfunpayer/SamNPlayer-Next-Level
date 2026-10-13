package main

import (
	"net"
	"strings"
	"testing"
	"time"

	"github.com/funfunpayer/SamNPlayer/device"
)

// D5: Tools → Device "TCode over Wi-Fi" connects, reports a stroker and
// is the device the Player reuses.
func TestConnectDeviceViaTCode(t *testing.T) {
	pc, err := net.ListenPacket("udp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	defer pc.Close()
	a := NewApp()
	st, err := a.ConnectDeviceVia("tcode", pc.LocalAddr().String())
	if err != nil {
		t.Fatal(err)
	}
	defer a.DisconnectDevice()
	if !st.Connected || st.Transport != "tcode" || !strings.Contains(st.Name, "TCode") {
		t.Fatalf("status %+v", st)
	}
	if !st.Capabilities.Has(device.ChStroke) {
		t.Fatalf("capabilities %+v", st.Capabilities)
	}
	dev, reused := a.claimSessionDevice(false)
	if !reused {
		t.Fatal("playback must reuse the TCode connection")
	}
	ld, ok := dev.(device.LinearDevice)
	if !ok || !ld.HasLinear() {
		t.Fatal("TCode device is a linear device")
	}
	if err := ld.SetPosition(0.5, 100*time.Millisecond); err != nil {
		t.Fatal(err)
	}
	// Earlier datagrams (e.g. DSTOP from the optional connect check) may come first.
	buf := make([]byte, 64)
	_ = pc.SetReadDeadline(time.Now().Add(2 * time.Second))
	for {
		n, _, err := pc.ReadFrom(buf)
		if err != nil {
			t.Fatalf("no move datagram: %v", err)
		}
		if got := string(buf[:n]); strings.HasPrefix(got, "L0") {
			if got != "L05000I100\n" {
				t.Fatalf("datagram %q", got)
			}
			break
		}
	}
}

func TestConnectDeviceViaTCodeNeedsAddress(t *testing.T) {
	a := NewApp()
	if _, err := a.ConnectDeviceVia("tcode", ""); err == nil || !strings.Contains(err.Error(), "address") {
		t.Fatalf("empty address must explain itself, got %v", err)
	}
}

func TestSetMotorLevelsClampsAndStores(t *testing.T) {
	a := NewApp()
	a.SetMotorLevels(map[string]float64{"vibration": 2, "vibration2": -1, "rotate": 0.3})
	a.stateMu.RLock()
	got := a.motorLevels
	a.stateMu.RUnlock()
	if got["vibration"] != 1 || got["vibration2"] != 0 || got["rotate"] != 0.3 {
		t.Fatalf("levels %+v", got)
	}
}
