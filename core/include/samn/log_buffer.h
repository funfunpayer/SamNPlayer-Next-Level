#pragma once
// The app's log (0.7 logging/ ring buffer and log.js): the newest entries in
// memory for the Log tab, a level below which entries are dropped (setting
// "log.level", default info), and one line format for view, copy and file.
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace samn {

enum class LogSeverity { kDebug = 0, kInfo = 1, kWarn = 2, kError = 3 };

struct LogEntry {
    int64_t ms = 0;  // milliseconds since the epoch
    LogSeverity level = LogSeverity::kInfo;
    std::string text;
};

// "debug" | "info" | "warn" | "error" (also "warning"); nullopt otherwise.
std::optional<LogSeverity> ParseLogLevel(const std::string& name);
const char* LogLevelName(LogSeverity level);  // "debug" ... "error"

// "HH:MM:SS.mmm  LEVEL  message" in local time (0.7 log.js).
std::string FormatLogLine(const LogEntry& entry);

// Thread-safe; the oldest entry goes when the buffer is full.
class LogBuffer {
public:
    static constexpr std::size_t kDefaultCapacity = 800;  // 0.7 log.js MAX
    explicit LogBuffer(std::size_t capacity = kDefaultCapacity);

    // false when below the level (nothing is kept).
    bool Add(LogEntry entry);
    std::vector<LogEntry> Entries(bool warn_only = false) const;
    void Clear();
    void SetLevel(LogSeverity level);
    LogSeverity level() const;
    // Changes with every Add or Clear: the view knows when to redraw.
    uint64_t generation() const;

private:
    mutable std::mutex mu_;
    std::deque<LogEntry> entries_;
    std::size_t capacity_;
    LogSeverity level_ = LogSeverity::kInfo;
    uint64_t generation_ = 0;
};

}  // namespace samn
