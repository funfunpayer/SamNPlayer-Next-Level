#pragma once

// Device diagnostics (Go device/diagnostics.go): the measurement series of
// docs/SAM_NEO_2_RESEARCH.md §11/12, as far as it is objectively measurable
// without a sensor on the device: which raw values the device accepts without
// error, how the write latency behaves at rising update rates, and whether
// the two channels disturb each other (write errors / latency outliers, NOT
// felt intensity). Not measured, and the report says so in `notes`: physical
// rise / fall time, felt intensity, felt channel overlap.
//
// The device must already be connected; RunDiagnostics neither connects nor
// disconnects. Sleeping and the cancel check are injectable so tests (and the
// app's cancel button) need no real waiting.

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "samn/device/device.h"

namespace samn::device {

struct DiagnosticsEntry {
    std::int64_t time_offset_ms = 0;
    std::string phase;
    std::string channel;
    double wanted_value = 0;
    std::optional<int> sent_raw;
    double latency_ms = 0;
    std::string error;
};

struct DiagnosticsPhaseSummary {
    std::string phase;
    int commands = 0;
    int errors = 0;
    double mean_latency_ms = 0;
    double max_latency_ms = 0;
};

struct DiagnosticsReport {
    std::int64_t started_at_unix_ms = 0;
    std::int64_t duration_ms = 0;
    bool raw_capable = false;
    std::vector<DiagnosticsPhaseSummary> phases;
    std::vector<DiagnosticsEntry> log;
    std::vector<std::string> notes;
    bool interrupted = false;
};

struct DiagnosticsOptions {
    // Falling gaps between commands, to find the highest stable update rate.
    std::vector<std::chrono::milliseconds> update_rate_intervals = {
        std::chrono::milliseconds(200), std::chrono::milliseconds(100),
        std::chrono::milliseconds(50), std::chrono::milliseconds(20),
        std::chrono::milliseconds(10)};
    int commands_per_rate_step = 8;
    std::chrono::milliseconds offset_change_delay{100};
    std::chrono::milliseconds rapid_switch_delay{30};
    // Default: std::this_thread::sleep_for.
    std::function<void(std::chrono::milliseconds)> sleep;
    // Polled between commands; true aborts the run (report.interrupted).
    std::function<bool()> cancelled;
    // Called once per log entry as it is recorded.
    std::function<void(const DiagnosticsEntry&)> on_entry;
};

// A raw-capable device (dynamic_cast to RawCapable) gets the raw sweeps.
// connect_latency_ms > 0 is logged as the first entry, phase "connection".
DiagnosticsReport RunDiagnostics(Device& dev, double connect_latency_ms = 0,
                                 const DiagnosticsOptions& options = {});

// 0, 8, 16 ... 248, 255: a sample over 0-255 instead of all 256 values.
std::vector<int> RawSweepValues();

// Go's time.Duration.String() for a millisecond value: "200ms", "1s", "1.5s".
std::string DurationString(std::chrono::milliseconds d);

// The report as 0.7 sends it to the GUI (camelCase keys, struct order,
// zero `wantedValue`, empty `channel` / `error` and absent `sentRaw` omitted).
// startedAt is UTC ISO-8601 with milliseconds.
std::string ToJson(const DiagnosticsEntry& e);
std::string ToJson(const DiagnosticsReport& r);

}  // namespace samn::device
