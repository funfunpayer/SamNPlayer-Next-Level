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
    if (durationMs_ <= 0) return videoMs;
    return std::min(videoMs, std::max(durationMs_, atLeast));
}

qint64 MediaPlayhead::report(qint64 videoMs) {
    const qint64 n = now();
    head_.Report(videoMs, n);
    lastSent_ = clampToVideo(head_.Estimate(n), videoMs);
    return lastSent_;
}

qint64 MediaPlayhead::estimate() const {
    return clampToVideo(head_.Estimate(now()), head_.last_report_ms());
}

void MediaPlayhead::setPlaying(bool playing) {
    head_.SetPlaying(playing);
    lastSent_ = -1;
    updateTimer();
}

void MediaPlayhead::setRate(double rate) { head_.SetRate(rate, now()); }

void MediaPlayhead::hold() { head_.Hold(now()); }

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
    const qint64 v = clampToVideo(head_.Estimate(n), head_.last_report_ms());
    if (v == lastSent_) return;
    lastSent_ = v;
    emit advanced(v);
}

}  // namespace samn::ui
