#include "script_clock.h"

#include <algorithm>
#include <cmath>

namespace samn::ui {

ScriptClock::ScriptClock(QObject* parent) : QObject(parent) {
    wall_.start();
    timer_.setInterval(16);
    connect(&timer_, &QTimer::timeout, this, &ScriptClock::tick);
}

qint64 ScriptClock::now() const { return nowMs_ ? nowMs_() : wall_.elapsed(); }

void ScriptClock::setTimeSource(std::function<qint64()> nowMs) {
    const qint64 pos = position();
    nowMs_ = std::move(nowMs);
    basePos_ = pos;
    baseTime_ = now();
}

qint64 ScriptClock::position() const {
    if (state_ != PlayingState) return basePos_;
    const qint64 p = basePos_ + qint64(std::llround(double(now() - baseTime_) * rate_));
    return duration_ > 0 ? std::min(p, duration_) : p;
}

void ScriptClock::setPosition(qint64 ms) {
    ms = std::max<qint64>(0, duration_ > 0 ? std::min(ms, duration_) : ms);
    basePos_ = ms;
    baseTime_ = now();
    if (ms != lastPos_) {
        lastPos_ = ms;
        emit positionChanged();
    }
}

void ScriptClock::setDuration(qint64 ms) {
    ms = std::max<qint64>(0, ms);
    if (ms == duration_) return;
    duration_ = ms;
    emit durationChanged();
    if (duration_ > 0 && basePos_ > duration_) setPosition(duration_);
}

void ScriptClock::setPlaybackRate(double rate) {
    if (!(rate > 0) || rate == rate_) return;
    const qint64 pos = position();  // keep the playhead where it is
    rate_ = rate;
    basePos_ = pos;
    baseTime_ = now();
    emit playbackRateChanged();
}

void ScriptClock::setState(State s) {
    if (s == state_) return;
    state_ = s;
    if (s == PlayingState)
        timer_.start();
    else
        timer_.stop();
    emit playbackStateChanged();
}

void ScriptClock::play() {
    if (duration_ <= 0) return;  // nothing to play
    if (basePos_ >= duration_) basePos_ = 0;  // as a video at its end: from the start
    baseTime_ = now();
    setState(PlayingState);
}

void ScriptClock::pause() {
    if (state_ != PlayingState) return;
    basePos_ = position();
    baseTime_ = now();
    setState(PausedState);
}

void ScriptClock::stop() {
    basePos_ = 0;
    baseTime_ = now();
    setState(StoppedState);
    if (lastPos_ != 0) {
        lastPos_ = 0;
        emit positionChanged();
    }
}

void ScriptClock::tick() {
    if (state_ != PlayingState) return;
    const qint64 p = position();
    if (p != lastPos_) {
        lastPos_ = p;
        emit positionChanged();
    }
    if (duration_ > 0 && p >= duration_) {
        basePos_ = duration_;
        baseTime_ = now();
        setState(PausedState);  // the playhead stays at the end
        emit finished();
    }
}

}  // namespace samn::ui
