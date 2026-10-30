#include "samn/training_store.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>

#include "samn/fileio.h"
#include "samn/settings.h"

namespace samn::training {

namespace fs = std::filesystem;
using nlohmann::json;

// ---------------------------------------------------------------- scripts

std::optional<std::string> script_file_slug(const std::string& name) {
    std::string out;
    bool dash = false;
    const auto first = name.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return std::nullopt;
    const auto last = name.find_last_not_of(" \t\r\n");
    for (size_t i = first; i <= last; ++i) {
        unsigned char c = static_cast<unsigned char>(name[i]);
        if (c >= 'A' && c <= 'Z') c = static_cast<unsigned char>(c - 'A' + 'a');
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
        if (ok) {
            out.push_back(static_cast<char>(c));
            dash = false;
        } else if (!dash) {
            out.push_back('-');
            dash = true;
        }
    }
    const auto a = out.find_first_not_of('-');
    if (a == std::string::npos) return std::nullopt;
    const auto b = out.find_last_not_of('-');
    return out.substr(a, b - a + 1);
}

fs::path scripts_dir() { return config_dir() / "training_scripts"; }

namespace {
std::optional<Script> read_script(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return std::nullopt;
    try {
        return json::parse(in).get<Script>();
    } catch (const std::exception&) {
        return std::nullopt;
    }
}
std::string trim(const std::string& s) {
    const auto a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    return s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
}
}  // namespace

std::string save_custom_script(Script script, const fs::path& dir) {
    script.name = trim(script.name);
    if (script.name.empty()) return "script needs a name";
    if (builtin_script(script.name)) return "\"" + script.name + "\" is a built-in script name - pick a different name";
    if (const std::string e = validate_script(script); !e.empty()) return e;
    const auto slug = script_file_slug(script.name);
    if (!slug) return "script name must contain at least one letter or number";
    std::error_code ec;
    fs::create_directories(dir, ec);
    const fs::path path = dir / (*slug + ".json");
    // Two names can reduce to one file name; editing in place (same name) must keep working.
    if (const auto existing = read_script(path); existing && existing->name != script.name)
        return "script name \"" + script.name + "\" reduces to the same file name (\"" + *slug +
               "\") as the already-saved script \"" + existing->name +
               "\" - pick a name that differs in letters or digits, not just spelling, punctuation or case";
    // Atomic: a crash or a full disk keeps the script as it was.
    try {
        WriteFileAtomic(path, json(script).dump(2));
    } catch (const std::exception&) {
        return "could not write " + path.u8string();
    }
    return {};
}

std::vector<Script> list_custom_scripts(const fs::path& dir) {
    std::vector<Script> out;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        if (!e.is_regular_file() || e.path().extension() != ".json") continue;
        if (auto s = read_script(e.path())) out.push_back(std::move(*s));
    }
    std::sort(out.begin(), out.end(), [](const Script& a, const Script& b) { return a.name < b.name; });
    return out;
}

std::optional<Script> find_script(const std::string& name, const fs::path& dir) {
    if (const Script* b = builtin_script(name)) return *b;
    for (auto& s : list_custom_scripts(dir))
        if (s.name == name) return std::move(s);
    return std::nullopt;
}

std::string delete_custom_script(const std::string& name, const fs::path& dir) {
    if (builtin_script(name)) return "\"" + name + "\" is a built-in script and cannot be deleted";
    const auto slug = script_file_slug(name);
    if (!slug) return "script name must contain at least one letter or number";
    std::error_code ec;
    if (!fs::remove(dir / (*slug + ".json"), ec)) return "no custom script named \"" + name + "\"";
    return {};
}

// ---------------------------------------------------------------- log

json to_json(const LogEntry& e) {
    json j = {{"timestamp", e.timestamp},
              {"technique", e.technique},
              {"channel", e.channel},
              {"cycleIndex", e.cycle_index},
              {"cyclesTotal", e.cycles_total},
              {"peakIntensity", e.peak_intensity},
              {"holdMs", e.hold_ms},
              {"durationMs", e.duration_ms},
              {"stoppedByUser", e.stopped_by_user},
              {"reachedPeakAfterMs", e.reached_peak_after_ms},
              {"arousalBefore", e.arousal_before},
              {"restMs", e.rest_ms}};
    if (!e.phase_name.empty()) j["phaseName"] = e.phase_name;
    if (e.vibration_peak != 0) j["vibrationPeak"] = e.vibration_peak;
    if (e.suction_peak != 0) j["suctionPeak"] = e.suction_peak;
    if (e.peak_factor_applied != 0) j["peakFactorApplied"] = e.peak_factor_applied;
    if (e.rest_factor_applied != 0) j["restFactorApplied"] = e.rest_factor_applied;
    return j;
}

LogEntry log_entry(const Options& o, const CycleResult& r, const std::string& timestamp) {
    LogEntry e;
    e.timestamp = timestamp;
    e.technique = to_string(o.technique);
    e.channel = to_string(o.channel);
    e.cycle_index = r.cycle_index;
    e.cycles_total = o.cycles;
    e.peak_intensity = r.peak_intensity;
    e.hold_ms = r.hold_ms;
    e.duration_ms = static_cast<long long>(r.ended_at_ms - r.started_at_ms);
    e.stopped_by_user = r.stopped_by_user;
    e.reached_peak_after_ms = r.reached_peak_after_ms;
    e.arousal_before = r.arousal_before;
    e.rest_ms = r.rest_ms;
    return e;
}

LogEntry log_entry(const std::string& script_name, const ScriptCycleResult& r, const std::string& timestamp) {
    LogEntry e;
    e.timestamp = timestamp;
    e.technique = script_name;
    e.channel = "script";
    e.cycle_index = r.phase_index * 1000 + r.repeat_index;  // only to keep the log readable in order
    e.peak_intensity = std::max(r.vibration_peak, r.suction_peak);
    e.duration_ms = static_cast<long long>(r.ended_at_ms - r.started_at_ms);
    e.stopped_by_user = r.stopped_by_user;
    e.reached_peak_after_ms = r.reached_peak_after_ms;
    e.arousal_before = r.arousal_before;
    e.rest_ms = r.rest_ms;
    e.phase_name = r.phase_name;
    e.vibration_peak = r.vibration_peak;
    e.suction_peak = r.suction_peak;
    e.peak_factor_applied = r.peak_factor_applied;
    e.rest_factor_applied = r.rest_factor_applied;
    return e;
}

fs::path sessions_dir() { return config_dir() / "logs" / "sessions"; }

std::string session_log_name(const std::string& key, const std::string& stamp) {
    const auto slug = script_file_slug(key);
    return "training-" + slug.value_or("session") + "-" + stamp + ".jsonl";
}

namespace {
std::string format_time(const char* fmt, bool utc) {
    const std::time_t t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm{};
#ifdef _WIN32
    if (utc) gmtime_s(&tm, &t); else localtime_s(&tm, &t);
#else
    if (utc) gmtime_r(&t, &tm); else localtime_r(&t, &tm);
#endif
    char buf[64];
    std::strftime(buf, sizeof buf, fmt, &tm);
    return buf;
}
}  // namespace

std::string timestamp_now() { return format_time("%Y-%m-%dT%H:%M:%SZ", true); }
std::string file_stamp_now() { return format_time("%Y%m%d-%H%M%S", false); }

// ---------------------------------------------------------------- history

std::optional<SessionSummary> summarize_session_log(const fs::path& file) {
    std::ifstream in(file);
    if (!in) return std::nullopt;
    SessionSummary s;
    int count = 0, reached_count = 0;
    double sum_peak = 0, sum_reached = 0, sum_arousal = 0;
    std::string line;
    while (std::getline(in, line)) {
        json j;
        try {
            j = json::parse(line);
        } catch (const std::exception&) {
            continue;
        }
        if (!j.is_object()) continue;
        if (count == 0) {
            s.started_at = j.value("timestamp", std::string());
            s.technique = j.value("technique", std::string());
            s.channel = j.value("channel", std::string());
        }
        ++count;
        if (j.value("stoppedByUser", false)) ++s.cycles_stopped_early;
        sum_peak += j.value("peakIntensity", 0.0);
        if (const int r = j.value("reachedPeakAfterMs", 0); r > 0) {
            sum_reached += r;
            ++reached_count;
        }
        if (const int a = j.value("arousalBefore", 0); a > 0) {
            sum_arousal += a;
            ++s.arousal_reports_count;
        }
    }
    if (count == 0) return std::nullopt;
    s.cycles_completed = count;
    s.mean_peak_intensity = sum_peak / count;
    if (reached_count) s.mean_reached_peak_after_ms = sum_reached / reached_count;
    if (s.arousal_reports_count) s.mean_arousal_reported = sum_arousal / s.arousal_reports_count;
    return s;
}

std::vector<SessionSummary> training_history(const fs::path& dir, size_t max) {
    std::vector<SessionSummary> out;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        if (!e.is_regular_file() || e.path().extension() != ".jsonl") continue;
        if (auto s = summarize_session_log(e.path())) {
            s->file_name = e.path().filename().u8string();
            out.push_back(std::move(*s));
        }
    }
    // By the timestamp inside the log, not the file name (that starts with the technique).
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.started_at > b.started_at; });
    if (out.size() > max) out.resize(max);
    return out;
}

namespace {
std::string csv_field(const std::string& s) {
    if (s.find_first_of(",\"\r\n") == std::string::npos) return s;
    std::string o = "\"";
    for (char c : s) o += c == '"' ? std::string("\"\"") : std::string(1, c);
    return o + "\"";
}
std::string fixed(double v, int digits) {
    std::ostringstream o;
    o << std::fixed << std::setprecision(digits) << v;
    return o.str();
}
}  // namespace

std::string history_csv(const std::vector<SessionSummary>& history) {
    std::string out =
        "startedAt,technique,channel,cyclesCompleted,cyclesStoppedEarly,meanPeakIntensityPct,"
        "meanReachedPeakAfterMs,meanArousalReported,arousalReportsCount\n";
    for (const auto& s : history)
        out += csv_field(s.started_at) + "," + csv_field(s.technique) + "," + csv_field(s.channel) + "," +
               std::to_string(s.cycles_completed) + "," + std::to_string(s.cycles_stopped_early) + "," +
               fixed(s.mean_peak_intensity * 100, 1) + "," + fixed(s.mean_reached_peak_after_ms, 0) + "," +
               fixed(s.mean_arousal_reported, 1) + "," + std::to_string(s.arousal_reports_count) + "\n";
    return out;
}

HistoryAdjustment history_adjustment(const std::vector<SessionSummary>& history, const std::string& key) {
    if (key.empty()) return {};
    std::vector<const SessionSummary*> match;
    for (const auto& s : history) {
        if (s.technique == key) match.push_back(&s);
        if (match.size() == 3) break;
    }
    if (match.empty()) return {};
    if (match[0]->cycles_stopped_early > 0) {
        const int n = match[0]->cycles_stopped_early;
        return {0.85, "Last session of this pattern had " + std::to_string(n) + " early stop" + (n > 1 ? "s" : "") +
                          " \xE2\x80\x94 maybe ease off a bit (lower peak or longer rest) this time."};
    }
    if (match.size() >= 2 &&
        std::all_of(match.begin(), match.end(), [](const auto* s) { return s->cycles_stopped_early == 0; }))
        return {1.1, "Last " + std::to_string(match.size()) +
                         " sessions of this pattern completed with no early stops \xE2\x80\x94 maybe a slightly "
                         "higher peak this time."};
    return {};
}

double combined_intensity_factor(bool script_selected, double tier, bool auto_adjust,
                                 const std::vector<SessionSummary>& history, const std::string& key) {
    if (!script_selected) return 1;
    double f = tier;
    if (auto_adjust) f *= history_adjustment(history, key).factor;
    return clamp_range(f, 0.55, 1.35);
}

}  // namespace samn::training
