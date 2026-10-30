#pragma once

// Local stand-ins for the devices' far ends: a Buttplug server over a real
// WebSocket, a TCode line sink over TCP and UDP, and a TCP port that accepts
// and never answers (for timeouts). They run on the test's own event loop.

#include <QByteArray>
#include <QHostAddress>
#include <QList>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUdpSocket>
#include <QWebSocket>
#include <QWebSocketServer>
#include <nlohmann/json.hpp>

#include "device_fake_buttplug.h"  // message shapes (tests/core)

namespace samn_test {

// The Buttplug v3 server of tests/core/device_fake_buttplug.h, on a WebSocket.
class FakeButtplugServer : public QObject {
public:
    using json = nlohmann::json;

    explicit FakeButtplugServer(json device_messages, QObject* parent = nullptr)
        : QObject(parent),
          device_messages_(std::move(device_messages)),
          server_("fake-buttplug", QWebSocketServer::NonSecureMode) {
        listening_ = server_.listen(QHostAddress::LocalHost, 0);
        connect(&server_, &QWebSocketServer::newConnection, this, [this] {
            QWebSocket* client = server_.nextPendingConnection();
            clients_ << client;
            connect(client, &QWebSocket::disconnected, this, [this, client] {
                clients_.removeAll(client);
                client->deleteLater();
            });
            connect(client, &QWebSocket::textMessageReceived, this,
                    [this, client](const QString& text) { Handle(client, text); });
        });
    }

    bool listening() const { return listening_; }
    quint16 port() const { return server_.serverPort(); }
    QString url() const { return QString("ws://127.0.0.1:%1").arg(port()); }
    // Stops listening and drops every client: the server "went away".
    void Shutdown() {
        server_.close();
        const QList<QWebSocket*> all = clients_;
        for (QWebSocket* c : all) c->abort();  // `disconnected` deletes them
    }
    ~FakeButtplugServer() override { Shutdown(); }

    static json StrokerMessages() {
        return {{"LinearCmd", json::array({json{{"StepCount", 100}, {"ActuatorType", "Position"},
                                                {"FeatureDescriptor", "Stroke"}}})},
                {"StopDeviceCmd", json::object()}};
    }

    std::vector<json> received;  // one {"Name": body} per received message
    int connections() const { return connections_; }
    // How BatteryLevelCmd is answered: a reading (73 %), never, or an Error.
    enum class Battery { kReading, kMute, kError };
    Battery battery = Battery::kReading;

    int Count(const std::string& name) const {
        int n = 0;
        for (const auto& m : received)
            if (m.contains(name)) ++n;
        return n;
    }
    // The newest body of that message name (null when none).
    json Last(const std::string& name) const {
        for (auto it = received.rbegin(); it != received.rend(); ++it)
            if (it->contains(name)) return it->at(name);
        return nullptr;
    }

private:
    void Reply(QWebSocket* c, const std::string& name, json body) {
        c->sendTextMessage(QString::fromStdString(json::array({json{{name, std::move(body)}}}).dump()));
    }

    void Handle(QWebSocket* client, const QString& text) {
        const json batch = json::parse(text.toStdString());
        for (const auto& message : batch) {
            received.push_back(message);
            for (const auto& item : message.items()) {
                const std::string& name = item.key();
                const json id = item.value().value("Id", json());
                if (name == "RequestServerInfo") {
                    ++connections_;
                    Reply(client, "ServerInfo", {{"Id", id}, {"ServerName", "FakeServer"},
                                                 {"MessageVersion", 3}, {"MaxPingTime", 5000}});
                } else if (name == "RequestDeviceList") {
                    json devices = json::array();
                    devices.push_back({{"DeviceIndex", 7}, {"DeviceName", "Testgerät"},
                                       {"DeviceMessages", device_messages_}});
                    Reply(client, "DeviceList", {{"Id", id}, {"Devices", devices}});
                } else if (name == "BatteryLevelCmd") {
                    if (battery == Battery::kReading)
                        Reply(client, "BatteryLevelReading",
                              {{"Id", id}, {"DeviceIndex", item.value().value("DeviceIndex", json())},
                               {"BatteryLevel", 0.73}});
                    else if (battery == Battery::kError)
                        Reply(client, "Error", {{"Id", id}, {"ErrorCode", 4},
                                                {"ErrorMessage", "battery not readable"}});
                } else if (name == "StartScanning" || name == "StopScanning" || name == "Ping" ||
                           name == "ScalarCmd" || name == "LinearCmd" || name == "RotateCmd" ||
                           name == "StopDeviceCmd") {
                    Reply(client, "Ok", {{"Id", id}});
                }
            }
        }
    }

    json device_messages_;
    QWebSocketServer server_;
    QList<QWebSocket*> clients_;
    bool listening_ = false;
    int connections_ = 0;
};

// Collects the bytes a TCP client writes (TCode lines).
class TcpLineSink : public QObject {
public:
    explicit TcpLineSink(QObject* parent = nullptr) : QObject(parent) {
        listening_ = server_.listen(QHostAddress::LocalHost, 0);
        connect(&server_, &QTcpServer::newConnection, this, [this] {
            QTcpSocket* c = server_.nextPendingConnection();
            ++connections_;
            connect(c, &QTcpSocket::readyRead, this, [this, c] { data_ += c->readAll(); });
        });
    }
    bool listening() const { return listening_; }
    quint16 port() const { return server_.serverPort(); }
    QString hostport() const { return QString("127.0.0.1:%1").arg(port()); }
    QByteArray data() const { return data_; }
    QStringList lines() const { return QString::fromUtf8(data_).split('\n', Qt::SkipEmptyParts); }
    int connections() const { return connections_; }
    void clear() { data_.clear(); }

private:
    QTcpServer server_;
    QByteArray data_;
    bool listening_ = false;
    int connections_ = 0;
};

// Collects UDP datagrams (TCode lines).
class UdpLineSink : public QObject {
public:
    explicit UdpLineSink(QObject* parent = nullptr) : QObject(parent) {
        listening_ = socket_.bind(QHostAddress::LocalHost, 0);
        connect(&socket_, &QUdpSocket::readyRead, this, [this] {
            while (socket_.hasPendingDatagrams()) {
                QByteArray d(socket_.pendingDatagramSize(), 0);
                socket_.readDatagram(d.data(), d.size());
                datagrams_ << d;
            }
        });
    }
    bool listening() const { return listening_; }
    quint16 port() const { return socket_.localPort(); }
    QString hostport() const { return QString("127.0.0.1:%1").arg(port()); }
    QList<QByteArray> datagrams() const { return datagrams_; }

private:
    QUdpSocket socket_;
    QList<QByteArray> datagrams_;
    bool listening_ = false;
};

// A port that accepts TCP connections and never says a word: a peer that
// hangs (the WebSocket upgrade never completes).
class SilentServer : public QObject {
public:
    explicit SilentServer(QObject* parent = nullptr) : QObject(parent) {
        listening_ = server_.listen(QHostAddress::LocalHost, 0);
        connect(&server_, &QTcpServer::newConnection, this, [this] {
            QTcpSocket* c = server_.nextPendingConnection();
            c->setParent(this);  // keep it open, ignore it
        });
    }
    bool listening() const { return listening_; }
    quint16 port() const { return server_.serverPort(); }

private:
    QTcpServer server_;
    bool listening_ = false;
};

// A TCP port nothing listens on: bind one, remember the number, close it.
inline quint16 ClosedPort() {
    QTcpServer s;
    s.listen(QHostAddress::LocalHost, 0);
    const quint16 port = s.serverPort();
    s.close();
    return port;
}

}  // namespace samn_test
