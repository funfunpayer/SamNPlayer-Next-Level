#pragma once

// Device capabilities (Go device/capabilities.go): every connection says
// which channels the device has, so the Player shows exactly those.

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "samn/device/device.h"

namespace samn::device {

// Channel ids. They match the axis / lane names the GUI uses.
inline constexpr const char* kChStroke = "stroke";        // linear position (L0)
inline constexpr const char* kChSurge = "surge";          // L1
inline constexpr const char* kChSway = "sway";            // L2
inline constexpr const char* kChTwist = "twist";          // R0
inline constexpr const char* kChRoll = "roll";            // R1
inline constexpr const char* kChPitch = "pitch";          // R2
inline constexpr const char* kChVibration = "vibration";  // vibration motor(s)
inline constexpr const char* kChSuction = "suction";      // suction / constrict
inline constexpr const char* kChRotate = "rotate";        // rotation
inline constexpr const char* kChOscillate = "oscillate";  // intensity-only oscillation

// How a channel is driven.
inline constexpr const char* kKindScalar = "scalar";  // intensity 0-1 (Buttplug ScalarCmd)
inline constexpr const char* kKindLinear = "linear";  // position + duration (LinearCmd, TCode)
inline constexpr const char* kKindRotate = "rotate";  // speed + direction (RotateCmd)

struct Feature {
    std::string channel;   // a 2nd vibration motor is "vibration2"
    std::string kind;      // kKind*
    int index = 0;         // transport index (Buttplug feature index)
    std::string actuator;  // Buttplug ActuatorType / TCode axis, if any
    // true = the Player sends this channel; false = the device has it but we
    // do not drive it (a scalar "Position" actuator).
    bool driven = false;
};

struct Capabilities {
    std::string device;
    std::vector<Feature> features;
    bool battery = false;

    bool Has(const std::string& channel) const;
    std::vector<std::string> Channels() const;  // in device order
};

// Implemented by devices that know their channels.
class CapabilityReporter {
public:
    virtual ~CapabilityReporter() = default;
    virtual Capabilities DeviceCapabilities() const = 0;
};

// Vibration + suction, both driven (Sam Neo 2 and the Mock).
Capabilities Neo2Capabilities(const std::string& name, bool battery);

// What d reports, or the two classic channels for a device that does not.
Capabilities CapabilitiesOf(const Device& d);

// Maps the `DeviceMessages` block of a Buttplug device to features:
//  - every Vibrate motor follows the vibration curve (LELO F1S: both);
//  - the first Constrict / Inflate / Oscillate (the one at constrict_idx)
//    follows the suction curve;
//  - other Oscillate and Rotate scalars and every RotateCmd follow the
//    vibration curve as intensity;
//  - the first LinearCmd gets the script's stroke positions.
// A scalar "Position" actuator is listed but not driven.
std::vector<Feature> ButtplugFeatures(const nlohmann::json& messages, int constrict_idx);

// JSON as 0.7 sends it to the GUI: struct key order, `actuator` only when set.
std::string ToJson(const Feature& f);
std::string ToJson(const Capabilities& c);
std::string ToJson(const ConnectionInfo& info);

}  // namespace samn::device
