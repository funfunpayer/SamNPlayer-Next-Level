#include <doctest/doctest.h>

#include "device_fake_buttplug.h"
#include "samn/device/capabilities.h"
#include "samn/device/mock.h"

using namespace samn::device;
using json = nlohmann::json;

namespace {

std::string Join(const std::vector<std::string>& v, const std::string& sep) {
    std::string out;
    for (const auto& s : v) out += (out.empty() ? "" : sep) + s;
    return out;
}

// A device that reports no capabilities of its own (Go: bareDevice).
class BareDevice : public Device {
public:
    Status Connect() override { return Status::Ok(); }
    Status Disconnect() override { return Status::Ok(); }
    Status SetVibration(double) override { return Status::Ok(); }
    Status SetSuction(double) override { return Status::Ok(); }
    Status Stop() override { return Status::Ok(); }
};

}  // namespace

TEST_CASE("device/capabilities: Buttplug features, Neo 2 like") {  // Go: TestButtplugFeaturesNeo2Like
    const auto f = ButtplugFeatures(samn_test::FakeButtplug::StandardDeviceMessages(), 1);
    REQUIRE(f.size() == 2);
    CHECK(f[0].channel == kChVibration);
    CHECK(f[0].driven);
    CHECK(f[1].channel == kChSuction);
    CHECK(f[1].driven);
}

TEST_CASE("device/capabilities: Buttplug features, multi axis") {  // Go: TestButtplugFeaturesMultiAxis
    const json msgs = {
        {"ScalarCmd", json::array({json{{"ActuatorType", "Vibrate"}}, json{{"ActuatorType", "Vibrate"}},
                                   json{{"ActuatorType", "Rotate"}}, json{{"ActuatorType", "Unknown"}}})},
        {"LinearCmd", json::array({json{{"StepCount", 100}}})},
        {"RotateCmd", json::array({json{{"StepCount", 20}}})}};
    const auto f = ButtplugFeatures(msgs, -1);
    std::vector<std::string> got;
    for (const auto& x : f) got.push_back(x.channel + ":" + x.kind);
    CHECK(Join(got, " ") == "vibration:scalar vibration2:scalar rotate:scalar stroke:linear rotate2:rotate");
    // D2 drives every motor, rotation and the stroke.
    for (const auto& x : f) CHECK(x.driven);
}

TEST_CASE("device/capabilities: a scalar Position actuator is listed but not driven") {  // new
    const json msgs = {{"ScalarCmd", json::array({json{{"ActuatorType", "Position"}}})}};
    const auto f = ButtplugFeatures(msgs, -1);
    REQUIRE(f.size() == 1);
    CHECK(f[0].channel == kChStroke);
    CHECK_FALSE(f[0].driven);
}

TEST_CASE("device/capabilities: Oscillate picked as suction, a later Constrict is listed but not driven") {  // new
    // 0.7 picks the first of Constrict / Oscillate / Inflate for suction.
    const json msgs = {{"ScalarCmd", json::array({json{{"ActuatorType", "Vibrate"}},
                                                  json{{"ActuatorType", "Oscillate"}},
                                                  json{{"ActuatorType", "Constrict"}}})}};
    const auto f = ButtplugFeatures(msgs, 1);
    REQUIRE(f.size() == 3);
    CHECK(f[1].channel == kChSuction);
    CHECK(f[1].driven);
    CHECK(f[2].channel == "suction2");
    CHECK_FALSE(f[2].driven);
}

TEST_CASE("device/capabilities: Neo 2 and Mock") {  // Go: TestCapabilitiesOfNeo2AndMock
    MockDevice mock(false);
    const Capabilities m = CapabilitiesOf(mock);
    CHECK(Join(m.Channels(), ",") == "vibration,suction");
    for (const auto& f : m.features) CHECK(f.driven);
    CHECK(m.device == "Mock device");
    // A device without its own report falls back to the two classic channels.
    BareDevice bare;
    CHECK(Join(CapabilitiesOf(bare).Channels(), ",") == "vibration,suction");
}

TEST_CASE("device/capabilities: JSON as 0.7 sends it to the GUI") {  // new
    CHECK(ToJson(Neo2Capabilities("Sam Neo 2", true)) ==
          R"({"device":"Sam Neo 2","features":[{"channel":"vibration","kind":"scalar","index":0,"driven":true},)"
          R"({"channel":"suction","kind":"scalar","index":1,"driven":true}],"battery":true})");
    Feature f{"stroke", "linear", 0, "L0", true};
    CHECK(ToJson(f) == R"({"channel":"stroke","kind":"linear","index":0,"actuator":"L0","driven":true})");
    ConnectionInfo info;
    info.connected = true;
    info.name = "x";
    CHECK(ToJson(info) ==
          R"({"Connected":true,"Name":"x","Address":"","RSSI":0,"BatteryPct":0,"BatteryOK":false})");
}
