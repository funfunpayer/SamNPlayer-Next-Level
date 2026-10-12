package device

import (
	"context"
	"encoding/json"
	"fmt"
	"strings"
	"sync"
	"time"

	"github.com/gorilla/websocket"

	"github.com/funfunpayer/SamNPlayer/logging"
)

// Intiface spricht mit einem Buttplug-Server (Intiface Central) über
// WebSocket, statt selbst eine Bluetooth-Verbindung aufzubauen.
//
// Warum das den Aufwand lohnt:
//
//   - Kein eigener Bluetooth-Adapter nötig. Intiface Central kümmert sich um
//     Verbindung, Pairing und Wiederverbindung - das sind erfahrungsgemäß die
//     fehleranfälligsten Teile, und sie sind dort für dutzende Geräte
//     erprobt.
//   - Es funktioniert mit JEDEM von Buttplug unterstützten Gerät, nicht nur
//     mit dem Sam Neo 2. Der eigene BLE-Pfad bleibt daneben bestehen, weil er
//     ohne Zusatzsoftware auskommt.
//
// Umgesetzt ist der Buttplug-Nachrichtenaustausch nach der offenen
// Protokollbeschreibung, nicht durch Übernahme fremden Codes. Nachrichten
// sind JSON-Objekte, jede in ein Array verpackt, mit fortlaufender Id.
//
// Driven (D2): every vibration motor, suction, rotation and the stroke
// position (LinearCmd) of the first device found. Not done: choosing a
// device by index. Sensors: battery only (BatteryLevelCmd), if listed.
type Intiface struct {
	url string

	mu        sync.Mutex
	conn      *websocket.Conn
	nextID    int
	deviceIdx int
	// scalarIndex bildet unsere Kanäle auf die ScalarCmd-Indizes des
	// Geräts ab. Welcher Index welche Funktion hat, sagt erst die
	// Gerätebeschreibung - fest verdrahtete Annahmen wären hier falsch.
	vibrateIdx   int
	constrictIdx int
	// constrictActuator ist der ActuatorType des zweiten Kanals, wie das
	// Gerät ihn meldet (Constrict, Oscillate oder Inflate). Buttplug lehnt
	// einen ScalarCmd ab, dessen ActuatorType nicht zum Feature passt.
	constrictActuator string
	// features: every channel the adopted device lists (D1), driven or not.
	features []Feature
	// motorLevels: per-motor output level (channel id → 0–1), default 1.
	motorLevels map[string]float64
	deviceName  string
	connected   bool
	hasBattery  bool
	batteryPct  int
	batteryOK   bool
	batteryAt   time.Time

	stopPing chan struct{}
	// batteryCh bekommt BatteryLevelReading-Werte von readLoop (je
	// Verbindung neu angelegt, Puffer 1).
	batteryCh chan float64
	// reconnectUsed: one automatic Connect after a dead socket per
	// healthy connection life (#281 liveness follow-up). Cleared on
	// successful Connect so a later blip can recover once more; stays
	// set after a failed one-shot so we do not hammer the server.
	reconnectUsed bool
}

const (
	intifaceDefaultURL = "ws://127.0.0.1:12345"
	intifaceTimeout    = 8 * time.Second
	// Buttplug-Server erwarten regelmäßige Ping-Nachrichten, sonst stoppen
	// sie aus Sicherheitsgründen alle Geräte. Der Server nennt in ServerInfo
	// ein Höchstintervall; wir senden deutlich häufiger.
	intifacePingInterval = 2 * time.Second
)

// NormalizeIntifaceURL macht aus einer Nutzereingabe eine gültige
// WebSocket-Adresse.
//
// Der Grund ist der Hauptanwendungsfall: Intiface Central läuft auf dem
// Handy, der Player auf dem Rechner. Dann tippt man eine IP-Adresse ein,
// keine vollständige ws://-URL - und ohne diese Umformung scheitert die
// Verbindung mit einer Meldung über ein unbekanntes Schema, was niemandem
// weiterhilft.
//
// Akzeptiert wird alles davon:
//
//	(leer)                  -> ws://127.0.0.1:12345
//	192.168.1.50            -> ws://192.168.1.50:12345
//	192.168.1.50:12345      -> ws://192.168.1.50:12345
//	ws://192.168.1.50:12345 -> unverändert
func NormalizeIntifaceURL(input string) string {
	value := strings.TrimSpace(input)
	if value == "" {
		return intifaceDefaultURL
	}
	if !strings.Contains(value, "://") {
		value = "ws://" + value
	}
	value = strings.TrimSuffix(value, "/")
	// Port ergänzen, wenn keiner angegeben wurde. Die Prüfung sucht nach
	// einem Doppelpunkt NACH dem Schema - der im "ws://" zählt nicht.
	rest := value[strings.Index(value, "://")+3:]
	if !strings.Contains(rest, ":") {
		value += ":12345"
	}
	return value
}

// NewIntiface erzeugt einen Client. Leere URL = Standardadresse von
// Intiface Central auf demselben Rechner.
func NewIntiface(url string) *Intiface {
	return &Intiface{url: NormalizeIntifaceURL(url), vibrateIdx: -1, constrictIdx: -1}
}

func (i *Intiface) send(message map[string]any) (int, error) {
	if i.conn == nil {
		return 0, fmt.Errorf("intiface: not connected")
	}
	i.nextID++
	id := i.nextID
	for _, payload := range message {
		if m, ok := payload.(map[string]any); ok {
			m["Id"] = id
		}
	}
	if err := i.conn.WriteJSON([]any{message}); err != nil {
		// Drop dead sockets so Info()/GUI stop showing “connected” while
		// every write fails (common after Intiface Central quit or Wi-Fi blip).
		i.markDeadLocked(err.Error())
		return id, err
	}
	return id, nil
}

// markDeadLocked clears connection state. Caller must hold i.mu.
func (i *Intiface) markDeadLocked(reason string) {
	was := i.connected || i.conn != nil
	if i.conn != nil {
		_ = i.conn.Close()
		i.conn = nil
	}
	i.connected = false
	if i.stopPing != nil {
		close(i.stopPing)
		i.stopPing = nil
	}
	if was && reason != "" {
		logging.Warn("intiface: connection lost", "reason", reason)
	}
}

// readUntil liest Nachrichten, bis eine vom gesuchten Typ kommt. Andere
// Nachrichten (etwa DeviceAdded) werden dabei mitverarbeitet - der Server
// sendet sie unaufgefordert, und sie einfach zu verwerfen hieße, das
// angeschlossene Gerät zu übersehen.
// readUntil requires the caller to hold i.mu (Connect handshake / BatteryLevel).
func (i *Intiface) readUntil(want string, deadline time.Time) (map[string]any, error) {
	if i.conn == nil {
		return nil, fmt.Errorf("intiface: not connected")
	}
	for time.Now().Before(deadline) {
		if i.conn == nil {
			return nil, fmt.Errorf("intiface: not connected")
		}
		_ = i.conn.SetReadDeadline(deadline)
		var batch []map[string]json.RawMessage
		if err := i.conn.ReadJSON(&batch); err != nil {
			return nil, fmt.Errorf("intiface: response not readable: %w", err)
		}
		for _, message := range batch {
			for name, raw := range message {
				var body map[string]any
				_ = json.Unmarshal(raw, &body)
				if name == "DeviceAdded" {
					i.adoptDevice(body)
				}
				if name == "Error" {
					return nil, fmt.Errorf("intiface: server reported error: %v", body["ErrorMessage"])
				}
				if name == want {
					return body, nil
				}
			}
		}
	}
	return nil, fmt.Errorf("intiface: no response of type %q within timeout", want)
}

// adoptDevice übernimmt das erste Gerät, das die benötigten Kanäle bietet.
func (i *Intiface) adoptDevice(body map[string]any) {
	if i.deviceName != "" {
		return // schon eines übernommen
	}
	index, ok := body["DeviceIndex"].(float64)
	if !ok {
		return
	}
	name, _ := body["DeviceName"].(string)

	messages, _ := body["DeviceMessages"].(map[string]any)
	scalars, _ := messages["ScalarCmd"].([]any)
	for idx, entry := range scalars {
		feature, _ := entry.(map[string]any)
		actuator, _ := feature["ActuatorType"].(string)
		switch actuator {
		case "Vibrate":
			if i.vibrateIdx < 0 {
				i.vibrateIdx = idx
			}
		case "Constrict", "Oscillate", "Inflate":
			// Sog heißt je nach Gerät anders. Oscillate und Inflate sind
			// die naheliegenden Entsprechungen, wenn Constrict fehlt -
			// besser ein sinnvoll belegter zweiter Kanal als gar keiner.
			if i.constrictIdx < 0 {
				i.constrictIdx = idx
				i.constrictActuator = actuator
			}
		}
	}
	features := buttplugFeatures(messages, i.constrictIdx)
	if len(features) == 0 {
		logging.Warn("intiface: device skipped (no usable channels)", "device", name)
		return
	}
	// Strokers (e.g. The Handy: LinearCmd only) are adopted too; the player
	// sends them the stroke positions (SetPosition, D2).
	i.features = features
	// Akku nur anbieten, wenn Buttplug BatteryLevelCmd listet.
	if _, ok := messages["BatteryLevelCmd"]; ok {
		i.hasBattery = true
	}
	i.deviceIdx = int(index)
	i.deviceName = name
	logging.Info("intiface: device adopted", "device", name, "index", i.deviceIdx,
		"vibration", i.vibrateIdx, "suction", i.constrictIdx, "battery", i.hasBattery,
		"channels", len(features))
}

func (i *Intiface) Connect(ctx context.Context) error {
	i.mu.Lock()
	defer i.mu.Unlock()

	dialer := websocket.Dialer{HandshakeTimeout: intifaceTimeout}
	conn, _, err := dialer.DialContext(ctx, i.url, nil)
	if err != nil {
		return fmt.Errorf("intiface: no connection to %s - %s (%w)",
			i.url, intifaceHint(i.url, err), err)
	}
	i.conn = conn
	i.nextID = 0
	i.deviceName = ""
	i.vibrateIdx, i.constrictIdx = -1, -1
	i.constrictActuator = ""
	i.features = nil
	i.hasBattery = false
	i.batteryOK = false
	i.batteryPct = 0
	i.batteryAt = time.Time{}

	deadline := time.Now().Add(intifaceTimeout)
	if _, err := i.send(map[string]any{"RequestServerInfo": map[string]any{
		"ClientName": "SamNPlayer", "MessageVersion": 3,
	}}); err != nil {
		conn.Close()
		return fmt.Errorf("intiface: handshake failed: %w", err)
	}
	info, err := i.readUntil("ServerInfo", deadline)
	if err != nil {
		conn.Close()
		return err
	}
	logging.Info("intiface: connected", "server", info["ServerName"])

	// Bereits verbundene Geräte abfragen. Ohne das würde nur ein Gerät
	// gefunden, das WÄHREND unserer Sitzung neu dazukommt.
	if _, err := i.send(map[string]any{"RequestDeviceList": map[string]any{}}); err != nil {
		conn.Close()
		return err
	}
	list, err := i.readUntil("DeviceList", deadline)
	if err == nil {
		if devices, ok := list["Devices"].([]any); ok {
			for _, entry := range devices {
				if body, ok := entry.(map[string]any); ok {
					i.adoptDevice(body)
				}
			}
		}
	}

	if i.deviceName == "" {
		// Kurz auf ein Gerät warten, das gerade erst gekoppelt wird.
		if _, err := i.send(map[string]any{"StartScanning": map[string]any{}}); err == nil {
			_, _ = i.readUntil("DeviceAdded", time.Now().Add(intifaceTimeout))
			_, _ = i.send(map[string]any{"StopScanning": map[string]any{}})
		}
	}
	if i.deviceName == "" {
		conn.Close()
		return fmt.Errorf("intiface: no usable device found — connect the device in Intiface Central and try again")
	}

	i.connected = true
	i.reconnectUsed = false
	i.stopPing = make(chan struct{})
	go i.pingLoop(i.stopPing)
	_ = i.conn.SetReadDeadline(time.Time{})
	i.batteryCh = make(chan float64, 1)
	go i.readLoop(conn, i.batteryCh)
	return nil
}

// readLoop liest nach dem Handshake alles, was der Server schickt. Buttplug
// beantwortet jeden ScalarCmd und Ping mit "Ok" - ungelesen stauen sich
// diese Antworten, bis der Server beim Schreiben blockiert, nicht mehr
// liest, und unser nächster Write (unter i.mu) für immer hängt: Wiedergabe
// und Info() stehen dann. Einziger Leser der Verbindung nach Connect;
// endet, wenn die Verbindung schließt.
func (i *Intiface) readLoop(conn *websocket.Conn, battery chan<- float64) {
	for {
		var batch []map[string]json.RawMessage
		if err := conn.ReadJSON(&batch); err != nil {
			i.mu.Lock()
			if i.conn == conn { // nicht schon durch Disconnect/Reconnect ersetzt
				i.markDeadLocked("read: " + err.Error())
			}
			i.mu.Unlock()
			return
		}
		for _, message := range batch {
			for name, raw := range message {
				switch name {
				case "BatteryLevelReading":
					var body struct{ BatteryLevel float64 }
					if json.Unmarshal(raw, &body) == nil {
						select {
						case battery <- body.BatteryLevel:
						default: // niemand wartet (mehr) - verwerfen
						}
					}
				case "Error":
					logging.Warn("intiface: server error", "message", string(raw))
				case "DeviceRemoved":
					logging.Warn("intiface: device removed", "message", string(raw))
				}
			}
		}
	}
}

// TryReconnectOnce dials Intiface again after liveness marked the socket
// dead. At most one attempt until the next successful Connect. No-op when
// already connected. Does not loop.
func (i *Intiface) TryReconnectOnce(ctx context.Context) (bool, error) {
	i.mu.Lock()
	if i.connected && i.conn != nil {
		i.mu.Unlock()
		return true, nil
	}
	if i.reconnectUsed {
		i.mu.Unlock()
		return false, fmt.Errorf("intiface: one-shot reconnect already used")
	}
	i.reconnectUsed = true
	i.mu.Unlock()

	if ctx == nil {
		ctx = context.Background()
	}
	cctx, cancel := context.WithTimeout(ctx, intifaceTimeout)
	defer cancel()
	if err := i.Connect(cctx); err != nil {
		logging.Warn("intiface: one-shot reconnect failed", "error", err)
		return false, err
	}
	logging.Info("intiface: one-shot reconnect ok")
	return true, nil
}

// ensureConnectedLocked returns nil when the WebSocket is live. If dead,
// unlocks, runs TryReconnectOnce, and re-locks. Caller must hold i.mu and
// must not use defer Unlock across this call.
func (i *Intiface) ensureConnectedLocked() error {
	if i.connected && i.conn != nil {
		return nil
	}
	i.mu.Unlock()
	ok, err := i.TryReconnectOnce(context.Background())
	i.mu.Lock()
	if ok && i.connected && i.conn != nil {
		return nil
	}
	if err != nil {
		return fmt.Errorf("intiface: not connected (%w)", err)
	}
	return fmt.Errorf("intiface: not connected")
}

// pingLoop hält die Verbindung am Leben. Bleibt der Ping aus, stoppt der
// Server aus Sicherheitsgründen alle Geräte - das ist gewollt, wäre hier
// aber ein Fehler.
func (i *Intiface) pingLoop(stop chan struct{}) {
	ticker := time.NewTicker(intifacePingInterval)
	defer ticker.Stop()
	for {
		select {
		case <-stop:
			return
		case <-ticker.C:
			i.mu.Lock()
			if i.conn != nil {
				if _, err := i.send(map[string]any{"Ping": map[string]any{}}); err != nil {
					// send already marked dead (closes this stop chan). One-shot
					// Connect starts a fresh pingLoop — this goroutine must exit
					// either way so we never run two keepalive loops.
					i.mu.Unlock()
					_, _ = i.TryReconnectOnce(context.Background())
					return
				}
			}
			i.mu.Unlock()
		}
	}
}

// command sends one message built by build (called under i.mu, so it sees
// the current device index); on a dead socket it reconnects once and
// retries. build returning nil = nothing to send on this device.
func (i *Intiface) command(build func() map[string]any) error {
	i.mu.Lock()
	if err := i.ensureConnectedLocked(); err != nil {
		i.mu.Unlock()
		return err
	}
	msg := build()
	if msg == nil {
		i.mu.Unlock()
		return nil
	}
	_, err := i.send(msg)
	i.mu.Unlock()
	if err == nil {
		return nil
	}
	// Dead socket — one-shot reconnect + single retry.
	if ok, _ := i.TryReconnectOnce(context.Background()); !ok {
		return err
	}
	i.mu.Lock()
	defer i.mu.Unlock()
	if !i.connected || i.conn == nil {
		return err
	}
	if msg = build(); msg == nil {
		return nil
	}
	_, err = i.send(msg)
	return err
}

func clampUnit(v float64) float64 {
	if v < 0 {
		return 0
	}
	if v > 1 {
		return 1
	}
	return v
}

// scalarsFor lists the ScalarCmd entries for one curve: "vibration" drives
// every driven scalar except suction, "suction" the suction feature only.
// Caller holds i.mu.
func (i *Intiface) scalarsFor(curve string, value float64) []any {
	var out []any
	for _, f := range i.features {
		if f.Kind != KindScalar || !f.Driven {
			continue
		}
		isSuction := f.Index == i.constrictIdx
		if (curve == ChSuction) != isSuction {
			continue
		}
		out = append(out, map[string]any{"Index": f.Index, "Scalar": value * i.motorLevelLocked(f.Channel), "ActuatorType": f.Actuator})
	}
	return out
}

// motorLevelLocked: the per-motor level of channel (1 when unset). Caller holds i.mu.
func (i *Intiface) motorLevelLocked(channel string) float64 {
	if l, ok := i.motorLevels[channel]; ok {
		return l
	}
	return 1
}

// SetMotorLevel sets one motor's level (0–1), also while playing; it takes
// effect with the next command.
func (i *Intiface) SetMotorLevel(channel string, level float64) {
	i.mu.Lock()
	defer i.mu.Unlock()
	if i.motorLevels == nil {
		i.motorLevels = map[string]float64{}
	}
	i.motorLevels[channel] = clampUnit(level)
}

// SetVibration drives every vibration motor (LELO F1S V3: both), other
// intensity scalars (Oscillate, Rotate) and rotating features (RotateCmd).
func (i *Intiface) SetVibration(intensity float64) error {
	v := clampUnit(intensity)
	if err := i.command(func() map[string]any {
		if scalars := i.scalarsFor(ChVibration, v); len(scalars) > 0 {
			return map[string]any{"ScalarCmd": map[string]any{"DeviceIndex": i.deviceIdx, "Scalars": scalars}}
		}
		return nil
	}); err != nil {
		return err
	}
	return i.command(func() map[string]any {
		var rotations []any
		for _, f := range i.features {
			if f.Kind == KindRotate && f.Driven {
				rotations = append(rotations, map[string]any{"Index": f.Index, "Speed": v * i.motorLevelLocked(f.Channel), "Clockwise": true})
			}
		}
		if len(rotations) == 0 {
			return nil
		}
		return map[string]any{"RotateCmd": map[string]any{"DeviceIndex": i.deviceIdx, "Rotations": rotations}}
	})
}

func (i *Intiface) SetSuction(intensity float64) error {
	v := clampUnit(intensity)
	return i.command(func() map[string]any {
		if scalars := i.scalarsFor(ChSuction, v); len(scalars) > 0 {
			return map[string]any{"ScalarCmd": map[string]any{"DeviceIndex": i.deviceIdx, "Scalars": scalars}}
		}
		return nil
	})
}

// linearIndexLocked: the driven LinearCmd feature, -1 if none.
func (i *Intiface) linearIndexLocked() int {
	for _, f := range i.features {
		if f.Kind == KindLinear && f.Driven {
			return f.Index
		}
	}
	return -1
}

// HasLinear reports a driven linear channel (The Handy and other strokers).
func (i *Intiface) HasLinear() bool {
	i.mu.Lock()
	defer i.mu.Unlock()
	return i.linearIndexLocked() >= 0
}

// SetPosition sends a LinearCmd: move to pos (0–1) within duration.
func (i *Intiface) SetPosition(pos float64, duration time.Duration) error {
	p := clampUnit(pos)
	ms := duration.Milliseconds()
	if ms < 1 {
		ms = 1
	}
	return i.command(func() map[string]any {
		idx := i.linearIndexLocked()
		if idx < 0 {
			return nil
		}
		return map[string]any{"LinearCmd": map[string]any{
			"DeviceIndex": i.deviceIdx,
			"Vectors":     []any{map[string]any{"Index": idx, "Duration": ms, "Position": p}},
		}}
	})
}

func (i *Intiface) Stop() error {
	i.mu.Lock()
	defer i.mu.Unlock()
	if !i.connected || i.conn == nil {
		return nil
	}
	_, err := i.send(map[string]any{"StopDeviceCmd": map[string]any{
		"DeviceIndex": i.deviceIdx,
	}})
	return err
}

func (i *Intiface) Disconnect() error {
	i.mu.Lock()
	conn := i.conn
	deviceIdx := i.deviceIdx
	i.conn = nil
	i.connected = false
	if i.stopPing != nil {
		close(i.stopPing)
		i.stopPing = nil
	}
	i.mu.Unlock()

	if conn == nil {
		return nil
	}
	// Vor dem Trennen abschalten - sonst läuft das Gerät weiter, und der
	// Server merkt den Abbruch erst über den ausbleibenden Ping.
	i.mu.Lock()
	i.nextID++
	id := i.nextID
	i.mu.Unlock()
	_ = conn.WriteJSON([]any{map[string]any{"StopDeviceCmd": map[string]any{
		"DeviceIndex": deviceIdx, "Id": id,
	}}})
	return conn.Close()
}

// Capabilities meldet, welche Kanäle/Sensoren Buttplug für dieses Gerät listet.
func (i *Intiface) Capabilities() (vibration, suction, battery bool) {
	i.mu.Lock()
	defer i.mu.Unlock()
	return i.vibrateIdx >= 0, i.constrictIdx >= 0, i.hasBattery
}

// DeviceCapabilities lists every channel of the adopted device (D1).
func (i *Intiface) DeviceCapabilities() Capabilities {
	i.mu.Lock()
	defer i.mu.Unlock()
	return Capabilities{
		Device:   i.deviceName,
		Features: append([]Feature(nil), i.features...),
		Battery:  i.hasBattery,
	}
}

// Info liefert den Verbindungszustand für die Anzeige.
func (i *Intiface) Info() ConnectionInfo {
	i.mu.Lock()
	defer i.mu.Unlock()
	name := i.deviceName
	if name != "" {
		channels := []string{}
		for _, f := range i.features {
			label := strings.ToUpper(f.Channel[:1]) + f.Channel[1:]
			if !f.Driven {
				label += " (not driven yet)"
			}
			channels = append(channels, label)
		}
		name = fmt.Sprintf("%s (via Intiface, %s)", name, strings.Join(channels, " + "))
	}
	return ConnectionInfo{
		Connected:  i.connected,
		Name:       name,
		Address:    i.url,
		BatteryPct: i.batteryPct,
		BatteryOK:  i.batteryOK,
	}
}

// BatteryLevel fragt Buttplug BatteryLevelCmd ab, sofern das Gerät sie anbietet.
// Die Antwort liest readLoop (einziger Leser der Verbindung) und reicht sie
// über batteryCh durch; gewartet wird ohne i.mu, damit Wiedergabe und
// Ping in der Zeit weiterlaufen.
func (i *Intiface) BatteryLevel() (int, bool) {
	i.mu.Lock()
	if !i.connected || i.conn == nil || !i.hasBattery {
		i.mu.Unlock()
		return 0, false
	}
	if i.batteryOK && time.Since(i.batteryAt) < batteryCacheTTL {
		pct := i.batteryPct
		i.mu.Unlock()
		return pct, true
	}
	ch := i.batteryCh
	if ch == nil { // kein readLoop - auf nil würde das Warten nie enden
		i.mu.Unlock()
		return 0, false
	}
	select { // veraltete Antwort einer abgelaufenen Anfrage verwerfen
	case <-ch:
	default:
	}
	if _, err := i.send(map[string]any{"BatteryLevelCmd": map[string]any{
		"DeviceIndex": i.deviceIdx,
	}}); err != nil {
		i.mu.Unlock()
		return 0, false
	}
	i.mu.Unlock()

	var raw float64
	select {
	case raw = <-ch:
	case <-time.After(3 * time.Second):
		logging.Debug("intiface: battery not readable (no reading within 3s)")
		return 0, false
	}
	pct := int(raw*100 + 0.5)
	if pct < 0 {
		pct = 0
	}
	if pct > 100 {
		pct = 100
	}
	i.mu.Lock()
	i.batteryPct = pct
	i.batteryOK = true
	i.batteryAt = time.Now()
	i.mu.Unlock()
	logging.Info("intiface: battery read", "percent", pct)
	return pct, true
}

// intifaceHint nennt die wahrscheinlichste Ursache. Eine rohe
// Netzwerkfehlermeldung sagt dem Anwender nichts; die Ursachen
// unterscheiden sich außerdem deutlich, je nachdem ob der Server auf
// demselben Rechner oder im Netzwerk laufen soll.
func intifaceHint(url string, err error) string {
	local := strings.Contains(url, "127.0.0.1") || strings.Contains(url, "localhost")
	text := strings.ToLower(err.Error())

	switch {
	case strings.Contains(text, "refused"):
		if local {
			return "the host is reachable but nothing is listening on this port. " +
				"Start the server in Intiface Central"
		}
		return "the device is reachable but nothing is listening on this port. " +
			"Start the server in Intiface Central and check it is exposed on the network " +
			"(not only on the device itself)"
	case strings.Contains(text, "timeout"), strings.Contains(text, "deadline"),
		strings.Contains(text, "no route"), strings.Contains(text, "unreachable"):
		if local {
			return "no response — is Intiface Central running?"
		}
		return "no response. Are the phone and computer on the same Wi-Fi? " +
			"Is the IP address correct? Is a firewall blocking the port?"
	case strings.Contains(text, "no such host"):
		return "hostname could not be resolved — enter the IP address directly"
	default:
		if local {
			return "is Intiface Central running with the server started?"
		}
		return "check the address and network connection"
	}
}
