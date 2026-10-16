#include <QElapsedTimer>
#include <QtTest>
#include <algorithm>

#include "device_io/ble_transport.h"
#include "device_io/serial_line_transport.h"
#include "device_io/socket_line_transport.h"
#include "device_io/websocket_transport.h"
#include "fake_servers.h"
#include "samn/device/intiface.h"
#include "samn/device/protocol.h"
#include "samn/device/samneo2.h"
#include "samn/device/tcode.h"

using namespace samn::device;
using samn::ui::BleTransport;
using samn::ui::SerialLineTransport;
using samn::ui::SocketLineTransport;
using samn::ui::WebSocketTransport;
using namespace samn_test;

namespace {
bool Has(const std::string& text, const char* word) {
    return QString::fromStdString(text).toLower().contains(word);
}
}  // namespace

// The Qt transports against local peers: no hardware, no internet.
class DeviceTransportsTest : public QObject {
    Q_OBJECT
private slots:
    // --- TCode over TCP / UDP ---
    void tcpWritesLines() {
        TcpLineSink sink;
        QVERIFY(sink.listening());
        SocketLineTransport t("tcp", sink.hostport().toStdString());
        QVERIFY(t.Connect());
        QVERIFY(t.IsConnected());
        QVERIFY(t.Send(ToBytes("L05000I250\n"), WriteMode::kWithoutResponse));
        QVERIFY(t.Send(ToBytes("DSTOP\n"), WriteMode::kWithoutResponse));
        QTRY_COMPARE(sink.data(), QByteArray("L05000I250\nDSTOP\n"));
        QCOMPARE(sink.connections(), 1);
        QCOMPARE(QString::fromStdString(t.Peer().name), QString("tcp://") + sink.hostport());
        t.Disconnect();
        QVERIFY(!t.IsConnected());
        QVERIFY(!t.Send(ToBytes("x\n"), WriteMode::kWithResponse));
    }

    void tcpDeviceSendsTCode() {
        TcpLineSink sink;
        TCodeAddress addr;
        const std::string address = "tcp://" + sink.hostport().toStdString();
        QVERIFY(ParseTCodeAddress(address, &addr));
        SocketLineTransport t(addr.network, addr.hostport);
        TCodeDevice device = TCodeDevice::OverNetwork(t, address);
        QVERIFY(device.Connect());
        QVERIFY(device.SetPosition(0.5, std::chrono::milliseconds(250)));
        QVERIFY(device.SetAxis("surge", 1.0, std::chrono::milliseconds(100)));
        QVERIFY(device.Stop());
        QVERIFY(device.Disconnect());
        QTRY_VERIFY(sink.lines().size() >= 3);
        QCOMPARE(sink.lines().at(0), QString::fromStdString(TCodeMove("L0", 0.5, std::chrono::milliseconds(250))));
        QCOMPARE(sink.lines().at(1), QString::fromStdString(TCodeMove("L1", 1.0, std::chrono::milliseconds(100))));
        QCOMPARE(sink.lines().at(2), QString("DSTOP"));
    }

    void tcpReceiveReturnsPeerData() {
        TcpLineSink sink;
        SocketLineTransport t("tcp", sink.hostport().toStdString());
        QVERIFY(t.Connect());
        // Nothing sent: a poll times out.
        QCOMPARE(t.Receive(std::chrono::milliseconds(0)).kind, ReceiveResult::Kind::kTimeout);
        QCOMPARE(t.Receive(std::chrono::milliseconds(50)).kind, ReceiveResult::Kind::kTimeout);
    }

    void udpWritesDatagrams() {
        UdpLineSink sink;
        QVERIFY(sink.listening());
        SocketLineTransport t("udp", sink.hostport().toStdString());
        QVERIFY(t.Connect());
        QVERIFY(t.Send(ToBytes("L09999I100\n"), WriteMode::kWithoutResponse));
        QVERIFY(t.Send(ToBytes("DSTOP\n"), WriteMode::kWithoutResponse));
        QTRY_COMPARE(sink.datagrams().size(), 2);
        QCOMPARE(sink.datagrams().at(0), QByteArray("L09999I100\n"));
        QCOMPARE(sink.datagrams().at(1), QByteArray("DSTOP\n"));
    }

    void socketErrorsNameTheCause() {
        {
            SocketLineTransport t("tcp", QString("127.0.0.1:%1").arg(ClosedPort()).toStdString());
            const Status s = t.Connect();
            QVERIFY(!s);
            QVERIFY2(Has(s.message, "refused"), s.message.c_str());
        }
        {
            SocketLineTransport t("tcp", "no-such-host.invalid:8000");
            const Status s = t.Connect();
            QVERIFY(!s);
            QVERIFY2(Has(s.message, "no such host"), s.message.c_str());
        }
        {
            SocketLineTransport t("tcp", "not-a-hostport");
            QVERIFY(!t.Connect());
        }
    }

    // --- Intiface WebSocket ---
    void webSocketRoundTrip() {
        FakeButtplugServer server(FakeButtplugServer::json{});
        QVERIFY(server.listening());
        WebSocketTransport t(server.url().toStdString(), 2000);
        QVERIFY(t.Connect());
        QVERIFY(t.IsConnected());
        QVERIFY(t.Send(ToBytes(BuildButtplugMessage("RequestServerInfo", {{"ClientName", "t"}, {"MessageVersion", 3}}, 1)),
                       WriteMode::kWithResponse));
        const ReceiveResult r = t.Receive(std::chrono::milliseconds(2000));
        QCOMPARE(r.kind, ReceiveResult::Kind::kMessage);
        QVERIFY(ToText(r.data).find("ServerInfo") != std::string::npos);
        // Nothing more is coming.
        QCOMPARE(t.Receive(std::chrono::milliseconds(30)).kind, ReceiveResult::Kind::kTimeout);
        t.Disconnect();
        QVERIFY(!t.IsConnected());
    }

    void webSocketReportsClosedPeer() {
        FakeButtplugServer server(FakeButtplugServer::json{});
        WebSocketTransport t(server.url().toStdString(), 2000);
        QVERIFY(t.Connect());
        server.Shutdown();
        const ReceiveResult r = t.Receive(std::chrono::milliseconds(2000));
        QCOMPARE(r.kind, ReceiveResult::Kind::kClosed);
        QVERIFY(!t.IsConnected());
        QVERIFY(!t.Send(ToBytes("[]"), WriteMode::kWithResponse));
    }

    void webSocketErrorsGiveTheRightHint() {
        {  // refused
            const std::string url = QString("ws://127.0.0.1:%1").arg(ClosedPort()).toStdString();
            // Windows retries a refused connect for about 2 s before it reports it;
            // the error ends the wait early, so a generous limit costs nothing.
            WebSocketTransport t(url, 10000);
            const Status s = t.Connect();
            QVERIFY(!s);
            QVERIFY2(Has(s.message, "refused"), s.message.c_str());
            QVERIFY(Has(IntifaceHint(url, s.message), "nothing is listening"));
        }
        {  // unknown host
            const std::string url = "ws://no-such-host.invalid:12345";
            WebSocketTransport t(url, 10000);  // DNS can be slow on CI runners
            const Status s = t.Connect();
            QVERIFY(!s);
            QVERIFY2(Has(s.message, "no such host"), s.message.c_str());
            QVERIFY(Has(IntifaceHint(url, s.message), "hostname could not be resolved"));
        }
        {  // a peer that accepts and never answers: the dial timeout
            SilentServer silent;
            QVERIFY(silent.listening());
            const std::string url = QString("ws://127.0.0.1:%1").arg(silent.port()).toStdString();
            WebSocketTransport t(url, 300);
            QElapsedTimer clock;
            clock.start();
            const Status s = t.Connect();
            QVERIFY(!s);
            QVERIFY2(Has(s.message, "timeout"), s.message.c_str());
            QVERIFY(clock.elapsed() < 3000);
            QVERIFY(Has(IntifaceHint(url, s.message), "intiface central"));
        }
    }

    void intifaceDeviceOverWebSocket() {
        FakeButtplugServer server(FakeButtplug::MessagesWithBattery());
        const std::string url = server.url().toStdString();
        WebSocketTransport t(url, 2000);
        IntifaceDevice device(t, url);
        QVERIFY(device.Connect());
        QVERIFY(device.SetVibration(0.5));
        QVERIFY(device.SetSuction(0.4));
        const BatteryReading battery = device.BatteryLevel();
        QVERIFY(battery.ok);
        QCOMPARE(battery.percent, 73);
        QVERIFY(device.Ping());
        QVERIFY(device.PumpIncoming());
        QVERIFY(device.Stop());
        QVERIFY(device.Disconnect());
        QTRY_VERIFY(server.Count("StopDeviceCmd") >= 1);
        const auto scalar = server.Last("ScalarCmd");
        QVERIFY(!scalar.is_null());
        QCOMPARE(scalar.at("DeviceIndex").get<int>(), 7);
        QVERIFY(server.Count("ScalarCmd") >= 2);
        QVERIFY(server.Count("Ping") >= 1);
    }

    void intifaceStrokerGetsLinearCmd() {
        FakeButtplugServer server(FakeButtplugServer::StrokerMessages());
        const std::string url = server.url().toStdString();
        WebSocketTransport t(url, 2000);
        IntifaceDevice device(t, url);
        QVERIFY(device.Connect());
        QVERIFY(device.HasLinear());
        QVERIFY(device.SetPosition(0.25, std::chrono::milliseconds(300)));
        QTRY_VERIFY(server.Count("LinearCmd") >= 1);
        const auto cmd = server.Last("LinearCmd");
        QCOMPARE(cmd.at("DeviceIndex").get<int>(), 7);
        QCOMPARE(cmd.at("Vectors").at(0).at("Duration").get<int>(), 300);
    }

    // --- serial ---
    void serialPortListIsSorted() {
        const std::vector<std::string> ports = samn::ui::SerialPorts();
        QVERIFY(std::is_sorted(ports.begin(), ports.end()));
        for (const auto& p : ports) QVERIFY(!p.empty());
    }

    void serialOpenOfMissingPortFailsCleanly() {
#ifdef Q_OS_WIN
        const std::string name = "COM250";
#else
        const std::string name = "/dev/ttyDoesNotExist-samn";
#endif
        SerialLineTransport t(name);
        QCOMPARE(t.Connect().ok, false);
        QVERIFY(Has(t.Connect().message, "serial port"));
        QVERIFY(!t.IsConnected());
        QVERIFY(!t.Send(ToBytes("DSTOP\n"), WriteMode::kWithoutResponse));
        // Through the device the user sees the cable / other-program advice.
        TCodeDevice device = TCodeDevice::OverSerial(t, name);
        const Status s = device.Connect();
        QVERIFY(!s);
        QVERIFY(Has(s.message, "cannot open"));
        QVERIFY(Has(s.message, name.c_str()) || Has(s.message, "serial"));
        QVERIFY(!SerialLineTransport("").Connect());
        QVERIFY(Has(SerialLineTransport("").Connect().message, "no serial port"));
    }

    // --- Bluetooth LE ---
    void bleScanWithoutAdapterFailsCleanly() {
        SamNeo2Protocol protocol;
        BleTransport::Options o;
        o.name_prefix = kSamNeo2NamePrefix;
        o.service_uuid = protocol.ServiceUuid();
        o.characteristic_uuid = protocol.CharacteristicUuid();
        o.scan_timeout_ms = 500;
        o.connect_timeout_ms = 500;
        BleTransport t(o);
        QElapsedTimer clock;
        clock.start();
        const Status s = t.Connect();
        // No Sam Neo 2 in reach (and usually no adapter at all): a clear
        // message, and well within scan timeout plus slack. No hang.
        QVERIFY(!s);
        QVERIFY2(Has(s.message, "bluetooth"), s.message.c_str());
        QVERIFY2(clock.elapsed() < 6000, QByteArray::number(clock.elapsed()).constData());
        QVERIFY(!t.IsConnected());
        QVERIFY(!t.Send(ToBytes("x"), WriteMode::kWithResponse));
        QVERIFY(!t.ReadCharacteristic(kBatteryServiceUuid, kBatteryLevelUuid));
        QCOMPARE(t.Receive(std::chrono::milliseconds(0)).kind, ReceiveResult::Kind::kClosed);
        t.Disconnect();  // safe when never connected
    }

    void bleDeviceReportsTheScanError() {
        SamNeo2Protocol protocol;
        BleTransport::Options o;
        o.name_prefix = kSamNeo2NamePrefix;
        o.service_uuid = protocol.ServiceUuid();
        o.characteristic_uuid = protocol.CharacteristicUuid();
        o.scan_timeout_ms = 300;
        BleTransport t(o);
        SamNeo2Device device(t);
        const Status s = device.Connect();
        QVERIFY(!s);
        QVERIFY2(Has(s.message, "bluetooth"), s.message.c_str());
        QVERIFY(!device.Connected());
    }

    // --- cancel ---
    void cancelAbortsADial() {
        SilentServer silent;
        WebSocketTransport t(QString("ws://127.0.0.1:%1").arg(silent.port()).toStdString(), 20000);
        QElapsedTimer clock;
        clock.start();
        t.SetCancel([&clock] { return clock.elapsed() > 200; });
        const Status s = t.Connect();
        QVERIFY(!s);
        QVERIFY(Has(s.message, "cancelled"));
        QVERIFY(clock.elapsed() < 3000);
    }
};

QTEST_MAIN(DeviceTransportsTest)
#include "device_transports_test.moc"
