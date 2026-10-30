#include <doctest/doctest.h>

#include "samn/device/mock.h"
#include "samn/device/samneo2.h"

using namespace samn::device;
using namespace std::chrono_literals;

namespace {
// Fails the n-th Send (0-based) once; every other Send goes through.
struct FlakyTransport : MockTransport {
    int fail_at = -1;
    int calls = 0;
    Status Send(const Bytes& data, WriteMode mode) override {
        if (calls++ == fail_at) return Status::Error("gatt busy");
        return MockTransport::Send(data, mode);
    }
};

struct Rig {
    FlakyTransport t;
    std::chrono::steady_clock::time_point now{};
    SamNeo2Device dev;
    Rig() : dev(t, nullptr, [this] { return now; }) {
        t.peer = {"Sam Neo 2 Pro", "AA:BB", -60};
        REQUIRE(dev.Connect());
    }
};
}  // namespace

TEST_CASE("device/samneo2: raw writes update the channel state") {  // Go: TestSamNeo2RawWritesUpdateChannelState
    Rig r;
    SamNeo2Protocol proto;
    REQUIRE(r.dev.SetVibration(1));
    REQUIRE(r.dev.SetVibrationRaw(0));
    // Keepalive state after raw 0 must be "off", or it would restart the device.
    CHECK(*r.dev.LastVibrationPacket() == proto.EncodeVibrationRaw(0));
    // The same normal value as before the raw write must go out again.
    std::size_t n = r.t.sent.size();
    REQUIRE(r.dev.SetVibration(1));
    CHECK(r.t.sent.size() == n + 1);

    REQUIRE(r.dev.SetSuction(1));
    REQUIRE(r.dev.SetSuctionRaw(0));
    CHECK(*r.dev.LastSuctionPacket() == proto.EncodeSuctionRaw(0));
    // A raw write repeats even when equal to the last packet.
    n = r.t.sent.size();
    REQUIRE(r.dev.SetSuctionRaw(0));
    CHECK(r.t.sent.size() == n + 1);
}

TEST_CASE("device/samneo2: unchanged packets are not sent, writes are with response") {  // new (Go writeChannel)
    Rig r;
    REQUIRE(r.dev.SetVibration(0.5));
    REQUIRE(r.dev.SetVibration(0.504));  // same step
    CHECK(r.t.sent.size() == 1);
    CHECK(r.t.sent[0].mode == WriteMode::kWithResponse);
    CHECK(r.t.sent[0].data == Bytes{0x55, 0x03, 0x00, 0x00, 0x05, 0x05, 0x00});
    REQUIRE(r.dev.SetSuction(0.6));
    CHECK(r.t.sent[1].data == Bytes{0x55, 0x09, 0x00, 0x00, 0x01, 0x03, 0x00});
}

TEST_CASE("device/samneo2: Stop always writes both channels and remembers them") {  // new
    Rig r;
    REQUIRE(r.dev.Stop());  // nothing played yet: still writes
    CHECK(r.t.sent.size() == 2);
    CHECK(r.t.sent[0].data[1] == 0x03);
    CHECK(r.t.sent[1].data[1] == 0x09);
    REQUIRE(r.dev.SetVibration(1));
    REQUIRE(r.dev.Stop());
    const std::size_t n = r.t.sent.size();
    REQUIRE(r.dev.SetVibration(1));  // same value as before Stop: must not be swallowed
    CHECK(r.t.sent.size() == n + 1);
}

// A failed vibration-off must not leave the suction on, and the keepalive
// must never replay the "on" packets of before the Stop.
TEST_CASE("device/samneo2: a failed off write still switches the other channel off") {  // new
    Rig r;
    SamNeo2Protocol proto;
    REQUIRE(r.dev.SetVibration(1));
    REQUIRE(r.dev.SetSuction(1));
    const std::size_t n = r.t.sent.size();
    r.t.fail_at = r.t.calls;  // the vibration-off write fails
    const Status s = r.dev.Stop();
    CHECK_FALSE(s);
    CHECK(s.message == "gatt busy");  // the first error comes back
    REQUIRE(r.t.sent.size() == n + 1);
    CHECK(r.t.sent[n].data == proto.EncodeSuction(0));  // suction-off went out anyway
    CHECK(*r.dev.LastVibrationPacket() == proto.EncodeVibration(0));
    CHECK(*r.dev.LastSuctionPacket() == proto.EncodeSuction(0));
    // The keepalive repeats "off" (also retrying the failed write), never "on".
    r.now += 5s;
    REQUIRE(r.dev.TickKeepalive());
    REQUIRE(r.t.sent.size() == n + 3);
    CHECK(r.t.sent[n + 1].data == proto.EncodeVibration(0));
    CHECK(r.t.sent[n + 2].data == proto.EncodeSuction(0));

    // The second write failing: the first error is still reported, both are "off".
    REQUIRE(r.dev.SetVibration(0.5));
    REQUIRE(r.dev.SetSuction(0.5));
    r.t.fail_at = r.t.calls + 1;  // suction-off fails
    CHECK_FALSE(r.dev.Stop());
    CHECK(r.t.sent.back().data == proto.EncodeVibration(0));
    CHECK(*r.dev.LastVibrationPacket() == proto.EncodeVibration(0));
    CHECK(*r.dev.LastSuctionPacket() == proto.EncodeSuction(0));
}

TEST_CASE("device/samneo2: disconnect tries both off writes before closing") {  // new
    Rig r;
    SamNeo2Protocol proto;
    REQUIRE(r.dev.SetVibration(1));
    REQUIRE(r.dev.SetSuction(1));
    r.t.fail_at = r.t.calls;  // vibration-off fails
    REQUIRE(r.dev.Disconnect());
    CHECK(r.t.sent.back().data == proto.EncodeSuction(0));
    CHECK_FALSE(r.t.IsConnected());
}

TEST_CASE("device/samneo2: keepalive repeats BOTH channels after 4 s, not before") {  // new
    Rig r;
    CHECK_FALSE(r.dev.TickKeepalive());  // nothing sent yet
    REQUIRE(r.dev.SetVibration(0.5));
    REQUIRE(r.dev.SetSuction(1));
    const std::size_t n = r.t.sent.size();
    r.now += 3900ms;
    CHECK_FALSE(r.dev.TickKeepalive());
    r.now += 100ms;
    CHECK(r.dev.TickKeepalive());
    CHECK(r.t.sent.size() == n + 2);
    CHECK(r.t.sent[n].data[1] == 0x03);
    CHECK(r.t.sent[n + 1].data[1] == 0x09);
    // A replay does not reset the idle timer: the next tick repeats again.
    r.now += 2s;
    CHECK(r.dev.TickKeepalive());
    // An unchanged SetVibration does not reset it either.
    const std::size_t m = r.t.sent.size();
    REQUIRE(r.dev.SetVibration(0.5));
    CHECK(r.t.sent.size() == m);
    CHECK(r.dev.TickKeepalive());
    // A new value does.
    REQUIRE(r.dev.SetVibration(0.9));
    CHECK_FALSE(r.dev.TickKeepalive());
}

TEST_CASE("device/samneo2: connect state, battery, capabilities, disconnect") {  // new
    MockTransport t;
    t.peer = {"Sam Neo 2", "AA", -50};
    t.characteristics[kBatteryLevelUuid] = Bytes{150};
    auto now = std::chrono::steady_clock::time_point{};
    SamNeo2Device dev(t, nullptr, [&now] { return now; });
    CHECK_FALSE(dev.SetVibration(1));  // not connected: error, not a crash
    CHECK(dev.Stop());                 // nothing to stop
    REQUIRE(dev.Connect());
    CHECK_FALSE(dev.Connect());  // already connected
    CHECK(dev.BatteryProbed());
    CHECK(dev.BatteryLevel().percent == 100);
    CHECK(dev.DeviceCapabilities().battery);
    CHECK(dev.DeviceCapabilities().device == "Sam Neo 2");
    ConnectionInfo info = dev.Info();
    CHECK(info.connected);
    CHECK(info.address == "AA");
    CHECK(info.rssi == -50);
    // cached for 30 s
    t.characteristics[kBatteryLevelUuid] = Bytes{40};
    now += 29s;
    CHECK(dev.BatteryLevel().percent == 100);
    now += 2s;
    CHECK(dev.BatteryLevel().percent == 40);

    REQUIRE(dev.SetVibration(1));
    REQUIRE(dev.Disconnect());
    CHECK_FALSE(dev.Info().connected);
    CHECK(t.sent.back().data[1] == 0x09);  // Stop went out before closing
    CHECK_FALSE(t.IsConnected());
    // Reconnect starts clean: the old value is sent again.
    REQUIRE(dev.Connect());
    const std::size_t n = t.sent.size();
    REQUIRE(dev.SetVibration(1));
    CHECK(t.sent.size() == n + 1);
}

TEST_CASE("device/samneo2: no battery service means no battery, a failed write is an error") {  // new
    Rig r;
    CHECK(r.dev.BatteryProbed());
    CHECK_FALSE(r.dev.BatteryLevel().ok);
    CHECK_FALSE(r.dev.DeviceCapabilities().battery);
    r.t.send_error = "gatt busy";
    CHECK_FALSE(r.dev.SetVibration(1));
    CHECK_FALSE(r.dev.LastVibrationPacket().has_value());  // not remembered, so a retry is sent
}

TEST_CASE("device/samneo2: connect failure is passed on") {  // new
    MockTransport t;
    t.connect_error = "no device with name prefix \"Sam Neo 2\" found within 15s";
    SamNeo2Device dev(t);
    Status s = dev.Connect();
    CHECK_FALSE(s);
    CHECK(s.message.find("Sam Neo 2") != std::string::npos);
    CHECK_FALSE(dev.Connected());
}
