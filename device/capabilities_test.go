package device

import (
	"context"
	"strings"
	"testing"
	"time"
)

func TestButtplugFeaturesNeo2Like(t *testing.T) {
	f := buttplugFeatures(standardDeviceMessages(), 1)
	if len(f) != 2 || f[0].Channel != ChVibration || !f[0].Driven || f[1].Channel != ChSuction || !f[1].Driven {
		t.Fatalf("features = %+v", f)
	}
}

func TestButtplugFeaturesMultiAxis(t *testing.T) {
	msgs := map[string]any{
		"ScalarCmd": []any{
			map[string]any{"ActuatorType": "Vibrate"},
			map[string]any{"ActuatorType": "Vibrate"},
			map[string]any{"ActuatorType": "Rotate"},
			map[string]any{"ActuatorType": "Unknown"},
		},
		"LinearCmd": []any{map[string]any{"StepCount": 100}},
		"RotateCmd": []any{map[string]any{"StepCount": 20}},
	}
	f := buttplugFeatures(msgs, -1)
	got := []string{}
	for _, x := range f {
		got = append(got, x.Channel+":"+x.Kind)
	}
	want := "vibration:scalar vibration2:scalar rotate:scalar stroke:linear rotate2:rotate"
	if strings.Join(got, " ") != want {
		t.Fatalf("channels = %q, want %q", strings.Join(got, " "), want)
	}
	for _, x := range f {
		if !x.Driven {
			t.Fatalf("D2 drives every motor, rotation and the stroke: %+v", f)
		}
	}
}

// The Handy over Intiface lists LinearCmd only. It used to be skipped
// ("no usable channels"); now it connects and reports its stroke channel.
func TestIntifaceAdoptsStroker(t *testing.T) {
	fake := &fakeButtplug{deviceMessages: map[string]any{
		"LinearCmd":     []any{map[string]any{"StepCount": 100, "ActuatorType": "Position"}},
		"StopDeviceCmd": map[string]any{},
	}}
	url, server := startFake(t, fake)
	defer server.Close()
	dev := NewIntiface(url)
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	if err := dev.Connect(ctx); err != nil {
		t.Fatalf("a stroker must connect: %v", err)
	}
	defer dev.Disconnect()
	caps := CapabilitiesOf(dev)
	if !caps.Has(ChStroke) || caps.Has(ChVibration) || caps.Features[0].Kind != KindLinear || !caps.Features[0].Driven {
		t.Fatalf("caps = %+v", caps)
	}
	if err := dev.SetVibration(0.5); err != nil {
		t.Fatalf("no vibration motor must not error: %v", err)
	}
	if name := dev.Info().Name; !strings.Contains(name, "Stroke") || strings.Contains(name, "not driven") {
		t.Fatalf("info names the driven stroke channel: %q", name)
	}
	if !dev.HasLinear() {
		t.Fatal("stroker must report a linear channel")
	}
	if err := dev.SetPosition(0.8, 250*time.Millisecond); err != nil {
		t.Fatalf("SetPosition: %v", err)
	}
	if err := dev.SetPosition(1.7, 0); err != nil {
		t.Fatalf("SetPosition clamps: %v", err)
	}
	time.Sleep(100 * time.Millisecond)
	cmds := fake.commands("LinearCmd")
	if len(cmds) != 2 {
		t.Fatalf("want 2 LinearCmd, got %d", len(cmds))
	}
	v := cmds[0]["Vectors"].([]any)[0].(map[string]any)
	if v["Index"].(float64) != 0 || v["Duration"].(float64) != 250 || v["Position"].(float64) != 0.8 {
		t.Fatalf("LinearCmd vector %+v", v)
	}
	v = cmds[1]["Vectors"].([]any)[0].(map[string]any)
	if v["Position"].(float64) != 1 || v["Duration"].(float64) < 1 {
		t.Fatalf("clamped vector %+v", v)
	}
	if n := len(fake.commands("ScalarCmd")); n != 0 {
		t.Fatalf("a stroker gets no ScalarCmd, got %d", n)
	}
}

// LELO F1S V3: two Vibrate motors — both follow the vibration curve in one
// ScalarCmd. Lovense Max 2: Vibrate + Constrict — suction goes to the pump.
func TestIntifaceDrivesEveryMotor(t *testing.T) {
	fake := &fakeButtplug{deviceMessages: map[string]any{
		"ScalarCmd": []any{
			map[string]any{"ActuatorType": "Vibrate"},
			map[string]any{"ActuatorType": "Vibrate"},
			map[string]any{"ActuatorType": "Constrict"},
		},
		"RotateCmd":     []any{map[string]any{"StepCount": 20}},
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
	if dev.HasLinear() {
		t.Fatal("no LinearCmd listed → no linear channel")
	}
	if err := dev.SetVibration(0.6); err != nil {
		t.Fatal(err)
	}
	if err := dev.SetSuction(0.3); err != nil {
		t.Fatal(err)
	}
	time.Sleep(100 * time.Millisecond)
	sc := fake.commands("ScalarCmd")
	if len(sc) != 2 {
		t.Fatalf("want 2 ScalarCmd, got %d", len(sc))
	}
	vib := sc[0]["Scalars"].([]any)
	if len(vib) != 2 {
		t.Fatalf("both motors in one command: %+v", vib)
	}
	for k, e := range vib {
		m := e.(map[string]any)
		if m["Index"].(float64) != float64(k) || m["Scalar"].(float64) != 0.6 || m["ActuatorType"] != "Vibrate" {
			t.Fatalf("motor %d: %+v", k, m)
		}
	}
	suc := sc[1]["Scalars"].([]any)
	if len(suc) != 1 || suc[0].(map[string]any)["Index"].(float64) != 2 || suc[0].(map[string]any)["ActuatorType"] != "Constrict" {
		t.Fatalf("suction to the pump only: %+v", suc)
	}
	rc := fake.commands("RotateCmd")
	if len(rc) != 1 {
		t.Fatalf("want 1 RotateCmd, got %d", len(rc))
	}
	r := rc[0]["Rotations"].([]any)[0].(map[string]any)
	if r["Speed"].(float64) != 0.6 || r["Clockwise"] != true {
		t.Fatalf("rotation follows vibration: %+v", r)
	}
}

func TestCapabilitiesOfNeo2AndMock(t *testing.T) {
	m := CapabilitiesOf(NewMock(false))
	if got := strings.Join(m.Channels(), ","); got != "vibration,suction" {
		t.Fatalf("mock channels %q", got)
	}
	for _, f := range m.Features {
		if !f.Driven {
			t.Fatalf("mock channels are driven: %+v", f)
		}
	}
	// A device without its own report falls back to the two classic channels.
	if got := strings.Join(CapabilitiesOf(bareDevice{}).Channels(), ","); got != "vibration,suction" {
		t.Fatalf("fallback channels %q", got)
	}
}

// bareDevice reports no capabilities of its own.
type bareDevice struct{}

func (bareDevice) Connect(context.Context) error { return nil }
func (bareDevice) Disconnect() error             { return nil }
func (bareDevice) SetVibration(float64) error    { return nil }
func (bareDevice) SetSuction(float64) error      { return nil }
func (bareDevice) Stop() error                   { return nil }

// Per-motor levels scale single motors (LELO F1S V3: motor 2 at 50 %).
func TestIntifaceMotorLevels(t *testing.T) {
	fake := &fakeButtplug{deviceMessages: map[string]any{
		"ScalarCmd": []any{
			map[string]any{"ActuatorType": "Vibrate"},
			map[string]any{"ActuatorType": "Vibrate"},
		},
		"RotateCmd":     []any{map[string]any{"StepCount": 20}},
		"StopDeviceCmd": map[string]any{},
	}}
	url, server := startFake(t, fake)
	defer server.Close()
	dev := NewIntiface(url)
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	if err := dev.Connect(ctx); err != nil {
		t.Fatal(err)
	}
	defer dev.Disconnect()
	var ms MotorLevelSetter = dev
	ms.SetMotorLevel("vibration2", 0.5)
	ms.SetMotorLevel("rotate", 2) // clamped to 1
	if err := dev.SetVibration(0.8); err != nil {
		t.Fatal(err)
	}
	time.Sleep(100 * time.Millisecond)
	sc := fake.commands("ScalarCmd")
	if len(sc) != 1 {
		t.Fatalf("want 1 ScalarCmd, got %d", len(sc))
	}
	v := sc[0]["Scalars"].([]any)
	if v[0].(map[string]any)["Scalar"].(float64) != 0.8 || v[1].(map[string]any)["Scalar"].(float64) != 0.4 {
		t.Fatalf("motor 1 full, motor 2 at half: %+v", v)
	}
	r := fake.commands("RotateCmd")[0]["Rotations"].([]any)[0].(map[string]any)
	if r["Speed"].(float64) != 0.8 {
		t.Fatalf("rotation level clamped to 1: %+v", r)
	}
}
