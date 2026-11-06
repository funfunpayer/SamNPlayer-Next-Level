#include "samn/playhead.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <vector>

namespace samn {

PlayheadExtrapolator::PlayheadExtrapolator(PlayheadOptions options) : o_(options) {}

int64_t PlayheadExtrapolator::CapMs() const {
    if (intervals_.empty()) return o_.default_cap_ms;
    std::vector<int64_t> v(intervals_.begin(), intervals_.end());
    const auto mid = v.begin() + std::ptrdiff_t(v.size() / 2);
    std::nth_element(v.begin(), mid, v.end());
    const auto cap = int64_t(std::llround(double(*mid) * o_.cap_factor));
    return std::clamp(cap, o_.min_cap_ms, o_.max_cap_ms);
}

int64_t PlayheadExtrapolator::Estimate(int64_t now_ms) const {
    if (!have_report_) return 0;
    if (!playing_ || !live_) return held_;
    const int64_t until = std::min(now_ms, report_at_ + CapMs());
    const int64_t ahead = std::max<int64_t>(0, until - report_at_);
    const int64_t e = report_pos_ + int64_t(std::llround(double(ahead) * rate_));
    return has_floor_ ? std::max(e, floor_) : e;
}

bool PlayheadExtrapolator::Advancing(int64_t now_ms) const {
    return playing_ && live_ && now_ms <= report_at_ + CapMs();
}

void PlayheadExtrapolator::Freeze(int64_t pos_ms) {
    held_ = pos_ms;
    live_ = false;
    has_floor_ = false;
    in_run_ = false;
}

void PlayheadExtrapolator::Report(int64_t pos_ms, int64_t now_ms) {
    const bool seek = seek_pending_;
    seek_pending_ = false;
    if (!playing_) {
        report_pos_ = pos_ms;
        report_at_ = now_ms;
        have_report_ = true;
        Freeze(pos_ms);
        return;
    }
    const int64_t est = have_report_ ? Estimate(now_ms) : pos_ms;
    // A timer report follows the last one by about the time since it; a
    // seek, a loop jump or a stall in between gives no report interval.
    bool jump = true;
    if (have_report_) {
        const int64_t dt = now_ms - report_at_;
        const double expected = double(report_pos_) + double(dt) * rate_;
        jump = std::abs(double(pos_ms) - expected) > double(o_.jump_ms);
        if (in_run_ && !jump && !seek && dt > 0) {
            intervals_.push_back(dt);
            while (intervals_.size() > o_.history) intervals_.pop_front();
        }
    }
    // Behind by no more than the window: the estimate holds where it is
    // until the report has caught up (no step back, so a move already sent
    // is not sent again). Further behind, ahead, or the report of a seek:
    // the report as it is.
    has_floor_ = !seek && pos_ms < est && est - pos_ms <= HoldWindowMs();
    floor_ = est;
    report_pos_ = pos_ms;
    report_at_ = now_ms;
    have_report_ = true;
    live_ = true;
    in_run_ = !jump && !seek;
}

void PlayheadExtrapolator::SetPlaying(bool playing) {
    if (playing == playing_) return;
    playing_ = playing;
    Freeze(report_pos_);  // the last report; it moves again from the next one
}

void PlayheadExtrapolator::Hold(int64_t now_ms) { Freeze(Estimate(now_ms)); }

void PlayheadExtrapolator::Seek(int64_t now_ms) {
    Hold(now_ms);
    seek_pending_ = true;
}

int64_t PlayheadExtrapolator::HoldWindowMs() const {
    return std::max(o_.back_tolerance_ms, int64_t(std::llround(double(CapMs()) * rate_)));
}

void PlayheadExtrapolator::SetRate(double rate, int64_t now_ms) {
    if (!(rate > 0) || !std::isfinite(rate) || rate == rate_) return;
    if (playing_) Hold(now_ms);  // at the old rate
    rate_ = rate;
}

void PlayheadExtrapolator::Clear() {
    have_report_ = false;
    report_pos_ = report_at_ = 0;
    Freeze(0);
    seek_pending_ = false;
    intervals_.clear();
}

}  // namespace samn
