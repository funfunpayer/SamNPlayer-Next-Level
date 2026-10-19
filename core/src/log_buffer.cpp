#include "samn/log_buffer.h"

#include <cctype>
#include <cstdio>
#include <ctime>

namespace samn {

std::optional<LogSeverity> ParseLogLevel(const std::string& name) {
    std::string n;
    for (char c : name)
        if (!std::isspace(static_cast<unsigned char>(c))) n += char(std::tolower(static_cast<unsigned char>(c)));
    if (n == "debug") return LogSeverity::kDebug;
    if (n == "info") return LogSeverity::kInfo;
    if (n == "warn" || n == "warning") return LogSeverity::kWarn;
    if (n == "error") return LogSeverity::kError;
    return std::nullopt;
}

const char* LogLevelName(LogSeverity level) {
    switch (level) {
        case LogSeverity::kDebug: return "debug";
        case LogSeverity::kInfo: return "info";
        case LogSeverity::kWarn: return "warn";
        case LogSeverity::kError: return "error";
    }
    return "info";
}

std::string FormatLogLine(const LogEntry& e) {
    const std::time_t secs = static_cast<std::time_t>(e.ms / 1000);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &secs);
#else
    localtime_r(&secs, &tm);
#endif
    const char* names[] = {"DEBUG", "INFO", "WARN", "ERROR"};
    char head[48];
    std::snprintf(head, sizeof head, "%02d:%02d:%02d.%03d  %-5s  ", tm.tm_hour, tm.tm_min, tm.tm_sec,
                  static_cast<int>(((e.ms % 1000) + 1000) % 1000), names[static_cast<int>(e.level)]);
    return head + e.text;
}

LogBuffer::LogBuffer(std::size_t capacity) : capacity_(capacity == 0 ? 1 : capacity) {}

bool LogBuffer::Add(LogEntry entry) {
    std::lock_guard<std::mutex> l(mu_);
    if (entry.level < level_) return false;
    entries_.push_back(std::move(entry));
    while (entries_.size() > capacity_) entries_.pop_front();
    ++generation_;
    return true;
}

std::vector<LogEntry> LogBuffer::Entries(bool warn_only) const {
    std::lock_guard<std::mutex> l(mu_);
    std::vector<LogEntry> out;
    out.reserve(entries_.size());
    for (const LogEntry& e : entries_)
        if (!warn_only || e.level >= LogSeverity::kWarn) out.push_back(e);
    return out;
}

void LogBuffer::Clear() {
    std::lock_guard<std::mutex> l(mu_);
    entries_.clear();
    ++generation_;
}

void LogBuffer::SetLevel(LogSeverity level) {
    std::lock_guard<std::mutex> l(mu_);
    level_ = level;
}

LogSeverity LogBuffer::level() const {
    std::lock_guard<std::mutex> l(mu_);
    return level_;
}

uint64_t LogBuffer::generation() const {
    std::lock_guard<std::mutex> l(mu_);
    return generation_;
}

}  // namespace samn
