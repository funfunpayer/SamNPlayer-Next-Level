#pragma once

#include <QAbstractSocket>
#include <QByteArray>
#include <QString>
#include <memory>

#include "qt_transport.h"

namespace samn::ui {

// TCode over the network: one text line per Send, as a UDP datagram or a TCP
// write. Built from samn::device::ParseTCodeAddress() ("udp" / "tcp" and
// "host:port"). Receive returns whatever the peer sent (TCode needs no
// replies for motion).
class SocketLineTransport : public QtTransport {
    Q_OBJECT
public:
    // `network` is "udp" or "tcp", `hostport` e.g. "192.168.1.50:8000" or "[::1]:8000".
    SocketLineTransport(std::string network, std::string hostport, int connect_timeout_ms = 5000,
                        QObject* parent = nullptr);
    ~SocketLineTransport() override;

    samn::device::Status Connect() override;
    void Disconnect() override;
    bool IsConnected() const override;
    samn::device::Status Send(const samn::device::Bytes& data, samn::device::WriteMode mode) override;
    samn::device::ReceiveResult Receive(std::chrono::milliseconds timeout) override;
    samn::device::PeerInfo Peer() const override;

private:
    bool tcp() const { return network_ == "tcp"; }
    bool HasData() const;
    QByteArray Drain();

    std::string network_;
    std::string hostport_;
    int connect_timeout_ms_;
    std::unique_ptr<QAbstractSocket> socket_;
    std::string close_reason_;
};

// The "refused" / "timeout" / "no such host" wording for a socket error,
// shared by the socket and WebSocket transports. `target` is "host:port".
std::string DescribeSocketError(QAbstractSocket::SocketError error, const QString& qt_text,
                                const std::string& target);

}  // namespace samn::ui
