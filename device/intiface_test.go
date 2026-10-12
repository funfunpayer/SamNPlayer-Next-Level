package device

import (
	"context"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"strings"
	"sync"
	"testing"
	"time"

	"github.com/gorilla/websocket"
)

// Ein nachgebauter Buttplug-Server. Er antwortet nach der offenen
// Protokollbeschreibung und zeichnet auf, was der Client sendet - damit ist
// prüfbar, dass wir uns an das Protokoll halten, ohne einen echten Server
// und ein echtes Gerät zu brauchen.
type fakeButtplug struct {
	mu       sync.Mutex
	received []map[string]json.RawMessage
	// deviceMessages bestimmt, welche Kanäle das gemeldete Gerät kann.
	deviceMessages map[string]any
	noDevices      bool
	// active holds the live server-side WebSocket so tests can drop it.
	active *websocket.Conn
	// burstAfterDeviceList > 0: right after the DeviceList reply, write
	// that many bytes of unsolicited Ok messages, then close burstDone.
	burstAfterDeviceList int
	burstDone            chan struct{}
}

func (f *fakeButtplug) dropConnection() {
	f.mu.Lock()
	conn := f.active
	f.active = nil
	f.mu.Unlock()
	if conn != nil {
		_ = conn.Close()
	}
}

func (f *fakeButtplug) record(message map[string]json.RawMessage) {
	f.mu.Lock()
	defer f.mu.Unlock()
	f.received = append(f.received, message)
}

func (f *fakeButtplug) names() []string {
	f.mu.Lock()
	defer f.mu.Unlock()
	var out []string
	for _, message := range f.received {
		for name := range message {
			out = append(out, name)
		}
	}
	return out
}

func (f *fakeButtplug) scalarCommands() []map[string]any { return f.commands("ScalarCmd") }

// commands returns the bodies of every received message of that name.
func (f *fakeButtplug) commands(name string) []map[string]any {
	f.mu.Lock()
	defer f.mu.Unlock()
	var out []map[string]any
	for _, message := range f.received {
		raw, ok := message[name]
		if !ok {
			continue
		}
		var body map[string]any
		if json.Unmarshal(raw, &body) == nil {
			out = append(out, body)
		}
	}
	return out
}

func (f *fakeButtplug) handler() http.HandlerFunc {
	upgrader := websocket.Upgrader{}
	return func(w http.ResponseWriter, r *http.Request) {
		conn, err := upgrader.Upgrade(w, r, nil)
		if err != nil {
			return
		}
		f.mu.Lock()
		f.active = conn
		f.mu.Unlock()
		defer func() {
			f.mu.Lock()
			if f.active == conn {
				f.active = nil
			}
			f.mu.Unlock()
			_ = conn.Close()
		}()

		for {
			var batch []map[string]json.RawMessage
			if err := conn.ReadJSON(&batch); err != nil {
				return
			}
			for _, message := range batch {
				f.record(message)
				for name, raw := range message {
					var body map[string]any
					_ = json.Unmarshal(raw, &body)
					id := body["Id"]
					switch name {
					case "RequestServerInfo":
						_ = conn.WriteJSON([]any{map[string]any{"ServerInfo": map[string]any{
							"Id": id, "ServerName": "FakeServer",
							"MessageVersion": 3, "MaxPingTime": 5000,
						}}})
					case "RequestDeviceList":
						devices := []any{}
						if !f.noDevices {
							devices = append(devices, map[string]any{
								"DeviceIndex": 7, "DeviceName": "Testgerät",
								"DeviceMessages": f.deviceMessages,
							})
						}
						_ = conn.WriteJSON([]any{map[string]any{"DeviceList": map[string]any{
							"Id": id, "Devices": devices,
						}}})
						if f.burstAfterDeviceList > 0 {
							pad := strings.Repeat("x", 16<<10)
							for sent := 0; sent < f.burstAfterDeviceList; sent += len(pad) {
								if conn.WriteJSON([]any{map[string]any{"Ok": map[string]any{"Id": 0, "Pad": pad}}}) != nil {
									return
								}
							}
							close(f.burstDone)
						}
					case "BatteryLevelCmd":
						_ = conn.WriteJSON([]any{map[string]any{"BatteryLevelReading": map[string]any{
							"Id": id, "DeviceIndex": body["DeviceIndex"], "BatteryLevel": 0.73,
						}}})
					case "StartScanning", "StopScanning", "Ping", "ScalarCmd", "LinearCmd", "RotateCmd", "StopDeviceCmd":
						_ = conn.WriteJSON([]any{map[string]any{"Ok": map[string]any{"Id": id}}})
					}
				}
			}
		}
	}
}

func standardDeviceMessages() map[string]any {
	return map[string]any{
		"ScalarCmd": []any{
			map[string]any{"StepCount": 10, "ActuatorType": "Vibrate", "FeatureDescriptor": "Vib"},
			map[string]any{"StepCount": 5, "ActuatorType": "Constrict", "FeatureDescriptor": "Sog"},
		},
		"StopDeviceCmd": map[string]any{},
	}
}

func deviceMessagesWithBattery() map[string]any {
	m := standardDeviceMessages()
	m["BatteryLevelCmd"] = map[string]any{}
	return m
}

func startFake(t *testing.T, fake *fakeButtplug) (string, *httptest.Server) {
	t.Helper()
	server := httptest.NewServer(fake.handler())
	url := "ws" + strings.TrimPrefix(server.URL, "http")
	return url, server
}

// Der Kern: Anmeldung, Geräteübernahme und Ansteuerung beider Kanäle.
func TestIntifaceConnectAndControl(t *testing.T) {
	fake := &fakeButtplug{deviceMessages: standardDeviceMessages()}
	url, server := startFake(t, fake)
	defer server.Close()

	dev := NewIntiface(url)
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	if err := dev.Connect(ctx); err != nil {
		t.Fatalf("Connect: %v", err)
	}
	defer dev.Disconnect()

	info := dev.Info()
	if !info.Connected {
		t.Error("nach Connect muss die Verbindung als bestehend gelten")
	}
	if !strings.Contains(info.Name, "Testgerät") {
		t.Errorf("Gerätename fehlt in der Anzeige: %q", info.Name)
	}
	// Die Anzeige soll sagen, WELCHE Kanäle gefunden wurden - sonst merkt
	// man erst beim Abspielen, dass der Sog fehlt.
	if !strings.Contains(info.Name, "Vibration") || !strings.Contains(info.Name, "Suction") {
		t.Errorf("gefundene Kanäle fehlen in der Anzeige: %q", info.Name)
	}

	names := fake.names()
	if !contains(names, "RequestServerInfo") || !contains(names, "RequestDeviceList") {
		t.Errorf("Anmeldefolge unvollständig: %v", names)
	}

	if err := dev.SetVibration(0.5); err != nil {
		t.Fatalf("SetVibration: %v", err)
	}
	if err := dev.SetSuction(1.0); err != nil {
		t.Fatalf("SetSuction: %v", err)
	}
	time.Sleep(100 * time.Millisecond)

	commands := fake.scalarCommands()
	if len(commands) < 2 {
		t.Fatalf("erwartet 2 ScalarCmd, bekam %d", len(commands))
	}
	for _, cmd := range commands {
		if idx, _ := cmd["DeviceIndex"].(float64); int(idx) != 7 {
			t.Errorf("falscher DeviceIndex: %v", cmd["DeviceIndex"])
		}
	}
}

// Werte müssen auf 0-1 begrenzt werden: das Protokoll verlangt es, und ein
// Wert über 1 würde vom Server abgelehnt - die Wiedergabe bräche dann
// mitten im Ablauf ab.
func TestIntifaceClampsValues(t *testing.T) {
	fake := &fakeButtplug{deviceMessages: standardDeviceMessages()}
	url, server := startFake(t, fake)
	defer server.Close()

	dev := NewIntiface(url)
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	if err := dev.Connect(ctx); err != nil {
		t.Fatalf("Connect: %v", err)
	}
	defer dev.Disconnect()

	_ = dev.SetVibration(3.5)
	_ = dev.SetVibration(-2.0)
	time.Sleep(100 * time.Millisecond)

	for _, cmd := range fake.scalarCommands() {
		scalars, _ := cmd["Scalars"].([]any)
		for _, entry := range scalars {
			value, _ := entry.(map[string]any)["Scalar"].(float64)
			if value < 0 || value > 1 {
				t.Errorf("Wert außerhalb 0-1 gesendet: %v", value)
			}
		}
	}
}

// Ohne Gerät muss die Verbindung mit einer verständlichen Meldung scheitern,
// statt scheinbar zu gelingen und beim ersten Befehl still nichts zu tun.
func TestIntifaceWithoutDeviceFails(t *testing.T) {
	fake := &fakeButtplug{noDevices: true, deviceMessages: standardDeviceMessages()}
	url, server := startFake(t, fake)
	defer server.Close()

	dev := NewIntiface(url)
	ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
	defer cancel()
	err := dev.Connect(ctx)
	if err == nil {
		dev.Disconnect()
		t.Fatal("ohne Gerät darf Connect nicht gelingen")
	}
	if !strings.Contains(strings.ToLower(err.Error()), "device") {
		t.Errorf("Meldung nennt das Problem nicht: %v", err)
	}
}

// Ein Gerät, das nur Vibration kann, soll trotzdem nutzbar sein - nur eben
// ohne Sog. Alles andere würde die meisten Buttplug-Geräte ausschließen.
func TestIntifaceVibrationOnlyDevice(t *testing.T) {
	fake := &fakeButtplug{deviceMessages: map[string]any{
		"ScalarCmd": []any{
			map[string]any{"StepCount": 20, "ActuatorType": "Vibrate"},
		},
	}}
	url, server := startFake(t, fake)
	defer server.Close()

	dev := NewIntiface(url)
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	if err := dev.Connect(ctx); err != nil {
		t.Fatalf("Connect: %v", err)
	}
	defer dev.Disconnect()

	if err := dev.SetSuction(0.8); err != nil {
		t.Errorf("fehlender Sog-Kanal darf keinen Fehler ergeben: %v", err)
	}
	if err := dev.SetVibration(0.4); err != nil {
		t.Errorf("SetVibration: %v", err)
	}
	info := dev.Info()
	if strings.Contains(info.Name, "Suction") {
		t.Errorf("Anzeige behauptet einen Kanal, den es nicht gibt: %q", info.Name)
	}
}

// Ist kein Server da, muss die Meldung auf Intiface Central hinweisen -
// das ist die mit Abstand häufigste Ursache.
func TestIntifaceNoServer(t *testing.T) {
	dev := NewIntiface("ws://127.0.0.1:1")
	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
	defer cancel()
	err := dev.Connect(ctx)
	if err == nil {
		t.Fatal("ohne Server darf Connect nicht gelingen")
	}
	if !strings.Contains(err.Error(), "Intiface") {
		t.Errorf("Meldung hilft nicht weiter: %v", err)
	}
}

func contains(values []string, want string) bool {
	for _, v := range values {
		if v == want {
			return true
		}
	}
	return false
}

// Der Hauptanwendungsfall ist: Intiface Central läuft auf dem Handy, der
// Player auf dem Rechner. Dann tippt man eine IP-Adresse ein, keine
// vollständige ws://-URL. Ohne Umformung scheitert die Verbindung mit einer
// Meldung über ein unbekanntes Schema, die niemandem weiterhilft.
func TestNormalizeIntifaceURL(t *testing.T) {
	cases := map[string]string{
		"":                        "ws://127.0.0.1:12345",
		"   ":                     "ws://127.0.0.1:12345",
		"192.168.1.50":            "ws://192.168.1.50:12345",
		"192.168.1.50:12345":      "ws://192.168.1.50:12345",
		"192.168.1.50:9999":       "ws://192.168.1.50:9999",
		"ws://192.168.1.50:12345": "ws://192.168.1.50:12345",
		"ws://192.168.1.50":       "ws://192.168.1.50:12345",
		"ws://10.0.0.5:12345/":    "ws://10.0.0.5:12345",
		"handy.local":             "ws://handy.local:12345",
	}
	for input, want := range cases {
		if got := NormalizeIntifaceURL(input); got != want {
			t.Errorf("NormalizeIntifaceURL(%q) = %q, erwartet %q", input, got, want)
		}
	}
}

// Die Fehlermeldung muss die wahrscheinlichste Ursache nennen - und die
// unterscheidet sich deutlich, je nachdem ob der Server lokal oder im
// Netzwerk laufen soll.
func TestIntifaceHintDistinguishesLocalAndNetwork(t *testing.T) {
	refused := errorString("dial tcp: connection refused")

	local := intifaceHint("ws://127.0.0.1:12345", refused)
	if !strings.Contains(local, "Start the server") {
		t.Errorf("lokaler Hinweis unbrauchbar: %q", local)
	}
	if strings.Contains(strings.ToLower(local), "wi-fi") {
		t.Errorf("lokaler Hinweis spricht fälschlich vom Netzwerk: %q", local)
	}

	remote := intifaceHint("ws://192.168.1.50:12345", refused)
	if !strings.Contains(strings.ToLower(remote), "network") {
		t.Errorf("Netzwerk-Hinweis fehlt: %q", remote)
	}

	timeout := intifaceHint("ws://192.168.1.50:12345",
		errorString("dial tcp: i/o timeout"))
	if !strings.Contains(strings.ToLower(timeout), "wi-fi") {
		t.Errorf("bei Zeitüberschreitung im Netzwerk fehlt der WLAN-Hinweis: %q", timeout)
	}

	host := intifaceHint("ws://handy.local:12345", errorString("no such host"))
	if !strings.Contains(strings.ToLower(host), "ip address") {
		t.Errorf("bei unauflösbarem Namen fehlt der Hinweis auf die IP: %q", host)
	}
}

// After the Buttplug server drops the WebSocket, Info must not stay
// "connected" — otherwise Device/Play UI lies until the user clicks Disconnect.
// Server is closed so one-shot reconnect cannot revive the link.
func TestIntifaceMarksDeadOnWriteFailure(t *testing.T) {
	fake := &fakeButtplug{deviceMessages: standardDeviceMessages()}
	url, server := startFake(t, fake)

	dev := NewIntiface(url)
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	if err := dev.Connect(ctx); err != nil {
		server.Close()
		t.Fatalf("Connect: %v", err)
	}
	if !dev.Info().Connected {
		server.Close()
		t.Fatal("expected connected after Connect")
	}

	fake.dropConnection()
	server.Close() // block one-shot reconnect — stay dead

	var err error
	for attempt := 0; attempt < 5; attempt++ {
		err = dev.SetVibration(0.4)
		if err != nil {
			break
		}
		time.Sleep(20 * time.Millisecond)
	}
	if err == nil {
		_ = dev.Disconnect()
		t.Fatal("expected SetVibration error after server close")
	}
	if dev.Info().Connected {
		_ = dev.Disconnect()
		t.Fatalf("Info still Connected after write failure: %v", err)
	}
	if err2 := dev.SetSuction(0.2); err2 == nil {
		t.Fatal("expected SetSuction error while dead (one-shot spent / server gone)")
	}
	_ = dev.Disconnect()
}

// One-shot reconnect: after a dropped socket, the next command dials again
// once while the Buttplug server is still reachable.
func TestIntifaceOneShotReconnect(t *testing.T) {
	fake := &fakeButtplug{deviceMessages: standardDeviceMessages()}
	url, server := startFake(t, fake)
	defer server.Close()

	dev := NewIntiface(url)
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	if err := dev.Connect(ctx); err != nil {
		t.Fatalf("Connect: %v", err)
	}

	fake.dropConnection()
	// Wait until the client read loop observes the drop — otherwise SetVibration
	// can still succeed on a half-closed socket, return nil, then markDead races
	// Info().Connected to false (flake under -race / loaded CI).
	deadBy := time.Now().Add(2 * time.Second)
	for time.Now().Before(deadBy) && dev.Info().Connected {
		time.Sleep(5 * time.Millisecond)
	}
	if dev.Info().Connected {
		_ = dev.Disconnect()
		t.Fatal("expected disconnect after dropConnection")
	}

	var err error
	for attempt := 0; attempt < 20; attempt++ {
		err = dev.SetVibration(0.35)
		if err == nil && dev.Info().Connected {
			break
		}
		time.Sleep(50 * time.Millisecond)
	}
	if err != nil {
		_ = dev.Disconnect()
		t.Fatalf("expected one-shot reconnect + SetVibration ok, got %v", err)
	}
	if !dev.Info().Connected {
		_ = dev.Disconnect()
		t.Fatal("expected Connected after one-shot reconnect")
	}
	if err := dev.SetSuction(0.2); err != nil {
		_ = dev.Disconnect()
		t.Fatalf("post-reconnect suction: %v", err)
	}
	_ = dev.Disconnect()
}

// After markDeadLocked closes the keepalive stop chan, Connect (via
// one-shot) must start a fresh pingLoop — the old stop stays closed so
// the old goroutine exits (no double keepalive).
func TestIntifacePingStopReplacedOnReconnect(t *testing.T) {
	fake := &fakeButtplug{deviceMessages: standardDeviceMessages()}
	url, server := startFake(t, fake)
	defer server.Close()

	dev := NewIntiface(url)
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	if err := dev.Connect(ctx); err != nil {
		t.Fatalf("Connect: %v", err)
	}

	dev.mu.Lock()
	oldStop := dev.stopPing
	dev.markDeadLocked("test-ping-drop")
	dev.mu.Unlock()
	if oldStop == nil {
		_ = dev.Disconnect()
		t.Fatal("expected stopPing before markDead")
	}
	select {
	case <-oldStop:
		// closed — old pingLoop must exit
	default:
		_ = dev.Disconnect()
		t.Fatal("markDeadLocked must close old stopPing")
	}

	ok, err := dev.TryReconnectOnce(context.Background())
	if !ok {
		_ = dev.Disconnect()
		t.Fatalf("one-shot reconnect: %v", err)
	}
	dev.mu.Lock()
	newStop := dev.stopPing
	dev.mu.Unlock()
	if newStop == nil || newStop == oldStop {
		_ = dev.Disconnect()
		t.Fatal("Connect after one-shot must install a new stopPing")
	}
	_ = dev.Disconnect()
}

// Failed one-shot spends the budget — later dials must not keep trying.
func TestIntifaceOneShotReconnectSpent(t *testing.T) {
	fake := &fakeButtplug{deviceMessages: standardDeviceMessages()}
	url, server := startFake(t, fake)

	dev := NewIntiface(url)
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	if err := dev.Connect(ctx); err != nil {
		server.Close()
		t.Fatalf("Connect: %v", err)
	}

	dev.mu.Lock()
	dev.markDeadLocked("test-drop")
	dev.mu.Unlock()
	server.Close()

	ok, _ := dev.TryReconnectOnce(context.Background())
	if ok {
		_ = dev.Disconnect()
		t.Fatal("first one-shot against closed server must fail")
	}
	if ok2, _ := dev.TryReconnectOnce(context.Background()); ok2 {
		_ = dev.Disconnect()
		t.Fatal("second TryReconnectOnce must not succeed after budget spent")
	}
	if dev.Info().Connected {
		_ = dev.Disconnect()
		t.Fatal("must stay disconnected")
	}
	_ = dev.Disconnect()
}

func TestIntifaceBatteryLevelLocked(t *testing.T) {
	fake := &fakeButtplug{deviceMessages: deviceMessagesWithBattery()}
	url, server := startFake(t, fake)
	defer server.Close()

	dev := NewIntiface(url)
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	if err := dev.Connect(ctx); err != nil {
		t.Fatalf("Connect: %v", err)
	}
	defer dev.Disconnect()

	vib, suction, battery := dev.Capabilities()
	if !vib || !suction || !battery {
		t.Fatalf("capabilities: vib=%v suction=%v battery=%v", vib, suction, battery)
	}

	pct, ok := dev.BatteryLevel()
	if !ok || pct != 73 {
		t.Fatalf("BatteryLevel first read: pct=%d ok=%v want 73/true", pct, ok)
	}
	// Cached path must not re-hit the wire while TTL holds.
	before := len(fake.names())
	pct2, ok2 := dev.BatteryLevel()
	if !ok2 || pct2 != 73 {
		t.Fatalf("BatteryLevel cached: pct=%d ok=%v", pct2, ok2)
	}
	after := len(fake.names())
	if after != before {
		t.Fatalf("cached BatteryLevel sent extra messages: before=%d after=%d names=%v",
			before, after, fake.names())
	}
	if !contains(fake.names(), "BatteryLevelCmd") {
		t.Fatalf("BatteryLevelCmd not sent: %v", fake.names())
	}
}

type errorString string

func (e errorString) Error() string { return string(e) }

// Buttplug answers every ScalarCmd/Ping with Ok. If the client never reads
// after Connect, those replies back up until the server blocks on its own
// write, stops reading, and the client's next write blocks while holding
// i.mu - playback, Info() and Disconnect hang (pre-fix: after ~200k
// commands on Linux loopback, far sooner with Windows' small buffers). The
// fake sends more unsolicited data right after DeviceList than socket
// buffers hold; it can only finish if the client keeps reading.
func TestIntifaceDrainsServerReplies(t *testing.T) {
	fake := &fakeButtplug{deviceMessages: standardDeviceMessages(),
		burstAfterDeviceList: 24 << 20, burstDone: make(chan struct{})}
	url, server := startFake(t, fake)
	defer server.Close()

	dev := NewIntiface(url)
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	if err := dev.Connect(ctx); err != nil {
		t.Fatalf("Connect: %v", err)
	}
	select {
	case <-fake.burstDone:
	case <-time.After(20 * time.Second):
		fake.dropConnection() // unblock the fake's write before Disconnect
		t.Fatal("server could not deliver its replies: the client stopped reading after Connect")
	}
	if err := dev.SetVibration(0.5); err != nil {
		t.Fatalf("SetVibration after the burst: %v", err)
	}
	if !dev.Info().Connected {
		t.Fatal("connection should still be up")
	}
	_ = dev.Disconnect()
}

// A second channel adopted from Oscillate/Inflate must be commanded with
// that actuator type; Buttplug rejects a ScalarCmd whose ActuatorType does
// not match the feature at that index.
func TestIntifaceSuctionUsesAdoptedActuatorType(t *testing.T) {
	fake := &fakeButtplug{deviceMessages: map[string]any{
		"ScalarCmd": []any{
			map[string]any{"StepCount": 20, "ActuatorType": "Vibrate"},
			map[string]any{"StepCount": 20, "ActuatorType": "Oscillate"},
		},
		"StopDeviceCmd": map[string]any{},
	}}
	url, server := startFake(t, fake)
	defer server.Close()
	dev := NewIntiface(url)
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	if err := dev.Connect(ctx); err != nil {
		t.Fatalf("Connect: %v", err)
	}
	defer dev.Disconnect()
	if err := dev.SetSuction(0.5); err != nil {
		t.Fatal(err)
	}
	time.Sleep(100 * time.Millisecond)
	cmds := fake.scalarCommands()
	if len(cmds) == 0 {
		t.Fatal("no ScalarCmd sent")
	}
	scalar := cmds[len(cmds)-1]["Scalars"].([]any)[0].(map[string]any)
	if scalar["ActuatorType"] != "Oscillate" || scalar["Index"] != float64(1) {
		t.Fatalf("suction sent as %v", scalar)
	}
}
