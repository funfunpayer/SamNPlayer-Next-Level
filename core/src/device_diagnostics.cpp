#include "samn/device/diagnostics.h"

#include <cstdio>
#include <map>
#include <thread>

#include "samn/device/go_json.h"

namespace samn::device {

using Clock = std::chrono::steady_clock;

std::vector<int> RawSweepValues() {
    std::vector<int> values;
    values.reserve(33);
    for (int v = 0; v <= 255; v += 8) values.push_back(v);
    if (values.back() != 255) values.push_back(255);
    return values;
}

std::string DurationString(std::chrono::milliseconds d) {
    long long ms = d.count();
    if (ms == 0) return "0s";
    std::string sign;
    if (ms < 0) {
        sign = "-";
        ms = -ms;
    }
    if (ms < 1000) return sign + std::to_string(ms) + "ms";
    const long long h = ms / 3600000;
    const long long m = (ms / 60000) % 60;
    const long long rest_ms = ms % 60000;
    std::string secs = std::to_string(rest_ms / 1000);
    if (rest_ms % 1000 != 0) {
        char frac[8];
        std::snprintf(frac, sizeof frac, "%03lld", rest_ms % 1000);
        std::string f = frac;
        while (!f.empty() && f.back() == '0') f.pop_back();
        secs += "." + f;
    }
    std::string out = sign;
    if (h > 0) out += std::to_string(h) + "h";
    if (h > 0 || m > 0) out += std::to_string(m) + "m";
    return out + secs + "s";
}

namespace {

struct Timed {
    Status status;
    double latency_ms = 0;
};

template <class F>
Timed Measure(F&& f) {
    const auto t0 = Clock::now();
    Status s = f();
    const auto us = std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - t0).count();
    return {std::move(s), static_cast<double>(us) / 1000.0};
}

}  // namespace

DiagnosticsReport RunDiagnostics(Device& dev, double connect_latency_ms, const DiagnosticsOptions& opt) {
    const auto start = Clock::now();
    DiagnosticsReport report;
    report.started_at_unix_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::system_clock::now().time_since_epoch())
                                    .count();

    std::map<std::string, int> phase_commands, phase_errors;
    std::map<std::string, double> phase_latency_sum, phase_latency_max;
    std::vector<std::string> phase_order;

    auto sleep = [&opt](std::chrono::milliseconds d) {
        if (opt.sleep) {
            opt.sleep(d);
        } else {
            std::this_thread::sleep_for(d);
        }
    };
    auto record = [&](DiagnosticsEntry e) {
        e.time_offset_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
        if (!phase_commands.count(e.phase)) phase_order.push_back(e.phase);
        phase_commands[e.phase]++;
        if (!e.error.empty()) phase_errors[e.phase]++;
        phase_latency_sum[e.phase] += e.latency_ms;
        if (e.latency_ms > phase_latency_max[e.phase]) phase_latency_max[e.phase] = e.latency_ms;
        if (opt.on_entry) opt.on_entry(e);
        report.log.push_back(std::move(e));
    };
    auto interrupted = [&opt]() { return opt.cancelled && opt.cancelled(); };

    if (connect_latency_ms > 0) {
        DiagnosticsEntry e;
        e.phase = "connection";
        e.latency_ms = connect_latency_ms;
        record(std::move(e));
    }

    auto* raw = dynamic_cast<RawCapable*>(&dev);
    report.raw_capable = raw != nullptr;

    // Phase 1: which raw values does the device accept without error, per channel.
    if (raw) {
        struct Ch {
            const char* name;
            std::function<Status(std::uint8_t)> set;
        };
        const Ch channels[] = {{"vibration", [raw](std::uint8_t v) { return raw->SetVibrationRaw(v); }},
                               {"suction", [raw](std::uint8_t v) { return raw->SetSuctionRaw(v); }}};
        for (const auto& ch : channels) {
            const std::string phase = std::string("raw_sweep_") + ch.name;
            for (int value : RawSweepValues()) {
                if (interrupted()) {
                    report.interrupted = true;
                    break;
                }
                Timed t = Measure([&] { return ch.set(static_cast<std::uint8_t>(value)); });
                DiagnosticsEntry e;
                e.phase = phase;
                e.channel = ch.name;
                e.wanted_value = value / 255.0;
                e.sent_raw = value;
                e.latency_ms = t.latency_ms;
                if (!t.status) e.error = t.status.message;
                record(std::move(e));
            }
            (void)ch.set(0);
            if (report.interrupted) break;
        }
    } else {
        report.notes.push_back(
            "Raw value sweep skipped: this transport has no raw values (only the real BLE connection and the Mock have them).");
    }

    // Phase 2: highest stable update rate (vibration only; raw if available,
    // else through the regular 0.0-1.0 channel).
    if (!report.interrupted) {
        const std::string phase = "update_rate";
        for (const auto interval : opt.update_rate_intervals) {
            for (int i = 0; i < opt.commands_per_rate_step; ++i) {
                if (interrupted()) {
                    report.interrupted = true;
                    break;
                }
                const double value = i % 2 == 0 ? 1.0 : 0.0;
                Timed t = Measure([&] {
                    if (raw) return raw->SetVibrationRaw(i % 2 == 0 ? 255 : 0);
                    return dev.SetVibration(value);
                });
                DiagnosticsEntry e;
                e.phase = phase + "_" + DurationString(interval);
                e.channel = "vibration";
                e.wanted_value = value;
                e.latency_ms = t.latency_ms;
                if (!t.status) e.error = t.status.message;
                record(std::move(e));
                sleep(interval);
            }
            if (report.interrupted) break;
        }
        if (raw) {
            (void)raw->SetVibrationRaw(0);
        } else {
            (void)dev.SetVibration(0);
        }
    }

    // Phase 3: channel interaction: vibration alone, suction alone, both at
    // once, offset in time, rapid switching. Through the regular 0.0-1.0
    // interface, because that is the real playback path.
    if (!report.interrupted) {
        auto run_setter = [&](const std::string& phase, const char* channel,
                              const std::function<Status(double)>& set, double value) {
            if (interrupted()) {
                report.interrupted = true;
                return;
            }
            Timed t = Measure([&] { return set(value); });
            DiagnosticsEntry e;
            e.phase = phase;
            e.channel = channel;
            e.wanted_value = value;
            e.latency_ms = t.latency_ms;
            if (!t.status) e.error = t.status.message;
            record(std::move(e));
        };
        const std::function<Status(double)> vib = [&dev](double v) { return dev.SetVibration(v); };
        const std::function<Status(double)> suc = [&dev](double v) { return dev.SetSuction(v); };

        for (double v : {0.25, 0.5, 0.75, 1.0, 0.5, 0.0}) run_setter("vibration_alone", "vibration", vib, v);
        for (double v : {0.25, 0.5, 0.75, 1.0, 0.5, 0.0}) run_setter("suction_alone", "suction", suc, v);
        for (double v : {0.25, 0.5, 0.75, 1.0, 0.0}) {
            run_setter("simultaneous", "vibration", vib, v);
            run_setter("simultaneous", "suction", suc, v);
        }
        for (double v : {0.5, 0.0}) {
            run_setter("offset_change", "vibration", vib, v);
            sleep(opt.offset_change_delay);
            run_setter("offset_change", "suction", suc, v);
            sleep(opt.offset_change_delay);
        }
        for (int i = 0; i < 10 && !report.interrupted; ++i) {
            if (i % 2 == 0) {
                run_setter("rapid_switching", "vibration", vib, 0.8);
                run_setter("rapid_switching", "suction", suc, 0);
            } else {
                run_setter("rapid_switching", "vibration", vib, 0);
                run_setter("rapid_switching", "suction", suc, 0.8);
            }
            sleep(opt.rapid_switch_delay);
        }
    }

    (void)dev.Stop();
    {
        DiagnosticsEntry e;
        e.phase = "stop";
        record(std::move(e));
    }

    for (const auto& phase : phase_order) {
        const int n = phase_commands[phase];
        DiagnosticsPhaseSummary s;
        s.phase = phase;
        s.commands = n;
        s.errors = phase_errors[phase];
        s.mean_latency_ms = n > 0 ? phase_latency_sum[phase] / n : 0.0;
        s.max_latency_ms = phase_latency_max[phase];
        report.phases.push_back(std::move(s));
    }

    report.notes.push_back(
        "Not measured (no objective way without a sensor on the device): "
        "the real physical rise and fall time, felt intensity, "
        "felt channel overlap. Latencies here are software round trips "
        "of the write, not the device's physical reaction time.");
    report.duration_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
    return report;
}

namespace {

nlohmann::ordered_json EntryJson(const DiagnosticsEntry& e) {
    nlohmann::ordered_json j = nlohmann::ordered_json::object();
    j["timeOffsetMs"] = e.time_offset_ms;
    j["phase"] = e.phase;
    if (!e.channel.empty()) j["channel"] = e.channel;
    if (e.wanted_value != 0) j["wantedValue"] = e.wanted_value;
    if (e.sent_raw) j["sentRaw"] = *e.sent_raw;
    j["latencyMs"] = e.latency_ms;
    if (!e.error.empty()) j["error"] = e.error;
    return j;
}

// Days since 1970-01-01 -> civil date (Howard Hinnant's algorithm).
std::string IsoUtc(std::int64_t unix_ms) {
    std::int64_t secs = unix_ms / 1000;
    int ms = static_cast<int>(unix_ms % 1000);
    if (ms < 0) {
        ms += 1000;
        --secs;
    }
    std::int64_t days = secs / 86400;
    std::int64_t rem = secs % 86400;
    if (rem < 0) {
        rem += 86400;
        --days;
    }
    const std::int64_t z = days + 719468;
    const std::int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const std::int64_t doe = z - era * 146097;
    const std::int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const std::int64_t y = yoe + era * 400;
    const std::int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const std::int64_t mp = (5 * doy + 2) / 153;
    const int d = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
    const int m = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
    const std::int64_t year = y + (m <= 2 ? 1 : 0);
    char buf[96];
    std::snprintf(buf, sizeof buf, "%04lld-%02d-%02dT%02d:%02d:%02d.%03dZ", static_cast<long long>(year), m, d,
                  static_cast<int>(rem / 3600), static_cast<int>(rem % 3600 / 60), static_cast<int>(rem % 60), ms);
    return buf;
}

}  // namespace

std::string ToJson(const DiagnosticsEntry& e) { return GoJsonDump(EntryJson(e)); }

std::string ToJson(const DiagnosticsReport& r) {
    nlohmann::ordered_json j = nlohmann::ordered_json::object();
    j["startedAt"] = IsoUtc(r.started_at_unix_ms);
    j["durationMs"] = r.duration_ms;
    j["rawCapable"] = r.raw_capable;
    auto phases = nlohmann::ordered_json::array();
    for (const auto& p : r.phases) {
        nlohmann::ordered_json s = nlohmann::ordered_json::object();
        s["phase"] = p.phase;
        s["commands"] = p.commands;
        s["errors"] = p.errors;
        s["meanLatencyMs"] = p.mean_latency_ms;
        s["maxLatencyMs"] = p.max_latency_ms;
        phases.push_back(std::move(s));
    }
    j["phases"] = std::move(phases);
    auto log = nlohmann::ordered_json::array();
    for (const auto& e : r.log) log.push_back(EntryJson(e));
    j["log"] = std::move(log);
    j["notes"] = r.notes;
    j["interrupted"] = r.interrupted;
    return GoJsonDump(j);
}

}  // namespace samn::device
