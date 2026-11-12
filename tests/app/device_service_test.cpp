// PlaybackOutput, the Player and Training over the DeviceService interface,
// with a fake behind it (no worker thread, no transport, no timing).
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>
#include <memory>

#include "device_controller.h"
#include "fake_device_service.h"
#include "player_controller.h"
#include "training_controller.h"

using samn::ui::DeviceController;
using samn::ui::DeviceService;
using samn::ui::PlayerController;
using samn::ui::TrainingController;

namespace {

QString writeScript(const QString& dir, const QString& name, const QList<QPair<int, int>>& pts) {
    QString json = "{\"version\":\"1.0\",\"actions\":[";
    for (int i = 0; i < pts.size(); ++i)
        json += QString("%1{\"at\":%2,\"pos\":%3}").arg(i ? "," : "").arg(pts[i].first).arg(pts[i].second);
    json += "]}";
    const QString path = dir + "/" + name;
    QFile f(path);
    if (f.open(QIODevice::WriteOnly)) f.write(json.toUtf8());
    return path;
}

QList<QPair<int, int>> zigzag(int n, int stepMs) {
    QList<QPair<int, int>> pts;
    for (int i = 0; i < n; ++i) pts.append({i * stepMs, i % 2 ? 100 : 0});
    return pts;
}

}  // namespace

class DeviceServiceTest : public QObject {
    Q_OBJECT
    QTemporaryDir config_, files_;

private slots:
    void initTestCase() {
        QVERIFY(config_.isValid() && files_.isValid());
        qputenv("SAMN_CONFIG_DIR", config_.path().toUtf8());
    }
    void init() { QFile::remove(config_.path() + "/settings.json"); }

    // The real controller is a DeviceService: the interface reaches it.
    void controllerIsADeviceService() {
        DeviceController dev;
        DeviceService* s = &dev;
        QVERIFY(!s->connected());
        QVERIFY(!s->sessionActive());
        QObject owner;
        s->setSessionActiveFor(&owner, true);
        QVERIFY(dev.sessionActive());
        s->setSessionActiveFor(&owner, false);
        QVERIFY(!dev.sessionActive());
        QSignalSpy status(s, &DeviceService::statusChanged);
        s->connectMock();
        QTRY_COMPARE_WITH_TIMEOUT(s->connected(), true, 10000);
        QVERIFY(status.count() > 0);
        QCOMPARE(s->generation(), dev.generation());
        s->disconnectDevice();
    }

    void qmlPropertyStaysTheConcreteType() {
        DeviceController dev;
        FakeDeviceService fake;
        PlayerController p;
        p.setDevice(&dev);
        QCOMPARE(p.device(), &dev);
        QCOMPARE(p.deviceService(), static_cast<DeviceService*>(&dev));
        p.setDeviceService(&fake);  // a service that is no controller: C++ only
        QCOMPARE(p.device(), nullptr);
        QCOMPARE(p.deviceService(), static_cast<DeviceService*>(&fake));
    }

    // --- PlaybackOutput through the interface ---
    void levelsArriveInOrderAndTheHoldFollowsPlaying() {
        const QString script = writeScript(files_.path(), "fast.funscript", zigzag(60, 250));
        FakeDeviceService fake;
        fake.setConnected(true);
        PlayerController p;
        p.setDeviceService(&fake);
        QVERIFY(p.openScript(script));
        QVERIFY(!fake.sessionActive());
        p.setPlaying(true);
        QVERIFY(fake.sessionActive());
        QCOMPARE(fake.callsOf("hold").first(), QStringLiteral("hold 1"));
        p.setVideoPositionMs(1000);
        QTRY_VERIFY_WITH_TIMEOUT(fake.count("vibration") > 0 && fake.count("suction") > 0, 5000);
        // Both channels are sent in one tick, vibration first, as before.
        const int firstVib = fake.calls.indexOf(fake.callsOf("vibration").first());
        const int firstSuc = fake.calls.indexOf(fake.callsOf("suction").first());
        QVERIFY(firstVib < firstSuc);
        p.setPlaying(false);
        QVERIFY(!fake.sessionActive());
        QVERIFY(fake.count("stop") >= 1);
        QCOMPARE(fake.callsOf("hold").last(), QStringLiteral("hold 0"));
    }

    void emergencyStopReachesTheDeviceAndKeepsTheOutputSilent() {
        const QString script = writeScript(files_.path(), "fast2.funscript", zigzag(60, 250));
        FakeDeviceService fake;
        fake.setConnected(true);
        PlayerController p;
        p.setDeviceService(&fake);
        QVERIFY(p.openScript(script));
        p.setPlaying(true);
        p.setVideoPositionMs(1000);
        QTRY_VERIFY_WITH_TIMEOUT(fake.count("vibration") > 0, 5000);
        p.output()->emergencyStop();
        QCOMPARE(fake.count("emergencyStop"), 1);
        QVERIFY(!fake.sessionActive());
        fake.calls.clear();
        p.setVideoPositionMs(1500);
        QTest::qWait(150);
        QCOMPARE(fake.count("vibration"), 0);  // halted until the next start
        QCOMPARE(fake.count("suction"), 0);
    }

    void aStrokerGetsPositionsThroughTheInterface() {
        const QString script = writeScript(files_.path(), "stroke.funscript", zigzag(300, 100));
        FakeDeviceService fake;
        fake.setConnected(true, {"L0"}, false, false);
        PlayerController p;
        p.setDeviceService(&fake);
        QVERIFY(p.openScript(script));
        p.setPlaying(true);
        p.setVideoPositionMs(0);
        QTRY_VERIFY_WITH_TIMEOUT(fake.count("position") > 0, 5000);
        QCOMPARE(fake.count("vibration"), 0);
    }

    void aDeviceThatVanishesMidPlayIsNotCalledAgain() {
        const QString script = writeScript(files_.path(), "vanish.funscript", zigzag(60, 250));
        auto fake = std::make_unique<FakeDeviceService>();
        fake->setConnected(true);
        PlayerController p;
        p.setDeviceService(fake.get());
        QVERIFY(p.openScript(script));
        p.setPlaying(true);
        p.setVideoPositionMs(1000);
        QTRY_VERIFY_WITH_TIMEOUT(fake->count("vibration") > 0, 5000);
        QVERIFY(fake->sessionActive());
        fake.reset();  // the QPointer goes null: no call into a dead object
        QCOMPARE(p.deviceService(), nullptr);
        p.setVideoPositionMs(1500);
        QTest::qWait(100);
        p.output()->emergencyStop();
        p.setPlaying(false);
        QVERIFY(!p.output()->playing());
        QVERIFY(p.output()->device() == nullptr);
    }

    // The old device gives its hold up when the output's device changes mid-play; the new one takes it.
    void swappingTheOutputsDeviceMovesTheHold() {
        const QString script = writeScript(files_.path(), "swap.funscript", zigzag(60, 250));
        FakeDeviceService first, second;
        first.setConnected(true);
        second.setConnected(true);
        PlayerController p;
        p.setDeviceService(&first);
        QVERIFY(p.openScript(script));
        p.setPlaying(true);
        QVERIFY(first.sessionActive());
        p.setDeviceService(&second);
        QVERIFY(!first.sessionActive());
        QVERIFY(second.sessionActive());
        p.setPlaying(false);
        QVERIFY(!second.sessionActive());
        // The old one can be disconnected again: nothing refused it.
        first.disconnectDevice();
        QCOMPARE(first.count("refused"), 0);
    }

    // The Player's lanes follow the device: a status change emits lanesChanged.
    void aStatusChangeUpdatesThePlayersLanes() {
        FakeDeviceService fake;
        PlayerController p;
        p.setDeviceService(&fake);
        QSignalSpy lanes(&p, &PlayerController::lanesChanged);
        fake.setConnected(true, {"L0"}, false, false);
        QCOMPARE(lanes.count(), 1);
        fake.setConnected(false);
        QCOMPARE(lanes.count(), 2);
        p.setDeviceService(nullptr);
        const int after = lanes.count();  // dropping the device updates the lanes once itself
        fake.setConnected(true);
        QCOMPARE(lanes.count(), after);  // no longer followed
    }

    void theFakeRefusesWhatTheRealControllerRefuses() {
        FakeDeviceService fake;
        QObject owner;
        fake.setConnected(true);
        fake.connectDevice("mock");  // already connected
        QCOMPARE(fake.count("refused"), 1);
        fake.setSessionActiveFor(&owner, true);
        fake.disconnectDevice();  // a hold is active
        QCOMPARE(fake.count("refused"), 2);
        QVERIFY(fake.connected());
        fake.setSessionActiveFor(&owner, false);
        fake.disconnectDevice();
        QTRY_VERIFY(!fake.connected());
    }

    void aLostConnectionStopsSending() {
        const QString script = writeScript(files_.path(), "lost.funscript", zigzag(60, 250));
        FakeDeviceService fake;
        fake.setConnected(true);
        PlayerController p;
        p.setDeviceService(&fake);
        QVERIFY(p.openScript(script));
        p.setPlaying(true);
        p.setVideoPositionMs(1000);
        QTRY_VERIFY_WITH_TIMEOUT(fake.count("vibration") > 0, 5000);
        fake.setConnected(false);
        fake.calls.clear();
        p.setVideoPositionMs(1500);
        QTest::qWait(100);
        QCOMPARE(fake.count("vibration"), 0);
        p.setPlaying(false);
    }

    // --- Training through the interface ---
    void trainingDrivesTheFakeAndHoldsTheSession() {
        FakeDeviceService fake;
        fake.setConnected(true);
        TrainingController tc;
        tc.setDeviceService(&fake);
        tc.setCycles(1);
        tc.setRampUpMs(100);
        tc.setHoldMs(100);
        tc.setRestMs(100);
        tc.setPeak(0.8);
        tc.setProgression(0);
        QSignalSpy done(&tc, &TrainingController::sessionFinished);
        QVERIFY(tc.start());
        QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 10000);
        QVERIFY(!fake.sessionActive());
        QCOMPARE(fake.callsOf("hold").first(), QStringLiteral("hold 1"));
        QCOMPARE(fake.callsOf("hold").last(), QStringLiteral("hold 0"));
        QVERIFY(fake.count("vibration") > 0);
        QCOMPARE(fake.count("connect"), 0);     // an existing connection is used
        QCOMPARE(fake.count("disconnect"), 0);  // and stays
    }

    void trainingConnectsAMockAndClosesItAgain() {
        FakeDeviceService fake;
        TrainingController tc;
        tc.setDeviceService(&fake);
        tc.setCycles(1);
        tc.setRampUpMs(100);
        tc.setHoldMs(100);
        tc.setRestMs(100);
        tc.setPeak(0.8);
        tc.setProgression(0);
        QSignalSpy done(&tc, &TrainingController::sessionFinished);
        QVERIFY(tc.start());
        QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 10000);
        QCOMPARE(fake.callsOf("connect").size(), 1);
        QVERIFY(fake.callsOf("connect").first().startsWith("connect mock"));
        // The hold is released before the disconnect: the real controller refuses it otherwise.
        QCOMPARE(fake.count("refused"), 0);
        QVERIFY(fake.count("disconnect") >= 1);  // the connection Training made is closed
        QTRY_VERIFY(!fake.connected());
    }

    void trainingEndsWhenTheDeviceChangesOrIsLost() {
        for (const bool lost : {true, false}) {
            FakeDeviceService fake;
            fake.setConnected(true);
            TrainingController tc;
            tc.setDeviceService(&fake);
            tc.setCycles(20);
            tc.setRampUpMs(100);
            tc.setHoldMs(100);
            tc.setRestMs(100);
            tc.setProgression(0);
            QSignalSpy done(&tc, &TrainingController::sessionFinished);
            QVERIFY(tc.start());
            QTRY_COMPARE_WITH_TIMEOUT(tc.state(), QString("running"), 5000);
            QVERIFY(fake.sessionActive());
            if (lost) fake.setConnected(false);
            else fake.swapDevice();
            QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 5000);
            QVERIFY(!fake.sessionActive());  // the hold is released, nothing drives the new device
            QCOMPARE(tc.state(), QString("idle"));
            QVERIFY(tc.logLines().join("\n").contains(lost ? "connection lost" : "device changed"));
        }
    }

    void trainingEmergencyStopReachesTheFake() {
        FakeDeviceService fake;
        fake.setConnected(true);
        TrainingController tc;
        tc.setDeviceService(&fake);
        tc.setCycles(20);
        tc.setRampUpMs(100);
        tc.setHoldMs(100);
        tc.setRestMs(100);
        tc.setProgression(0);
        QVERIFY(tc.start());
        QTRY_COMPARE_WITH_TIMEOUT(tc.state(), QString("running"), 5000);
        tc.emergencyStop();
        QCOMPARE(fake.count("emergencyStop"), 1);
        QVERIFY(!fake.sessionActive());
    }
};

QTEST_MAIN(DeviceServiceTest)
#include "device_service_test.moc"
