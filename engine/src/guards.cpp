#include <algorithm>
#include <cmath>

#include "track_parts.h"

namespace samn::engine {

namespace {
// Rolling window of accepted displacements; a jump must exceed 8x their
// median and never counts below 60 px (largest real single-frame motion
// measured on a real clip ~50 px; LESSONS GD-10).
constexpr size_t kGuardWindow = 20;
constexpr double kGuardRatio = 8.0;
constexpr double kGuardFloorPx = 60.0;

double Median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    const size_t n = v.size();
    return n % 2 ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2;
}

// Go's round-half-away-from-zero on the coast velocity.
int RoundAway(double v) { return v < 0 ? static_cast<int>(v - 0.5) : static_cast<int>(v + 0.5); }
}  // namespace

bool DispGuard::Implausible(double d) const {
    if (d < kGuardFloorPx || recent_.empty()) return false;
    return d > kGuardRatio * Median(recent_);
}

void DispGuard::Accept(double d) {
    recent_.push_back(d);
    if (recent_.size() > kGuardWindow) recent_.erase(recent_.begin());
}

void Coast::ObserveOk(const cv::Rect& r) {
    const double cx = r.x + r.width / 2.0, cy = r.y + r.height / 2.0;
    if (have_pos_) {
        vx_ = cx - prev_cx_;
        vy_ = cy - prev_cy_;
        have_vel_ = true;
    }
    prev_cx_ = cx;
    prev_cy_ = cy;
    have_pos_ = true;
    box_ = r;
    lost_ = 0;
}

std::optional<cv::Rect> Coast::OnLost() {
    ++lost_;
    if (!have_pos_ || lost_ > kDefaultFrames || box_.width <= 0 || box_.height <= 0) return std::nullopt;
    if (have_vel_) {
        box_.x += RoundAway(vx_);
        box_.y += RoundAway(vy_);
        prev_cx_ += vx_;
        prev_cy_ += vy_;
    }
    return box_;
}

}  // namespace samn::engine
