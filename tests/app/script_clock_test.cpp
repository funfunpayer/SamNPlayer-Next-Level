// ScriptClock: the playhead of a script without a video (0.7 "Feel without
// film"). The time source is injected, so nothing waits on a real clock.
#include <QSignalSpy>
#include <QtTest>

#include "script_clock.h"

using samn::ui::ScriptClock;

class ScriptClockTest : public QObject {
    Q_OBJECT
    qint64 now_ = 0;

private slots:
    void playAdvancesWithTheRateAndPauseHolds() {
        ScriptClock c;
        c.setTimeSource([this] { return now_; });
        c.play();  // no duration: nothing to play
        QCOMPARE(c.playbackState(), int(ScriptClock::StoppedState));
        c.setDuration(10000);
        c.play();
        QCOMPARE(c.playbackState(), int(ScriptClock::PlayingState));
        now_ += 1000;
        c.tick();
        QCOMPARE(c.position(), qint64(1000));
        c.setPlaybackRate(2.0);  // the playhead keeps its place
        QCOMPARE(c.position(), qint64(1000));
        now_ += 500;
        QCOMPARE(c.position(), qint64(2000));
        c.pause();
        now_ += 5000;
        QCOMPARE(c.position(), qint64(2000));
        QCOMPARE(c.playbackState(), int(ScriptClock::PausedState));
        c.play();  // resumes where it paused
        now_ += 250;
        QCOMPARE(c.position(), qint64(2500));
    }

    void seekWorksPlayingAndPausedAndIsClamped() {
        ScriptClock c;
        c.setTimeSource([this] { return now_; });
        c.setDuration(4000);
        QSignalSpy pos(&c, &ScriptClock::positionChanged);
        c.setPosition(1500);
        QCOMPARE(c.position(), qint64(1500));
        QCOMPARE(pos.count(), 1);
        c.setPosition(-20);
        QCOMPARE(c.position(), qint64(0));
        c.setPosition(99999);
        QCOMPARE(c.position(), qint64(4000));
        c.setPosition(1000);
        c.play();
        now_ += 300;
        c.setPosition(3000);  // a seek while playing continues from there
        now_ += 100;
        QCOMPARE(c.position(), qint64(3100));
    }

    void theEndStopsAtTheEndAndPlayStartsOver() {
        ScriptClock c;
        c.setTimeSource([this] { return now_; });
        c.setDuration(1000);
        QSignalSpy done(&c, &ScriptClock::finished);
        c.play();
        now_ += 1500;
        c.tick();
        QCOMPARE(done.count(), 1);
        QCOMPARE(c.position(), qint64(1000));
        QCOMPARE(c.playbackState(), int(ScriptClock::PausedState));
        c.play();  // as a video at its end: from the start
        QCOMPARE(c.position(), qint64(0));
        c.stop();
        QCOMPARE(c.position(), qint64(0));
        QCOMPARE(c.playbackState(), int(ScriptClock::StoppedState));
    }

    void theTimerDrivesThePlayheadInRealTime() {
        ScriptClock c;  // the wall clock
        c.setDuration(60000);
        QSignalSpy pos(&c, &ScriptClock::positionChanged);
        c.play();
        // Both together: on a loaded machine one late tick can jump past 100 ms.
        QTRY_VERIFY_WITH_TIMEOUT(c.position() >= 100 && pos.count() >= 2, 5000);
        c.pause();
    }
};

QTEST_GUILESS_MAIN(ScriptClockTest)
#include "script_clock_test.moc"
