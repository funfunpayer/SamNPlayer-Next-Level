#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>
#include <QtQml/qqmlregistration.h>
#include <functional>

namespace samn::ui {

// The playhead for a script without a video (0.7 "Feel without film"): the
// same surface as the MediaPlayer the Player page uses (position, duration,
// playbackState, playbackRate, play/pause/stop), so the page drives the
// curve, the lanes and the device the same way with or without a video.
class ScriptClock : public QObject {
    Q_OBJECT
    QML_ELEMENT
    // qint64 properties are written as double from QML (Qt 6.4).
    Q_PROPERTY(qint64 position READ position WRITE setPosition NOTIFY positionChanged)
    Q_PROPERTY(qint64 duration READ duration WRITE setDuration NOTIFY durationChanged)
    Q_PROPERTY(int playbackState READ playbackState NOTIFY playbackStateChanged)
    Q_PROPERTY(double playbackRate READ playbackRate WRITE setPlaybackRate NOTIFY playbackRateChanged)

public:
    // Same values as QMediaPlayer::PlaybackState.
    enum State { StoppedState = 0, PlayingState = 1, PausedState = 2 };
    Q_ENUM(State)

    explicit ScriptClock(QObject* parent = nullptr);

    qint64 position() const;
    void setPosition(qint64 ms);
    qint64 duration() const { return duration_; }
    void setDuration(qint64 ms);
    int playbackState() const { return state_; }
    double playbackRate() const { return rate_; }
    void setPlaybackRate(double rate);

    Q_INVOKABLE void play();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void stop();

    // Tests: a clock in place of the wall clock (ms since an arbitrary start).
    void setTimeSource(std::function<qint64()> nowMs);
    // Tests: one tick now (the timer does this every 16 ms while playing).
    void tick();

signals:
    void positionChanged();
    void durationChanged();
    void playbackStateChanged();
    void playbackRateChanged();
    // The playhead reached the end (as the video's EndOfMedia).
    void finished();

private:
    qint64 now() const;
    void setState(State s);

    std::function<qint64()> nowMs_;
    QElapsedTimer wall_;
    QTimer timer_;
    State state_ = StoppedState;
    qint64 duration_ = 0;
    double rate_ = 1.0;
    qint64 basePos_ = 0;   // position at baseTime_
    qint64 baseTime_ = 0;  // now() when basePos_ was set
    qint64 lastPos_ = 0;   // last reported position
};

}  // namespace samn::ui
