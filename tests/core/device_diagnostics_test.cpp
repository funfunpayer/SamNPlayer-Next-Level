#include <doctest/doctest.h>

#include "samn/device/diagnostics.h"
#include "samn/device/mock.h"

using namespace samn::device;
using namespace std::chrono_literals;

namespace {

// No real waiting: the run's behaviour (phases, log) does not depend on pauses.
DiagnosticsOptions Fast() {
    DiagnosticsOptions o;
    o.update_rate_intervals = {1ms, 1ms};
    o.offset_change_delay = 1ms;
    o.rapid_switch_delay = 1ms;
    o.sleep = [](std::chrono::milliseconds) {};
    return o;
}

// Forwards only the five Device methods: no RawCapable (like Intiface in 0.7).
class PlainDevice : public Device {
public:
    explicit PlainDevice(Device& inner) : inner_(inner) {}
    Status Connect() override { return inner_.Connect(); }
    Status Disconnect() override { return inner_.Disconnect(); }
    Status SetVibration(double v) override { return inner_.SetVibration(v); }
    Status SetSuction(double v) override { return inner_.SetSuction(v); }
    Status Stop() override { return inner_.Stop(); }
private:
    Device& inner_;
};

}  // namespace

TEST_CASE("device/diagnostics: raw capable device") {  // Go: TestRunDiagnosticsWithRawCapableDevice
    MockDevice dev(false);
    std::vector<DiagnosticsEntry> streamed;
    DiagnosticsOptions o = Fast();
    o.on_entry = [&streamed](const DiagnosticsEntry& e) { streamed.push_back(e); };
    DiagnosticsReport report = RunDiagnostics(dev, 0, o);

    CHECK(report.raw_capable);
    CHECK_FALSE(report.interrupted);
    REQUIRE_FALSE(report.log.empty());
    CHECK(streamed.size() == report.log.size());  // onEntry exactly once per entry

    std::map<std::string, bool> want = {{"raw_sweep_vibration", false}, {"raw_sweep_suction", false},
                                        {"vibration_alone", false},     {"suction_alone", false},
                                        {"simultaneous", false},        {"offset_change", false},
                                        {"rapid_switching", false},     {"stop", false}};
    for (const auto& p : report.phases) {
        if (want.count(p.phase)) want[p.phase] = true;
        CHECK(p.errors == 0);  // the mock never errors
    }
    for (const auto& [phase, seen] : want) {
        INFO(phase);
        CHECK(seen);
    }
    CHECK_FALSE(report.notes.empty());
}

TEST_CASE("device/diagnostics: connect latency is the first entry") {  // Go: TestRunDiagnosticsIncludesConnectLatency
    MockDevice dev(false);
    DiagnosticsReport report = RunDiagnostics(dev, 1234.5, Fast());
    REQUIRE_FALSE(report.log.empty());
    CHECK(report.log[0].phase == "connection");
    CHECK(report.log[0].latency_ms == 1234.5);
}

TEST_CASE("device/diagnostics: raw sweep skipped without RawCapable") {  // Go: TestRunDiagnosticsSkipsRawSweepWithoutRawCapable
    MockDevice inner(false);
    PlainDevice dev(inner);
    DiagnosticsReport report = RunDiagnostics(dev, 0, Fast());
    CHECK_FALSE(report.raw_capable);
    for (const auto& p : report.phases) {
        CHECK(p.phase != "raw_sweep_vibration");
        CHECK(p.phase != "raw_sweep_suction");
    }
    bool note = false;
    for (const auto& n : report.notes)
        if (n.find("Rohwert-Sweep übersprungen") != std::string::npos) note = true;
    CHECK(note);
    // Update-rate phase then used the regular 0-1 channel.
    CHECK(inner.commands.size() > 0);
}

TEST_CASE("device/diagnostics: stops on a cancelled run") {  // Go: TestRunDiagnosticsStopsOnCanceledContext
    MockDevice dev(false);
    DiagnosticsOptions o = Fast();
    o.cancelled = [] { return true; };
    DiagnosticsReport report = RunDiagnostics(dev, 0, o);
    CHECK(report.interrupted);
    CHECK(report.log.back().phase == "stop");  // always ends with Stop
}

TEST_CASE("device/diagnostics: sweep values, durations, JSON") {  // new (Go rawSweepValues, Duration.String, json tags)
    const auto v = RawSweepValues();
    CHECK(v.size() == 33);
    CHECK(v.front() == 0);
    CHECK(v[1] == 8);
    CHECK(v[31] == 248);
    CHECK(v.back() == 255);
    CHECK(DurationString(200ms) == "200ms");
    CHECK(DurationString(1ms) == "1ms");
    CHECK(DurationString(1000ms) == "1s");
    CHECK(DurationString(1500ms) == "1.5s");
    CHECK(DurationString(61000ms) == "1m1s");
    CHECK(DurationString(0ms) == "0s");

    DiagnosticsEntry e;
    e.phase = "p";
    e.latency_ms = 0.5;
    CHECK(ToJson(e) == R"({"timeOffsetMs":0,"phase":"p","latencyMs":0.5})");  // zero / empty omitted like Go omitempty
    e.channel = "vibration";
    e.wanted_value = 0.25;
    e.sent_raw = 0;
    e.error = "x";
    CHECK(ToJson(e) ==
          R"({"timeOffsetMs":0,"phase":"p","channel":"vibration","wantedValue":0.25,"sentRaw":0,"latencyMs":0.5,"error":"x"})");

    DiagnosticsReport r;
    r.started_at_unix_ms = 1760000000123;
    r.duration_ms = 5;
    CHECK(ToJson(r).rfind(R"({"startedAt":"2025-10-09T08:53:20.123Z","durationMs":5,"rawCapable":false,)", 0) == 0);
}
