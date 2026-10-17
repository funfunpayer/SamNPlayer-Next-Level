#include <algorithm>

#include <opencv2/imgproc.hpp>

#include "track_parts.h"

namespace samn::engine {

cv::Mat ExtractTemplate(const cv::Mat& gray, cv::Rect box, double downscale) {
    // As 0.7: the origin is clamped, the size only cut at the far edge.
    box.x = std::max(0, box.x);
    box.y = std::max(0, box.y);
    box.width = std::min(box.width, gray.cols - box.x);
    box.height = std::min(box.height, gray.rows - box.y);
    if (box.width < 8 || box.height < 8) return {};
    cv::Mat out;
    if (downscale != 1.0)
        cv::resize(gray(box), out, cv::Size(), downscale, downscale, cv::INTER_AREA);
    else
        out = gray(box).clone();
    if (out.rows < 4 || out.cols < 4) return {};
    return out;
}

namespace {
struct Match {
    cv::Point at;
    double score = 0;
};
std::optional<Match> MatchTemplate(const cv::Mat& frame, const cv::Mat& tmpl) {
    if (tmpl.rows >= frame.rows || tmpl.cols >= frame.cols) return std::nullopt;
    cv::Mat result;
    cv::matchTemplate(frame, tmpl, result, cv::TM_CCOEFF_NORMED);
    Match m;
    cv::minMaxLoc(result, nullptr, &m.score, nullptr, &m.at);
    return m;
}
}  // namespace

void AppearanceMemory::Remember(const cv::Mat& gray, const cv::Rect& box) {
    if (box.width <= 0 || box.height <= 0) return;
    cv::Mat t = ExtractTemplate(gray, box, kDownscale);
    if (t.empty()) return;
    templates_.push_back(std::move(t));
    // Drop the oldest but keep the first: the user-confirmed start region.
    if (templates_.size() > kMaxTemplates) templates_.erase(templates_.begin() + 1);
}

std::optional<double> AppearanceMemory::MatchesOriginal(const cv::Mat& gray, const cv::Rect& box) const {
    if (templates_.empty()) return std::nullopt;
    const cv::Mat& orig = templates_.front();
    // The search window follows the first crop's size, not the box's: CSRT's
    // scale can drift (one real clip shrank 171x216 to 50x65), and a window
    // shrinking with it would switch this check off when it matters most.
    const double scale = 1.0 / kDownscale;
    const int sw = static_cast<int>(orig.cols * scale) * 2 + 16;
    const int sh = static_cast<int>(orig.rows * scale) * 2 + 16;
    const int cx = box.x + box.width / 2, cy = box.y + box.height / 2;
    const cv::Mat region = ExtractTemplate(gray, cv::Rect(cx - sw / 2, cy - sh / 2, sw, sh), kDownscale);
    if (region.empty() || orig.cols >= region.cols || orig.rows >= region.rows) return std::nullopt;
    const auto m = MatchTemplate(region, orig);
    if (!m) return std::nullopt;
    return m->score;
}

std::optional<cv::Rect> AppearanceMemory::Reacquire(const cv::Mat& gray) const {
    if (templates_.empty() || gray.empty()) return std::nullopt;
    cv::Mat frame;
    cv::resize(gray, frame, cv::Size(), kDownscale, kDownscale, cv::INTER_AREA);
    const double scale = 1.0 / kDownscale;
    std::vector<std::pair<double, cv::Rect>> cands;
    for (const cv::Mat& t : templates_) {
        const auto m = MatchTemplate(frame, t);
        if (!m || m->score < kMinScore) continue;
        cands.emplace_back(m->score, cv::Rect(static_cast<int>(m->at.x * scale), static_cast<int>(m->at.y * scale),
                                              std::max(8, static_cast<int>(t.cols * scale)),
                                              std::max(8, static_cast<int>(t.rows * scale))));
    }
    std::stable_sort(cands.begin(), cands.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    // Fail closed: a candidate must still look like the start region.
    for (const auto& c : cands) {
        const auto score = MatchesOriginal(gray, c.second);
        if (score && *score >= kMinScore) return c.second;
    }
    return std::nullopt;
}

}  // namespace samn::engine
