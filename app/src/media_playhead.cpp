#include "media_playhead.h"

#include <algorithm>

namespace samn::ui {

MediaPlayhead::MediaPlayhead(QObject* parent) : QObject(parent) {
    mono_.start();
    timer_.setInterval(kTickMs);
    // Qt already runs a coarse timer of 20 ms or less as a precise one
    // (docs/TIMING.md 1.1); said here so it does not depend on that.
    timer_.setTimerType(Qt::PreciseTimer);
    connect(&timer_, &QTimer::timeout, this, &MediaPlayhead::tick);
}

qint64 MediaPlayhead::clampToVideo(qint64 videoMs, qint64 atLeast) const {
    qint64 end = durationMs_ > 0 ? durationMs_ : -1;
    if (limitMs_ >= 0) end = end < 0 ? limitMs_ : std::min(end, limitMs_);
    if (end < 0) return videoMs;
    return std::min(videoMs, std::max(end, atLeast));  // never below the report itself
}

qint64 MediaPlayhead::bounded(qint64 estimate) const {
    // The limits (duration, loop end) never take the output below the report
    // nor below what was already sent, unless the estimate itself went back
    // (a seek): a limit lowered under the playhead holds it there.
    qint64 atLeast = head_.last_report_ms();
    if (lastSent_ >= 0 && estimate >= lastSent_) atLeast = std::max(atLeast, lastSent_);
    return clampToVideo(estimate, atLeast);
}

qint64 MediaPlayhead::report(qint64 videoMs) {
    const qint64 n = now();
    head_.Report(videoMs, n);
    lastSent_ = bounded(head_.Estimate(n));
    return lastSent_;
}

qint64 MediaPlayhead::estimate() const { return bounded(head_.Estimate(now())); }

void MediaPlayhead::setPlaying(bool playing) {
    head_.SetPlaying(playing);
    lastSent_ = -1;
    updateTimer();
}

void MediaPlayhead::setRate(double rate) { head_.SetRate(rate, now()); }

void MediaPlayhead::hold() { head_.Hold(now()); }

void MediaPlayhead::seek() {
    head_.Seek(now());
    ++seeks_;
}

void MediaPlayhead::clear() {
    head_.Clear();
    lastSent_ = -1;
}

void MediaPlayhead::setClock(std::function<qint64()> nowMs, bool tickTimer) {
    clock_ = std::move(nowMs);
    timerOn_ = tickTimer;
    updateTimer();
}

void MediaPlayhead::updateTimer() {
    const bool need = timerOn_ && head_.playing();
    if (need && !timer_.isActive())
        timer_.start();
    else if (!need && timer_.isActive())
        timer_.stop();
}

void MediaPlayhead::tick() {
    const qint64 n = now();
    if (!head_.Advancing(n)) return;  // paused, waiting for a report, or at the cap
    const qint64 v = bounded(head_.Estimate(n));
    if (v == lastSent_) return;
    lastSent_ = v;
    emit advanced(v);
}

}  // namespace samn::ui
