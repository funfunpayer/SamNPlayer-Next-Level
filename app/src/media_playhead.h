#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>
#include <functional>

#include "samn/playhead.h"

namespace samn::ui {

// The video's playhead for the device output between two position reports
// of the media player (samn::PlayheadExtrapolator on a monotonic clock).
// While the video plays, a 16 ms precise timer (the ScriptClock's rate)
// emits the estimate as it moves; it stops at a pause, a stop and the end,
// and it never moves the estimate further than the cap after the last
// report, so it never keeps the output alive on its own: when the reports
// stop, the ticks stop too (and PlaybackOutput's stale watchdog only counts
// reports). The estimate is never more than max(40 ms, cap x rate) of video
// (the hold window of samn/playhead.h; the cap is 20-150 ms) ahead of the
// last position the player reported.
class MediaPlayhead : public QObject {
    Q_OBJECT
public:
    static constexpr int kTickMs = 16;

    explicit MediaPlayhead(QObject* parent = nullptr);

    // A position report of the media player (video ms): the position the
    // output gets now (the report, or the estimate when that is a little
    // ahead of it).
    qint64 report(qint64 videoMs);
    // The video plays (the tick runs) or not.
    void setPlaying(bool playing);
    bool playing() const { return head_.playing(); }
    // The play rate; a change holds the estimate until the next report.
    void setRate(double rate);
    double rate() const { return head_.rate(); }
    // Freezes the estimate until the next report (an offset change, the
    // emergency stop); a report a little behind then holds it.
    void hold();
    // The page is about to seek (a seek, a scrub, a jump key, the loop
    // jump): frozen too, and the seek's report is taken as it is.
    void seek();
    // Tests: how many seeks the page announced.
    int seekCount() const { return seeks_; }
    // A new video: the report interval is measured again.
    void clear();
    // The video's length (0 = unknown): the estimate never passes it.
    void setDurationMs(qint64 ms) { durationMs_ = ms; }
    // The last video ms the estimate may reach (-1 = none): with the loop on,
    // the ms before its end, so only a report reaches the end (and makes the
    // page jump back).
    void setLimitMs(qint64 ms) { limitMs_ = ms; }
    qint64 estimate() const;
    qint64 capMs() const { return head_.CapMs(); }

    // Tests: the clock (ms) the estimate runs on, default monotonic. Without
    // the timer, tests step the clock and call tick().
    void setClock(std::function<qint64()> nowMs, bool tickTimer = true);
    // One tick now (the timer does this every 16 ms while the video plays).
    void tick();

signals:
    // The estimate moved between two reports (video ms).
    void advanced(qint64 videoMs);

private:
    qint64 now() const { return clock_ ? clock_() : mono_.elapsed(); }
    qint64 clampToVideo(qint64 videoMs, qint64 atLeast) const;
    void updateTimer();

    samn::PlayheadExtrapolator head_;
    std::function<qint64()> clock_;
    QElapsedTimer mono_;
    QTimer timer_;
    bool timerOn_ = true;
    qint64 durationMs_ = 0;
    qint64 limitMs_ = -1;
    int seeks_ = 0;
    qint64 lastSent_ = -1;
};

}  // namespace samn::ui
