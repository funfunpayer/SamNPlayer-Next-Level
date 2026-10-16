#pragma once
// App settings: one JSON file in the user's config folder. Port of samn/settings.py.
// Same keys as SamNPlayer 0.7 where a setting exists there (for example
// "playback.offset.<script path>"). Writes are atomic (temp file + replace).
#include <filesystem>
#include <map>
#include <mutex>
#include <string>

#include <nlohmann/json.hpp>

namespace samn {

// SAMN_CONFIG_DIR, else %APPDATA%/SamNPlayer2 (Windows), else
// $XDG_CONFIG_HOME or ~/.config + /SamNPlayer2.
std::filesystem::path config_dir();

class Settings {
public:
    // Empty path: config_dir()/settings.json. A broken file is kept aside as
    // settings.broken.json and the settings start empty.
    explicit Settings(std::filesystem::path path = {});
    Settings(const Settings&) = delete;
    Settings& operator=(const Settings&) = delete;

    const std::filesystem::path& path() const { return path_; }

    nlohmann::json get(const std::string& key, const nlohmann::json& default_value = nullptr) const;
    // Throws std::runtime_error when the file cannot be written.
    void set(const std::string& key, const nlohmann::json& value);
    void set_many(const std::map<std::string, nlohmann::json>& values);

private:
    std::filesystem::path path_;
    mutable std::mutex mutex_;
    nlohmann::json data_ = nlohmann::json::object();
};

}  // namespace samn
