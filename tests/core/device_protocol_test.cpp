#include <doctest/doctest.h>

#include <cmath>
#include <set>

#include "samn/device/battery.h"
#include "samn/device/go_json.h"
#include "samn/device/protocol.h"

using namespace samn::device;
using B = samn::device::Bytes;

// Cases derived 1:1 from the official Buttplug Rust source (svakom_sam2.rs +
// svakom-sam2.yml); regression guard against drifting from the verified protocol.
TEST_CASE("device/protocol: Sam Neo 2 EncodeVibration") {  // Go: TestSamNeo2Protocol_EncodeVibration
    SamNeo2Protocol p;
    CHECK(p.EncodeVibration(0.0) == B{0x55, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00});    // off
    CHECK(p.EncodeVibration(1.0) == B{0x55, 0x03, 0x00, 0x00, 0x05, 0x0A, 0x00});    // full (10)
    CHECK(p.EncodeVibration(0.7) == B{0x55, 0x03, 0x00, 0x00, 0x05, 0x07, 0x00});    // 70% -> 7
    CHECK(p.EncodeVibration(0.06) == B{0x55, 0x03, 0x00, 0x00, 0x05, 0x01, 0x00});   // minimal non-zero -> 1
}

TEST_CASE("device/protocol: Sam Neo 2 EncodeSuction") {  // Go: TestSamNeo2Protocol_EncodeSuction
    SamNeo2Protocol p;
    CHECK(p.EncodeSuction(0.0) == B{0x55, 0x09, 0x00, 0x00, 0x00, 0x00, 0x00});  // off
    CHECK(p.EncodeSuction(1.0) == B{0x55, 0x09, 0x00, 0x00, 0x01, 0x05, 0x00});  // full (5)
    CHECK(p.EncodeSuction(0.6) == B{0x55, 0x09, 0x00, 0x00, 0x01, 0x03, 0x00});  // 60% -> 3
}

TEST_CASE("device/protocol: Sam Neo 2 UUIDs and write mode") {  // Go: TestSamNeo2Protocol_UUIDsAndWriteMode
    SamNeo2Protocol p;
    CHECK(p.ServiceUuid() == "0000ffe0-0000-1000-8000-00805f9b34fb");
    CHECK(p.CharacteristicUuid() == "0000ffe1-0000-1000-8000-00805f9b34fb");
    // The Sam Neo 2 needs write-with-response per the source.
    CHECK(p.WriteWithResponse());
    CHECK(std::string(kSamNeo2NamePrefix) == "Sam Neo 2");
}

// The raw encoding must NOT quantise: it exists to try values beyond the
// ranges taken from svakom-sam2.yml (0-10 vibration, 0-5 suction).
TEST_CASE("device/protocol: raw encoding does not quantise") {  // Go: TestSamNeo2RawEncodingDoesNotQuantize
    SamNeo2Protocol p;
    for (int speed : {0, 1, 3, 4, 10, 11, 50, 200, 255}) {
        const B packet = p.EncodeVibrationRaw(static_cast<std::uint8_t>(speed));
        REQUIRE(packet.size() == 7);
        CHECK(packet[5] == speed);
        CHECK(packet[4] == (speed == 0 ? 0x00 : 0x05));
        CHECK(packet[0] == 0x55);
        CHECK(packet[1] == 0x03);
    }
    for (int level : {0, 1, 5, 6, 99, 255}) {
        const B packet = p.EncodeSuctionRaw(static_cast<std::uint8_t>(level));
        CHECK(packet[5] == level);
        CHECK(packet[4] == (level == 0 ? 0x00 : 0x01));
        CHECK(packet[1] == 0x09);
    }
}

TEST_CASE("device/protocol: normal encoding still quantises") {  // Go: TestSamNeo2NormalEncodingStillQuantizes
    SamNeo2Protocol p;
    struct C { double intensity; int speed; };
    for (C c : {C{0, 0}, C{0.05, 1}, C{0.5, 5}, C{0.94, 9}, C{1.0, 10}})
        CHECK(p.EncodeVibration(c.intensity)[5] == c.speed);
}

// The device has whole steps only, so neighbouring intensities often land on
// the same one; a write that changes nothing still costs a confirmed round
// trip. This holds how many distinct packets a ramp gives.
TEST_CASE("device/protocol: encoding collapses nearby intensities") {  // Go: TestEncodingCollapsesNearbyIntensities
    SamNeo2Protocol p;
    std::set<B> distinct;
    for (int i = 0; i <= 100; ++i) distinct.insert(p.EncodeVibration(i / 100.0));
    CHECK(distinct.size() <= 11);  // steps 0-10
    CHECK(distinct.size() >= 5);   // the ramp would be too coarse
    // Two neighbours on the same step MUST give the same packet, or the
    // de-duplication cannot work.
    CHECK(p.EncodeVibration(0.50) == p.EncodeVibration(0.504));
}

// Vibration and suction must be told apart by their packets, or the channel
// state could not tell them apart and the keepalive would overwrite one
// channel with the other's packet.
TEST_CASE("device/protocol: vibration and suction packets differ") {  // Go: TestVibrationAndSuctionPacketsDiffer
    SamNeo2Protocol p;
    const B vibration = p.EncodeVibration(0.5);
    const B suction = p.EncodeSuction(0.5);
    CHECK(vibration != suction);
    CHECK(vibration[1] != suction[1]);
}

TEST_CASE("device/protocol: stop is vibration off") {  // new
    SamNeo2Protocol p;
    CHECK(p.EncodeStop() == B{0x55, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00});
}

TEST_CASE("device/protocol: ScaleToRange rounds half away from zero, clamps, NaN is 0") {  // new (Go scaleToRange)
    CHECK(ScaleToRange(0.25, 10) == 3);  // 2.5 -> 3, not truncated and not banker's
    CHECK(ScaleToRange(0.35, 10) == 4);  // 3.5 -> 4
    CHECK(ScaleToRange(0.1, 5) == 1);    // 0.5 -> 1
    CHECK(ScaleToRange(-3, 10) == 0);
    CHECK(ScaleToRange(7, 10) == 10);
    // 0.7: Go's math.Round(0.7*10) = 7 (0.7*10 = 7.000000000000001)
    CHECK(ScaleToRange(0.7, 10) == 7);
    // Go casts NaN to byte with an undefined result; here it is 0 (safe).
    CHECK(ScaleToRange(std::nan(""), 10) == 0);
}

TEST_CASE("device/protocol: legacy Sam Neo (original) packets") {  // new, from the Go source's comments
    LegacySamProtocol p;
    CHECK(p.ServiceUuid() == "0000ae00-0000-1000-8000-00805f9b34fb");
    CHECK(p.CharacteristicUuid() == "0000ae01-0000-1000-8000-00805f9b34fb");
    CHECK_FALSE(p.WriteWithResponse());
    CHECK(p.EncodeVibration(0) == B{18, 1, 3, 0, 0, 0});
    CHECK(p.EncodeVibration(1) == B{18, 1, 3, 0, 5, 10});
    CHECK(p.EncodeVibration(0.3) == B{18, 1, 3, 0, 5, 3});
    CHECK(p.EncodeSuction(0) == B{18, 6, 1, 0});
    CHECK(p.EncodeSuction(0.4) == B{18, 6, 1, 0});
    CHECK(p.EncodeSuction(0.5) == B{18, 6, 1, 1});  // 0.5 rounds up: effectively on / off
    CHECK(p.EncodeStop() == B{18, 1, 3, 0, 0, 0});
}

TEST_CASE("device/battery: FormatBatteryPct") {  // Go: TestFormatBatteryPct
    CHECK(FormatBatteryPct(73) == "Battery 73%");
}

TEST_CASE("device/battery: ConnectionInfo battery defaults") {  // Go: TestConnectionInfoBatteryDefaults
    ConnectionInfo info;
    CHECK_FALSE(info.battery_ok);
}

TEST_CASE("device/battery: GATT and Buttplug readings") {  // new (Go readStandardBatteryLevel / BatteryLevel)
    CHECK(BatteryFromGatt({73}) == 73);
    CHECK(BatteryFromGatt({200}) == 100);  // above 100 is capped
    CHECK_FALSE(BatteryFromGatt({}).has_value());
    CHECK(BatteryFromButtplug(0.73) == 73);
    CHECK(BatteryFromButtplug(0.005) == 1);   // int(0.5 + 0.5)
    CHECK(BatteryFromButtplug(0.994) == 99);
    CHECK(BatteryFromButtplug(0.996) == 100);
    CHECK(BatteryFromButtplug(1.7) == 100);
    CHECK(BatteryFromButtplug(-0.2) == 0);
    CHECK(BatteryFromButtplug(std::nan("")) == 0);
}

TEST_CASE("device/go_json: floats and objects as Go's encoding/json writes them") {  // new
    CHECK(GoFloat(1.0) == "1");
    CHECK(GoFloat(0.0) == "0");
    CHECK(GoFloat(0.5) == "0.5");
    CHECK(GoFloat(0.8) == "0.8");
    CHECK(GoFloat(0.4) == "0.4");
    CHECK(GoFloat(100.0) == "100");
    CHECK(GoFloat(0.123456789) == "0.123456789");
    CHECK(GoFloat(0.000001) == "0.000001");
    CHECK(GoFloat(1e-7) == "1e-7");
    CHECK(GoFloat(1.5e-9) == "1.5e-9");
    CHECK(GoFloat(1e21) == "1e+21");
    CHECK(GoFloat(123456789012345678901.0) == "123456789012345680000");
    CHECK(GoFloat(-0.25) == "-0.25");
    CHECK(GoFloat(0.1 + 0.2) == "0.30000000000000004");

    nlohmann::json j = {{"b", 1.0}, {"a", "x<y&\"z\n"}, {"c", nlohmann::json::array({1, 2.5, true, nullptr})}};
    // Outside the macro: MSVC 17.14's preprocessor breaks on this raw string inside CHECK().
    const std::string want = R"({"a":"x\u003cy\u0026\"z\n","b":1,"c":[1,2.5,true,null]})";
    CHECK(GoJsonDump(j) == want);

    nlohmann::ordered_json o = nlohmann::ordered_json::object();
    o["z"] = 1;
    o["a"] = 2;
    CHECK(GoJsonDump(o) == R"({"z":1,"a":2})");
}
