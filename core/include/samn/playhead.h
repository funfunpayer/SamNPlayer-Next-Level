#pragma once
// The video playhead between two position reports of the media player.
//
// The media backend reports the video position only every 50 to 100 ms (Qt
// FFmpeg backend: 100 ms in Qt 6.4, 50 ms in Qt 6.7). Sampled only at the
// reports, the device output is late by the time since the last report
// (docs/TIMING.md). PlayheadExtrapolator estimates the position at any
// moment from the last report, the time since it and the play rate:
//
//   estimate = last report + (now - report time) x rate
//
// with these rules (all times in ms; "now" is a steady clock):
//  - Cap: the estimate advances for at most `CapMs()` of wall time after the
//    last report: the median report interval x 1.2, clamped to 20-150 ms
//    (120 ms until two reports in a row gave an interval). A media clock
//    that stalls (no report) is overrun by at most the cap, so the estimate
//    is never more than cap x rate ms of video ahead of the last report the
//    player gave, and then it holds.
//  - Never backwards by more than the tolerance: a report up to
//    `back_tolerance_ms` behind the estimate holds the estimate where it is
//    until the reported position has caught up (no stroke is sent twice).
//    A report further behind, or one far ahead, is a seek (a loop jump, a
//    jump of the player): the estimate takes it as it is.
//  - Not playing (paused, stopped, at the end) the estimate is the last
//    report and does not move. After play() it moves only from the first
//    report on.
//  - Hold() (an offset change, a rate change, a loop jump the player is
//    about to make) freezes the estimate where it is until the next report.
#include <cstddef>
#include <cstdint>
#include <deque>

namespace samn {

struct PlayheadOptions {
    int64_t default_cap_ms = 120;     // until the report interval is known
    int64_t min_cap_ms = 20;
    int64_t max_cap_ms = 150;
    double cap_factor = 1.2;          // x the median report interval
    int64_t back_tolerance_ms = 40;   // a report this far behind holds the estimate
    // A report this far from where the last one and the time since predict
    // is a jump, not a report interval of the timer.
    int64_t jump_ms = 250;
    std::size_t history = 9;          // report intervals kept for the median
};

class PlayheadExtrapolator {
public:
    explicit PlayheadExtrapolator(PlayheadOptions options = {});

    // A position report of the media player at `now_ms`.
    void Report(int64_t pos_ms, int64_t now_ms);
    // The player started or stopped playing (pause, stop, end of media).
    void SetPlaying(bool playing);
    // A new play rate (> 0; anything else is ignored). The estimate holds
    // until the next report and then moves at the new rate.
    void SetRate(double rate, int64_t now_ms);
    // Freeze the estimate where it is until the next report.
    void Hold(int64_t now_ms);
    // A new video: everything is forgotten, the report interval too.
    void Clear();

    // The estimated position at `now_ms` (the last report while not
    // playing; 0 before any report).
    int64_t Estimate(int64_t now_ms) const;
    // True while the estimate moves at `now_ms`: playing, a report since the
    // start or the last Hold(), and within the cap after it.
    bool Advancing(int64_t now_ms) const;
    // How long after a report the estimate may advance (wall ms).
    int64_t CapMs() const;

    bool playing() const { return playing_; }
    double rate() const { return rate_; }
    int64_t last_report_ms() const { return report_pos_; }

private:
    void Freeze(int64_t pos_ms);

    PlayheadOptions o_;
    bool playing_ = false;
    double rate_ = 1.0;
    bool have_report_ = false;
    int64_t report_pos_ = 0;   // the last report
    int64_t report_at_ = 0;    // when it came
    bool live_ = false;        // moving from the last report
    int64_t held_ = 0;         // the estimate while not live
    int64_t floor_ = 0;        // the estimate holds here until the report catches up
    bool has_floor_ = false;
    bool in_run_ = false;      // the last report and the next one are timer reports in a row
    std::deque<int64_t> intervals_;
};

}  // namespace samn
