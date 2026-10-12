package device

import (
	"context"
	"fmt"
	"io"
	"math"
	"net"
	"net/url"
	"strings"
	"sync"
	"time"
)

// TCode (docs/DEVICE_SUPPORT.md, D5): the open text protocol of OSR2 / SR6
// and other DIY strokers. A command is "<axis><value>[I<ms>]", the value a
// decimal fraction written as digits ("L05" = 0.5, "L09999" = 0.9999);
// several commands share one line, a newline executes them. "DSTOP" stops.
//
// This transport talks TCode over the network (the Wi-Fi firmware of the
// ESP32 boards listens on UDP or TCP). It only writes: TCode needs no
// replies for motion. Driven: L0 (stroke) via SetPosition, and L1 / L2 /
// R0–R2 via SetAxis from multi-axis companion files (D4).

// tcodeDigits: value precision (4 digits = 0–9999).
const tcodeDigits = 4

// TCodeValue formats pos (0–1) as TCode digits, e.g. 0.5 → "5000".
func TCodeValue(pos float64) string {
	if !(pos >= 0) {
		pos = 0
	}
	if pos > 1 {
		pos = 1
	}
	// Digits are the decimal fraction ("8000" = 0.8); 1.0 caps at "9999".
	v := int(math.Round(pos * math.Pow10(tcodeDigits)))
	if top := int(math.Pow10(tcodeDigits)) - 1; v > top {
		v = top
	}
	return fmt.Sprintf("%0*d", tcodeDigits, v)
}

// TCodeMove builds one linear move: axis to pos within d ("L05000I250").
func TCodeMove(axis string, pos float64, d time.Duration) string {
	ms := d.Milliseconds()
	if ms < 1 {
		ms = 1
	}
	return fmt.Sprintf("%s%sI%d", axis, TCodeValue(pos), ms)
}

// TCode is a network TCode device (udp://host:port or tcp://host:port).
type TCode struct {
	address string // as given, for display

	mu        sync.Mutex
	conn      io.WriteCloser
	connected bool
	dial      func(ctx context.Context, network, addr string) (net.Conn, error)
}

// DefaultTCodePort is the port the common OSR / SR6 Wi-Fi firmwares use.
const DefaultTCodePort = "8000"

// NewTCode creates a TCode device for address ("udp://192.168.1.50:8000",
// "tcp://osr.local:8000"; without scheme UDP, without port 8000).
func NewTCode(address string) *TCode {
	var d net.Dialer
	return &TCode{address: strings.TrimSpace(address), dial: d.DialContext}
}

// ParseTCodeAddress splits address into network ("udp" / "tcp") and host:port.
func ParseTCodeAddress(address string) (network, hostport string, err error) {
	a := strings.TrimSpace(address)
	if a == "" {
		return "", "", fmt.Errorf("tcode: no address — enter the device's IP, e.g. 192.168.1.50")
	}
	network = "udp"
	if strings.Contains(a, "://") {
		u, perr := url.Parse(a)
		if perr != nil || u.Host == "" {
			return "", "", fmt.Errorf("tcode: invalid address %q", address)
		}
		switch strings.ToLower(u.Scheme) {
		case "udp", "tcp":
			network = strings.ToLower(u.Scheme)
		default:
			return "", "", fmt.Errorf("tcode: use udp:// or tcp:// (got %q)", u.Scheme)
		}
		a = u.Host
	}
	if _, _, splitErr := net.SplitHostPort(a); splitErr != nil {
		a = net.JoinHostPort(strings.Trim(a, "[]"), DefaultTCodePort)
	}
	return network, a, nil
}

// Connect opens the socket. UDP has no handshake, so a wrong address only
// shows when the device does not move; TCP fails here.
func (t *TCode) Connect(ctx context.Context) error {
	network, hostport, err := ParseTCodeAddress(t.address)
	if err != nil {
		return err
	}
	conn, err := t.dial(ctx, network, hostport)
	if err != nil {
		return fmt.Errorf("tcode: no connection to %s (%s): %w", hostport, network, err)
	}
	t.mu.Lock()
	t.conn = conn
	t.connected = true
	t.mu.Unlock()
	return nil
}

// Disconnect stops the device and closes the socket.
func (t *TCode) Disconnect() error {
	_ = t.Stop()
	t.mu.Lock()
	defer t.mu.Unlock()
	if t.conn == nil {
		return nil
	}
	err := t.conn.Close()
	t.conn = nil
	t.connected = false
	return err
}

func (t *TCode) writeLine(line string) error {
	t.mu.Lock()
	defer t.mu.Unlock()
	if t.conn == nil {
		return fmt.Errorf("tcode: not connected")
	}
	if _, err := io.WriteString(t.conn, line+"\n"); err != nil {
		t.connected = false
		return fmt.Errorf("tcode: send failed: %w", err)
	}
	return nil
}

// SetVibration / SetSuction: an OSR2 / SR6 has neither — no-ops, so the
// player's feel channels do not error on a stroker.
func (t *TCode) SetVibration(float64) error { return nil }
func (t *TCode) SetSuction(float64) error   { return nil }

// Stop halts every axis immediately.
func (t *TCode) Stop() error {
	t.mu.Lock()
	open := t.conn != nil
	t.mu.Unlock()
	if !open {
		return nil
	}
	return t.writeLine("DSTOP")
}

// HasLinear: a TCode device always has L0.
func (t *TCode) HasLinear() bool { return true }

// SetPosition moves L0 (stroke) to pos within d.
func (t *TCode) SetPosition(pos float64, d time.Duration) error {
	return t.writeLine(TCodeMove("L0", pos, d))
}

// tcodeAxes maps SamNPlayer axis names to TCode channels (the standard
// OSR2 / SR6 assignment). A device ignores channels it does not have.
var tcodeAxes = []struct{ name, code string }{
	{ChSurge, "L1"}, {ChSway, "L2"}, {ChTwist, "R0"}, {ChRoll, "R1"}, {ChPitch, "R2"},
}

func tcodeCode(axis string) string {
	for _, a := range tcodeAxes {
		if a.name == axis {
			return a.code
		}
	}
	return ""
}

// HasAxis: the TCode channels L1, L2, R0–R2 (SR6; an OSR2 uses R1 / R2).
func (t *TCode) HasAxis(axis string) bool { return tcodeCode(axis) != "" }

// SetAxis moves one extra axis (D4 output) to pos within d.
func (t *TCode) SetAxis(axis string, pos float64, d time.Duration) error {
	code := tcodeCode(axis)
	if code == "" {
		return fmt.Errorf("tcode: unknown axis %q", axis)
	}
	return t.writeLine(TCodeMove(code, pos, d))
}

// DeviceCapabilities: stroke (L0) plus the standard extra axes. Which of
// them the hardware has cannot be read back over UDP; extra-axis commands
// only go out when the script has that axis (companion file).
func (t *TCode) DeviceCapabilities() Capabilities {
	feats := []Feature{{Channel: ChStroke, Kind: KindLinear, Index: 0, Actuator: "L0", Driven: true}}
	for i, a := range tcodeAxes {
		feats = append(feats, Feature{Channel: a.name, Kind: KindLinear, Index: i + 1, Actuator: a.code, Driven: true})
	}
	return Capabilities{Device: "TCode device", Features: feats}
}

// Info reports the connection for the GUI.
func (t *TCode) Info() ConnectionInfo {
	t.mu.Lock()
	defer t.mu.Unlock()
	network, hostport, err := ParseTCodeAddress(t.address)
	addr := t.address
	if err == nil {
		addr = network + "://" + hostport
	}
	return ConnectionInfo{Connected: t.connected, Name: "TCode device (L0 + extra axes)", Address: addr}
}
