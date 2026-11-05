#include "samn/settings.h"
#include "samn/fileio.h"
#include "samn/time_util.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <random>
#include <sstream>
#include <stdexcept>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace samn {
namespace fs = std::filesystem;

namespace {

#ifdef _WIN32
// Wide env → path. The narrow _dupenv_s / getenv path uses the ANSI code
// page, so a user name outside that page (e.g. Jürgen, 太郎) yields a wrong
// %APPDATA% and the settings land in another folder.
fs::path env_path(const wchar_t* name) {
    wchar_t* buf = nullptr;
    size_t len = 0;
    if (_wdupenv_s(&buf, &len, name) != 0 || buf == nullptr) return {};
    fs::path p(buf);
    std::free(buf);
    return p;
}
#else
fs::path env_path(const char* name) {
    const char* v = std::getenv(name);
    return v ? fs::path(v) : fs::path();
}
#endif

fs::path home_dir() {
#ifdef _WIN32
    if (auto p = env_path(L"USERPROFILE"); !p.empty()) return p;
    if (auto h = env_path(L"HOME"); !h.empty()) return h;
#else
    if (auto h = env_path("HOME"); !h.empty()) return h;
#endif
    return fs::path();
}

// Atomic replace: MoveFileExW on Windows, rename elsewhere.
bool replace_file(const fs::path& from, const fs::path& to) {
#ifdef _WIN32
    return MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
#else
    std::error_code ec;
    fs::rename(from, to, ec);
    return !ec;
#endif
}

// settings.json → settings.broken.YYYYMMDD-HHMMSS.json; .2, .3… if that name is taken
// (0.7 / GAP-20: never overwrite an earlier corrupt aside).
fs::path broken_aside_path(const fs::path& settings_path) {
    using clock = std::chrono::system_clock;
    const auto t = clock::to_time_t(clock::now());
    const std::tm tm = TimeToTm(t, true);
    char stamp[32];
    std::snprintf(stamp, sizeof stamp, "%04d%02d%02d-%02d%02d%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                  tm.tm_hour, tm.tm_min, tm.tm_sec);
    const fs::path parent = settings_path.parent_path().empty() ? fs::path(".") : settings_path.parent_path();
    const std::string stem = settings_path.stem().string();
    fs::path candidate = parent / (stem + ".broken." + stamp + ".json");
    std::error_code ec;
    for (int n = 2; fs::exists(candidate, ec); ++n)
        candidate = parent / (stem + ".broken." + stamp + "." + std::to_string(n) + ".json");
    return candidate;
}

}  // namespace

fs::path config_dir() {
#ifdef _WIN32
    if (auto e = env_path(L"SAMN_CONFIG_DIR"); !e.empty()) return e;
    if (auto a = env_path(L"APPDATA"); !a.empty()) return a / "SamNPlayer2";
    return home_dir() / "AppData" / "Roaming" / "SamNPlayer2";
#else
    if (auto e = env_path("SAMN_CONFIG_DIR"); !e.empty()) return e;
    if (auto x = env_path("XDG_CONFIG_HOME"); !x.empty()) return x / "SamNPlayer2";
    return home_dir() / ".config" / "SamNPlayer2";
#endif
}

Settings::Settings(fs::path path) : path_(path.empty() ? config_dir() / "settings.json" : std::move(path)) {
    std::error_code ec;
    if (!fs::exists(path_, ec) && !ec) return;  // not found: start empty
    bool broken = false;
    {
        const auto text = ReadFileToString(path_);
        if (!text) {
            broken = true;
        } else {
            auto parsed = nlohmann::json::parse(*text, nullptr, false);
            if (parsed.is_object())
                data_ = std::move(parsed);
            else if (!parsed.is_null())
                broken = true;  // invalid JSON, or not an object
            // A file holding just `null` is empty, not broken (0.7 did the same).
        }
    }
    if (broken) {
        // A broken file is kept aside, not silently overwritten.
        replace_file(path_, broken_aside_path(path_));
        return;
    }
    stamp_ = fs::last_write_time(path_, ec);
    size_ = fs::file_size(path_, ec);
}

void Settings::RefreshLocked(bool force) const {
    std::error_code ec;
    const auto stamp = fs::last_write_time(path_, ec);
    if (ec) return;  // no file (yet): keep what we have
    const auto size = fs::file_size(path_, ec);
    if (ec || (!force && stamp == stamp_ && size == size_)) return;
    const auto text = ReadFileToString(path_);
    if (!text) return;
    auto parsed = nlohmann::json::parse(*text, nullptr, false);
    if (!parsed.is_object()) return;  // a half-broken file never wipes our values
    data_ = std::move(parsed);
    stamp_ = stamp;
    size_ = size;
}

nlohmann::json Settings::get(const std::string& key, const nlohmann::json& default_value) const {
    std::lock_guard<std::mutex> lk(mutex_);
    RefreshLocked(false);
    auto it = data_.find(key);
    return it == data_.end() ? default_value : *it;
}

void Settings::set(const std::string& key, const nlohmann::json& value) {
    set_many({{key, value}});
}

void Settings::set_many(const std::map<std::string, nlohmann::json>& values) {
    std::lock_guard<std::mutex> lk(mutex_);
    RefreshLocked(true);  // keep what other Settings objects wrote
    // Change a copy: when the write fails the values in memory stay as they
    // are on disk, and the next set() does not carry the bad one along.
    nlohmann::json next = data_;
    for (const auto& [k, v] : values) next[k] = v;
    fs::path dir = path_.parent_path();
    if (dir.empty()) dir = ".";
    std::error_code ec;
    fs::create_directories(dir, ec);
    try {
        // Invalid UTF-8 in a key or value (a file name from an old encoding)
        // becomes U+FFFD instead of an exception.
        WriteFileAtomic(path_, next.dump(2, ' ', true, nlohmann::json::error_handler_t::replace));
    } catch (const std::exception& e) {
        throw std::runtime_error("cannot write settings file: " + path_.u8string() + ": " + e.what());
    }
    data_ = std::move(next);
    stamp_ = fs::last_write_time(path_, ec);
    size_ = fs::file_size(path_, ec);
}

}  // namespace samn
