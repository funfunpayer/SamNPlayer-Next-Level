#include <doctest/doctest.h>

#include <algorithm>
#include <utility>

#include "device_fake_buttplug.h"
#include "samn/device/battery.h"
#include "samn/device/capabilities.h"
#include "samn/device/driver.h"
#include "samn/device/intiface.h"
#include "samn/device/mock.h"
#include "samn/device/protocol.h"

using namespace samn::device;
using samn_test::FakeButtplug;
using namespace std::chrono_literals;

namespace {

bool Contains(const std::string& s, const std::string& needle) { return s.find(needle) != std::string::npos; }

DriverRegistry Builtin() {
    DriverRegistry registry;
    RegisterBuiltinDrivers(registry);
    return registry;
}

const Driver& Get(const DriverRegistry& registry, const std::string& id) {
    const Driver* d = registry.Find(id);
    REQUIRE(d);
    return *d;
}

// A driver of its own, for the registry rules.
struct NamedDriver : Driver {
    DriverInfo info;
    explicit NamedDriver(std::string id) { info.id = std::move(id); }
    const DriverInfo& Info() const override { return info; }
    Status Resolve(const std::string&, TransportRequest* out) const override {
        *out = {};
        return Status::Ok();
    }
    std::unique_ptr<DeviceSession> Create(Transport*, const std::string&, const SessionOptions&) const override {
        return nullptr;
    }
};

}  // namespace

TEST_CASE("device/driver: the built-in drivers in the order of the device choice") {  // new
    const DriverRegistry registry = Builtin();
    std::vector<std::string> ids;
    for (const DriverInfo& d : registry.List()) {
        ids.push_back(d.id);
        CHECK_FALSE(d.name.empty());
        CHECK_FALSE(d.help.empty());
    }
    CHECK(ids == std::vector<std::string>{"ble", "tcode", "tcode-serial", "intiface", "mock"});

    CHECK(Get(registry, "ble").Info().transport == TransportKind::kBle);
    CHECK(Get(registry, "tcode").Info().transport == TransportKind::kSocketLine);
    CHECK(Get(registry, "tcode-serial").Info().transport == TransportKind::kSerialLine);
    CHECK(Get(registry, "intiface").Info().transport == TransportKind::kWebSocket);
    CHECK(Get(registry, "mock").Info().transport == TransportKind::kNone);

    // Only the typed targets get an address field.
    CHECK(Get(registry, "tcode").Info().needs_address);
    CHECK(Get(registry, "intiface").Info().needs_address);
    CHECK_FALSE(Get(registry, "ble").Info().needs_address);
    CHECK_FALSE(Get(registry, "tcode-serial").Info().needs_address);
    CHECK_FALSE(Get(registry, "mock").Info().needs_address);

    // The 0.7 settings keys of the last target, unchanged.
    CHECK(Get(registry, "tcode").Info().target_setting == "device.tcodeAddress");
    CHECK(Get(registry, "tcode-serial").Info().target_setting == "device.tcodeSerialPort");
    CHECK(Get(registry, "intiface").Info().target_setting == "device.intifaceUrl");
    CHECK(Get(registry, "ble").Info().target_setting.empty());
    CHECK(Get(registry, "mock").Info().target_setting.empty());

    // 0.7 testConnectTimeout: 20 s, Intiface 30 s.
    CHECK(Get(registry, "ble").Info().connect_timeout_ms == 20000);
    CHECK(Get(registry, "intiface").Info().connect_timeout_ms == 30000);
    CHECK(Get(registry, "mock").Info().simulated);
    CHECK_FALSE(Get(registry, "ble").Info().simulated);
}

TEST_CASE("device/driver: an unknown id finds nothing") {  // new
    const DriverRegistry registry = Builtin();
    CHECK(registry.Find("bogus") == nullptr);
    CHECK(registry.Find("") == nullptr);
    CHECK(registry.Find("BLE") == nullptr);  // ids are exact
    CHECK(DriverRegistry().Find("ble") == nullptr);
    CHECK(DriverRegistry().List().empty());
}

TEST_CASE("device/driver: a second driver with a listed id is refused") {  // new
    DriverRegistry registry = Builtin();
    const std::size_t n = registry.List().size();
    const Status s = registry.Add(std::make_unique<NamedDriver>("mock"));
    CHECK_FALSE(s);
    CHECK(Contains(s.message, "\"mock\""));
    CHECK(registry.List().size() == n);
    // The first one stays.
    CHECK(Get(registry, "mock").Info().simulated);

    CHECK_FALSE(registry.Add(nullptr));
    CHECK_FALSE(registry.Add(std::make_unique<NamedDriver>("")));
    CHECK(registry.List().size() == n);

    REQUIRE(registry.Add(std::make_unique<NamedDriver>("extra")));
    CHECK(registry.List().back().id == "extra");  // added last, listed last
}

TEST_CASE("device/driver: TCode addresses resolve to UDP, TCP or the serial port") {  // new
    const DriverRegistry registry = Builtin();
    const Driver& net = Get(registry, "tcode");
    struct C { const char *in, *network, *hostport; };
    for (C c : {C{"192.168.1.50", "udp", "192.168.1.50:8000"}, C{"udp://10.0.0.2:9000", "udp", "10.0.0.2:9000"},
                C{"tcp://osr.local:8000", "tcp", "osr.local:8000"}, C{"tcp://10.0.0.2", "tcp", "10.0.0.2:8000"}}) {
        INFO(c.in);
        TransportRequest r;
        REQUIRE(net.Resolve(c.in, &r));
        CHECK(r.kind == TransportKind::kSocketLine);
        CHECK(r.network == c.network);
        CHECK(r.target == c.hostport);
    }
    for (const char* bad : {"", "http://x", "udp://host:abc"}) {
        INFO(bad);
        TransportRequest r;
        const Status s = net.Resolve(bad, &r);
        CHECK_FALSE(s);
        CHECK_FALSE(s.message.empty());  // shown to the user
    }

    TransportRequest serial;
    REQUIRE(Get(registry, "tcode-serial").Resolve("/dev/ttyUSB0", &serial));
    CHECK(serial.kind == TransportKind::kSerialLine);
    CHECK(serial.target == "/dev/ttyUSB0");
    CHECK(serial.network.empty());
}

TEST_CASE("device/driver: the other kinds resolve their transports") {  // new
    const DriverRegistry registry = Builtin();
    TransportRequest ble;
    REQUIRE(Get(registry, "ble").Resolve("", &ble));
    CHECK(ble.kind == TransportKind::kBle);
    CHECK(ble.ble_name_prefix == kSamNeo2NamePrefix);
    const SamNeo2Protocol protocol;
    CHECK(ble.service_uuid == protocol.ServiceUuid());
    CHECK(ble.characteristic_uuid == protocol.CharacteristicUuid());

    TransportRequest ws;
    REQUIRE(Get(registry, "intiface").Resolve("", &ws));
    CHECK(ws.kind == TransportKind::kWebSocket);
    CHECK(ws.target == kIntifaceDefaultUrl);
    REQUIRE(Get(registry, "intiface").Resolve("192.168.1.50", &ws));
    CHECK(ws.target == "ws://192.168.1.50:12345");

    TransportRequest none;
    none.target = "left over";
    REQUIRE(Get(registry, "mock").Resolve("ignored", &none));
    CHECK(none.kind == TransportKind::kNone);
    CHECK(none.target.empty());
}

TEST_CASE("device/driver: hints for a failed connect") {  // new
    const DriverRegistry registry = Builtin();
    const Driver& net = Get(registry, "tcode");
    CHECK(Contains(net.Hint("1.2.3.4", "Connection REFUSED"), "nothing is listening"));
    CHECK(Contains(net.Hint("1.2.3.4", "tcp: timeout"), "same network"));
    CHECK(Contains(net.Hint("1.2.3.4", "network unreachable"), "same network"));
    CHECK(Contains(net.Hint("x.local", "no such host"), "hostname could not be resolved"));
    CHECK(net.Hint("1.2.3.4", "something else").empty());
    // Serial, BLE and mock add nothing (as before).
    CHECK(Get(registry, "tcode-serial").Hint("/dev/ttyUSB0", "connection refused").empty());
    CHECK(Get(registry, "ble").Hint("", "connection refused").empty());
    CHECK(Get(registry, "mock").Hint("", "connection refused").empty());
    // Intiface: the 0.7 texts for the normalised URL.
    CHECK(Get(registry, "intiface").Hint("", "connection refused") ==
          IntifaceHint(kIntifaceDefaultUrl, "connection refused"));
    CHECK(Get(registry, "intiface").Hint("192.168.1.50", "i/o timeout") ==
          IntifaceHint("ws://192.168.1.50:12345", "i/o timeout"));
}

TEST_CASE("device/driver: a mock session needs no transport and logs every command") {  // new
    const DriverRegistry registry = Builtin();
    std::vector<std::pair<LogLevel, std::string>> lines;
    SessionOptions options;
    options.log = [&](LogLevel level, const std::string& text) { lines.emplace_back(level, text); };
    std::unique_ptr<DeviceSession> s = Get(registry, "mock").Create(nullptr, "", options);
    REQUIRE(s);
    REQUIRE(s->GetDevice().Connect());
    CHECK(s->Alive());
    CHECK(s->HasRawValues());
    CHECK(s->AdoptedId().empty());
    const ConnectionInfo info = s->Info(true);
    CHECK(info.connected);
    CHECK(info.name == "Mock device (no real hardware)");
    CHECK(info.address.empty());
    CHECK_FALSE(info.battery_ok);
    REQUIRE_FALSE(lines.empty());  // the connect is logged too
    lines.clear();
    REQUIRE(s->GetDevice().SetVibration(0.5));
    REQUIRE(lines.size() == 1);
    CHECK(lines[0].first == LogLevel::kInfo);
    CHECK(lines[0].second == "[mock] vibration=0.50");
    CHECK(s->Tick());
    PumpOutcome outcome = PumpOutcome::kDone;
    CHECK(s->Pump(&outcome));
    CHECK(outcome == PumpOutcome::kCheck);
    CHECK(CapabilitiesOf(s->GetDevice()).Has(kChVibration));
}

TEST_CASE("device/driver: a Sam Neo 2 session over a transport") {  // new
    const DriverRegistry registry = Builtin();
    const Driver& ble = Get(registry, "ble");
    CHECK(ble.Create(nullptr, "", {}) == nullptr);  // it needs its transport

    MockTransport t;
    t.peer = {"Sam Neo 2 Pro", "AA:BB", -60};
    t.characteristics[kBatteryLevelUuid] = {73};
    std::chrono::steady_clock::time_point now{};
    std::vector<std::string> log;
    SessionOptions options;
    options.clock = [&] { return now; };
    options.log = [&](LogLevel, const std::string& text) { log.push_back(text); };
    std::unique_ptr<DeviceSession> s = ble.Create(&t, "", options);
    REQUIRE(s);
    CHECK_FALSE(s->Alive());  // not connected yet
    REQUIRE(s->GetDevice().Connect());
    CHECK(s->Alive());
    CHECK(s->HasRawValues());
    CHECK(s->AdoptedId().empty());
    CHECK_FALSE(log.empty());  // the device's own log lines reach the sink

    ConnectionInfo info = s->Info(false);
    CHECK(info.connected);
    CHECK(info.name == "Sam Neo 2 Pro");
    CHECK(info.address == "AA:BB");
    CHECK(info.rssi == -60);
    CHECK(info.battery_ok);  // read at the connect
    CHECK(info.battery_pct == 73);
    // Only a tick's Info reads the battery again (after the cache time).
    t.characteristics[kBatteryLevelUuid] = {50};
    now += kBatteryCacheTtl;
    CHECK(s->Info(false).battery_pct == 73);
    CHECK(s->Info(true).battery_pct == 50);

    // The tick is the keepalive (on the session's clock): both channels
    // again after 4 s without a write.
    REQUIRE(s->GetDevice().SetVibration(0.5));
    REQUIRE(s->GetDevice().SetSuction(1));
    const std::size_t n = t.sent.size();
    now += 3s;
    REQUIRE(s->Tick());
    CHECK(t.sent.size() == n);
    now += 1s;
    REQUIRE(s->Tick());
    CHECK(t.sent.size() == n + 2);

    // What the peer sends is read and dropped.
    t.PushText("noise");
    t.PushText("more");
    PumpOutcome outcome = PumpOutcome::kDone;
    REQUIRE(s->Pump(&outcome));
    CHECK(outcome == PumpOutcome::kCheck);
    CHECK(t.Receive(0ms).kind == ReceiveResult::Kind::kTimeout);

    // A closed link is not alive.
    t.Disconnect();
    CHECK_FALSE(s->Alive());
}

TEST_CASE("device/driver: a TCode session over a transport") {  // new
    const DriverRegistry registry = Builtin();
    MockTransport t;
    std::unique_ptr<DeviceSession> s = Get(registry, "tcode").Create(&t, "tcp://127.0.0.1:9000", {});
    REQUIRE(s);
    REQUIRE(s->GetDevice().Connect());
    CHECK(s->Alive());
    CHECK_FALSE(s->HasRawValues());
    CHECK(s->Info(true).address == "tcp://127.0.0.1:9000");
    auto* linear = dynamic_cast<LinearDevice*>(&s->GetDevice());
    REQUIRE(linear);
    REQUIRE(linear->SetPosition(0.8, 300ms));
    REQUIRE(t.sent.size() == 1);
    CHECK(t.SentText(0) == "L08000I300\n");
    CHECK(s->Tick());
    CHECK(t.sent.size() == 1);  // TCode has no keepalive
    t.Disconnect();
    CHECK_FALSE(s->Alive());

    MockTransport port;
    std::unique_ptr<DeviceSession> serial = Get(registry, "tcode-serial").Create(&port, "/dev/ttyUSB0", {});
    REQUIRE(serial);
    REQUIRE(serial->GetDevice().Connect());
    CHECK(serial->Alive());
    REQUIRE(serial->GetDevice().Stop());
    CHECK(port.SentText(0) == "DSTOP\n");
}

TEST_CASE("device/driver: an Intiface session pings, reads the battery and tells the toy apart") {  // new
    const DriverRegistry registry = Builtin();
    FakeButtplug fake(FakeButtplug::MessagesWithBattery());
    std::unique_ptr<DeviceSession> s = Get(registry, "intiface").Create(&fake.transport, "fake", {});
    REQUIRE(s);
    REQUIRE(s->GetDevice().Connect());
    CHECK(s->Alive());
    CHECK_FALSE(s->HasRawValues());
    CHECK(s->AdoptedId() == "7");
    ConnectionInfo info = s->Info(true);
    CHECK(info.connected);
    CHECK(Contains(info.name, "Testgerät"));
    CHECK(info.battery_ok);
    CHECK(info.battery_pct == 73);

    // The tick reads what waits and pings.
    const std::size_t pings = fake.Commands("Ping").size();
    REQUIRE(s->Tick());
    CHECK(fake.Commands("Ping").size() == pings + 1);

    // A healthy pump watches the link itself: the worker checks nothing more.
    PumpOutcome outcome = PumpOutcome::kCheck;
    REQUIRE(s->Pump(&outcome));
    CHECK(outcome == PumpOutcome::kDone);
}

TEST_CASE("device/driver: an Intiface command whose reconnect finds another toy is refused") {  // new
    const DriverRegistry registry = Builtin();
    for (const bool other_index : {false, true}) {
        INFO("another " << (other_index ? "index" : "name"));
        FakeButtplug fake(FakeButtplug::StandardDeviceMessages());
        std::unique_ptr<DeviceSession> s = Get(registry, "intiface").Create(&fake.transport, "fake", {});
        REQUIRE(s);
        REQUIRE(s->GetDevice().Connect());
        REQUIRE(s->GetDevice().SetVibration(0.3));
        const std::string identity = s->Info(false).name + "\n" + s->AdoptedId();
        fake.transport.Drop();
        if (other_index)
            fake.device_index = 8;  // a second toy of the same name
        else
            fake.device_name = "Other toy";
        const std::size_t before = fake.Commands("ScalarCmd").size();
        const Status refused = s->GetDevice().SetVibration(0.6);
        CHECK_FALSE(refused);
        CHECK(Contains(refused.message, "another device"));
        CHECK(fake.transport.connect_count == 2);  // it did reconnect
        CHECK(s->Alive());
        CHECK(fake.Commands("ScalarCmd").size() == before);  // nothing went to the new toy
        // The session says it is another device (the worker's identity).
        CHECK(s->AdoptedId() == (other_index ? "8" : "7"));
        CHECK(s->Info(false).name + "\n" + s->AdoptedId() != identity);
    }
}

TEST_CASE("device/driver: an Intiface pump or tick on a dead socket reconnects once") {  // new
    const DriverRegistry registry = Builtin();
    SUBCASE("the pump reconnects and asks for a snapshot") {
        FakeButtplug fake(FakeButtplug::StandardDeviceMessages());
        std::unique_ptr<DeviceSession> s = Get(registry, "intiface").Create(&fake.transport, "fake", {});
        REQUIRE(s);
        REQUIRE(s->GetDevice().Connect());
        fake.transport.Drop();
        fake.device_index = 8;
        PumpOutcome outcome = PumpOutcome::kCheck;
        REQUIRE(s->Pump(&outcome));
        CHECK(outcome == PumpOutcome::kReconnected);
        CHECK(fake.transport.connect_count == 2);
        CHECK(s->AdoptedId() == "8");
        // A reconnect that fails is a lost link with the reason; after it
        // the one-shot is spent.
        fake.transport.Drop();
        fake.transport.connect_error = "connection refused";
        Status lost = s->Pump(&outcome);
        CHECK_FALSE(lost);
        CHECK(Contains(lost.message, "connection refused"));
        CHECK_FALSE(s->Alive());
        fake.transport.connect_error.clear();
        lost = s->Pump(&outcome);
        CHECK_FALSE(lost);
        CHECK(lost.message == "intiface: one-shot reconnect already used");
    }
    SUBCASE("the tick reconnects, then pings") {
        FakeButtplug fake(FakeButtplug::StandardDeviceMessages());
        std::unique_ptr<DeviceSession> s = Get(registry, "intiface").Create(&fake.transport, "fake", {});
        REQUIRE(s);
        REQUIRE(s->GetDevice().Connect());
        fake.transport.Drop();
        const std::size_t pings = fake.Commands("Ping").size();
        REQUIRE(s->Tick());
        CHECK(fake.transport.connect_count == 2);
        CHECK(fake.Commands("Ping").size() == pings + 1);
        CHECK(s->Alive());
    }
    SUBCASE("a refused reconnect in the tick is a lost link with the reason") {
        FakeButtplug fake(FakeButtplug::StandardDeviceMessages());
        std::unique_ptr<DeviceSession> s = Get(registry, "intiface").Create(&fake.transport, "fake", {});
        REQUIRE(s);
        REQUIRE(s->GetDevice().Connect());
        fake.transport.Drop();
        fake.transport.connect_error = "connection refused";
        const Status lost = s->Tick();
        CHECK_FALSE(lost);
        CHECK(Contains(lost.message, "connection refused"));
        CHECK_FALSE(s->Alive());
    }
}
