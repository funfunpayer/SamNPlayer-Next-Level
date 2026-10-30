#include <doctest/doctest.h>

#include <algorithm>

#include "device_fake_buttplug.h"
#include "samn/device/intiface.h"

using namespace samn::device;
using samn_test::FakeButtplug;
using json = nlohmann::json;
using namespace std::chrono_literals;

namespace {

bool Contains(const std::string& s, const std::string& needle) { return s.find(needle) != std::string::npos; }

bool Has(const std::vector<std::string>& v, const std::string& x) {
    return std::find(v.begin(), v.end(), x) != v.end();
}

// A connected device against a fake server.
struct Rig {
    FakeButtplug fake;
    IntifaceDevice dev;
    explicit Rig(json messages) : fake(std::move(messages)), dev(fake.transport, "ws://fake:12345") {}
    void Up() { REQUIRE(dev.Connect()); }
};

}  // namespace

// The core: handshake, device adoption and driving both channels.
TEST_CASE("device/intiface: connect and control") {  // Go: TestIntifaceConnectAndControl
    Rig r(FakeButtplug::StandardDeviceMessages());
    r.Up();
    ConnectionInfo info = r.dev.Info();
    CHECK(info.connected);
    CHECK(Contains(info.name, "Testgerät"));
    // The display says WHICH channels were found.
    CHECK(Contains(info.name, "Vibration"));
    CHECK(Contains(info.name, "Suction"));
    CHECK(Has(r.fake.Names(), "RequestServerInfo"));
    CHECK(Has(r.fake.Names(), "RequestDeviceList"));

    REQUIRE(r.dev.SetVibration(0.5));
    REQUIRE(r.dev.SetSuction(1.0));
    const auto cmds = r.fake.Commands("ScalarCmd");
    REQUIRE(cmds.size() >= 2);
    for (const auto& c : cmds) CHECK(c["DeviceIndex"] == 7);
}

// Byte-exact against what 0.7 sends (Go encoding/json of map[string]any:
// sorted keys, 1 not 1.0). Ids count from 1 per connection.
TEST_CASE("device/intiface: messages are byte-identical with 0.7") {  // new (parity, checked against Go json.Marshal)
    Rig r(FakeButtplug::StandardDeviceMessages());
    r.Up();
    REQUIRE(r.dev.SetVibration(0.5));
    REQUIRE(r.dev.SetSuction(1.0));
    REQUIRE(r.dev.Stop());
    const auto& s = r.fake.transport.sent;
    REQUIRE(s.size() == 5);
    CHECK(r.fake.transport.SentText(0) == R"([{"RequestServerInfo":{"ClientName":"SamNPlayer","Id":1,"MessageVersion":3}}])");
    CHECK(r.fake.transport.SentText(1) == R"([{"RequestDeviceList":{"Id":2}}])");
    CHECK(r.fake.transport.SentText(2) ==
          R"([{"ScalarCmd":{"DeviceIndex":7,"Id":3,"Scalars":[{"ActuatorType":"Vibrate","Index":0,"Scalar":0.5}]}}])");
    CHECK(r.fake.transport.SentText(3) ==
          R"([{"ScalarCmd":{"DeviceIndex":7,"Id":4,"Scalars":[{"ActuatorType":"Constrict","Index":1,"Scalar":1}]}}])");
    CHECK(r.fake.transport.SentText(4) == R"([{"StopDeviceCmd":{"DeviceIndex":7,"Id":5}}])");
    CHECK(BuildButtplugMessage("Ping", json::object(), 9) == R"([{"Ping":{"Id":9}}])");
}

// Values are clamped to 0-1: the server would refuse more, ending playback.
TEST_CASE("device/intiface: clamps values") {  // Go: TestIntifaceClampsValues
    Rig r(FakeButtplug::StandardDeviceMessages());
    r.Up();
    (void)r.dev.SetVibration(3.5);
    (void)r.dev.SetVibration(-2.0);
    (void)r.dev.SetVibration(std::nan(""));  // 0.7 would have broken the socket (NaN is not JSON)
    CHECK(r.dev.Connected());
    for (const auto& cmd : r.fake.Commands("ScalarCmd"))
        for (const auto& e : cmd["Scalars"]) {
            CHECK(e["Scalar"].get<double>() >= 0);
            CHECK(e["Scalar"].get<double>() <= 1);
        }
    CHECK(r.fake.Commands("ScalarCmd").size() == 3);
}

// Without a device, a clear message instead of silent nothing.
TEST_CASE("device/intiface: without a device connect fails") {  // Go: TestIntifaceWithoutDeviceFails
    Rig r(FakeButtplug::StandardDeviceMessages());
    r.fake.no_devices = true;
    Status s = r.dev.Connect();
    CHECK_FALSE(s);
    CHECK(Contains(s.message, "device"));
    CHECK_FALSE(r.fake.transport.IsConnected());
    // It scanned for a late device and stopped scanning.
    CHECK(Has(r.fake.Names(), "StartScanning"));
    CHECK(Has(r.fake.Names(), "StopScanning"));
}

// A vibration-only device is still usable, just without suction.
TEST_CASE("device/intiface: vibration-only device") {  // Go: TestIntifaceVibrationOnlyDevice
    Rig r({{"ScalarCmd", json::array({json{{"StepCount", 20}, {"ActuatorType", "Vibrate"}}})}});
    r.Up();
    CHECK(r.dev.SetSuction(0.8));  // missing suction channel is no error
    CHECK(r.dev.SetVibration(0.4));
    CHECK_FALSE(Contains(r.dev.Info().name, "Suction"));
    CHECK(r.fake.Commands("ScalarCmd").size() == 1);
}

// No server: the message must point at Intiface Central.
TEST_CASE("device/intiface: no server") {  // Go: TestIntifaceNoServer
    MockTransport t;
    t.connect_error = "dial tcp 127.0.0.1:1: connect: connection refused";
    IntifaceDevice dev(t, "ws://127.0.0.1:1");
    Status s = dev.Connect();
    CHECK_FALSE(s);
    CHECK(Contains(s.message, "Intiface"));
    CHECK(Contains(s.message, "no connection to ws://127.0.0.1:1"));
}

TEST_CASE("device/intiface: NormalizeIntifaceUrl") {  // Go: TestNormalizeIntifaceURL
    struct C { const char *in, *want; };
    for (C c : {C{"", "ws://127.0.0.1:12345"}, C{"   ", "ws://127.0.0.1:12345"},
                C{"192.168.1.50", "ws://192.168.1.50:12345"}, C{"192.168.1.50:12345", "ws://192.168.1.50:12345"},
                C{"192.168.1.50:9999", "ws://192.168.1.50:9999"},
                C{"ws://192.168.1.50:12345", "ws://192.168.1.50:12345"},
                C{"ws://192.168.1.50", "ws://192.168.1.50:12345"}, C{"ws://10.0.0.5:12345/", "ws://10.0.0.5:12345"},
                C{"handy.local", "ws://handy.local:12345"}}) {
        INFO(c.in);
        CHECK(NormalizeIntifaceUrl(c.in) == c.want);
    }
}

TEST_CASE("device/intiface: hint distinguishes local and network") {  // Go: TestIntifaceHintDistinguishesLocalAndNetwork
    const std::string refused = "dial tcp: connection refused";
    const std::string local = IntifaceHint("ws://127.0.0.1:12345", refused);
    CHECK(Contains(local, "Start the server"));
    CHECK_FALSE(Contains(local, "Wi-Fi"));
    CHECK(Contains(IntifaceHint("ws://192.168.1.50:12345", refused), "network"));
    CHECK(Contains(IntifaceHint("ws://192.168.1.50:12345", "dial tcp: i/o timeout"), "Wi-Fi"));
    CHECK(Contains(IntifaceHint("ws://handy.local:12345", "no such host"), "IP address"));
    CHECK(Contains(IntifaceHint("ws://127.0.0.1:1", "x"), "Intiface Central running"));
    CHECK(Contains(IntifaceHint("ws://h:1", "x"), "check the address"));
}

// After the server drops the socket, Info must not stay "connected".
TEST_CASE("device/intiface: marks dead on write failure") {  // Go: TestIntifaceMarksDeadOnWriteFailure
    Rig r(FakeButtplug::StandardDeviceMessages());
    r.Up();
    CHECK(r.dev.Info().connected);
    r.fake.transport.Drop();
    r.fake.transport.connect_error = "connection refused";  // block the one-shot reconnect
    Status s = r.dev.SetVibration(0.4);
    CHECK_FALSE(s);
    CHECK_FALSE(r.dev.Info().connected);
    CHECK_FALSE(r.dev.SetSuction(0.2));  // one-shot spent / server gone
    (void)r.dev.Disconnect();
}

// One-shot reconnect: after a dropped socket the next command dials again once.
TEST_CASE("device/intiface: one-shot reconnect") {  // Go: TestIntifaceOneShotReconnect
    Rig r(FakeButtplug::StandardDeviceMessages());
    r.Up();
    r.fake.transport.Drop();
    CHECK_FALSE(r.dev.PumpIncoming());  // the read side notices the drop
    CHECK_FALSE(r.dev.Info().connected);
    REQUIRE(r.dev.SetVibration(0.35));
    CHECK(r.dev.Info().connected);
    CHECK(r.dev.SetSuction(0.2));
    CHECK(r.fake.transport.connect_count == 2);
    // Ids restart with the new connection.
    CHECK(Contains(r.fake.transport.SentText(r.fake.transport.sent.size() - 2),
                   R"("ScalarCmd":{"DeviceIndex":7,"Id":3,)"));
}

// Go: TestIntifacePingStopReplacedOnReconnect checks goroutine / channel
// internals (stopPing) that do not exist here: the app owns the ping timer.
// Adapted: a failed Ping reconnects once, and Ping works again afterwards.
TEST_CASE("device/intiface: ping survives a one-shot reconnect") {  // Go: TestIntifacePingStopReplacedOnReconnect (adapted)
    Rig r(FakeButtplug::StandardDeviceMessages());
    r.Up();
    REQUIRE(r.dev.Ping());
    r.fake.transport.Drop();
    REQUIRE(r.dev.Ping());  // send fails, reconnects
    CHECK(r.dev.Connected());
    CHECK(r.fake.transport.connect_count == 2);
    const std::size_t n = r.fake.transport.sent.size();
    REQUIRE(r.dev.Ping());
    CHECK(r.fake.transport.sent.size() == n + 1);
    CHECK(Contains(r.fake.transport.SentText(n), R"([{"Ping":{"Id":)"));
}

// A failed one-shot spends the budget.
TEST_CASE("device/intiface: one-shot reconnect is spent after a failure") {  // Go: TestIntifaceOneShotReconnectSpent
    Rig r(FakeButtplug::StandardDeviceMessages());
    r.Up();
    r.fake.transport.Drop();
    CHECK_FALSE(r.dev.PumpIncoming());
    r.fake.transport.connect_error = "connection refused";
    Status why;
    CHECK_FALSE(r.dev.TryReconnectOnce(&why));
    CHECK_FALSE(r.dev.TryReconnectOnce(&why));
    CHECK(why.message == "intiface: one-shot reconnect already used");
    r.fake.transport.connect_error.clear();
    CHECK_FALSE(r.dev.TryReconnectOnce());  // even a reachable server: budget spent
    CHECK_FALSE(r.dev.Info().connected);
    // An explicit Connect works again and restores the budget.
    REQUIRE(r.dev.Connect());
    CHECK(r.dev.Info().connected);
}

TEST_CASE("device/intiface: battery level") {  // Go: TestIntifaceBatteryLevelLocked
    Rig r(FakeButtplug::MessagesWithBattery());
    r.Up();
    const auto listed = r.dev.ListedChannels();
    CHECK(listed.vibration);
    CHECK(listed.suction);
    CHECK(listed.battery);
    BatteryReading b = r.dev.BatteryLevel();
    CHECK(b.ok);
    CHECK(b.percent == 73);
    // The cached path must not touch the wire while the TTL holds.
    const std::size_t before = r.fake.Names().size();
    b = r.dev.BatteryLevel();
    CHECK(b.ok);
    CHECK(b.percent == 73);
    CHECK(r.fake.Names().size() == before);
    CHECK(Has(r.fake.Names(), "BatteryLevelCmd"));
    CHECK(r.dev.Info().battery_ok);
}

TEST_CASE("device/intiface: battery cache expires, no battery listed, no reading") {  // new
    auto now = std::chrono::steady_clock::time_point{};
    FakeButtplug fake(FakeButtplug::MessagesWithBattery());
    IntifaceDevice dev(fake.transport, "ws://x", [&now] { return now; });
    REQUIRE(dev.Connect());
    REQUIRE(dev.BatteryLevel().ok);
    now += 31s;
    const std::size_t n = fake.Commands("BatteryLevelCmd").size();
    REQUIRE(dev.BatteryLevel().ok);
    CHECK(fake.Commands("BatteryLevelCmd").size() == n + 1);

    Rig plain(FakeButtplug::StandardDeviceMessages());
    plain.Up();
    CHECK_FALSE(plain.dev.BatteryLevel().ok);  // not listed: not even asked
    CHECK(plain.fake.Commands("BatteryLevelCmd").empty());

    // A server that never answers: no reading within the wait.
    FakeButtplug mute(FakeButtplug::MessagesWithBattery());
    IntifaceDevice d2(mute.transport, "ws://x");
    REQUIRE(d2.Connect());
    mute.transport.on_send = nullptr;  // from now on no replies
    CHECK_FALSE(d2.BatteryLevel().ok);
}

// A server that never answers the battery read (or refuses it) is asked
// again only after the cache time, like a successful read: the wait must not
// hold up the device thread at every status tick.
TEST_CASE("device/intiface: a failed battery read is cached too") {  // new
    auto now = std::chrono::steady_clock::time_point{};
    FakeButtplug mute(FakeButtplug::MessagesWithBattery());
    IntifaceDevice dev(mute.transport, "ws://x", [&now] { return now; });
    REQUIRE(dev.Connect());
    mute.transport.on_send = nullptr;  // from now on no replies (and nothing recorded by the fake)
    const auto asked = [&mute] {
        int n = 0;
        for (std::size_t i = 0; i < mute.transport.sent.size(); ++i)
            if (Contains(mute.transport.SentText(i), "BatteryLevelCmd")) ++n;
        return n;
    };
    CHECK_FALSE(dev.BatteryLevel().ok);
    CHECK(asked() == 1);
    now += 10s;
    CHECK_FALSE(dev.BatteryLevel().ok);
    CHECK(asked() == 1);  // not asked again
    now += 21s;
    CHECK_FALSE(dev.BatteryLevel().ok);
    CHECK(asked() == 2);  // the cache expired

    // The server answers the request with an Error: the wait ends there, a
    // later reading is not taken for it, and the failure is cached.
    FakeButtplug refusing(FakeButtplug::MessagesWithBattery());
    IntifaceDevice d2(refusing.transport, "ws://x", [&now] { return now; });
    REQUIRE(d2.Connect());
    refusing.transport.on_send = [&refusing](const Bytes& data) {
        const json batch = json::parse(ToText(data));
        const json id = batch.at(0).begin().value().at("Id");
        refusing.transport.PushText(
            json::array({json{{"Error", {{"Id", id}, {"ErrorCode", 4}, {"ErrorMessage", "no battery"}}}}}).dump());
        refusing.transport.PushText(R"([{"BatteryLevelReading":{"Id":0,"DeviceIndex":7,"BatteryLevel":0.5}}])");
    };
    CHECK_FALSE(d2.BatteryLevel().ok);
    CHECK_FALSE(d2.Info().battery_ok);
    CHECK(refusing.transport.Receive(0ms).kind == ReceiveResult::Kind::kMessage);  // the reading is still queued
    const std::size_t sent = refusing.transport.sent.size();
    CHECK_FALSE(d2.BatteryLevel().ok);
    CHECK(refusing.transport.sent.size() == sent);  // cached
}

// Go: TestIntifaceDrainsServerReplies tests that a goroutine keeps reading so
// the socket buffer never fills. Adapted: PumpIncoming drains any number of
// unsolicited replies and the device keeps working.
TEST_CASE("device/intiface: PumpIncoming drains server replies") {  // Go: TestIntifaceDrainsServerReplies (adapted)
    Rig r(FakeButtplug::StandardDeviceMessages());
    r.Up();
    for (int i = 0; i < 5000; ++i) r.fake.transport.PushText(R"([{"Ok":{"Id":0}}])");
    CHECK(r.dev.PumpIncoming());
    CHECK(r.fake.transport.Receive(0ms).kind == ReceiveResult::Kind::kTimeout);
    REQUIRE(r.dev.SetVibration(0.5));
    CHECK(r.dev.Info().connected);
}

// A second channel adopted from Oscillate/Inflate must be commanded with that
// actuator type; Buttplug rejects a mismatch.
TEST_CASE("device/intiface: suction uses the adopted actuator type") {  // Go: TestIntifaceSuctionUsesAdoptedActuatorType
    Rig r({{"ScalarCmd", json::array({json{{"StepCount", 20}, {"ActuatorType", "Vibrate"}},
                                      json{{"StepCount", 20}, {"ActuatorType", "Oscillate"}}})},
           {"StopDeviceCmd", json::object()}});
    r.Up();
    REQUIRE(r.dev.SetSuction(0.5));
    const auto cmds = r.fake.Commands("ScalarCmd");
    REQUIRE(!cmds.empty());
    const json scalar = cmds.back()["Scalars"][0];
    CHECK(scalar["ActuatorType"] == "Oscillate");
    CHECK(scalar["Index"] == 1);
}

// The Handy over Intiface lists LinearCmd only: it connects and reports its stroke channel.
TEST_CASE("device/intiface: adopts a stroker") {  // Go: TestIntifaceAdoptsStroker
    Rig r({{"LinearCmd", json::array({json{{"StepCount", 100}, {"ActuatorType", "Position"}}})},
           {"StopDeviceCmd", json::object()}});
    r.Up();
    const Capabilities caps = CapabilitiesOf(r.dev);
    CHECK(caps.Has(kChStroke));
    CHECK_FALSE(caps.Has(kChVibration));
    CHECK(caps.features[0].kind == kKindLinear);
    CHECK(caps.features[0].driven);
    CHECK(r.dev.SetVibration(0.5));  // no vibration motor is no error
    const std::string name = r.dev.Info().name;
    CHECK(Contains(name, "Stroke"));
    CHECK_FALSE(Contains(name, "not driven"));
    CHECK(r.dev.HasLinear());
    REQUIRE(r.dev.SetPosition(0.8, 250ms));
    REQUIRE(r.dev.SetPosition(1.7, 0ms));
    const auto cmds = r.fake.Commands("LinearCmd");
    REQUIRE(cmds.size() == 2);
    json v = cmds[0]["Vectors"][0];
    CHECK(v["Index"] == 0);
    CHECK(v["Duration"] == 250);
    CHECK(v["Position"].get<double>() == 0.8);
    v = cmds[1]["Vectors"][0];
    CHECK(v["Position"].get<double>() == 1);
    CHECK(v["Duration"].get<int>() >= 1);
    CHECK(r.fake.Commands("ScalarCmd").empty());  // a stroker gets no ScalarCmd
    CHECK(r.fake.transport.SentText(2) ==
          R"([{"LinearCmd":{"DeviceIndex":7,"Id":3,"Vectors":[{"Duration":250,"Index":0,"Position":0.8}]}}])");
}

// LELO F1S V3: two Vibrate motors both follow the vibration curve in one
// ScalarCmd. Lovense Max 2: Vibrate + Constrict, suction goes to the pump.
TEST_CASE("device/intiface: drives every motor") {  // Go: TestIntifaceDrivesEveryMotor
    Rig r({{"ScalarCmd", json::array({json{{"ActuatorType", "Vibrate"}}, json{{"ActuatorType", "Vibrate"}},
                                      json{{"ActuatorType", "Constrict"}}})},
           {"RotateCmd", json::array({json{{"StepCount", 20}}})},
           {"StopDeviceCmd", json::object()}});
    r.Up();
    CHECK_FALSE(r.dev.HasLinear());
    REQUIRE(r.dev.SetVibration(0.6));
    REQUIRE(r.dev.SetSuction(0.3));
    const auto sc = r.fake.Commands("ScalarCmd");
    REQUIRE(sc.size() == 2);
    const json vib = sc[0]["Scalars"];
    REQUIRE(vib.size() == 2);  // both motors in one command
    for (int k = 0; k < 2; ++k) {
        CHECK(vib[k]["Index"] == k);
        CHECK(vib[k]["Scalar"].get<double>() == 0.6);
        CHECK(vib[k]["ActuatorType"] == "Vibrate");
    }
    const json suc = sc[1]["Scalars"];
    REQUIRE(suc.size() == 1);  // suction to the pump only
    CHECK(suc[0]["Index"] == 2);
    CHECK(suc[0]["ActuatorType"] == "Constrict");
    const auto rc = r.fake.Commands("RotateCmd");
    REQUIRE(rc.size() == 1);
    const json rot = rc[0]["Rotations"][0];
    CHECK(rot["Speed"].get<double>() == 0.6);
    CHECK(rot["Clockwise"] == true);
}

// Per-motor levels scale single motors (LELO F1S V3: motor 2 at 50 %).
TEST_CASE("device/intiface: motor levels") {  // Go: TestIntifaceMotorLevels
    Rig r({{"ScalarCmd", json::array({json{{"ActuatorType", "Vibrate"}}, json{{"ActuatorType", "Vibrate"}}})},
           {"RotateCmd", json::array({json{{"StepCount", 20}}})},
           {"StopDeviceCmd", json::object()}});
    r.Up();
    MotorLevelSetter& ms = r.dev;
    ms.SetMotorLevel("vibration2", 0.5);
    ms.SetMotorLevel("rotate", 2);  // clamped to 1
    REQUIRE(r.dev.SetVibration(0.8));
    const auto sc = r.fake.Commands("ScalarCmd");
    REQUIRE(sc.size() == 1);
    CHECK(sc[0]["Scalars"][0]["Scalar"].get<double>() == 0.8);
    CHECK(sc[0]["Scalars"][1]["Scalar"].get<double>() == 0.4);  // motor 2 at half
    CHECK(r.fake.Commands("RotateCmd")[0]["Rotations"][0]["Speed"].get<double>() == 0.8);
}

TEST_CASE("device/intiface: handshake errors") {  // new (Go readUntil)
    {   // server reports an error
        MockTransport t;
        t.on_send = [&t](const Bytes&) { t.PushText(R"([{"Error":{"Id":1,"ErrorMessage":"nope","ErrorCode":2}}])"); };
        IntifaceDevice dev(t, "ws://x");
        Status s = dev.Connect();
        CHECK_FALSE(s);
        CHECK(s.message == "intiface: server reported error: nope");
        CHECK_FALSE(t.IsConnected());
    }
    {   // silence: the read times out
        MockTransport t;
        IntifaceDevice dev(t, "ws://x");
        Status s = dev.Connect();
        CHECK_FALSE(s);
        CHECK(Contains(s.message, "response not readable"));
    }
    {   // not a JSON array
        MockTransport t;
        t.on_send = [&t](const Bytes&) { t.PushText("hello"); };
        IntifaceDevice dev(t, "ws://x");
        CHECK_FALSE(dev.Connect());
    }
}

TEST_CASE("device/intiface: device that arrives while scanning is adopted") {  // new (Go Connect scan step)
    FakeButtplug fake(FakeButtplug::StandardDeviceMessages());
    fake.no_devices = true;
    fake.transport.on_connect = nullptr;
    auto base = fake.transport.on_send;
    fake.transport.on_send = [&](const Bytes& d) {
        base(d);
        if (ToText(d).find("StartScanning") != std::string::npos)
            fake.transport.PushText(
                R"([{"DeviceAdded":{"DeviceIndex":3,"DeviceName":"Late","DeviceMessages":{"ScalarCmd":[{"ActuatorType":"Vibrate"}]}}}])");
    };
    IntifaceDevice dev(fake.transport, "ws://x");
    REQUIRE(dev.Connect());
    CHECK(dev.DeviceCapabilities().device == "Late");
    REQUIRE(dev.SetVibration(1));
    CHECK(fake.Commands("ScalarCmd")[0]["DeviceIndex"] == 3);
}

TEST_CASE("device/intiface: disconnect stops the device first") {  // new (Go Disconnect)
    Rig r(FakeButtplug::StandardDeviceMessages());
    r.Up();
    REQUIRE(r.dev.Disconnect());
    CHECK(Contains(r.fake.transport.SentText(r.fake.transport.sent.size() - 1), "StopDeviceCmd"));
    CHECK_FALSE(r.fake.transport.IsConnected());
    CHECK_FALSE(r.dev.Info().connected);
    CHECK(r.dev.Stop());  // not connected: nothing to do
}
