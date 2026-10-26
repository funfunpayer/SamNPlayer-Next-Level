#include "samn/engine/track_cache.h"

#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <vector>

#include <nlohmann/json.hpp>
#include <opencv2/core/utility.hpp>

#include "samn/fileio.h"
#include "samn/sha256.h"

namespace samn::engine {

namespace {

constexpr std::streamoff kEdgeBytes = 1 << 20;

// The first and last MiB: enough to tell files apart without reading hours
// of video on every run.
bool HashEdges(const std::filesystem::path& video, std::streamoff size, Sha256& h) {
    std::ifstream in(video, std::ios::binary);
    if (!in) return false;
    std::vector<char> buf(static_cast<size_t>(std::min(size, kEdgeBytes)));
    in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
    h.Update(buf.data(), static_cast<size_t>(in.gcount()));
    if (size > kEdgeBytes) {
        const std::streamoff tail = std::min(kEdgeBytes, size - kEdgeBytes);
        in.seekg(size - tail);
        buf.resize(static_cast<size_t>(tail));
        in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
        h.Update(buf.data(), static_cast<size_t>(in.gcount()));
    }
    return !in.bad();
}

std::filesystem::path EntryPath(const std::filesystem::path& dir, const std::string& key) {
    return dir / (key + ".track.json");
}

nlohmann::json RectJson(const cv::Rect& r) { return {r.x, r.y, r.width, r.height}; }

}  // namespace

std::string TrackCacheKey(const std::filesystem::path& video, const cv::Rect& box, const TrackOptions& o) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(video, ec);
    if (ec) return {};
    const auto mtime = std::filesystem::last_write_time(video, ec);
    if (ec) return {};
    Sha256 h;
    if (!HashEdges(video, static_cast<std::streamoff>(size), h)) return {};
    // Every option that changes the track, in a fixed order. cancel and
    // on_progress do not.
    nlohmann::json opts{{"version", kTrackCacheVersion},
                        {"opencv", cv::getVersionString()},
                        {"size", size},
                        {"mtime", mtime.time_since_epoch().count()},
                        {"box", RectJson(box)},
                        {"start_ms", o.start_ms},
                        {"end_ms", o.end_ms},
                        {"max_frames", o.max_frames},
                        {"max_analysis_fps", o.max_analysis_fps},
                        {"camera_compensation", o.camera_compensation},
                        {"scene_cuts", o.scene_cuts},
                        {"appearance_memory", o.appearance_memory},
                        {"axis", static_cast<int>(o.axis)},
                        {"partner_fixed", o.partner_fixed},
                        {"distance_stroke", o.distance_stroke}};
    nlohmann::json masks = nlohmann::json::array();
    for (const cv::Rect& m : o.mask_rois) masks.push_back(RectJson(m));
    opts["mask_rois"] = masks;
    opts["partner_box"] = o.partner_box ? RectJson(*o.partner_box) : nlohmann::json();
    const std::string text = opts.dump();
    h.Update(text.data(), text.size());
    return h.HexDigest();
}

std::optional<TrackResult> LoadTrackCache(const std::filesystem::path& dir, const std::string& key) {
    if (key.empty()) return std::nullopt;
    std::ifstream in(EntryPath(dir, key), std::ios::binary);
    if (!in) return std::nullopt;
    try {
        const nlohmann::json j = nlohmann::json::parse(in);
        if (j.at("version").get<int>() != kTrackCacheVersion) return std::nullopt;
        TrackResult r;
        r.times_ms = j.at("times_ms").get<std::vector<int64_t>>();
        r.positions = j.at("positions").get<std::vector<double>>();
        r.lost = j.at("lost").get<std::vector<uint8_t>>();
        r.xs = j.at("xs").get<std::vector<double>>();
        r.ys = j.at("ys").get<std::vector<double>>();
        r.frame_ys = j.at("frame_ys").get<std::vector<double>>();
        r.partner_xs = j.at("partner_xs").get<std::vector<double>>();
        r.partner_ys = j.at("partner_ys").get<std::vector<double>>();
        r.horizontal = j.at("horizontal").get<bool>();
        r.distance_stroke = j.at("distance_stroke").get<bool>();
        r.size = cv::Size(j.at("width").get<int>(), j.at("height").get<int>());
        r.scene_cuts = j.at("scene_cuts").get<std::vector<int>>();
        const nlohmann::json& s = j.at("stats");
        r.stats.lost_frames = s.at("lost_frames").get<int>();
        r.stats.camera_frames_lost = s.at("camera_frames_lost").get<int>();
        r.stats.total_frames = s.at("total_frames").get<int>();
        r.stats.valid_frames = s.at("valid_frames").get<int>();
        r.stats.vertical_range = s.at("vertical_range").get<double>();
        r.stats.horizontal_range = s.at("horizontal_range").get<double>();
        r.stats.confidence = s.at("confidence").get<double>();
        r.stats.reason = s.at("reason").get<std::string>();
        if (r.positions.size() != r.times_ms.size() || r.lost.size() != r.times_ms.size()) return std::nullopt;
        return r;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

void SaveTrackCache(const std::filesystem::path& dir, const std::string& key, const TrackResult& r) {
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) throw std::runtime_error("cannot create the cache folder " + dir.u8string() + ": " + ec.message());
    const nlohmann::json stats{{"lost_frames", r.stats.lost_frames},
                               {"camera_frames_lost", r.stats.camera_frames_lost},
                               {"total_frames", r.stats.total_frames},
                               {"valid_frames", r.stats.valid_frames},
                               {"vertical_range", r.stats.vertical_range},
                               {"horizontal_range", r.stats.horizontal_range},
                               {"confidence", r.stats.confidence},
                               {"reason", r.stats.reason}};
    const nlohmann::json j{{"version", kTrackCacheVersion},
                           {"times_ms", r.times_ms},
                           {"positions", r.positions},
                           {"lost", r.lost},
                           {"xs", r.xs},
                           {"ys", r.ys},
                           {"frame_ys", r.frame_ys},
                           {"partner_xs", r.partner_xs},
                           {"partner_ys", r.partner_ys},
                           {"horizontal", r.horizontal},
                           {"distance_stroke", r.distance_stroke},
                           {"width", r.size.width},
                           {"height", r.size.height},
                           {"scene_cuts", r.scene_cuts},
                           {"stats", stats}};
    WriteFileAtomic(EntryPath(dir, key), j.dump());
}

}  // namespace samn::engine
