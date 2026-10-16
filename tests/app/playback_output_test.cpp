// Playback drives the device: the levels at the playhead, the stroker's
// moves (filter, safety range, soft seek), pause / stop / home, the emergency
// stop and the lanes that follow the device (D-132). Real DeviceController on
// its worker thread against the Mock device and a UDP sink (TCode).
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <chrono>

#include "device_controller.h"
#include "fake_servers.h"
#include "player_controller.h"
#include "samn/device/tcode.h"
#include "samn/stroke_filter.h"

using samn::ui::DeviceController;
using samn::ui::PlaybackOutput;
using samn::ui::PlayerController;
using namespace samn_test;

namespace {

constexpr int kWait = 10000;  // a connect may take a while on a busy machine

QString Move(const char* axis, double pos, int ms) {
    return QString::fromStdString(samn::device::TCodeMove(axis, pos, std::chrono::milliseconds(ms)));
}

QStringList Lines(const UdpLineSink& sink) {
    QStringList out;
    for (const QByteArray& d : sink.datagrams()) out << QString::fromUtf8(d).trimmed();
    return out;
}

QStringList MockLines(const QStringList& log) {
    QStringList out;
    for (const QString& l : log)
        if (l.startsWith("[mock]")) out << l;
    return out;
}

// A synthetic script: points at the given times and positions.
QString writeScript(const QString& dir, const QString& name, const QList<QPair<int, int>>& pts) {
    QByteArray s = "{\"actions\":[";
    for (int i = 0; i < pts.size(); ++i) {
        if (i) s += ",";
        s += QStringLiteral("{\"at\":%1,\"pos\":%2}").arg(pts[i].first).arg(pts[i].second).toUtf8();
    }
    s += "]}";
    const QString path = dir + "/" + name;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return {};
    f.write(s);
    return path;
}

// Alternating 0 / 100 every `stepMs` (a fast stroke), `n` points.
QList<QPair<int, int>> zigzag(int n, int stepMs) {
    QList<QPair<int, int>> pts;
    for (int i = 0; i < n; ++i) pts.append({i * stepMs, i % 2 ? 100 : 0});
    return pts;
}

}  // namespace

class PlaybackOutputTest : public QObject {
    Q_OBJECT
    QTemporaryDir config_;
    QTemporaryDir files_;

    void connectMock(DeviceController& c) {
        c.connectMock();
        QTRY_COMPARE_WITH_TIMEOUT(c.state(), QString("connected"), kWait);
    }
    void connectStroker(DeviceController& c, const UdpLineSink& sink) {
        c.connectTCode(sink.hostport());
        QTRY_COMPARE_WITH_TIMEOUT(c.state(), QString("connected"), kWait);
        QVERIFY(c.capLinear());
    }

private slots:
    void initTestCase() {
        QVERIFY(config_.isValid());
        QVERIFY(files_.isValid());
        qputenv("SAMN_CONFIG_DIR", config_.path().toUtf8());
    }
    // Every test starts from the default settings.
    void init() { QFile::remove(config_.path() + "/settings.json"); }

    // --- levels ---
    void playingSendsLevelsAndSessionIsActive() {
        const QString script = writeScript(files_.path(), "fast.funscript", zigzag(60, 250));
        DeviceController dev;
        PlayerController p;
        p.setDevice(&dev);
        QVERIFY(p.openScript(script));
        connectMock(dev);
        QSignalSpy logSpy(&dev, &DeviceController::logMessage);
        auto mock = [&] { return MockLines([&] { QStringList l; for (const auto& a : logSpy) l << a.at(0).toString(); return l; }()); };

        QVERIFY(!dev.sessionActive());
        p.setPlaying(true);
        QVERIFY(dev.sessionActive());
        QVERIFY(p.output()->playing());
        p.setVideoPositionMs(1000);
        const QString vib = QString::asprintf("[mock] vibration=%.2f", p.vibrationNow() / 100.0);
        const QString suc = QString::asprintf("[mock] suction=%.2f", p.suctionNow() / 100.0);
        QTRY_VERIFY_WITH_TIMEOUT(mock().contains(vib) && mock().contains(suc), kWait);
        QVERIFY(p.vibrationNow() > 0);

        // The same level again is not sent again.
        const int before = mock().size();
        p.setVideoPositionMs(1000);
        QTest::qWait(200);
        QCOMPARE(mock().size(), before);

        // Stop on pause; the session ends.
        p.setPlaying(false);
        QVERIFY(!dev.sessionActive());
        QTRY_VERIFY_WITH_TIMEOUT(mock().last() == "[mock] stop", kWait);
    }

    void channelOffAndLevelScaleTheOutput() {
        const QString script = writeScript(files_.path(), "fast2.funscript", zigzag(60, 250));
        DeviceController dev;
        PlayerController p;
        p.setDevice(&dev);
        QVERIFY(p.openScript(script));
        connectMock(dev);
        QSignalSpy logSpy(&dev, &DeviceController::logMessage);
        p.output()->setVibrationLevel(50);
        p.output()->setSuctionOn(false);
        p.setPlaying(true);
        p.setVideoPositionMs(1000);
        const double full = p.vibrationNow() / 100.0;
        const QString vib = QString::asprintf("[mock] vibration=%.2f", full * 0.5);
        QTRY_VERIFY_WITH_TIMEOUT(
            [&] { QStringList l; for (const auto& a : logSpy) l << a.at(0).toString(); return l.contains(vib) && l.contains("[mock] suction=0.00"); }(), kWait);
        p.setPlaying(false);
    }

    void staleWatchdogStopsTheDevice() {
        const QString script = writeScript(files_.path(), "fast3.funscript", zigzag(60, 250));
        DeviceController dev;
        PlayerController p;
        p.setDevice(&dev);
        QVERIFY(p.openScript(script));
        connectMock(dev);
        QSignalSpy logSpy(&dev, &DeviceController::logMessage);
        p.output()->setStaleMs(200);
        p.setPlaying(true);
        p.setVideoPositionMs(1000);
        QTRY_VERIFY_WITH_TIMEOUT([&] { for (const auto& a : logSpy) if (a.at(0).toString().startsWith("[mock] vib")) return true; return false; }(), kWait);
        // No position any more (the video stands): the device stops by itself.
        QTRY_VERIFY_WITH_TIMEOUT([&] { for (const auto& a : logSpy) if (a.at(0).toString() == "[mock] stop") return true; return false; }(), kWait);
        QVERIFY(dev.sessionActive());  // still "playing" as far as the player knows
        p.setPlaying(false);
    }

    // --- stroker ---
    void strokerGetsTheNextActionWithSafetyRange() {
        const QString script = writeScript(files_.path(), "slow.funscript", {{0, 0}, {1000, 100}, {2000, 0}, {3000, 100}});
        UdpLineSink sink;
        QVERIFY(sink.listening());
        DeviceController dev;
        PlayerController p;
        p.setDevice(&dev);
        QVERIFY(p.openScript(script));
        connectStroker(dev, sink);
        p.output()->setStrokeRange(20, 80, true);
        QCOMPARE(p.output()->strokeLo(), 20);
        QCOMPARE(p.output()->strokeHi(), 80);
        p.setPlaying(true);
        p.setVideoPositionMs(0);
        // Next action: 1000 ms away, pos 100 -> the top of the range (80 %).
        QTRY_VERIFY_WITH_TIMEOUT(Lines(sink).contains(Move("L0", 0.8, 1000)), kWait);
        // The same target is not sent again within the move.
        p.setVideoPositionMs(250);
        QTest::qWait(150);
        QCOMPARE(Lines(sink).count(Move("L0", 0.8, 1000)), 1);
        // Reaching it: the next target (bottom of the range) in the time until due.
        p.setVideoPositionMs(1100);
        QTRY_VERIFY_WITH_TIMEOUT(Lines(sink).contains(Move("L0", 0.2, 900)), kWait);
        p.setPlaying(false);
    }

    void rangeKeepsTenPercentApart() {
        PlaybackOutput o;
        o.setStrokeRange(50, 55, true);
        QCOMPARE(o.strokeHi() - o.strokeLo(), 10);
        QCOMPARE(o.strokeLo(), 50);  // the end being moved stays, the other follows
        QCOMPARE(o.strokeHi(), 60);
        o.setStrokeRange(40, 38, false);
        QCOMPARE(o.strokeHi() - o.strokeLo(), 10);
        QCOMPARE(o.strokeHi(), 38);
    }

    void strokeOffSendsNothing() {
        const QString script = writeScript(files_.path(), "slow2.funscript", {{0, 0}, {1000, 100}, {2000, 0}});
        UdpLineSink sink;
        DeviceController dev;
        PlayerController p;
        p.setDevice(&dev);
        QVERIFY(p.openScript(script));
        connectStroker(dev, sink);
        p.output()->setStrokeOn(false);
        p.setPlaying(true);
        p.setVideoPositionMs(0);
        p.setVideoPositionMs(300);
        QTest::qWait(300);
        QVERIFY(Lines(sink).isEmpty());
        p.output()->setStrokeOn(true);
        p.setVideoPositionMs(400);
        QTRY_VERIFY_WITH_TIMEOUT(!Lines(sink).isEmpty(), kWait);
        p.setPlaying(false);
    }

    void seekEasesIn() {
        const QString script = writeScript(files_.path(), "zig.funscript", zigzag(300, 100));
        UdpLineSink sink;
        DeviceController dev;
        PlayerController p;
        p.setDevice(&dev);
        QVERIFY(p.openScript(script));
        connectStroker(dev, sink);
        p.setPlaying(true);
        // The start eases in: the next point is 100 ms away but a full stroke takes 500 ms.
        p.setVideoPositionMs(0);
        QTRY_VERIFY_WITH_TIMEOUT(Lines(sink).contains(Move("L0", 1.0, 500)), kWait);
        // Following the script: the real time until due.
        p.setVideoPositionMs(100);
        QTRY_VERIFY_WITH_TIMEOUT(Lines(sink).contains(Move("L0", 0.0, 100)), kWait);
        // A seek far ahead: eased in again (a full stroke away -> 500 ms).
        p.setVideoPositionMs(10000);
        QTRY_VERIFY_WITH_TIMEOUT(Lines(sink).last() == Move("L0", 1.0, 500), kWait);
        // Back to normal right after.
        p.setVideoPositionMs(10100);
        QTRY_VERIFY_WITH_TIMEOUT(Lines(sink).last() == Move("L0", 0.0, 100), kWait);
        // A step back of more than 250 ms is a seek too.
        p.setVideoPositionMs(9000);
        QTRY_VERIFY_WITH_TIMEOUT(Lines(sink).last() == Move("L0", 1.0, 500), kWait);
        // A small step back is not.
        p.setVideoPositionMs(9100);
        QTRY_VERIFY_WITH_TIMEOUT(Lines(sink).last() == Move("L0", 0.0, 100), kWait);
        p.setVideoPositionMs(9000);  // 100 ms back: no seek, same target as before? next at 9100 pos 100
        QTRY_VERIFY_WITH_TIMEOUT(Lines(sink).last() == Move("L0", 1.0, 100), kWait);
        p.setPlaying(false);
    }

    void playSpeedShortensTheMoves() {
        const QString script = writeScript(files_.path(), "slow3.funscript", {{0, 0}, {1000, 50}, {2000, 0}});
        UdpLineSink sink;
        DeviceController dev;
        PlayerController p;
        p.setDevice(&dev);
        QVERIFY(p.openScript(script));
        connectStroker(dev, sink);
        p.output()->setSpeed(2.0);
        p.setPlaying(true);
        p.setVideoPositionMs(0);
        QTRY_VERIFY_WITH_TIMEOUT(Lines(sink).contains(Move("L0", 0.5, 500)), kWait);  // 1000 ms of script at 2x
        p.setPlaying(false);
    }

    void speedLimitFilterShapesWhatIsSent() {
        const QString script = writeScript(files_.path(), "zig2.funscript", zigzag(40, 100));
        UdpLineSink sink;
        DeviceController dev;
        PlayerController p;
        p.setDevice(&dev);
        QVERIFY(p.openScript(script));
        connectStroker(dev, sink);
        p.output()->setMaxSpeed(250);
        QCOMPARE(p.output()->maxSpeed(), 250);
        samn::StrokeFilter f;
        f.maxSpeed = 250;
        std::vector<samn::Action> raw;
        for (const auto& a : zigzag(40, 100)) raw.push_back({a.first, a.second});
        const auto filtered = samn::ApplyStrokeFilter(raw, f);
        // A stroke of 100 units in 100 ms is cut to what 250 units/s allows.
        const double expected = [&] {
            for (const auto& a : filtered)
                if (a.at > 0) return a.pos / 100.0;
            return -1.0;
        }();
        QVERIFY(expected > 0 && expected < 0.5);
        p.setPlaying(true);
        p.setVideoPositionMs(0);
        QTRY_VERIFY_WITH_TIMEOUT(!Lines(sink).isEmpty(), kWait);
        QCOMPARE(Lines(sink).first().left(2), QString("L0"));
        QVERIFY(Lines(sink).first().contains(Move("L0", expected, 500).left(6)));
        p.setPlaying(false);
    }

    void extraAxesFollowTheScript() {
        UdpLineSink sink;
        DeviceController dev;
        PlayerController p;
        p.setDevice(&dev);
        connectStroker(dev, sink);
        QVERIFY(dev.axes().contains("surge"));
        p.output()->setScript({{0, 0}, {1000, 100}}, {{0, 0}, {1000, 0}}, {{0, 0}, {1000, 0}},
                              {{"surge", {{0, 50}, {1000, 100}}}, {"nonsense", {{0, 0}, {1000, 100}}}});
        p.output()->setPlaying(true);
        p.output()->update(0);
        QTRY_VERIFY_WITH_TIMEOUT(Lines(sink).contains(Move("L1", 1.0, 1000)), kWait);
        QVERIFY(!Lines(sink).filter("L0").isEmpty());
        p.output()->setPlaying(false);
    }

    // --- pause, stop, home ---
    void pauseStopsAndMovesHome() {
        const QString script = writeScript(files_.path(), "slow4.funscript", {{0, 0}, {1000, 100}, {2000, 0}});
        UdpLineSink sink;
        DeviceController dev;
        PlayerController p;
        p.setDevice(&dev);
        QVERIFY(p.openScript(script));
        connectStroker(dev, sink);
        p.output()->setStrokeRange(10, 90, true);
        p.output()->setHomeOnPause(true);
        p.setPlaying(true);
        p.setVideoPositionMs(0);
        QTRY_VERIFY_WITH_TIMEOUT(!Lines(sink).isEmpty(), kWait);
        p.setPlaying(false);
        QVERIFY(!dev.sessionActive());
        // Stop, then the slow move to the bottom of the range.
        QTRY_VERIFY_WITH_TIMEOUT(Lines(sink).size() >= 3, kWait);
        const QStringList l = Lines(sink);
        QCOMPARE(l[l.size() - 2], QString("DSTOP"));
        QCOMPARE(l.last(), Move("L0", 0.1, 1200));
        // After the pause the first move eases in again.
        p.setPlaying(true);
        p.setVideoPositionMs(0);
        QTRY_VERIFY_WITH_TIMEOUT(Lines(sink).last() == Move("L0", 0.9, 1000) || Lines(sink).last() == Move("L0", 0.9, 640), kWait);
        p.setPlaying(false);
    }

    void pauseWithoutHomeOnlyStops() {
        const QString script = writeScript(files_.path(), "slow5.funscript", {{0, 0}, {1000, 100}, {2000, 0}});
        UdpLineSink sink;
        DeviceController dev;
        PlayerController p;
        p.setDevice(&dev);
        QVERIFY(p.openScript(script));
        connectStroker(dev, sink);
        QVERIFY(!p.output()->homeOnPause());
        p.setPlaying(true);
        p.setVideoPositionMs(0);
        QTRY_VERIFY_WITH_TIMEOUT(!Lines(sink).isEmpty(), kWait);
        p.setPlaying(false);
        QTRY_COMPARE_WITH_TIMEOUT(Lines(sink).last(), QString("DSTOP"), kWait);
        QTest::qWait(300);
        QCOMPARE(Lines(sink).last(), QString("DSTOP"));
    }

    void connectionIsRefusedWhilePlaying() {
        const QString script = writeScript(files_.path(), "fast4.funscript", zigzag(20, 250));
        DeviceController dev;
        PlayerController p;
        p.setDevice(&dev);
        QVERIFY(p.openScript(script));
        connectMock(dev);
        p.setPlaying(true);
        dev.disconnectDevice();  // refused during a session (0.7)
        QTest::qWait(200);
        QVERIFY(dev.connected());
        p.setPlaying(false);
        dev.disconnectDevice();
        QTRY_VERIFY_WITH_TIMEOUT(!dev.connected(), kWait);
    }

    // --- emergency stop ---
    void emergencyStopDropsQueuedLevelsAndStaysSilent() {
        const QString script = writeScript(files_.path(), "fast5.funscript", zigzag(400, 100));
        DeviceController dev;
        PlayerController p;
        p.setDevice(&dev);
        QVERIFY(p.openScript(script));
        connectMock(dev);
        QSignalSpy logSpy(&dev, &DeviceController::logMessage);
        QSignalSpy stopped(p.output(), &PlaybackOutput::emergencyStopped);
        auto mock = [&] { QStringList l; for (const auto& a : logSpy) l << a.at(0).toString(); return MockLines(l); };
        p.setPlaying(true);
        // A burst of positions, then the emergency stop at once (no event loop between).
        for (int i = 0; i < 80; ++i) p.setVideoPositionMs(i * 130);
        p.output()->emergencyStop();
        QCOMPARE(stopped.count(), 1);
        QVERIFY(!dev.sessionActive());
        QTRY_VERIFY_WITH_TIMEOUT(mock().contains("[mock] stop"), kWait);
        QTest::qWait(300);
        // Nothing follows the stop: no queued level got through.
        QCOMPARE(mock().last(), QString("[mock] stop"));
        const int n = mock().size();
        // More positions while the video has not paused yet: still silent.
        p.setVideoPositionMs(9000);
        p.setVideoPositionMs(9100);
        QTest::qWait(300);
        QCOMPARE(mock().size(), n);
        // The pause that follows does not send a second stop or a home move.
        p.setPlaying(false);
        QTest::qWait(200);
        QCOMPARE(mock().size(), n);
        // Playing again works.
        p.setPlaying(true);
        p.setVideoPositionMs(1000);
        QTRY_VERIFY_WITH_TIMEOUT(mock().size() > n, kWait);
        p.setPlaying(false);
    }

    void emergencyStopWithoutDeviceIsHarmless() {
        PlayerController p;
        QSignalSpy stopped(p.output(), &PlaybackOutput::emergencyStopped);
        p.output()->emergencyStop();
        QCOMPARE(stopped.count(), 1);
    }

    // --- D-132: the lanes follow the device ---
    void lanesFollowTheDevice() {
        const QString script = writeScript(files_.path(), "lanes.funscript", zigzag(40, 250));
        DeviceController dev;
        PlayerController p;
        p.setDevice(&dev);
        QSignalSpy lanes(&p, &PlayerController::lanesChanged);
        QVERIFY(p.openScript(script));
        // No device: what the script carries (a funscript carries the stroke only).
        QVERIFY(p.showStrokeLane());
        QVERIFY(!p.showVibrationLane());
        QVERIFY(!p.showSuctionLane());

        // A Sam Neo 2 class device (the Mock): vibration and suction, no stroke.
        lanes.clear();
        connectMock(dev);
        QVERIFY(lanes.count() >= 1);
        QVERIFY(!p.showStrokeLane());
        QVERIFY(p.showVibrationLane());
        QVERIFY(p.showSuctionLane());

        // Disconnected: back to the script's own channels.
        dev.disconnectDevice();
        QTRY_VERIFY_WITH_TIMEOUT(!dev.connected(), kWait);
        QVERIFY(p.showStrokeLane());
        QVERIFY(!p.showVibrationLane());

        // A stroker: the stroke only.
        UdpLineSink sink;
        connectStroker(dev, sink);
        QVERIFY(p.showStrokeLane());
        QVERIFY(!p.showVibrationLane());
        QVERIFY(!p.showSuctionLane());
    }

    void noScriptNoLanes() {
        DeviceController dev;
        PlayerController p;
        p.setDevice(&dev);
        connectMock(dev);
        QVERIFY(!p.showStrokeLane());
        QVERIFY(!p.showVibrationLane());
        QVERIFY(!p.showSuctionLane());
    }

    // --- settings ---
    void outputChoicesAreRemembered() {
        {
            PlayerController p;
            auto* o = p.output();
            o->setSmooth("makima");
            o->setMaxSpeed(400);
            o->setIdleFill(true);
            o->setHomeOnPause(true);
            o->setStrokeRange(15, 85, true);
            o->setStrokeOn(false);
            o->setVibrationLevel(60);
            o->setSuctionOn(false);
        }
        PlayerController q;
        auto* o = q.output();
        QCOMPARE(o->smooth(), QString("makima"));
        QCOMPARE(o->maxSpeed(), 400);
        QVERIFY(o->idleFill());
        QVERIFY(o->homeOnPause());
        QCOMPARE(o->strokeLo(), 15);
        QCOMPARE(o->strokeHi(), 85);
        QVERIFY(!o->strokeOn());
        QCOMPARE(o->vibrationLevel(), 60);
        QVERIFY(!o->suctionOn());
        QVERIFY(o->vibrationOn());
        // Out of range values are clamped, as 0.7 did.
        o->setMaxSpeed(10);
        QCOMPARE(o->maxSpeed(), 50);
        o->setMaxSpeed(5000);
        QCOMPARE(o->maxSpeed(), 1000);
        o->setMaxSpeed(0);
        QCOMPARE(o->maxSpeed(), 0);
    }
};

QTEST_MAIN(PlaybackOutputTest)
#include "playback_output_test.moc"
