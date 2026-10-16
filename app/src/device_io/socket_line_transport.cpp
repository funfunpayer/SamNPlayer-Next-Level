#include "socket_line_transport.h"

#include <QHostAddress>
#include <QNetworkDatagram>
#include <QTcpSocket>
#include <QUdpSocket>
#include <QUrl>

namespace samn::ui {

using samn::device::Bytes;
using samn::device::PeerInfo;
using samn::device::ReceiveResult;
using samn::device::Status;
using samn::device::WriteMode;

std::string DescribeSocketError(QAbstractSocket::SocketError error, const QString& qt_text,
                                const std::string& target) {
    switch (error) {
        case QAbstractSocket::ConnectionRefusedError:
            return "connection refused by " + target;
        case QAbstractSocket::HostNotFoundError:
            return "no such host: " + target;
        case QAbstractSocket::SocketTimeoutError:
            return "timeout: no answer from " + target;
        case QAbstractSocket::RemoteHostClosedError:
            return "connection closed by " + target;
        default: {
            // Qt's text, e.g. "Network is unreachable" (the hint matches it).
            const std::string text = qt_text.toStdString();
            return (text.empty() ? "network error" : text) + " (" + target + ")";
        }
    }
}

SocketLineTransport::SocketLineTransport(std::string network, std::string hostport,
                                         int connect_timeout_ms, QObject* parent)
    : QtTransport(parent),
      network_(std::move(network)),
      hostport_(std::move(hostport)),
      connect_timeout_ms_(connect_timeout_ms) {}

SocketLineTransport::~SocketLineTransport() { SocketLineTransport::Disconnect(); }

Status SocketLineTransport::Connect() {
    Disconnect();
    close_reason_.clear();
    const QUrl url(QString("tcp://") + QString::fromStdString(hostport_));
    if (url.host().isEmpty() || url.port(-1) <= 0)
        return Fail("invalid address \"" + hostport_ + "\" (expected host:port)");

    socket_.reset(tcp() ? static_cast<QAbstractSocket*>(new QTcpSocket)
                        : static_cast<QAbstractSocket*>(new QUdpSocket));
    bool failed = false;
    QAbstractSocket::SocketError error = QAbstractSocket::UnknownSocketError;
    QString error_text;
    QAbstractSocket* sock = socket_.get();
    QObject::connect(sock, &QAbstractSocket::connected, sock, [this] { waiter_.Wake(); });
    QObject::connect(sock, &QAbstractSocket::errorOccurred, sock,
                     [&, this](QAbstractSocket::SocketError e) {
                         if (!failed) {
                             failed = true;
                             error = e;
                             error_text = socket_ ? socket_->errorString() : QString();
                         }
                         waiter_.Wake();
                     });
    QObject::connect(sock, &QAbstractSocket::readyRead, sock, [this] { emit incoming(); });
    QObject::connect(sock, &QAbstractSocket::disconnected, sock, [this] {
        if (close_reason_.empty() && socket_)
            close_reason_ = DescribeSocketError(socket_->error(), socket_->errorString(), hostport_);
        emit incoming();
    });

    sock->connectToHost(url.host(), static_cast<quint16>(url.port()));
    const bool ok = waiter_.Until(
        [&] { return failed || sock->state() == QAbstractSocket::ConnectedState; },
        connect_timeout_ms_, [this] { return Cancelled(); });
    if (!ok || failed) {
        std::string why;
        if (failed)
            why = DescribeSocketError(error, error_text, hostport_);
        else if (Cancelled())
            why = "cancelled";
        else
            why = "timeout: no answer from " + hostport_ + " within " +
                  std::to_string(connect_timeout_ms_) + " ms";
        // The handlers capture locals: drop the socket before they go away.
        socket_.reset();
        return Fail(why);
    }
    // Later errors are reported through Send / Receive.
    QObject::disconnect(sock, &QAbstractSocket::errorOccurred, nullptr, nullptr);
    return Status::Ok();
}

void SocketLineTransport::Disconnect() {
    if (!socket_) return;
    QAbstractSocket* sock = socket_.get();
    QObject::disconnect(sock, nullptr, nullptr, nullptr);
    if (sock->state() != QAbstractSocket::UnconnectedState) {
        sock->disconnectFromHost();
        if (sock->state() != QAbstractSocket::UnconnectedState) sock->waitForDisconnected(300);
    }
    socket_.reset();
}

bool SocketLineTransport::IsConnected() const {
    return socket_ && socket_->state() == QAbstractSocket::ConnectedState;
}

Status SocketLineTransport::Send(const Bytes& data, WriteMode) {
    if (!IsConnected()) return Fail("not connected to " + hostport_);
    const QByteArray out(reinterpret_cast<const char*>(data.data()), static_cast<int>(data.size()));
    const qint64 written = socket_->write(out);
    if (written != out.size())
        return Fail("write to " + hostport_ + " failed: " + socket_->errorString().toStdString());
    if (tcp() && !socket_->waitForBytesWritten(2000))
        return Fail("write to " + hostport_ + " failed: " + socket_->errorString().toStdString());
    return Status::Ok();
}

bool SocketLineTransport::HasData() const {
    if (!socket_) return false;
    if (tcp()) return socket_->bytesAvailable() > 0;
    return static_cast<QUdpSocket*>(socket_.get())->hasPendingDatagrams();
}

QByteArray SocketLineTransport::Drain() {
    if (!socket_) return {};
    if (!tcp()) {
        auto* udp = static_cast<QUdpSocket*>(socket_.get());
        QByteArray all;
        while (udp->hasPendingDatagrams()) all += udp->receiveDatagram().data();
        return all;
    }
    return socket_->readAll();
}

ReceiveResult SocketLineTransport::Receive(std::chrono::milliseconds timeout) {
    if (!socket_) return ReceiveResult::Closed(close_reason_.empty() ? "not connected" : close_reason_);
    if (!HasData())
        waiter_.Until(
            [this] { return !socket_ || HasData() || socket_->state() != QAbstractSocket::ConnectedState; },
            static_cast<int>(timeout.count()), [this] { return Cancelled(); });
    const QByteArray got = Drain();
    if (!got.isEmpty()) return ReceiveResult::Message(Bytes(got.begin(), got.end()));
    if (!socket_ || socket_->state() != QAbstractSocket::ConnectedState)
        return ReceiveResult::Closed(close_reason_.empty() ? "connection closed" : close_reason_);
    return ReceiveResult::Timeout();
}

PeerInfo SocketLineTransport::Peer() const {
    PeerInfo p;
    p.name = network_ + "://" + hostport_;
    p.address = p.name;
    return p;
}

}  // namespace samn::ui
