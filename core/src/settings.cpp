#include "samn/settings.h"
#include "samn/fileio.h"

#include <chrono>
#include <cstdlib>
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
        std::ifstream in(path_, std::ios::binary);
        if (!in) {
            broken = true;
        } else {
            std::ostringstream ss;
            ss << in.rdbuf();
            auto parsed = nlohmann::json::parse(ss.str(), nullptr, false);
            if (parsed.is_object())
                data_ = std::move(parsed);
            else if (!parsed.is_null())
                broken = true;  // invalid JSON, or not an object
            // A file holding just `null` is empty, not broken (0.7 did the same).
        }
    }
    if (broken) {
        // A broken file is kept aside, not silently overwritten.
        fs::path bad = path_;
        bad.replace_extension(".broken.json");
        replace_file(path_, bad);
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
    std::ifstream in(path_, std::ios::binary);
    if (!in) return;
    std::ostringstream ss;
    ss << in.rdbuf();
    auto parsed = nlohmann::json::parse(ss.str(), nullptr, false);
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
    for (const auto& [k, v] : values) data_[k] = v;
    fs::path dir = path_.parent_path();
    if (dir.empty()) dir = ".";
    std::error_code ec;
    fs::create_directories(dir, ec);
    try {
        WriteFileAtomic(path_, data_.dump(2, ' ', true));
    } catch (const std::exception&) {
        throw std::runtime_error("cannot write settings file: " + path_.u8string());
    }
    stamp_ = fs::last_write_time(path_, ec);
    size_ = fs::file_size(path_, ec);
}

}  // namespace samn
