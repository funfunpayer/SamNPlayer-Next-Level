#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

#include "device_controller.h"
#include "device_io/qt_transport.h"
#include "device_io/serial_line_transport.h"
#include "fake_servers.h"
#include "samn/device/protocol.h"
#include "samn/device/tcode.h"

using samn::ui::DeviceController;
using namespace samn_test;

namespace {
QStringList MockLines(const QSignalSpy& log) {
    QStringList out;
    for (const auto& args : log)
        if (args.at(0).toString().startsWith("[mock]")) out << args.at(0).toString();
    return out;
}

using samn::device::Bytes;

// What the stand-in Sam Neo 2 received; shared with the test thread.
struct FakeNeoLink {
    std::mutex mutex;
    std::vector<Bytes> written;  // acknowledged writes, in order
    std::atomic<bool> hang{false};  // from now on no write is acknowledged
    std::vector<Bytes> Written() {
        std::lock_guard<std::mutex> lock(mutex);
        return written;
    }
};

// The Sam Neo 2's Bluetooth link without Bluetooth: a write with response
// waits for its acknowledgement (a few ms) with the controller's cancel, the
// way BleTransport::Send does.
class FakeNeoTransport : public samn::ui::QtTransport {
public:
    explicit FakeNeoTransport(std::shared_ptr<FakeNeoLink> link) : link_(std::move(link)) {}
    samn::device::Status Connect() override {
        open_ = true;
        return samn::device::Status::Ok();
    }
    void Disconnect() override { open_ = false; }
    bool IsConnected() const override { return open_; }
    samn::device::Status Send(const Bytes& data, samn::device::WriteMode) override {
        if (!open_) return Fail("Bluetooth: not connected");
        QElapsedTimer clock;
        clock.start();
        const bool acked = waiter_.Until([&] { return !link_->hang && clock.elapsed() >= 5; }, 60000,
                                         [this] { return Cancelled(); });
        if (!acked) return Fail(Cancelled() ? "Bluetooth: cancelled" : "Bluetooth: GATT write timeout");
        std::lock_guard<std::mutex> lock(link_->mutex);
        link_->written.push_back(data);
        return samn::device::Status::Ok();
    }
    samn::device::ReceiveResult Receive(std::chrono::milliseconds) override {
        return samn::device::ReceiveResult::Timeout();
    }
    samn::device::PeerInfo Peer() const override { return {"Sam Neo 2 (stand-in)", "00:11:22:33:44:55", -40}; }

private:
    std::shared_ptr<FakeNeoLink> link_;
    bool open_ = false;
};

// A "ble" connect of `c` reaches the stand-in; vibration and suction are on.
void ConnectFakeNeo(DeviceController& c, const std::shared_ptr<FakeNeoLink>& link) {
    c.setBleTransportFactory([link] { return std::make_unique<FakeNeoTransport>(link); });
    c.connectSamNeo2();
    QTRY_COMPARE(c.state(), QString("connected"));
    c.setVibration(0.6);
    c.setSuction(0.5);
    QTRY_COMPARE(link->Written().size(), std::size_t(2));
}

// Both off packets arrived after the last "on" packet.
bool EndsSwitchedOff(const std::vector<Bytes>& written) {
    const samn::device::SamNeo2Protocol proto;
    bool vibration_off = false, suction_off = false;
    for (std::size_t i = 2; i < written.size(); ++i) {
        if (written[i] == proto.EncodeVibration(0)) vibration_off = true;
        else if (written[i] == proto.EncodeSuction(0)) suction_off = true;
        else return false;  // anything but "off" after the stop
    }
    return vibration_off && suction_off;
}
}  // namespace

// DeviceController end to end: the worker thread, the real Qt transports and
// local fake peers. The GUI thread (this one) never blocks on a device.
class DeviceControllerTest : public QObject {
    Q_OBJECT
    QTemporaryDir config_;

private slots:
    void initTestCase() {
        QVERIFY(config_.isValid());
        qputenv("SAMN_CONFIG_DIR", config_.path().toUtf8());
    }

    // --- Mock, through the controller ---
    void mockConnectDriveDisconnect() {
        DeviceController c;
        QSignalSpy status(&c, &DeviceController::statusChanged);
        QSignalSpy log(&c, &DeviceController::logMessage);
        QCOMPARE(c.state(), QString("disconnected"));
        c.connectMock();
        QCOMPARE(c.state(), QString("connecting"));  // visible at once
        QTRY_COMPARE(c.state(), QString("connected"));
        QCOMPARE(c.kind(), QString("mock"));
        QVERIFY(c.isMock());
        QVERIFY(c.name().contains("Mock"));
        QVERIFY(c.capVibration());
        QVERIFY(c.capSuction());
        QVERIFY(c.capRaw());
        QVERIFY(!c.capLinear());
        QVERIFY(c.channels().contains("vibration"));
        QVERIFY(c.channels().contains("suction"));
        QCOMPARE(c.capabilities().size(), 2);
        QCOMPARE(c.capabilities().at(0).toMap().value("channel").toString(), QString("vibration"));
        QVERIFY(c.lastError().isEmpty());
        QVERIFY(status.count() >= 1);

        c.setVibration(0.5);
        QTRY_VERIFY(MockLines(log).contains("[mock] vibration=0.50"));
        c.setSuction(2.0);  // clamped
        QTRY_VERIFY(MockLines(log).contains("[mock] suction=1.00"));
        c.testRawValue("vibration", 200);
        QTRY_VERIFY(MockLines(log).contains("[mock] vibration_raw=200"));
        c.stop();
        QTRY_VERIFY(MockLines(log).contains("[mock] stop"));

        c.testRawValue("vibration", 300);
        QTRY_COMPARE(c.lastError(), QString("raw value must be between 0 and 255 (got 300)"));
        c.testRawValue("bogus", 5);
        QTRY_COMPARE(c.lastError(), QString("unknown channel \"bogus\""));

        c.disconnectDevice();
        QTRY_COMPARE(c.state(), QString("disconnected"));
        QVERIFY(c.channels().isEmpty());
        QVERIFY(!c.capVibration());
        // Stop before closing (0.7): the last mock line before "getrennt" is a stop.
        const QStringList lines = MockLines(log);
        QVERIFY(lines.size() >= 2);
        QCOMPARE(lines.last(), QString("[mock] getrennt"));
        QCOMPARE(lines.at(lines.size() - 2), QString("[mock] stop"));
    }

    void secondConnectIsRefused() {
        DeviceController c;
        c.connectMock();
        QTRY_COMPARE(c.state(), QString("connected"));
        c.connectMock();
        QTRY_VERIFY(c.lastError().contains("already connected"));
        QCOMPARE(c.state(), QString("connected"));
    }

    void commandsWithoutDeviceSayWhy() {
        DeviceController c;
        QSignalSpy errors(&c, &DeviceController::errorChanged);
        c.setVibration(0.3);
        QTRY_VERIFY(c.lastError().contains("no device connected"));
        QCOMPARE(c.state(), QString("disconnected"));
        // emergencyStop with nothing connected is silent.
        const int before = errors.count();
        c.emergencyStop();
        QTest::qWait(100);
        QCOMPARE(errors.count(), before);
        c.stop();
        QTest::qWait(100);
        QVERIFY(c.lastError().contains("no device connected"));
    }

    void sessionActiveBlocksConnectAndTests() {
        DeviceController c;
        c.setSessionActive(true);
        c.connectMock();
        QVERIFY(c.lastError().contains("playback or training is in progress"));
        QCOMPARE(c.state(), QString("disconnected"));
        c.setSessionActive(false);
        c.connectMock();
        QTRY_COMPARE(c.state(), QString("connected"));
        c.setSessionActive(true);
        c.disconnectDevice();
        QVERIFY(c.lastError().contains("is using this connection"));
        c.testRawValue("vibration", 1);
        QVERIFY(c.lastError().contains("not available during playback"));
        QCOMPARE(c.state(), QString("connected"));
        // The player drives levels through the same controller.
        QSignalSpy log(&c, &DeviceController::logMessage);
        c.setVibration(0.25);
        QTRY_VERIFY(MockLines(log).contains("[mock] vibration=0.25"));
        c.setSessionActive(false);
        c.disconnectDevice();
        QTRY_COMPARE(c.state(), QString("disconnected"));
    }

    // Playback and Training hold the session each on their own: one of them
    // ending leaves connect, disconnect and the device tests refused.
    void sessionHoldsArePerOwner() {
        DeviceController c;
        QObject playback, training;
        QSignalSpy changed(&c, &DeviceController::sessionActiveChanged);
        c.setSessionActiveFor(&playback, true);
        c.setSessionActiveFor(&training, true);
        QCOMPARE(changed.count(), 1);
        c.setSessionActiveFor(&training, false);
        QVERIFY(c.sessionActive());
        QCOMPARE(changed.count(), 1);
        c.connectMock();
        QVERIFY(c.lastError().contains("playback or training is in progress"));
        c.runDiagnostics();
        QVERIFY(c.lastError().contains("not available during playback"));
        QVERIFY(!c.diagnosticsRunning());
        c.setSessionActiveFor(&training, false);  // a second release changes nothing
        QVERIFY(c.sessionActive());
        c.setSessionActiveFor(&playback, false);
        QVERIFY(!c.sessionActive());
        QCOMPARE(changed.count(), 2);
        c.connectMock();
        QTRY_COMPARE(c.state(), QString("connected"));
    }

    void levelsAreCoalescedNewestWins() {
        DeviceController c;
        QSignalSpy log(&c, &DeviceController::logMessage);
        c.connectMock();
        QTRY_COMPARE(c.state(), QString("connected"));
        for (int i = 0; i < 500; ++i) c.setVibration(i / 1000.0);
        c.setVibration(0.77);
        QTRY_VERIFY(MockLines(log).contains("[mock] vibration=0.77"));
        QCOMPARE(MockLines(log).last(), QString("[mock] vibration=0.77"));
        QVERIFY(MockLines(log).count() < 400);  // dropped values never went out
    }

    void emergencyStopDropsQueuedLevels() {
        DeviceController c;
        QSignalSpy log(&c, &DeviceController::logMessage);
        c.connectMock();
        QTRY_COMPARE(c.state(), QString("connected"));
        c.setVibration(0.9);
        c.emergencyStop();
        c.setVibration(0.0);
        QTRY_VERIFY(MockLines(log).contains("[mock] stop"));
        QTest::qWait(50);
        // The worker may send 0.9 before the stop arrives (it wakes on the
        // queue); what the stop must prevent is a queued level going out
        // after it.
        const QStringList lines = MockLines(log);
        QVERIFY(!lines.mid(lines.indexOf("[mock] stop")).contains("[mock] vibration=0.90"));
    }

    void motorLevelsAreAccepted() {
        DeviceController c;
        c.connectMock();
        QTRY_COMPARE(c.state(), QString("connected"));
        c.setMotorLevels({{"vibration", 1.0}, {"vibration2", 0.5}, {"bad", -3}});
        c.refreshStatus();
        QTest::qWait(50);
        QCOMPARE(c.state(), QString("connected"));
    }

    void savedChoiceSurvives() {
        {
            DeviceController c;
            c.connectMock();
            QTRY_COMPARE(c.state(), QString("connected"));
            QTRY_COMPARE(c.savedKind(), QString("mock"));
        }
        FakeButtplugServer server(FakeButtplug::StandardDeviceMessages());
        DeviceController c;
        c.connectIntiface(server.url());
        QTRY_COMPARE_WITH_TIMEOUT(c.state(), QString("connected"), 5000);
        QCOMPARE(c.savedKind(), QString("intiface"));
        QCOMPARE(c.savedTarget("intiface"), server.url());
        QCOMPARE(c.savedTarget("nothing"), QString());
    }

    // --- TCode ---
    void tcodeOverTcp() {
        TcpLineSink sink;
        DeviceController c;
        c.connectTCode("tcp://" + sink.hostport());
        QTRY_COMPARE_WITH_TIMEOUT(c.state(), QString("connected"), 5000);
        QCOMPARE(c.kind(), QString("tcode"));
        QVERIFY(c.capLinear());
        QVERIFY(c.axes().contains("stroke"));
        QVERIFY(c.axes().contains("surge"));
        QVERIFY(!c.capVibration());
        c.setPosition(0.5, 250);
        QTRY_VERIFY(sink.lines().contains(QString::fromStdString(
            samn::device::TCodeMove("L0", 0.5, std::chrono::milliseconds(250)))));
        c.setAxes({{"surge", 1.0}}, 100);
        QTRY_VERIFY(sink.lines().contains(QString::fromStdString(
            samn::device::TCodeMove("L1", 1.0, std::chrono::milliseconds(100)))));
        c.setAxis("nonsense", 0.5, 100);
        QTRY_VERIFY(c.lastError().contains("no axis"));
        c.stop();
        QTRY_VERIFY(sink.lines().contains("DSTOP"));
        c.disconnectDevice();
        QTRY_COMPARE(c.state(), QString("disconnected"));
        QCOMPARE(c.savedTarget("tcode"), "tcp://" + sink.hostport());
    }

    // 0.7 cmd/gui-wails/app_test.go TestShutdownStopsActiveSessionDevice:
    // closing the app while a device runs stops it (the device gets DSTOP).
    void shutdownStopsTheConnectedDevice() {
        TcpLineSink sink;
        {
            DeviceController c;
            c.connectTCode("tcp://" + sink.hostport());
            QTRY_COMPARE_WITH_TIMEOUT(c.state(), QString("connected"), 5000);
            c.setPosition(0.8, 200);
            QTRY_VERIFY(!sink.lines().isEmpty());
            QVERIFY(!sink.lines().contains("DSTOP"));
        }  // the app closes: the controller goes away while connected
        QTRY_VERIFY_WITH_TIMEOUT(sink.lines().contains("DSTOP"), 5000);
    }

    // TestShutdownWithoutActiveSessionIsSafe: a controller that never
    // connected, or already disconnected, goes away without trouble.
    void shutdownWithoutSessionIsSafe() {
        { DeviceController never; }
        TcpLineSink sink;
        DeviceController c;
        c.connectTCode("tcp://" + sink.hostport());
        QTRY_COMPARE_WITH_TIMEOUT(c.state(), QString("connected"), 5000);
        c.disconnectDevice();
        QTRY_COMPARE(c.state(), QString("disconnected"));
    }

    void tcodeOverUdpDefaultScheme() {
        UdpLineSink sink;
        DeviceController c;
        c.connectTCode(sink.hostport());  // no scheme: UDP
        QTRY_COMPARE_WITH_TIMEOUT(c.state(), QString("connected"), 5000);
        c.setPosition(1.0, 100);
        QTRY_VERIFY(!sink.datagrams().isEmpty());
        QCOMPARE(sink.datagrams().first(),
                 QByteArray::fromStdString(samn::device::TCodeMove("L0", 1.0, std::chrono::milliseconds(100)) + "\n"));
    }

    void tcodeRefusedGivesHint() {
        DeviceController c;
        c.connectTCode(QString("tcp://127.0.0.1:%1").arg(ClosedPort()));
        QTRY_VERIFY_WITH_TIMEOUT(!c.lastError().isEmpty(), 5000);
        QVERIFY2(c.lastError().contains("refused"), qPrintable(c.lastError()));
        QVERIFY(c.hint().contains("nothing is listening"));
        QCOMPARE(c.state(), QString("disconnected"));
    }

    void tcodeBadAddress() {
        DeviceController c;
        c.connectTCode("");
        QTRY_VERIFY(c.lastError().contains("no address"));
        QCOMPARE(c.state(), QString("disconnected"));
        c.connectTCode("ftp://host");
        QTRY_VERIFY(c.lastError().contains("udp:// or tcp://"));
    }

    void tcodeSerialMissingPort() {
        DeviceController c;
#ifdef Q_OS_WIN
        c.connectTCodeSerial("COM250");
#else
        c.connectTCodeSerial("/dev/ttyDoesNotExist-samn");
#endif
        QTRY_VERIFY_WITH_TIMEOUT(!c.lastError().isEmpty(), 5000);
        QVERIFY2(c.lastError().contains("cannot open"), qPrintable(c.lastError()));
        QCOMPARE(c.state(), QString("disconnected"));
        c.connectTCodeSerial("");
        QTRY_VERIFY(c.lastError().contains("no serial port"));
    }

    void serialPortListArrives() {
        DeviceController c;
        QSignalSpy spy(&c, &DeviceController::serialPortsChanged);
        c.refreshSerialPorts();
        QTRY_COMPARE(spy.count(), 1);
        QStringList expected;
        for (const auto& p : samn::ui::SerialPorts()) expected << QString::fromStdString(p);
        QCOMPARE(c.serialPorts(), expected);
    }

    // --- Intiface ---
    void intifaceDriveAndBattery() {
        FakeButtplugServer server(FakeButtplug::MessagesWithBattery());
        DeviceController c;
        c.setTickIntervalMs(50);
        c.connectIntiface(server.url());
        QTRY_COMPARE_WITH_TIMEOUT(c.state(), QString("connected"), 5000);
        QCOMPARE(c.kind(), QString("intiface"));
        QVERIFY(c.name().contains("Testger"));
        QVERIFY(c.channels().contains("vibration"));
        QVERIFY(c.channels().contains("suction"));
        QVERIFY(!c.capRaw());
        QTRY_COMPARE(c.batteryPercent(), 73);
        QVERIFY(c.batteryOk());
        QVERIFY(c.capBattery());

        c.setVibration(0.5);
        QTRY_VERIFY(server.Count("ScalarCmd") >= 1);
        c.setSuction(0.4);
        QTRY_VERIFY(server.Count("ScalarCmd") >= 2);
        // The ping timer runs (every tick interval; 50 ms here).
        QTRY_VERIFY(server.Count("Ping") >= 3);
        c.stop();
        QTRY_VERIFY(server.Count("StopDeviceCmd") >= 1);
        c.disconnectDevice();
        QTRY_COMPARE(c.state(), QString("disconnected"));
    }

    void intifaceStrokerLinear() {
        FakeButtplugServer server(FakeButtplugServer::StrokerMessages());
        DeviceController c;
        c.connectIntiface(server.url());
        QTRY_COMPARE_WITH_TIMEOUT(c.state(), QString("connected"), 5000);
        QVERIFY(c.capLinear());
        c.setPosition(0.75, 400);
        QTRY_VERIFY(server.Count("LinearCmd") >= 1);
        const auto v = server.Last("LinearCmd").at("Vectors").at(0);
        QCOMPARE(v.at("Duration").get<int>(), 400);
        QVERIFY(std::abs(v.at("Position").get<double>() - 0.75) < 0.02);
    }

    void intifaceLostLinkIsReported() {
        auto server = std::make_unique<FakeButtplugServer>(FakeButtplug::StandardDeviceMessages());
        DeviceController c;
        c.setTickIntervalMs(50);
        c.setDialTimeoutMs(500);
        c.connectIntiface(server->url());
        QTRY_COMPARE_WITH_TIMEOUT(c.state(), QString("connected"), 5000);
        server->Shutdown();  // the server goes away for good
        QTRY_COMPARE_WITH_TIMEOUT(c.state(), QString("disconnected"), 8000);
        QVERIFY2(c.lastError().contains("connection lost"), qPrintable(c.lastError()));
        QVERIFY(!c.hint().isEmpty());
        QVERIFY(c.channels().isEmpty());
    }

    void intifaceRefusedGivesHint() {
        DeviceController c;
        c.connectIntiface(QString("ws://127.0.0.1:%1").arg(ClosedPort()));
        QTRY_VERIFY_WITH_TIMEOUT(!c.lastError().isEmpty(), 5000);
        QVERIFY2(c.lastError().contains("refused"), qPrintable(c.lastError()));
        QVERIFY2(c.hint().contains("nothing is listening"), qPrintable(c.hint()));
        QVERIFY(c.hint().contains("Intiface Central"));
        QCOMPARE(c.state(), QString("disconnected"));
    }

    void intifaceUnknownHostGivesHint() {
        DeviceController c;
        c.connectIntiface("ws://no-such-host.invalid:12345");
        QTRY_VERIFY_WITH_TIMEOUT(!c.lastError().isEmpty(), 8000);
        QVERIFY2(c.lastError().contains("no such host"), qPrintable(c.lastError()));
        QVERIFY2(c.hint().contains("hostname could not be resolved"), qPrintable(c.hint()));
    }

    void intifaceTimeoutGivesHint() {
        SilentServer silent;
        DeviceController c;
        c.setDialTimeoutMs(300);
        c.connectIntiface(QString("ws://127.0.0.1:%1").arg(silent.port()));
        QTRY_VERIFY_WITH_TIMEOUT(!c.lastError().isEmpty(), 8000);
        QVERIFY2(c.lastError().contains("timeout"), qPrintable(c.lastError()));
        QVERIFY2(c.hint().contains("no response"), qPrintable(c.hint()));
        QCOMPARE(c.state(), QString("disconnected"));
    }

    void disconnectAbortsAConnectInProgress() {
        SilentServer silent;
        DeviceController c;
        c.setDialTimeoutMs(30000);
        c.connectIntiface(QString("ws://127.0.0.1:%1").arg(silent.port()));
        QTRY_COMPARE(c.state(), QString("connecting"));
        QTest::qWait(100);
        QElapsedTimer clock;
        clock.start();
        c.disconnectDevice();
        QTRY_COMPARE_WITH_TIMEOUT(c.state(), QString("disconnected"), 3000);
        QVERIFY(clock.elapsed() < 3000);
    }

    void destroyingWhileConnectingReturnsPromptly() {
        SilentServer silent;
        QElapsedTimer clock;
        {
            DeviceController c;
            c.setDialTimeoutMs(30000);
            c.connectIntiface(QString("ws://127.0.0.1:%1").arg(silent.port()));
            QTRY_COMPARE(c.state(), QString("connecting"));
            QTest::qWait(100);
            clock.start();
        }
        QVERIFY2(clock.elapsed() < 3000, QByteArray::number(clock.elapsed()).constData());
    }

    // --- Bluetooth ---
    void bleConnectWithoutDeviceFailsAndDoesNotHang() {
        DeviceController c;
        c.setBleScanTimeoutMs(400);
        QElapsedTimer clock;
        clock.start();
        c.connectSamNeo2();
        QTRY_VERIFY_WITH_TIMEOUT(!c.lastError().isEmpty(), 10000);
        QVERIFY2(c.lastError().contains("Bluetooth"), qPrintable(c.lastError()));
        QCOMPARE(c.state(), QString("disconnected"));
        QVERIFY(clock.elapsed() < 10000);
        // And the controller still works afterwards.
        c.connectMock();
        QTRY_COMPARE(c.state(), QString("connected"));
    }

    // A disconnect sets the abort flag before the worker's stop runs: the
    // stop must still get both off packets through (with response, so they
    // wait for the device), or the Sam Neo 2 keeps running.
    void samNeo2DisconnectSwitchesBothChannelsOff() {
        auto link = std::make_shared<FakeNeoLink>();
        DeviceController c;
        ConnectFakeNeo(c, link);
        c.disconnectDevice();
        QTRY_COMPARE(c.state(), QString("disconnected"));
        const std::vector<Bytes> written = link->Written();
        QVERIFY2(EndsSwitchedOff(written), qPrintable(QString("%1 writes").arg(written.size())));
        QVERIFY2(c.lastError().isEmpty(), qPrintable(c.lastError()));
    }

    // Quitting the app while the toy runs switches it off as well.
    void samNeo2QuitSwitchesBothChannelsOff() {
        auto link = std::make_shared<FakeNeoLink>();
        {
            DeviceController c;
            ConnectFakeNeo(c, link);
        }
        const std::vector<Bytes> written = link->Written();
        QVERIFY2(EndsSwitchedOff(written), qPrintable(QString("%1 writes").arg(written.size())));
    }

    // Training's end: stop, then at once disconnect. The stop is not cut
    // short and reports no error.
    void samNeo2StopThenDisconnectIsClean() {
        auto link = std::make_shared<FakeNeoLink>();
        DeviceController c;
        ConnectFakeNeo(c, link);
        c.stop();
        c.disconnectDevice();
        QTRY_COMPARE(c.state(), QString("disconnected"));
        QVERIFY(EndsSwitchedOff(link->Written()));
        QVERIFY2(c.lastError().isEmpty(), qPrintable(c.lastError()));
    }

    // A link that stops acknowledging writes cannot hold up the quit forever.
    void samNeo2HangingLinkDoesNotHoldUpTheQuit() {
        auto link = std::make_shared<FakeNeoLink>();
        QElapsedTimer clock;
        {
            DeviceController c;
            ConnectFakeNeo(c, link);
            link->hang = true;
            clock.start();
        }
        QVERIFY2(clock.elapsed() < 8000, QByteArray::number(clock.elapsed()).constData());
    }

    // A server that never answers the battery read costs the wait once, not
    // at every tick: the emergency stop reaches the device at once.
    void intifaceMuteBatteryDoesNotBlockTheStop() {
        FakeButtplugServer server(FakeButtplug::MessagesWithBattery());
        server.battery = FakeButtplugServer::Battery::kMute;
        DeviceController c;
        c.setTickIntervalMs(50);
        c.connectIntiface(server.url());
        QTRY_COMPARE_WITH_TIMEOUT(c.state(), QString("connected"), 10000);  // after the first, unanswered read
        QVERIFY(!c.batteryOk());
        QTest::qWait(300);  // several ticks
        QElapsedTimer clock;
        clock.start();
        c.emergencyStop();
        QTRY_VERIFY_WITH_TIMEOUT(server.Count("StopDeviceCmd") >= 1, 5000);
        // Under the 3 s battery wait it used to queue behind (slow CI runners too).
        QVERIFY2(clock.elapsed() < 2500, QByteArray::number(clock.elapsed()).constData());
        QCOMPARE(server.Count("BatteryLevelCmd"), 1);
    }

    // A server that refuses the battery read: no wait at all.
    void intifaceBatteryErrorEndsTheWait() {
        FakeButtplugServer server(FakeButtplug::MessagesWithBattery());
        server.battery = FakeButtplugServer::Battery::kError;
        DeviceController c;
        c.setTickIntervalMs(50);
        QElapsedTimer clock;
        clock.start();
        c.connectIntiface(server.url());
        QTRY_COMPARE_WITH_TIMEOUT(c.state(), QString("connected"), 10000);
        QVERIFY2(clock.elapsed() < 2500, QByteArray::number(clock.elapsed()).constData());
        QVERIFY(!c.batteryOk());
        QTest::qWait(300);
        QCOMPARE(server.Count("BatteryLevelCmd"), 1);
    }

    // --- Diagnostics ---
    void diagnosticsOnMock() {
        DeviceController c;
        samn::device::DiagnosticsOptions o;
        o.update_rate_intervals = {std::chrono::milliseconds(1), std::chrono::milliseconds(1)};
        o.commands_per_rate_step = 2;
        o.offset_change_delay = std::chrono::milliseconds(1);
        o.rapid_switch_delay = std::chrono::milliseconds(1);
        c.setDiagnosticsOptions(o);
        QSignalSpy entries(&c, &DeviceController::diagnosticsEntry);
        QSignalSpy done(&c, &DeviceController::diagnosticsFinished);
        QSignalSpy running(&c, &DeviceController::diagnosticsRunningChanged);

        c.runDiagnostics();  // nothing connected
        QTRY_VERIFY(c.lastError().contains("no device connected"));
        QTRY_VERIFY(!c.diagnosticsRunning());

        c.connectMock();
        QTRY_COMPARE(c.state(), QString("connected"));
        c.runDiagnostics();
        QVERIFY(c.diagnosticsRunning());
        c.runDiagnostics();  // second at once is refused
        QVERIFY(c.lastError().contains("already in progress"));
        QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 15000);
        QTRY_VERIFY(!c.diagnosticsRunning());
        const QJsonObject report = QJsonDocument::fromJson(done.first().at(0).toString().toUtf8()).object();
        QVERIFY(report.value("rawCapable").toBool());
        QVERIFY(!report.value("interrupted").toBool());
        const QJsonArray log = report.value("log").toArray();
        QVERIFY(log.size() > 5);
        QCOMPARE(entries.count(), static_cast<int>(log.size()));
        QVERIFY(!report.value("notes").toArray().isEmpty());
        QVERIFY(running.count() >= 2);
    }

    void diagnosticsCanBeCancelled() {
        DeviceController c;
        samn::device::DiagnosticsOptions o;
        o.update_rate_intervals = {std::chrono::milliseconds(5000)};
        o.commands_per_rate_step = 50;
        o.offset_change_delay = std::chrono::milliseconds(5000);
        o.rapid_switch_delay = std::chrono::milliseconds(5000);
        c.setDiagnosticsOptions(o);
        QSignalSpy entries(&c, &DeviceController::diagnosticsEntry);
        QSignalSpy done(&c, &DeviceController::diagnosticsFinished);
        c.connectMock();
        QTRY_COMPARE(c.state(), QString("connected"));
        c.runDiagnostics();
        QTRY_VERIFY(entries.count() >= 1);
        QElapsedTimer clock;
        clock.start();
        c.emergencyStop();  // also cancels the run
        QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 5000);
        QVERIFY(clock.elapsed() < 4000);
        const QJsonObject report = QJsonDocument::fromJson(done.first().at(0).toString().toUtf8()).object();
        QVERIFY(report.value("interrupted").toBool());
        QTRY_VERIFY(!c.diagnosticsRunning());
    }
};

QTEST_MAIN(DeviceControllerTest)
#include "device_controller_test.moc"
