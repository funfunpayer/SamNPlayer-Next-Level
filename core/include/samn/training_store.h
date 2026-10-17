#pragma once
// Training on disk (0.7 app_training_scripts.go, app_training_history.go and the
// session log of app_training.go): custom scripts, the per-session JSONL log, the
// history summary, its CSV and the "auto-adjust from recent sessions" nudge.
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "samn/training.h"

namespace samn::training {

// ---- custom scripts: one JSON file per script, named by a slug of the name ----

// Lower case, runs of anything but [a-z0-9-] become "-", trimmed. nullopt when
// nothing is left ("script name must contain at least one letter or number").
std::optional<std::string> script_file_slug(const std::string& name);
// config_dir()/training_scripts
std::filesystem::path scripts_dir();
// Validates and writes; "" on success, else the reason (built-in name, no name,
// invalid script, or a different script already uses the same file name).
std::string save_custom_script(Script script, const std::filesystem::path& dir);
// Sorted by name; unreadable files are skipped.
std::vector<Script> list_custom_scripts(const std::filesystem::path& dir);
// Built-ins first, then custom ones.
std::optional<Script> find_script(const std::string& name, const std::filesystem::path& dir);
// "" on success, else the reason (built-in, unknown, no such script).
std::string delete_custom_script(const std::string& name, const std::filesystem::path& dir);

// ---- session log: JSONL, one line per cycle ----

struct LogEntry {
    std::string timestamp, technique, channel;
    int cycle_index = 0, cycles_total = 0;
    double peak_intensity = 0;
    int hold_ms = 0;
    long long duration_ms = 0;
    bool stopped_by_user = false;
    int reached_peak_after_ms = 0, arousal_before = 0, rest_ms = 0;
    // script sessions only
    std::string phase_name;
    double vibration_peak = 0, suction_peak = 0, peak_factor_applied = 0, rest_factor_applied = 0;
};
nlohmann::json to_json(const LogEntry& e);
LogEntry log_entry(const Options& o, const CycleResult& r, const std::string& timestamp);
LogEntry log_entry(const std::string& script_name, const ScriptCycleResult& r, const std::string& timestamp);
// config_dir()/logs/sessions
std::filesystem::path sessions_dir();
// training-<slug>-YYYYMMDD-HHMMSS.jsonl ("session" when the name has no usable letters).
std::string session_log_name(const std::string& technique_or_script, const std::string& stamp);
// RFC 3339, UTC.
std::string timestamp_now();
// "YYYYMMDD-HHMMSS" (local) for file names.
std::string file_stamp_now();

// ---- history ----

struct SessionSummary {
    std::string file_name, started_at, technique, channel;
    int cycles_completed = 0, cycles_stopped_early = 0;
    double mean_peak_intensity = 0, mean_reached_peak_after_ms = 0, mean_arousal_reported = 0;
    int arousal_reports_count = 0;
};
// nullopt for an empty or unreadable log. A broken line only drops that line.
std::optional<SessionSummary> summarize_session_log(const std::filesystem::path& file);
// Newest first, at most `max` (0.7: 50).
std::vector<SessionSummary> training_history(const std::filesystem::path& dir, size_t max = 50);
// Header + one row per session.
std::string history_csv(const std::vector<SessionSummary>& history);

struct HistoryAdjustment {
    double factor = 1;
    std::string message;  // "" = nothing to say
};
// The last 3 sessions with this technique / script name: an early stop in the
// newest -> x0.85, two or more all clean -> x1.1.
HistoryAdjustment history_adjustment(const std::vector<SessionSummary>& history, const std::string& key);
// tier x (history nudge if auto) clamped to 0.55-1.35; 1 when no script is selected.
double combined_intensity_factor(bool script_selected, double tier, bool auto_adjust,
                                 const std::vector<SessionSummary>& history, const std::string& key);

}  // namespace samn::training
