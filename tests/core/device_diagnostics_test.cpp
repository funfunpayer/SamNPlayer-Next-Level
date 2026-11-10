#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>

#include "samn/device/diagnostics.h"
#include "samn/device/mock.h"
#include "samn/settings.h"

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
        if (n.find("Raw value sweep skipped") != std::string::npos) note = true;
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

// Review C6: after a cancel the offset-change loop still slept its delay up
// to four times.
TEST_CASE("device/diagnostics: a cancel in the offset change stops the pauses") {
    MockDevice dev(false);
    DiagnosticsOptions o = Fast();
    o.offset_change_delay = 7ms;  // told apart from the other pauses (1 ms)
    int offset_pauses = 0;
    o.sleep = [&](std::chrono::milliseconds d) {
        if (d == 7ms) ++offset_pauses;
    };
    o.cancelled = [&] { return offset_pauses >= 1; };  // cancel after the first offset pause
    DiagnosticsReport report = RunDiagnostics(dev, 0, o);
    CHECK(report.interrupted);
    CHECK(offset_pauses == 1);
    CHECK(report.log.back().phase == "stop");
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

namespace {

struct TempDir {
    std::filesystem::path path;
    TempDir() {
        static std::atomic<int> counter{0};
        std::random_device rd;
        path = std::filesystem::temp_directory_path() /
               ("samn_diag_" + std::to_string(rd()) + "_" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "_" +
                std::to_string(counter++));
        std::filesystem::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
};

}  // namespace

TEST_CASE("device/diagnostics: history JSONL round-trip, newest first") {
    DiagnosticsReport r;
    r.started_at_unix_ms = 1760000000123;
    r.interrupted = true;
    DiagnosticsPhaseSummary a, b;
    a.commands = 12;
    a.errors = 1;
    b.commands = 4;
    b.errors = 0;
    r.phases.push_back(a);
    r.phases.push_back(b);

    const DiagnosticsHistoryEntry e = HistoryEntryFromReport(r, "Mock device", "mock");
    CHECK(e.started_at == "2025-10-09T08:53:20.123Z");
    CHECK(e.name == "Mock device");
    CHECK(e.kind == "mock");
    CHECK(e.commands == 16);
    CHECK(e.errors == 1);
    CHECK(e.interrupted);
    CHECK(FormatDiagnosticsHistoryLine(e) ==
          "2025-10-09 (Mock) \xE2\x80\x94 16 commands, 1 errors, interrupted");
    CHECK(ToJson(e) ==
          R"({"startedAt":"2025-10-09T08:53:20.123Z","name":"Mock device","kind":"mock","commands":16,"errors":1,"interrupted":true})");

    DiagnosticsHistoryEntry ble;
    ble.started_at = "2025-10-10T00:00:00.000Z";
    ble.name = "Sam Neo 2";
    ble.kind = "ble";
    ble.commands = 8;
    ble.errors = 0;
    CHECK(FormatDiagnosticsHistoryLine(ble) == "2025-10-10 (Sam Neo 2) \xE2\x80\x94 8 commands, 0 errors");

    TempDir t;
    const auto path = t.path / "nested" / "device_diagnostics_history.jsonl";
    CHECK(LoadDiagnosticsHistory(path).empty());  // missing file is not an error

    AppendDiagnosticsHistory(path, e);
    AppendDiagnosticsHistory(path, ble);
    {
        std::ofstream(path, std::ios::binary | std::ios::app) << "not json\n{\"commands\":\"x\"}\n";
    }
    const auto loaded = LoadDiagnosticsHistory(path);
    REQUIRE(loaded.size() == 2);
    CHECK(loaded[0].kind == "ble");  // newest first
    CHECK(loaded[0].name == "Sam Neo 2");
    CHECK(loaded[1].kind == "mock");
    CHECK(loaded[1].interrupted);

    CHECK(ResolveDiagnosticsHistoryPath("").filename() == "device_diagnostics_history.jsonl");
    CHECK(ResolveDiagnosticsHistoryPath("  \t ").filename() == "device_diagnostics_history.jsonl");
    CHECK(ResolveDiagnosticsHistoryPath(path.u8string()) == path);
}
