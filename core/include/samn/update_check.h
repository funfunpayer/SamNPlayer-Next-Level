#pragma once
// Update check (0.7 update/update.go, update_ui.js), check only (D-147): the
// newest GitHub release is compared with this build and its page is offered
// as a link. Nothing is downloaded or replaced.
#include <array>
#include <optional>
#include <string>

namespace samn {

// Where releases are published.
inline constexpr const char* kReleaseRepo = "funfunpayer/SamNPlayer-Next-Level";
std::string LatestReleaseApiUrl();  // https://api.github.com/repos/<repo>/releases/latest
std::string ReleasesPageUrl();      // https://github.com/<repo>/releases
std::string ProjectPageUrl();       // https://github.com/<repo> (the support note, D-153)

// "v1.2.3" or "1.2.3" exactly (0.7 isPlainVersionTag); patch or other
// channel tags ("patch-v1.2.3-p1", "v1.2.3-rc1") are not releases.
std::optional<std::array<int, 3>> ParseReleaseTag(const std::string& tag);

// Whether `latest` (a release tag) is newer than `current` (this build,
// "2.0.0" or "2.0.0-dev"). A development build of a version is older than
// that version's release. An unreadable `current` counts as older (0.7:
// "dev" always sees the update); an unreadable `latest` is never newer.
bool IsNewerRelease(const std::string& current, const std::string& latest);

// Release notes in one short line (0.7 releaseNotesPreview): the first four
// non-empty lines that are not HTML comments, leading markup (#*_>`) stripped,
// joined with " · ", cut to `max_len` characters with "…".
std::string ReleaseNotesPreview(const std::string& body, std::size_t max_len = 220);

struct ReleaseInfo {
    std::string tag;
    std::string page_url;  // the release's page; ReleasesPageUrl() when not ours
    std::string notes;     // the full body
};

// Reads the GitHub "latest release" answer. nullopt when it is not JSON, has
// no plain version tag (0.7 refuses a patch tag published as a release) or
// is a draft / prerelease. `page_url` is only kept when it points into this
// project's releases, so the app never opens a link it did not expect.
std::optional<ReleaseInfo> ParseLatestRelease(const std::string& json_text);

}  // namespace samn
