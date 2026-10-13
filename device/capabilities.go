package device

// Device capabilities (docs/DEVICE_SUPPORT.md, step D1): every connection
// says which channels the device has, so the Player can show exactly those.
// The Sam Neo 2 stays first-class; strokers (The Handy, OSR2 / SR6),
// multi-motor vibrators and rotating devices report their own channels.

// Channel ids. They match the axis / lane names the GUI uses.
const (
	ChStroke    = "stroke"    // linear position (L0)
	ChSurge     = "surge"     // L1
	ChSway      = "sway"      // L2
	ChTwist     = "twist"     // R0
	ChRoll      = "roll"      // R1
	ChPitch     = "pitch"     // R2
	ChVibration = "vibration" // vibration motor(s)
	ChSuction   = "suction"   // suction / constrict
	ChRotate    = "rotate"    // rotation
	ChOscillate = "oscillate" // oscillation (stroke-like motor, intensity only)
)

// Feature kinds: how a channel is driven.
const (
	KindScalar = "scalar" // intensity 0–1 (Buttplug ScalarCmd)
	KindLinear = "linear" // position + duration (Buttplug LinearCmd, TCode L0)
	KindRotate = "rotate" // speed + direction (Buttplug RotateCmd)
)

// Feature is one channel of a device.
type Feature struct {
	Channel  string `json:"channel"`            // see Ch* (a 2nd vibration motor is "vibration2")
	Kind     string `json:"kind"`               // see Kind*
	Index    int    `json:"index"`              // transport index (Buttplug feature index)
	Actuator string `json:"actuator,omitempty"` // Buttplug ActuatorType, if any
	// Driven: the Player sends this channel today. False = the device has
	// it but SamNPlayer does not drive it yet (D2 adds linear / rotate).
	Driven bool `json:"driven"`
}

// Capabilities describes a connected device.
type Capabilities struct {
	Device   string    `json:"device"`
	Features []Feature `json:"features"`
	Battery  bool      `json:"battery"`
}

// Has reports whether the device has the channel (driven or not).
func (c Capabilities) Has(channel string) bool {
	for _, f := range c.Features {
		if f.Channel == channel {
			return true
		}
	}
	return false
}

// Channels lists the channel ids in device order.
func (c Capabilities) Channels() []string {
	out := make([]string, 0, len(c.Features))
	for _, f := range c.Features {
		out = append(out, f.Channel)
	}
	return out
}

// CapabilityReporter is implemented by devices that know their channels.
type CapabilityReporter interface {
	DeviceCapabilities() Capabilities
}

// neo2Capabilities: vibration + suction, both driven (BLE and Mock).
func neo2Capabilities(name string, battery bool) Capabilities {
	return Capabilities{
		Device: name,
		Features: []Feature{
			{Channel: ChVibration, Kind: KindScalar, Driven: true},
			{Channel: ChSuction, Kind: KindScalar, Index: 1, Driven: true},
		},
		Battery: battery,
	}
}

// CapabilitiesOf returns what d reports, or the two classic channels for a
// device that does not report (every Device has SetVibration / SetSuction).
func CapabilitiesOf(d Device) Capabilities {
	if r, ok := d.(CapabilityReporter); ok {
		return r.DeviceCapabilities()
	}
	return neo2Capabilities("", false)
}

// DeviceCapabilities: the Sam Neo 2 has vibration and suction.
func (s *SamNeo2) DeviceCapabilities() Capabilities {
	info := s.Info()
	// Cached battery state only: a capability query must not start a BLE read.
	s.mu.Lock()
	battery := s.batteryChecked && s.batteryOK
	s.mu.Unlock()
	return neo2Capabilities(info.Name, battery)
}

// DeviceCapabilities: the Mock stands in for a Sam Neo 2.
func (m *Mock) DeviceCapabilities() Capabilities {
	return neo2Capabilities("Mock device", false)
}

// buttplugFeatures maps a Buttplug DeviceMessages block to features (D2):
//   - every Vibrate motor follows the vibration curve (LELO F1S: both);
//   - the first Constrict / Inflate (or the Oscillate picked as suction)
//     follows the suction curve;
//   - other Oscillate and Rotate scalars and every RotateCmd follow the
//     vibration curve as intensity;
//   - the first LinearCmd gets the script's stroke positions.
//
// A scalar "Position" actuator is listed but not driven.
func buttplugFeatures(messages map[string]any, constrictIdx int) []Feature {
	var out []Feature
	count := map[string]int{}
	name := func(ch string) string {
		count[ch]++
		if count[ch] == 1 {
			return ch
		}
		return ch + string(rune('0'+count[ch]))
	}
	scalars, _ := messages["ScalarCmd"].([]any)
	for idx, entry := range scalars {
		feature, _ := entry.(map[string]any)
		actuator, _ := feature["ActuatorType"].(string)
		var ch string
		switch actuator {
		case "Vibrate":
			ch = ChVibration
		case "Constrict", "Inflate":
			ch = ChSuction
		case "Oscillate":
			if idx == constrictIdx {
				ch = ChSuction
			} else {
				ch = ChOscillate
			}
		case "Rotate":
			ch = ChRotate
		case "Position":
			ch = ChStroke
		default:
			continue
		}
		driven := ch != ChStroke && (ch != ChSuction || idx == constrictIdx)
		out = append(out, Feature{
			Channel: name(ch), Kind: KindScalar, Index: idx, Actuator: actuator, Driven: driven,
		})
	}
	linears, _ := messages["LinearCmd"].([]any)
	for idx := range linears {
		out = append(out, Feature{Channel: name(ChStroke), Kind: KindLinear, Index: idx, Driven: idx == 0})
	}
	rotates, _ := messages["RotateCmd"].([]any)
	for idx := range rotates {
		out = append(out, Feature{Channel: name(ChRotate), Kind: KindRotate, Index: idx, Driven: true})
	}
	return out
}
