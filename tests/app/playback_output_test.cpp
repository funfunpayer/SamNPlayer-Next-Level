// Playback drives the device: the levels at the playhead, the stroker's
// moves (filter, safety range, soft seek), pause / stop / home, the emergency
// stop and the lanes that follow the device (D-132). Real DeviceController on
// its worker thread against the Mock device and a UDP sink (TCode).
#include <QFile>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <chrono>
#include <cmath>

#include "device_controller.h"
#include "fake_servers.h"
#include "player_controller.h"
#include "samn/device/tcode.h"
#include "samn/stroke_filter.h"
#include "training_controller.h"

using samn::ui::DeviceController;
using samn::ui::PlaybackOutput;
using samn::ui::PlayerController;
using samn::ui::TrainingController;
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

// The Mock device's log lines, collected as they come.
struct Log {
    QSignalSpy spy;
    explicit Log(DeviceController& dev) : spy(&dev, &DeviceController::logMessage) {}
    QStringList lines(const QString& channel = {}) const {
        QStringList out;
        for (const auto& a : spy) {
            const QString l = a.at(0).toString();
            if (l.startsWith("[mock]") && (channel.isEmpty() || l.startsWith("[mock] " + channel + "="))) out << l;
        }
        return out;
    }
    bool has(const QString& line) const { return lines().contains(line); }
    QString last(const QString& channel) const {
        const QStringList l = lines(channel);
        return l.isEmpty() ? QString() : l.last();
    }
    int count(const QString& channel) const { return lines(channel).size(); }
};

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

    // The speed limit holds for what the device is told, not only for the
    // script: a move starts at a position report and must not squeeze the
    // way from the last sent position into the time until the next action
    // (late reports, dense actions, play speed above 1).
    void speedLimitHoldsForLateReports_data() {
        QTest::addColumn<double>("speed");
        QTest::newRow("1x") << 1.0;
        QTest::newRow("2x") << 2.0;
    }
    void speedLimitHoldsForLateReports() {
        QFETCH(double, speed);
        // Up and down in 10-unit steps every 100 ms: exactly 100 units/s.
        QList<QPair<int, int>> ramp;
        for (int i = 0; i <= 60; ++i) {
            const int phase = i % 20;
            ramp.append({i * 100, (phase <= 10 ? phase : 20 - phase) * 10});
        }
        const QString script = writeScript(files_.path(), "ramp.funscript", ramp);
        UdpLineSink sink;
        DeviceController dev;
        PlayerController p;
        p.setDevice(&dev);
        QVERIFY(p.openScript(script));
        connectStroker(dev, sink);
        constexpr int kLimit = 100;
        p.output()->setMaxSpeed(kLimit);
        p.output()->setSpeed(speed);
        p.setPlaying(true);
        // A report every 250 ms; every report has a new target, so each
        // sends one move (waited for: the worker sends only the newest).
        int sent = 0;
        for (int t = 0; t <= 5500; t += 250) {
            p.setVideoPositionMs(t);
            ++sent;
            QTRY_COMPARE_WITH_TIMEOUT(Lines(sink).filter(QRegularExpression("^L0")).size(), sent, kWait);
        }
        p.setPlaying(false);
        const QRegularExpression move("^L0(\\d{4})I(\\d+)$");
        double last = -1;
        int checked = 0;
        for (const QString& line : Lines(sink).filter(QRegularExpression("^L0"))) {
            const auto m = move.match(line);
            QVERIFY2(m.hasMatch(), qPrintable(line));
            const double pos = m.captured(1).toInt() / 100.0;  // units 0-100
            const int ms = m.captured(2).toInt();
            if (last >= 0) {
                const double unitsPerS = std::abs(pos - last) * 1000.0 / ms;
                QVERIFY2(unitsPerS <= kLimit + 1, qPrintable(QString("%1: %2 units/s").arg(line).arg(unitsPerS)));
                ++checked;
            }
            last = pos;
        }
        QCOMPARE(checked, sent - 1);
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

    // 0.7 TestDriveAxesSendsEachAxisOnce. Over TCode UDP: twist is R0, roll is R1.
    void extraAxesSendEachAxisOnce() {
        UdpLineSink sink;
        DeviceController dev;
        PlayerController p;
        p.setDevice(&dev);
        connectStroker(dev, sink);
        auto count = [&](const char* prefix) { return Lines(sink).filter(QRegularExpression(QString("^") + prefix)).size(); };
        p.output()->setScript({{0, 0}, {1000, 100}, {2000, 0}}, {}, {},
                              {{"twist", {{0, 50}, {800, 90}}},
                               {"roll", {{600, 10}, {0, 50}}},  // unsorted on purpose
                               {"nonsense", {{0, 50}, {500, 0}}},  // the device does not take it
                               {"sway", {}}});
        p.output()->setPlaying(true);
        p.output()->update(100);
        // One move per axis, sorted by name (roll before twist), the untaken and the empty axis skipped.
        QTRY_VERIFY_WITH_TIMEOUT(Lines(sink).contains(Move("R0", 0.9, 700)), kWait);
        QCOMPARE(Lines(sink).filter("R1").size(), 1);
        QVERIFY(Lines(sink).contains(Move("R1", 0.1, 500)));
        QVERIFY(Lines(sink).indexOf(Move("R1", 0.1, 500)) < Lines(sink).indexOf(Move("R0", 0.9, 700)));
        QCOMPARE(count("L2"), 0);  // sway: empty
        // The same targets are not sent again while the playhead moves on inside the move.
        p.output()->update(200);
        QTest::qWait(250);
        QCOMPARE(count("R0"), 1);
        QCOMPARE(count("R1"), 1);
        // Stroke off stops the extra axes too.
        p.output()->setStrokeOn(false);
        p.output()->update(300);
        QTest::qWait(250);
        QCOMPARE(count("R0"), 1);
        QCOMPARE(count("R1"), 1);
        // Back on resends; the stroke range does not scale the extra axes.
        p.output()->setStrokeRange(40, 60, true);
        p.output()->setStrokeOn(true);
        p.output()->update(300);
        QTRY_COMPARE_WITH_TIMEOUT(count("R1"), 2, kWait);
        QCOMPARE(count("R0"), 2);
        QCOMPARE(Lines(sink).filter("R1").last(), Move("R1", 0.1, 300));
        QCOMPARE(Lines(sink).filter("R0").last(), Move("R0", 0.9, 500));
        QCOMPARE(Lines(sink).filter("L0").last(), Move("L0", 0.6, 700));  // the stroke itself is scaled into 40-60 %
        // A pause resets the targets: the next start sends every axis again.
        p.output()->setPlaying(false);
        p.output()->setPlaying(true);
        p.output()->update(300);
        QTRY_COMPARE_WITH_TIMEOUT(count("R1"), 3, kWait);
        QCOMPARE(count("R0"), 3);
        p.output()->setPlaying(false);
    }

    // 0.7 TestDriveAxesNeedsMultiAxisDevice: a plain stroker (Buttplug LinearCmd) has the stroke only.
    void extraAxesNeedAMultiAxisDevice() {
        FakeButtplugServer server(FakeButtplugServer::StrokerMessages());
        DeviceController dev;
        PlayerController p;
        p.setDevice(&dev);
        dev.connectIntiface(server.url());
        QTRY_COMPARE_WITH_TIMEOUT(dev.state(), QString("connected"), kWait);
        QVERIFY(dev.capLinear());
        QVERIFY(!dev.axes().contains("surge"));
        p.output()->setScript({{0, 0}, {1000, 100}}, {}, {}, {{"surge", {{0, 50}, {800, 90}}}, {"twist", {{0, 50}, {800, 90}}}});
        p.output()->setPlaying(true);
        p.output()->update(100);
        QTRY_COMPARE_WITH_TIMEOUT(server.Count("LinearCmd"), 1, kWait);  // the stroke only
        p.output()->update(200);
        QTest::qWait(250);
        QCOMPARE(server.Count("LinearCmd"), 1);
        QVERIFY2(dev.lastError().isEmpty(), qPrintable(dev.lastError()));  // no axis command, no error
        p.output()->setPlaying(false);
    }

    // 0.7 TestChannelLevelsDefaultFullAndClamp (levels are 0-100 here, not 0-1).
    void channelLevelsDefaultFullAndClamp() {
        PlaybackOutput o;
        QCOMPARE(o.vibrationLevel(), 100);
        QCOMPARE(o.suctionLevel(), 100);
        o.setVibrationLevel(40);
        o.setSuctionLevel(-3);
        QCOMPARE(o.vibrationLevel(), 40);
        QCOMPARE(o.suctionLevel(), 0);
        o.setVibrationLevel(700);
        o.setSuctionLevel(25);
        QCOMPARE(o.vibrationLevel(), 100);
        QCOMPARE(o.suctionLevel(), 25);
        o.setSuctionLevel(1000);
        QCOMPARE(o.suctionLevel(), 100);
    }

    // 0.7 TestDriveStrokeHonoursSpeedAndMinimum, the minimum half.
    void lateStrokePositionStillGetsTheMinimumMove() {
        const QString script = writeScript(files_.path(), "late.funscript", {{0, 0}, {1000, 100}, {2000, 0}});
        UdpLineSink sink;
        DeviceController dev;
        PlayerController p;
        p.setDevice(&dev);
        QVERIFY(p.openScript(script));
        connectStroker(dev, sink);
        p.setPlaying(true);
        p.setVideoPositionMs(900);  // start: eases in towards the point at 1000
        QTRY_COMPARE_WITH_TIMEOUT(Lines(sink).filter("L0").size(), 1, kWait);
        // 5 ms before the next point (2000): not a 5 ms move, the minimum.
        p.setVideoPositionMs(1995);
        QTRY_COMPARE_WITH_TIMEOUT(Lines(sink).filter("L0").size(), 2, kWait);
        QCOMPARE(Lines(sink).filter("L0").last(), Move("L0", 0.0, 20));  // 0.7 minStrokeMove
        QCOMPARE(PlaybackOutput::kMinMoveMs, 20);
        p.setPlaying(false);
    }

    // 0.7 TestDriveStrokeOnlyForLinearDevices.
    void strokeOnlyForLinearDevices() {
        // The Mock has no linear channel: no positions, no error (vibration still flows).
        {
            DeviceController dev;
            PlayerController p;
            p.setDevice(&dev);
            connectMock(dev);
            QVERIFY(!dev.capLinear());
            QSignalSpy logSpy(&dev, &DeviceController::logMessage);
            p.output()->setScript({{0, 0}, {1000, 100}, {2000, 0}}, {{0, 80}, {2000, 80}}, {});
            p.output()->setPlaying(true);
            p.output()->update(100);
            QTRY_VERIFY_WITH_TIMEOUT([&] { for (const auto& a : logSpy) if (a.at(0).toString().startsWith("[mock] vibration")) return true; return false; }(), kWait);
            QTest::qWait(200);
            for (const auto& a : logSpy) QVERIFY2(!a.at(0).toString().contains("position"), qPrintable(a.at(0).toString()));
            QVERIFY2(dev.lastError().isEmpty(), qPrintable(dev.lastError()));
            p.output()->setPlaying(false);
        }
        // A stroker with a script that has no stroke actions: nothing is sent.
        {
            UdpLineSink sink;
            DeviceController dev;
            PlayerController p;
            p.setDevice(&dev);
            connectStroker(dev, sink);
            p.output()->setScript({}, {}, {});
            p.output()->setPlaying(true);
            p.output()->update(100);
            QTest::qWait(300);
            QVERIFY(Lines(sink).filter("L0").isEmpty());
            // The same device does move once the script has a stroke.
            p.output()->setScript({{0, 0}, {1000, 100}}, {}, {});
            p.output()->update(100);
            QTRY_VERIFY_WITH_TIMEOUT(!Lines(sink).filter("L0").isEmpty(), kWait);
            p.output()->setPlaying(false);
        }
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

    // 0.7 companion axis files beside the script (<stem>.surge.funscript …):
    // read lanes in 0.7's order, only the axes a connected device has, and
    // the device gets them.
    void companionAxisFilesBecomeLanesAndDriveTheAxes() {
        const QString script = writeScript(files_.path(), "multi.funscript", zigzag(40, 250));
        writeScript(files_.path(), "multi.roll.funscript", zigzag(40, 250));
        writeScript(files_.path(), "multi.surge.funscript", zigzag(40, 250));
        UdpLineSink sink;
        DeviceController dev;
        PlayerController p;
        p.setDevice(&dev);
        QVERIFY(p.openScript(script));
        QCOMPARE(p.axisLanes(), (QStringList{"surge", "roll"}));
        connectStroker(dev, sink);  // TCode: L0-L2, R0-R2
        QCOMPARE(p.axisLanes(), (QStringList{"surge", "roll"}));
        p.output()->setPlaying(true);
        p.output()->update(0);
        QTRY_VERIFY_WITH_TIMEOUT(!Lines(sink).filter("L1").isEmpty(), kWait);  // surge
        QTRY_VERIFY_WITH_TIMEOUT(!Lines(sink).filter("R1").isEmpty(), kWait);  // roll
        p.output()->setPlaying(false);
        // Another script without companions: no axis lanes.
        QVERIFY(p.openScript(writeScript(files_.path(), "single.funscript", zigzag(40, 250))));
        QVERIFY(p.axisLanes().isEmpty());
    }

    // A start or a seek eases the extra axes in like the stroke: twist, roll
    // and pitch must not jump their full range in the time until the next
    // point (soft seek, per axis from where it was sent last).
    void extraAxesEaseInAfterAStartAndASeek() {
        const QString script = writeScript(files_.path(), "soft.funscript", zigzag(40, 250));
        writeScript(files_.path(), "soft.roll.funscript", zigzag(40, 250));
        UdpLineSink sink;
        DeviceController dev;
        PlayerController p;
        p.setDevice(&dev);
        QVERIFY(p.openScript(script));
        connectStroker(dev, sink);
        p.setPlaying(true);
        // The start: the next point is 250 ms away, the position unknown -> the whole range in 500 ms.
        p.setVideoPositionMs(0);
        QTRY_VERIFY_WITH_TIMEOUT(Lines(sink).contains(Move("R1", 1.0, 500)), kWait);
        QVERIFY(Lines(sink).contains(Move("L0", 1.0, 500)));
        // Following the script: the real time until due.
        p.setVideoPositionMs(250);
        QTRY_VERIFY_WITH_TIMEOUT(Lines(sink).contains(Move("R1", 0.0, 250)), kWait);
        // A seek far ahead: 150 ms to a point a full stroke away -> eased to 500 ms.
        p.setVideoPositionMs(5100);
        QTRY_VERIFY_WITH_TIMEOUT(Lines(sink).filter("R1").last() == Move("R1", 1.0, 500), kWait);
        QTRY_VERIFY_WITH_TIMEOUT(Lines(sink).filter("L0").last() == Move("L0", 1.0, 500), kWait);
        // Back to normal right after.
        p.setVideoPositionMs(5250);
        QTRY_VERIFY_WITH_TIMEOUT(Lines(sink).filter("R1").last() == Move("R1", 0.0, 250), kWait);
        p.setPlaying(false);
    }

    // Playback and Training both use the device: one of them ending must not
    // unlock connect / disconnect / the device tests while the other still drives it.
    void sessionStaysActiveWhileAnyUserDrivesTheDevice() {
        const QString script = writeScript(files_.path(), "both.funscript", zigzag(40, 250));
        DeviceController dev;
        connectMock(dev);
        PlayerController p;
        p.setDevice(&dev);
        QVERIFY(p.openScript(script));
        TrainingController tc;
        tc.setDevice(&dev);
        tc.setCycles(1);
        tc.setRampUpMs(100);
        tc.setHoldMs(100);
        tc.setRestMs(100);
        p.setPlaying(true);
        QVERIFY(dev.sessionActive());
        QVERIFY(tc.start());  // reuses the connection
        QCOMPARE(tc.state(), QString("running"));
        tc.stop();
        QCOMPARE(tc.state(), QString("idle"));
        QVERIFY(dev.sessionActive());  // the player still plays
        dev.disconnectDevice();
        QVERIFY(dev.lastError().contains("is using this connection"));
        dev.runDiagnostics();
        QVERIFY(dev.lastError().contains("not available during playback"));
        QTest::qWait(100);
        QVERIFY(dev.connected());
        // The other way round: playback ends first, training goes on.
        QVERIFY(tc.start());
        p.setPlaying(false);
        QVERIFY(dev.sessionActive());
        tc.stop();
        QVERIFY(!dev.sessionActive());
        dev.disconnectDevice();
        QTRY_VERIFY_WITH_TIMEOUT(!dev.connected(), kWait);
    }

    // 0.7 lane / legend selection: the main curve shows the picked lane;
    // Up / Down step through the shown lanes; a lane that goes away and a
    // new script fall back to the stroke.
    void theMainCurveShowsThePickedLane() {
        const QString script = writeScript(files_.path(), "pick.funscript", zigzag(40, 250));
        writeScript(files_.path(), "pick.twist.funscript", zigzag(40, 250));
        DeviceController dev;
        PlayerController p;
        p.setDevice(&dev);
        QVERIFY(p.openScript(script));
        QCOMPARE(p.laneChoices(), (QStringList{"stroke", "twist"}));
        QCOMPARE(p.activeLane(), QStringLiteral("stroke"));
        QSignalSpy changed(&p, &PlayerController::activeLaneChanged);
        p.setActiveLane("twist");
        QCOMPARE(p.activeLane(), QStringLiteral("twist"));
        QCOMPARE(changed.count(), 1);
        p.setActiveLane("vibration");  // not shown: back to the stroke
        QCOMPARE(p.activeLane(), QStringLiteral("stroke"));
        p.stepActiveLane(-1);  // wraps
        QCOMPARE(p.activeLane(), QStringLiteral("twist"));
        p.stepActiveLane(1);
        QCOMPARE(p.activeLane(), QStringLiteral("stroke"));
        // A Sam Neo 2 class device has no twist: the pick falls back.
        p.setActiveLane("twist");
        connectMock(dev);
        QCOMPARE(p.laneChoices(), (QStringList{"stroke", "vibration", "suction"}));
        QCOMPARE(p.activeLane(), QStringLiteral("stroke"));
        p.setActiveLane("suction");
        QCOMPARE(p.activeLane(), QStringLiteral("suction"));
        // A new script starts on the stroke.
        QVERIFY(p.openScript(script));
        QCOMPARE(p.activeLane(), QStringLiteral("stroke"));
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

    // --- soft start (PS-04, 0.7 sync.go softStartWithPositions) ---
    void softStartRampsTheLevels() {
        DeviceController dev;
        PlayerController p;
        p.setDevice(&dev);
        connectMock(dev);
        Log log(dev);
        qint64 clk = 0;
        auto* o = p.output();
        o->setClock([&] { return clk; }, false);
        QCOMPARE(o->softStartMs(), 500);  // the 0.7 default
        o->setScript({}, {{0, 80}, {60000, 80}}, {{0, 50}, {60000, 50}});
        o->setPlaying(true);
        // The first frame: step 1 of 10 at once, for both channels.
        o->update(1000);
        QTRY_VERIFY_WITH_TIMEOUT(log.has("[mock] vibration=0.08") && log.has("[mock] suction=0.05"), kWait);
        QVERIFY(!log.has("[mock] vibration=0.80"));
        clk = 250;  // step 6
        o->update(1250);
        QTRY_VERIFY_WITH_TIMEOUT(log.last("vibration") == "[mock] vibration=0.48", kWait);
        clk = 500;  // done: the script
        o->update(1500);
        QTRY_VERIFY_WITH_TIMEOUT(log.last("vibration") == "[mock] vibration=0.80" && log.last("suction") == "[mock] suction=0.50", kWait);
        // A seek while playing does not ramp again.
        clk = 600;
        o->update(30000);
        QTest::qWait(150);
        QCOMPARE(log.last("vibration"), QString("[mock] vibration=0.80"));
        // After a pause it ramps again.
        o->setPlaying(false);
        QTRY_COMPARE_WITH_TIMEOUT(log.last({}), QString("[mock] stop"), kWait);
        o->setPlaying(true);
        clk = 5000;
        o->update(30100);
        QTRY_COMPARE_WITH_TIMEOUT(log.last("vibration"), QString("[mock] vibration=0.08"), kWait);
        o->setPlaying(false);
        // Soft start 0 is off: the full level at once.
        o->setSoftStartMs(0);
        QCOMPARE(o->softStartMs(), 0);
        o->setPlaying(true);
        o->update(30200);
        QTRY_COMPARE_WITH_TIMEOUT(log.last("vibration"), QString("[mock] vibration=0.80"), kWait);
        o->setPlaying(false);
    }

    void softStartRunsBetweenPositions() {
        DeviceController dev;
        PlayerController p;
        p.setDevice(&dev);
        connectMock(dev);
        Log log(dev);
        auto* o = p.output();
        o->setSoftStartMs(200);
        o->setScript({}, {{0, 80}, {60000, 80}}, {});
        o->setPlaying(true);
        o->update(1000);  // one position only: the ramp timer finishes the ramp
        QTRY_COMPARE_WITH_TIMEOUT(log.last("vibration"), QString("[mock] vibration=0.80"), kWait);
        QVERIFY(log.count("vibration") >= 3);  // in steps, not one jump
        QCOMPARE(log.lines("vibration").value(0), QString("[mock] vibration=0.08"));
        o->setPlaying(false);
    }

    // 0.7 TestSyncIdleDuringSoftStartGoesQuiet: a pause inside the ramp stops
    // at once, nothing of the ramp follows, and the next start ramps again.
    void pauseInsideTheSoftStartStopsAtOnce() {
        DeviceController dev;
        PlayerController p;
        p.setDevice(&dev);
        connectMock(dev);
        Log log(dev);
        auto* o = p.output();
        o->setSoftStartMs(2000);
        o->setScript({}, {{0, 100}, {60000, 100}}, {{0, 100}, {60000, 100}});
        o->setPlaying(true);
        o->update(500);
        // Mid-ramp.
        QTRY_VERIFY_WITH_TIMEOUT(log.has("[mock] vibration=0.20"), kWait);
        QVERIFY(!log.has("[mock] vibration=1.00"));
        o->setPlaying(false);
        QTRY_COMPARE_WITH_TIMEOUT(log.last({}), QString("[mock] stop"), kWait);
        const int n = log.lines().size();
        QTest::qWait(400);  // longer than two ramp steps
        QCOMPARE(log.lines().size(), n);
        // Resume: soft start again, not the full level at once.
        o->setPlaying(true);
        o->update(800);
        QTRY_VERIFY_WITH_TIMEOUT(log.lines().size() > n, kWait);
        QCOMPARE(log.lines().value(n), QString("[mock] vibration=0.10"));
        QVERIFY(!log.lines().mid(n).contains("[mock] vibration=1.00"));
        o->setPlaying(false);
    }

    // The watchdog (no position for a while) inside the ramp stops too.
    void watchdogInsideTheSoftStartStops() {
        DeviceController dev;
        PlayerController p;
        p.setDevice(&dev);
        connectMock(dev);
        Log log(dev);
        auto* o = p.output();
        o->setSoftStartMs(3000);
        o->setStaleMs(200);
        o->setScript({}, {{0, 100}, {60000, 100}}, {});
        o->setPlaying(true);
        o->update(500);
        QTRY_COMPARE_WITH_TIMEOUT(log.last({}), QString("[mock] stop"), kWait);
        const int n = log.lines().size();
        QTest::qWait(400);
        QCOMPARE(log.lines().size(), n);
        QVERIFY(!log.has("[mock] vibration=1.00"));
        o->setPlaying(false);
    }

    // 0.7 TestSyncStrokeHomeOnPauseDuringSoftStart: Stop and the home move
    // also run while the soft start ramps.
    void homeOnPauseDuringSoftStart() {
        const QString script = writeScript(files_.path(), "soft.funscript", {{0, 0}, {1000, 100}, {2000, 0}});
        UdpLineSink sink;
        DeviceController dev;
        PlayerController p;
        p.setDevice(&dev);
        QVERIFY(p.openScript(script));
        connectStroker(dev, sink);
        p.output()->setSoftStartMs(3000);
        p.output()->setStrokeRange(20, 80, true);
        p.output()->setHomeOnPause(true);
        p.setPlaying(true);
        p.setVideoPositionMs(200);
        QTRY_VERIFY_WITH_TIMEOUT(!Lines(sink).filter("L0").isEmpty(), kWait);
        p.setPlaying(false);  // the soft start is still running
        QTRY_VERIFY_WITH_TIMEOUT(Lines(sink).contains(Move("L0", 0.2, 1200)), kWait);
        const QStringList l = Lines(sink);
        QCOMPARE(l[l.indexOf(Move("L0", 0.2, 1200)) - 1], QString("DSTOP"));
    }

    // --- Reduce peaks (0.7 TestSetOutputReducePeaks) ---
    void reducePeaksSoftensTheOutput() {
        DeviceController dev;
        PlayerController p;
        p.setDevice(&dev);
        connectMock(dev);
        Log log(dev);
        auto* o = p.output();
        QVERIFY(!o->reducePeaks());  // off by default
        o->setSoftStartMs(0);
        o->setReducePeaks(true);
        o->setVibrationLevel(50);
        o->setScript({}, {{0, 100}, {60000, 100}}, {{0, 40}, {60000, 40}});
        o->setPlaying(true);
        o->update(1000);
        // The peak first, then the channel level: 1 -> 0.76 -> x 0.5; 0.4 stays below the knee.
        QTRY_VERIFY_WITH_TIMEOUT(log.last("vibration") == "[mock] vibration=0.38" && log.last("suction") == "[mock] suction=0.40", kWait);
        o->setReducePeaks(false);  // takes effect while playing
        QTRY_COMPARE_WITH_TIMEOUT(log.last("vibration"), QString("[mock] vibration=0.50"), kWait);
        o->setPlaying(false);
    }

    // 0.7 Effects "Intensity": the master level on top of both channels; 0 is
    // silence (PS-11), saved like the channel levels.
    void masterIntensityScalesBothChannels() {
        DeviceController dev;
        {
            PlayerController p;
            p.setDevice(&dev);
            connectMock(dev);
            Log log(dev);
            auto* o = p.output();
            QCOMPARE(o->intensity(), 100);  // full by default
            o->setSoftStartMs(0);
            o->setVibrationLevel(50);
            o->setScript({}, {{0, 100}, {60000, 100}}, {{0, 40}, {60000, 40}});
            o->setPlaying(true);
            o->update(1000);
            QTRY_VERIFY_WITH_TIMEOUT(log.last("vibration") == "[mock] vibration=0.50" && log.last("suction") == "[mock] suction=0.40", kWait);
            o->setIntensity(50);  // live, without a new position
            QTRY_VERIFY_WITH_TIMEOUT(log.last("vibration") == "[mock] vibration=0.25" && log.last("suction") == "[mock] suction=0.20", kWait);
            o->setIntensity(0);
            QTRY_VERIFY_WITH_TIMEOUT(log.last("vibration") == "[mock] vibration=0.00" && log.last("suction") == "[mock] suction=0.00", kWait);
            o->setIntensity(150);
            QCOMPARE(o->intensity(), 100);
            o->setIntensity(0);
            o->setPlaying(false);
            dev.disconnectDevice();
            QTRY_VERIFY_WITH_TIMEOUT(!dev.connected(), kWait);
        }
        PlayerController q;
        QCOMPARE(q.output()->intensity(), 0);  // a stored 0 stays 0
    }

    // A mapping setting changes the levels the device gets at once (0.7 sent
    // them with the next Play; SamN2 maps again while playing), without
    // a new position and without ending the session.
    void mappingChangeReachesTheDeviceAtOnce() {
        const QString script = writeScript(files_.path(), "map.funscript", zigzag(60, 250));
        DeviceController dev;
        PlayerController p;
        p.setDevice(&dev);
        QVERIFY(p.openScript(script));
        connectMock(dev);
        Log log(dev);
        p.output()->setSoftStartMs(0);
        p.setPlaying(true);
        p.setVideoPositionMs(1000);
        const int full = p.vibrationNow();
        QVERIFY(full > 50);
        QTRY_COMPARE_WITH_TIMEOUT(log.last("vibration"), QString::asprintf("[mock] vibration=%.2f", full / 100.0), kWait);
        p.mapping()->setMaxSpeed(2.0);  // the same stroke is now a third of full speed
        QVERIFY(p.vibrationNow() < full);
        QTRY_COMPARE_WITH_TIMEOUT(log.last("vibration"), QString::asprintf("[mock] vibration=%.2f", p.vibrationNow() / 100.0), kWait);
        p.mapping()->setSyncMode("suction_only");
        QCOMPARE(p.vibrationNow(), 0);
        QTRY_COMPARE_WITH_TIMEOUT(log.last("vibration"), QString("[mock] vibration=0.00"), kWait);
        QVERIFY(dev.sessionActive());
        QVERIFY(p.output()->playing());
        p.setPlaying(false);
    }

    // --- Extended-O (0.7 player/extendedo.go) ---
    // 0.7 TestExtendedOScalesAmplitudeKeepsRhythm.
    void extendedOScalesAmplitudeKeepsRhythm() {
        DeviceController dev;
        PlayerController p;
        p.setDevice(&dev);
        connectMock(dev);
        Log log(dev);
        qint64 clk = 0;
        auto* o = p.output();
        o->setClock([&] { return clk; }, false);
        o->setSoftStartMs(0);
        o->setExtendedOMin(0.5);
        o->setExtendedORestoreMs(0);
        o->setExtendedOHoldS(1);
        std::vector<samn::Action> vib;
        for (int i = 0; i < 100; ++i) vib.push_back({i * 40, i % 2 ? 100 : 0});
        o->setScript({}, vib, {});
        QVERIFY(!o->triggerExtendedO());  // not playing: nothing to scale
        o->setPlaying(true);
        o->update(40);
        QTRY_COMPARE_WITH_TIMEOUT(log.last("vibration"), QString("[mock] vibration=1.00"), kWait);
        QCOMPARE(o->extendedOScale(), 1.0);  // no cycle: full height
        QVERIFY(o->canTriggerExtendedO());
        QVERIFY(o->triggerExtendedO());
        QVERIFY(o->extendedOActive());
        // The peaks come out at half, the troughs stay 0: the rhythm goes on.
        QTRY_COMPARE_WITH_TIMEOUT(log.last("vibration"), QString("[mock] vibration=0.50"), kWait);
        clk = 100;
        o->update(80);
        QTRY_COMPARE_WITH_TIMEOUT(log.last("vibration"), QString("[mock] vibration=0.00"), kWait);
        clk = 200;
        o->update(120);
        QTRY_COMPARE_WITH_TIMEOUT(log.last("vibration"), QString("[mock] vibration=0.50"), kWait);
        // After the hold (restore 0: at once) full height again.
        clk = 1000;
        o->update(200);
        QTRY_COMPARE_WITH_TIMEOUT(log.last("vibration"), QString("[mock] vibration=1.00"), kWait);
        QVERIFY(!o->extendedOActive());
        o->setPlaying(false);
    }

    void extendedORampsDownHoldsAndBack() {
        DeviceController dev;
        PlayerController p;
        p.setDevice(&dev);
        connectMock(dev);
        Log log(dev);
        qint64 clk = 0;
        auto* o = p.output();
        o->setClock([&] { return clk; }, false);
        // The 0.7 defaults.
        QCOMPARE(o->extendedOMin(), 0.1);
        QCOMPARE(o->extendedOHoldS(), 10.0);
        QCOMPARE(o->extendedORestoreMs(), 500);
        QVERIFY(o->extendedOEnabled());
        o->setSoftStartMs(0);
        o->setScript({}, {{0, 100}, {60000, 100}}, {{0, 100}, {60000, 100}});
        o->setPlaying(true);
        o->update(1000);
        QTRY_COMPARE_WITH_TIMEOUT(log.last("vibration"), QString("[mock] vibration=1.00"), kWait);
        QVERIFY(o->triggerExtendedO());
        QTRY_COMPARE_WITH_TIMEOUT(log.last("vibration"), QString("[mock] vibration=0.91"), kWait);  // step 1 at once
        const auto at = [&](qint64 c, qint64 t, const char* want) {
            clk = c;
            o->update(t);
            QTRY_COMPARE_WITH_TIMEOUT(log.last("vibration"), QString(want), kWait);
            // Suction is written right after vibration: wait for it too.
            QTRY_COMPARE_WITH_TIMEOUT(log.last("suction"), QString(want).replace("vibration", "suction"), kWait);
        };
        at(250, 1250, "[mock] vibration=0.46");   // step 6 down
        at(600, 1600, "[mock] vibration=0.10");   // hold
        at(10400, 11400, "[mock] vibration=0.10");
        at(10500, 11500, "[mock] vibration=0.19");  // step 1 back up
        QVERIFY(o->extendedOActive());
        at(11000, 12000, "[mock] vibration=1.00");
        QVERIFY(!o->extendedOActive());
        o->setPlaying(false);
    }

    // PS-11 / 0.7 TestExtendedOMinLevelZeroSilences: a minimum of 0 is
    // silence, never "unset" (0.7 once ran the device at full power).
    void extendedOMinLevelZeroSilences() {
        DeviceController dev;
        PlayerController p;
        p.setDevice(&dev);
        connectMock(dev);
        Log log(dev);
        qint64 clk = 0;
        auto* o = p.output();
        o->setClock([&] { return clk; }, false);
        o->setSoftStartMs(0);
        o->setExtendedOMin(0);
        QCOMPARE(o->extendedOMin(), 0.0);
        o->setExtendedORestoreMs(0);
        o->setScript({}, {{0, 80}, {60000, 80}}, {{0, 40}, {60000, 40}});
        o->setPlaying(true);
        o->update(1000);
        QTRY_COMPARE_WITH_TIMEOUT(log.last("vibration"), QString("[mock] vibration=0.80"), kWait);
        QVERIFY(o->triggerExtendedO());
        QCOMPARE(o->extendedOScale(), 0.0);
        clk = 500;
        o->update(1500);
        QTRY_VERIFY_WITH_TIMEOUT(log.last("vibration") == "[mock] vibration=0.00" && log.last("suction") == "[mock] suction=0.00", kWait);
        clk = 5000;
        o->update(5000);
        QTest::qWait(150);
        QCOMPARE(log.last("vibration"), QString("[mock] vibration=0.00"));
        o->setPlaying(false);
    }

    void secondTriggerIsIgnored() {
        qint64 clk = 0;
        PlaybackOutput o;
        o.setClock([&] { return clk; }, false);
        o.setExtendedOMin(0.5);
        o.setExtendedORestoreMs(0);
        o.setExtendedOHoldS(2);
        QSignalSpy changed(&o, &PlaybackOutput::extendedOChanged);
        o.setPlaying(true);
        QVERIFY(o.triggerExtendedO());
        clk = 1500;
        // A second press while the cycle runs neither restarts nor extends it.
        QVERIFY(!o.triggerExtendedO());
        QCOMPARE(o.extendedOScale(), 0.5);
        clk = 2000;  // the end of the first cycle
        QCOMPARE(o.extendedOScale(), 1.0);
        QVERIFY(!o.extendedOActive());
        QVERIFY(changed.count() >= 2);
        // After it, a new trigger works again.
        QVERIFY(o.triggerExtendedO());
        QCOMPARE(o.extendedOScale(), 0.5);
        // Switched off: no trigger; the emergency stop ends the cycle.
        o.emergencyStop();
        QVERIFY(!o.extendedOActive());
        QCOMPARE(o.extendedOScale(), 1.0);
        QVERIFY(!o.canTriggerExtendedO());  // halted until the next start
        o.setPlaying(false);
        o.setPlaying(true);
        o.setExtendedOEnabled(false);
        QVERIFY(!o.canTriggerExtendedO());
        QVERIFY(!o.triggerExtendedO());
        o.setExtendedOEnabled(true);
        QVERIFY(o.triggerExtendedO());
        // A new script is a new session: the cycle ends.
        o.setScript({}, {}, {});
        QVERIFY(!o.extendedOActive());
        // A pause does not end it (0.7: the cycle runs on through a pause).
        QVERIFY(o.triggerExtendedO());
        o.setPlaying(false);
        QVERIFY(o.extendedOActive());
    }

    // 0.7 checkAutoExtendedO: "Auto Extended-O in marked range".
    void autoExtendedOInMarkedRange() {
        const QString script = writeScript(files_.path(), "auto.funscript", zigzag(100, 250));
        PlayerController p;
        QVERIFY(p.openScript(script));
        qint64 clk = 0;
        auto* o = p.output();
        o->setClock([&] { return clk; }, false);
        o->setExtendedORestoreMs(0);
        o->setExtendedOHoldS(1);
        p.setMarker(5000, 8000);
        QVERIFY(!p.autoExtendedO());  // off by default (0.7)
        p.setPlaying(true);
        p.setVideoPositionMs(6000);
        QVERIFY(!o->extendedOActive());  // off: nothing
        p.setAutoExtendedO(true);
        p.setVideoPositionMs(6100);
        QVERIFY(o->extendedOActive());  // inside the range while playing
        clk = 2000;
        QCOMPARE(o->extendedOScale(), 1.0);  // the cycle is over
        p.setVideoPositionMs(7000);
        QVERIFY(!o->extendedOActive());  // once per pass
        p.setVideoPositionMs(9000);
        QVERIFY(!o->extendedOActive());
        // A seek before the range arms it again.
        p.setVideoPositionMs(1000);
        p.setVideoPositionMs(5000);
        QVERIFY(o->extendedOActive());
        clk = 4000;
        QCOMPARE(o->extendedOScale(), 1.0);
        // Paused: positions (seeks) never trigger.
        p.setPlaying(false);
        p.setVideoPositionMs(1000);
        p.setVideoPositionMs(6000);
        QVERIFY(!o->extendedOActive());
        // Playing again re-arms it (0.7).
        p.setPlaying(true);
        p.setVideoPositionMs(6050);
        QVERIFY(o->extendedOActive());
        p.setPlaying(false);
    }

    void safetyChoicesAreRemembered() {
        {
            PlayerController p;
            auto* o = p.output();
            o->setSoftStartMs(1200);
            o->setReducePeaks(true);
            o->setExtendedOEnabled(false);
            o->setExtendedOMin(0);  // a real value, not "unset"
            o->setExtendedOHoldS(25);
            o->setExtendedORestoreMs(0);
        }
        PlayerController q;
        auto* o = q.output();
        QCOMPARE(o->softStartMs(), 1200);
        QVERIFY(o->reducePeaks());
        QVERIFY(!o->extendedOEnabled());
        QCOMPARE(o->extendedOMin(), 0.0);
        QCOMPARE(o->extendedOHoldS(), 25.0);
        QCOMPARE(o->extendedORestoreMs(), 0);
        // Clamped to the 0.7 slider ranges.
        o->setSoftStartMs(99999);
        QCOMPARE(o->softStartMs(), 3000);
        o->setSoftStartMs(-4);
        QCOMPARE(o->softStartMs(), 0);
        o->setExtendedOMin(3);
        QCOMPARE(o->extendedOMin(), 1.0);
        o->setExtendedOMin(-1);
        QCOMPARE(o->extendedOMin(), 0.0);
        o->setExtendedOMin(std::nan(""));
        QCOMPARE(o->extendedOMin(), 0.0);
        o->setExtendedOHoldS(0);
        QCOMPARE(o->extendedOHoldS(), 1.0);
        o->setExtendedOHoldS(500);
        QCOMPARE(o->extendedOHoldS(), 60.0);
        o->setExtendedORestoreMs(99999);
        QCOMPARE(o->extendedORestoreMs(), 3000);
    }

    // A broken stored value is "unset" and takes the default; 0 stays 0.
    void brokenSafetySettingsTakeTheDefaults() {
        QFile f(config_.path() + "/settings.json");
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(R"({"playback.soft_start_ms":"fast","playback.extended_o_min":null,)"
                R"("playback.extended_o_hold_seconds":true,"playback.extended_o_restore_ms":0})");
        f.close();
        PlayerController p;
        auto* o = p.output();
        QCOMPARE(o->softStartMs(), 500);
        QCOMPARE(o->extendedOMin(), 0.1);
        QCOMPARE(o->extendedOHoldS(), 10.0);
        QCOMPARE(o->extendedORestoreMs(), 0);
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
