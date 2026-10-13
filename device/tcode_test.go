package device

import (
	"bufio"
	"context"
	"net"
	"strings"
	"testing"
	"time"
)

func TestTCodeValueAndMove(t *testing.T) {
	cases := map[float64]string{0: "0000", 0.5: "5000", 1: "9999", -2: "0000", 3: "9999", 0.25: "2500"}
	for in, want := range cases {
		if got := TCodeValue(in); got != want {
			t.Errorf("TCodeValue(%v) = %q, want %q", in, got, want)
		}
	}
	if got := TCodeMove("L0", 0.5, 250*time.Millisecond); got != "L05000I250" {
		t.Fatalf("move %q", got)
	}
	if got := TCodeMove("L0", 1, 0); got != "L09999I1" {
		t.Fatalf("zero duration becomes 1 ms: %q", got)
	}
}

func TestParseTCodeAddress(t *testing.T) {
	cases := []struct{ in, network, hostport string }{
		{"192.168.1.50", "udp", "192.168.1.50:8000"},
		{"192.168.1.50:9000", "udp", "192.168.1.50:9000"},
		{"udp://osr.local", "udp", "osr.local:8000"},
		{"tcp://10.0.0.2:7000", "tcp", "10.0.0.2:7000"},
		{" TCP://10.0.0.2 ", "tcp", "10.0.0.2:8000"},
	}
	for _, c := range cases {
		n, h, err := ParseTCodeAddress(c.in)
		if err != nil || n != c.network || h != c.hostport {
			t.Errorf("ParseTCodeAddress(%q) = %q %q %v", c.in, n, h, err)
		}
	}
	for _, bad := range []string{"", "http://x", "udp://"} {
		if _, _, err := ParseTCodeAddress(bad); err == nil {
			t.Errorf("ParseTCodeAddress(%q) must fail", bad)
		}
	}
}

// Over TCP against a local listener: moves arrive as TCode lines, Stop
// sends DSTOP, vibration / suction are silent no-ops.
func TestTCodeOverTCP(t *testing.T) {
	ln, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	defer ln.Close()
	lines := make(chan string, 16)
	go func() {
		c, err := ln.Accept()
		if err != nil {
			return
		}
		defer c.Close()
		sc := bufio.NewScanner(c)
		for sc.Scan() {
			lines <- sc.Text()
		}
	}()
	dev := NewTCode("tcp://" + ln.Addr().String())
	ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
	defer cancel()
	if err := dev.Connect(ctx); err != nil {
		t.Fatal(err)
	}
	var ld LinearDevice = dev
	if !ld.HasLinear() || !CapabilitiesOf(dev).Has(ChStroke) {
		t.Fatal("TCode reports a driven stroke channel")
	}
	if err := dev.SetVibration(0.7); err != nil {
		t.Fatal(err)
	}
	if err := dev.SetPosition(0.8, 300*time.Millisecond); err != nil {
		t.Fatal(err)
	}
	if err := dev.Stop(); err != nil {
		t.Fatal(err)
	}
	want := []string{"L08000I300", "DSTOP"}
	for _, w := range want {
		select {
		case got := <-lines:
			if got != w {
				t.Fatalf("line %q, want %q", got, w)
			}
		case <-time.After(2 * time.Second):
			t.Fatalf("no line, want %q", w)
		}
	}
	info := dev.Info()
	if !info.Connected || !strings.HasPrefix(info.Address, "tcp://127.0.0.1:") {
		t.Fatalf("info %+v", info)
	}
	if err := dev.Disconnect(); err != nil {
		t.Fatal(err)
	}
	if dev.Info().Connected {
		t.Fatal("disconnected")
	}
	if err := dev.SetPosition(0.5, time.Millisecond); err == nil {
		t.Fatal("send after disconnect must fail")
	}
}

func TestTCodeOverUDP(t *testing.T) {
	pc, err := net.ListenPacket("udp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	defer pc.Close()
	dev := NewTCode(pc.LocalAddr().String()) // no scheme → UDP
	if err := dev.Connect(context.Background()); err != nil {
		t.Fatal(err)
	}
	defer dev.Disconnect()
	if err := dev.SetPosition(0.25, 120*time.Millisecond); err != nil {
		t.Fatal(err)
	}
	buf := make([]byte, 64)
	_ = pc.SetReadDeadline(time.Now().Add(2 * time.Second))
	n, _, err := pc.ReadFrom(buf)
	if err != nil {
		t.Fatal(err)
	}
	if got := string(buf[:n]); got != "L02500I120\n" {
		t.Fatalf("datagram %q", got)
	}
}

func TestTCodeTCPConnectFails(t *testing.T) {
	ln, _ := net.Listen("tcp", "127.0.0.1:0")
	addr := ln.Addr().String()
	ln.Close() // nothing listens any more
	dev := NewTCode("tcp://" + addr)
	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
	defer cancel()
	if err := dev.Connect(ctx); err == nil || !strings.Contains(err.Error(), "no connection") {
		t.Fatalf("want a clear connect error, got %v", err)
	}
}

func TestTCodeExtraAxes(t *testing.T) {
	pc, err := net.ListenPacket("udp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	defer pc.Close()
	dev := NewTCode(pc.LocalAddr().String())
	if err := dev.Connect(context.Background()); err != nil {
		t.Fatal(err)
	}
	defer dev.Disconnect()
	var md MultiAxisDevice = dev
	if !md.HasAxis(ChTwist) || md.HasAxis("banana") {
		t.Fatal("HasAxis follows the TCode channel table")
	}
	if err := md.SetAxis(ChTwist, 0.75, 200*time.Millisecond); err != nil {
		t.Fatal(err)
	}
	if err := md.SetAxis(ChSurge, 0.5, 100*time.Millisecond); err != nil {
		t.Fatal(err)
	}
	if err := md.SetAxis("banana", 0.5, time.Millisecond); err == nil {
		t.Fatal("unknown axis must fail")
	}
	buf := make([]byte, 64)
	for _, want := range []string{"R07500I200\n", "L15000I100\n"} {
		_ = pc.SetReadDeadline(time.Now().Add(2 * time.Second))
		n, _, err := pc.ReadFrom(buf)
		if err != nil || string(buf[:n]) != want {
			t.Fatalf("datagram %q (%v), want %q", buf[:n], err, want)
		}
	}
	caps := CapabilitiesOf(dev)
	for _, ch := range []string{ChStroke, ChSurge, ChSway, ChTwist, ChRoll, ChPitch} {
		if !caps.Has(ch) {
			t.Fatalf("capabilities lack %s: %+v", ch, caps)
		}
	}
}
