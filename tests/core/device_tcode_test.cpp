#include <doctest/doctest.h>

#include <cmath>

#include "samn/device/mock.h"
#include "samn/device/tcode.h"

using namespace samn::device;
using namespace std::chrono_literals;

namespace {
std::string AllSent(const MockTransport& t) {
    std::string out;
    for (const auto& s : t.sent) out += ToText(s.data);
    return out;
}
}  // namespace

TEST_CASE("device/tcode: value and move") {  // Go: TestTCodeValueAndMove
    CHECK(TCodeValue(0) == "0000");
    CHECK(TCodeValue(0.5) == "5000");
    CHECK(TCodeValue(1) == "9999");
    CHECK(TCodeValue(-2) == "0000");
    CHECK(TCodeValue(3) == "9999");
    CHECK(TCodeValue(0.25) == "2500");
    CHECK(TCodeValue(std::nan("")) == "0000");
    CHECK(TCodeMove("L0", 0.5, 250ms) == "L05000I250");
    CHECK(TCodeMove("L0", 1, 0ms) == "L09999I1");  // zero duration becomes 1 ms
}

TEST_CASE("device/tcode: ParseTCodeAddress") {  // Go: TestParseTCodeAddress
    struct C { const char *in, *network, *hostport; };
    for (C c : {C{"192.168.1.50", "udp", "192.168.1.50:8000"}, C{"192.168.1.50:9000", "udp", "192.168.1.50:9000"},
                C{"udp://osr.local", "udp", "osr.local:8000"}, C{"tcp://10.0.0.2:7000", "tcp", "10.0.0.2:7000"},
                C{" TCP://10.0.0.2 ", "tcp", "10.0.0.2:8000"},
                C{"::1", "udp", "[::1]:8000"}, C{"[::1]:9", "udp", "[::1]:9"}}) {
        TCodeAddress a;
        INFO(c.in);
        REQUIRE(ParseTCodeAddress(c.in, &a));
        CHECK(a.network == c.network);
        CHECK(a.hostport == c.hostport);
    }
    for (const char* bad : {"", "http://x", "udp://", "udp://host:abc"}) {
        TCodeAddress a;
        INFO(bad);
        CHECK_FALSE(ParseTCodeAddress(bad, &a));
    }
}

// Go: over TCP against a local listener. Here the line writer is the mock.
TEST_CASE("device/tcode: over TCP (lines, DSTOP, vibration no-op)") {  // Go: TestTCodeOverTCP
    MockTransport t;
    auto dev = TCodeDevice::OverNetwork(t, "tcp://127.0.0.1:9000");
    REQUIRE(dev.Connect());
    CHECK(dev.HasLinear());
    CHECK(CapabilitiesOf(dev).Has(kChStroke));
    REQUIRE(dev.SetVibration(0.7));
    REQUIRE(dev.SetPosition(0.8, 300ms));
    REQUIRE(dev.Stop());
    REQUIRE(t.sent.size() == 2);
    CHECK(t.SentText(0) == "L08000I300\n");
    CHECK(t.SentText(1) == "DSTOP\n");
    ConnectionInfo info = dev.Info();
    CHECK(info.connected);
    CHECK(info.address == "tcp://127.0.0.1:9000");
    REQUIRE(dev.Disconnect());
    CHECK_FALSE(dev.Info().connected);
    CHECK_FALSE(t.IsConnected());
    CHECK_FALSE(dev.SetPosition(0.5, 1ms));  // send after disconnect must fail
}

TEST_CASE("device/tcode: over UDP (no scheme)") {  // Go: TestTCodeOverUDP
    MockTransport t;
    auto dev = TCodeDevice::OverNetwork(t, "127.0.0.1:5555");
    REQUIRE(dev.Connect());
    REQUIRE(dev.SetPosition(0.25, 120ms));
    REQUIRE(t.sent.size() == 1);
    CHECK(t.SentText(0) == "L02500I120\n");  // one datagram
    CHECK(dev.Info().address == "udp://127.0.0.1:5555");
}

TEST_CASE("device/tcode: TCP connect fails with a clear message") {  // Go: TestTCodeTCPConnectFails
    MockTransport t;
    t.connect_error = "connection refused";
    auto dev = TCodeDevice::OverNetwork(t, "tcp://127.0.0.1:9");
    Status s = dev.Connect();
    CHECK_FALSE(s);
    CHECK(s.message.find("no connection") != std::string::npos);
    CHECK(s.message.find("127.0.0.1:9 (tcp)") != std::string::npos);
    // A bad address fails before the transport is touched.
    auto bad = TCodeDevice::OverNetwork(t, "ftp://x");
    CHECK_FALSE(bad.Connect());
    CHECK(t.connect_count == 0);
}

TEST_CASE("device/tcode: extra axes") {  // Go: TestTCodeExtraAxes
    MockTransport t;
    auto dev = TCodeDevice::OverNetwork(t, "127.0.0.1:5555");
    REQUIRE(dev.Connect());
    MultiAxisDevice& md = dev;
    CHECK(md.HasAxis(kChTwist));
    CHECK_FALSE(md.HasAxis("banana"));
    REQUIRE(md.SetAxis(kChTwist, 0.75, 200ms));
    REQUIRE(md.SetAxis(kChSurge, 0.5, 100ms));
    CHECK_FALSE(md.SetAxis("banana", 0.5, 1ms));
    REQUIRE(t.sent.size() == 2);
    CHECK(t.SentText(0) == "R07500I200\n");
    CHECK(t.SentText(1) == "L15000I100\n");
    const Capabilities caps = CapabilitiesOf(dev);
    for (const char* ch : {kChStroke, kChSurge, kChSway, kChTwist, kChRoll, kChPitch}) CHECK(caps.Has(ch));
    CHECK(TCodeAxisCode("sway") == "L2");
    CHECK(TCodeAxisCode("roll") == "R1");
    CHECK(TCodeAxisCode("pitch") == "R2");
}

TEST_CASE("device/tcode: serial") {  // Go: TestTCodeSerial
    MockTransport t;
    auto dev = TCodeDevice::OverSerial(t, " COM7 ");
    REQUIRE(dev.Connect());
    ConnectionInfo info = dev.Info();
    CHECK(info.connected);
    CHECK(info.address == "serial://COM7");
    (void)dev.SetPosition(0.5, 100ms);
    (void)dev.SetAxis(kChRoll, 0.25, 50ms);
    (void)dev.Disconnect();
    CHECK(AllSent(t) == "L05000I100\nR12500I50\nDSTOP\n");
    CHECK(t.disconnect_count == 1);

    MockTransport bad_t;
    bad_t.connect_error = "access denied";
    auto bad = TCodeDevice::OverSerial(bad_t, "COM9");
    Status s = bad.Connect();
    CHECK_FALSE(s);
    CHECK(s.message.find("COM9") != std::string::npos);
    // Go: openTCodeSerial("") must fail.
    auto none = TCodeDevice::OverSerial(bad_t, "");
    s = none.Connect();
    CHECK_FALSE(s);
    CHECK(s.message.find("no serial port chosen") != std::string::npos);
    CHECK(kTCodeSerialBaud == 115200);
}

TEST_CASE("device/tcode: a failed write marks the device disconnected") {  // new (Go writeLine)
    MockTransport t;
    auto dev = TCodeDevice::OverNetwork(t, "127.0.0.1");
    REQUIRE(dev.Connect());
    t.send_error = "broken pipe";
    Status s = dev.SetPosition(0.5, 10ms);
    CHECK_FALSE(s);
    CHECK(s.message == "tcode: send failed: broken pipe");
    CHECK_FALSE(dev.Info().connected);
}
