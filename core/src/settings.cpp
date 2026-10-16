#include "samn/settings.h"

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

std::string env(const char* name) {
#ifdef _WIN32
    // _dupenv_s: the safe CRT variant (getenv is deprecated on MSVC).
    char* buf = nullptr;
    size_t len = 0;
    if (_dupenv_s(&buf, &len, name) != 0 || buf == nullptr) return {};
    std::string v(buf);
    std::free(buf);
    return v;
#else
    const char* v = std::getenv(name);
    return v ? std::string(v) : std::string();
#endif
}

fs::path home_dir() {
#ifdef _WIN32
    if (auto p = env("USERPROFILE"); !p.empty()) return fs::path(p);
#endif
    if (auto h = env("HOME"); !h.empty()) return fs::path(h);
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

std::string unique_suffix() {
    static std::mt19937_64 rng{std::random_device{}() ^
                               static_cast<uint64_t>(
                                   std::chrono::steady_clock::now().time_since_epoch().count())};
    static std::mutex m;
    std::lock_guard<std::mutex> lk(m);
    std::ostringstream os;
    os << std::hex << rng();
    return os.str();
}

}  // namespace

fs::path config_dir() {
    if (auto e = env("SAMN_CONFIG_DIR"); !e.empty()) return fs::path(e);
    fs::path base;
#ifdef _WIN32
    if (auto a = env("APPDATA"); !a.empty())
        base = fs::path(a);
    else
        base = home_dir() / "AppData" / "Roaming";
#else
    if (auto x = env("XDG_CONFIG_HOME"); !x.empty())
        base = fs::path(x);
    else
        base = home_dir() / ".config";
#endif
    return base / "SamNPlayer2";
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
            else
                broken = true;  // invalid JSON, or not an object
        }
    }
    if (broken) {
        // A broken file is kept aside, not silently overwritten.
        fs::path bad = path_;
        bad.replace_extension(".broken.json");
        replace_file(path_, bad);
    }
}

nlohmann::json Settings::get(const std::string& key, const nlohmann::json& default_value) const {
    std::lock_guard<std::mutex> lk(mutex_);
    auto it = data_.find(key);
    return it == data_.end() ? default_value : *it;
}

void Settings::set(const std::string& key, const nlohmann::json& value) {
    set_many({{key, value}});
}

void Settings::set_many(const std::map<std::string, nlohmann::json>& values) {
    std::lock_guard<std::mutex> lk(mutex_);
    for (const auto& [k, v] : values) data_[k] = v;
    fs::path dir = path_.parent_path();
    if (dir.empty()) dir = ".";
    std::error_code ec;
    fs::create_directories(dir, ec);
    fs::path tmp = dir / (".settings-" + unique_suffix() + ".tmp");
    bool ok = false;
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (out) {
            out << data_.dump(2, ' ', true);
            out.flush();
            ok = static_cast<bool>(out);
        }
    }
    if (ok) ok = replace_file(tmp, path_);
    if (!ok) {
        std::error_code ec2;
        fs::remove(tmp, ec2);
        throw std::runtime_error("cannot write settings file: " + path_.string());
    }
}

}  // namespace samn
