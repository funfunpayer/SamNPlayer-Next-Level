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
//    that stalls (no report) is overrun by at most the cap, and then the
//    estimate stands.
//  - Hold window: max(back_tolerance_ms, cap x rate). A report behind the
//    estimate by no more than the window holds the estimate where it is
//    until the reported position has caught up. The window of a report
//    never shrinks inside it: it is the larger of the window before and
//    after the report's own interval, the window from before a Hold() (so a
//    rate decrease keeps the old rate's window), and, while the last report
//    held the estimate on a floor, that report's window. `HoldWindowMs()` is
//    a lower bound of the window the next report gets (a new report interval
//    can only make it larger).
//  - No step back unless a report is more than the window behind the
//    estimate (which, apart from a rate change, means the media clock
//    itself went backwards) or follows a seek (Seek()); pausing returns to
//    the last report. A report ahead is taken as it is.
//  - Bound: the estimate is never more than `HoldWindowMs()` of video ahead
//    of the latest report (after a rate change, until the next report, the
//    window from before it).
//  - Not playing (paused, stopped, at the end) the estimate is the last
//    report and does not move. After play() it moves only from the first
//    report on.
//  - Hold() (an offset change, a rate change, the emergency stop) freezes
//    the estimate where it is until the next report, which then follows the
//    rules above. Seek() (the player is about to jump: a seek of the page, a
//    loop jump) freezes it too, and takes the next report as it is if it
//    comes within the cap. A seek that gives no report expires after the cap;
//    that only helps when the reports have stalled: a timer report that comes
//    within the cap is still taken as it is.
#include <cstddef>
#include <cstdint>
#include <deque>

namespace samn {

struct PlayheadOptions {
    int64_t default_cap_ms = 120;     // until the report interval is known
    int64_t min_cap_ms = 20;
    int64_t max_cap_ms = 150;
    double cap_factor = 1.2;          // x the median report interval
    int64_t back_tolerance_ms = 40;   // the hold window is at least this (see HoldWindowMs)
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
    // The player is about to jump: Hold(), and the next report is taken as
    // it is, also when it is only a little behind, if it comes within the
    // cap; later, the seek has expired (it gave no report; a timer report
    // within the cap is still taken as it is).
    void Seek(int64_t now_ms);
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
    // How far (video ms) the next report may be behind the estimate and
    // still hold it: max(back_tolerance_ms, cap x rate), or more after a
    // Hold() or a held report (see above).
    int64_t HoldWindowMs() const;

    bool playing() const { return playing_; }
    double rate() const { return rate_; }
    int64_t last_report_ms() const { return report_pos_; }

private:
    void Freeze(int64_t pos_ms);
    int64_t FormulaWindowMs() const;  // max(back_tolerance_ms, cap x rate)

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
    bool seek_pending_ = false; // the next report is the one of a seek ...
    int64_t seek_at_ = 0;       // ... if it comes within the cap after this
    int64_t freeze_window_ = 0; // the window at the last Hold()
    int64_t last_window_ = 0;   // the window the last report used
    std::deque<int64_t> intervals_;
};

}  // namespace samn
