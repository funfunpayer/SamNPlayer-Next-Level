#include "websocket_transport.h"

#include <QtGlobal>
#include <QUrl>
#include <QWebSocket>

#include "socket_line_transport.h"  // DescribeSocketError

// QWebSocket::errorOccurred came with Qt 6.5; 6.4 has the `error` signal.
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
#define SAMN_WS_ERROR_SIGNAL &QWebSocket::errorOccurred
#else
#define SAMN_WS_ERROR_SIGNAL qOverload<QAbstractSocket::SocketError>(&QWebSocket::error)
#endif

namespace samn::ui {

using samn::device::Bytes;
using samn::device::PeerInfo;
using samn::device::ReceiveResult;
using samn::device::Status;
using samn::device::WriteMode;

WebSocketTransport::WebSocketTransport(std::string url, int dial_timeout_ms, QObject* parent)
    : QtTransport(parent), url_(std::move(url)), dial_timeout_ms_(dial_timeout_ms) {}

WebSocketTransport::~WebSocketTransport() { WebSocketTransport::Disconnect(); }

Status WebSocketTransport::Connect() {
    Disconnect();
    inbox_.clear();
    open_ = false;
    closed_ = false;
    reported_closed_ = false;
    close_reason_.clear();
    const QUrl url(QString::fromStdString(url_));
    if (!url.isValid() || url.host().isEmpty() || (url.scheme() != "ws" && url.scheme() != "wss"))
        return Fail("invalid WebSocket address \"" + url_ + "\"");
    const std::string target = url.host().toStdString() + ":" + std::to_string(url.port(80));

    socket_ = std::make_unique<QWebSocket>();
    QWebSocket* ws = socket_.get();
    bool failed = false;
    QAbstractSocket::SocketError error = QAbstractSocket::UnknownSocketError;
    QString error_text;
    QObject::connect(ws, &QWebSocket::connected, ws, [this] {
        open_ = true;
        waiter_.Wake();
    });
    QObject::connect(ws, SAMN_WS_ERROR_SIGNAL, ws, [&, this](QAbstractSocket::SocketError e) {
        if (!open_ && !failed) {
            failed = true;
            error = e;
            error_text = socket_ ? socket_->errorString() : QString();
        } else if (open_ && close_reason_.empty() && socket_) {
            close_reason_ = DescribeSocketError(e, socket_->errorString(), target);
        }
        waiter_.Wake();
    });
    QObject::connect(ws, &QWebSocket::textMessageReceived, ws, [this](const QString& text) {
        const QByteArray utf8 = text.toUtf8();
        inbox_.emplace_back(utf8.begin(), utf8.end());
        waiter_.Wake();
        emit incoming();
    });
    QObject::connect(ws, &QWebSocket::binaryMessageReceived, ws, [this](const QByteArray& data) {
        inbox_.emplace_back(data.begin(), data.end());
        waiter_.Wake();
        emit incoming();
    });
    QObject::connect(ws, &QWebSocket::disconnected, ws, [this] {
        open_ = false;
        closed_ = true;
        if (close_reason_.empty())
            close_reason_ = "connection closed by " + url_ +
                            (socket_ && !socket_->closeReason().isEmpty()
                                 ? " (" + socket_->closeReason().toStdString() + ")"
                                 : "");
        waiter_.Wake();
        emit incoming();
    });

    ws->open(url);
    const bool ok = waiter_.Until([&] { return open_ || failed; }, dial_timeout_ms_,
                                  [this] { return Cancelled(); });
    if (!ok || failed) {
        std::string why;
        if (failed)
            why = DescribeSocketError(error, error_text, target);
        else if (Cancelled())
            why = "cancelled";
        else
            why = "timeout: no answer from " + target + " within " +
                  std::to_string(dial_timeout_ms_) + " ms";
        ws->abort();
        // The handlers capture locals: drop the socket before they go away.
        socket_.reset();
        return Fail(why);
    }
    // After the handshake the handler must not touch Connect()'s locals.
    QObject::disconnect(ws, SAMN_WS_ERROR_SIGNAL, nullptr, nullptr);
    QObject::connect(ws, SAMN_WS_ERROR_SIGNAL, ws, [this, target](QAbstractSocket::SocketError e) {
        if (close_reason_.empty() && socket_)
            close_reason_ = DescribeSocketError(e, socket_->errorString(), target);
    });
    return Status::Ok();
}

void WebSocketTransport::Disconnect() {
    if (!socket_) return;
    QWebSocket* ws = socket_.get();
    if (open_) {
        ws->close();
        waiter_.Until([this] { return !open_; }, 500);
    }
    QObject::disconnect(ws, nullptr, nullptr, nullptr);
    ws->abort();
    socket_.reset();
    open_ = false;
    reported_closed_ = true;
}

// Like the core's MockTransport, the link counts as up until Disconnect() or
// until Receive() has told the device that the peer closed it: the device's
// read loop is what notices a dead socket (IntifaceDevice::PumpIncoming).
bool WebSocketTransport::IsConnected() const { return socket_ && !reported_closed_; }

Status WebSocketTransport::Send(const Bytes& data, WriteMode) {
    if (!socket_ || !open_) return Fail("websocket: not connected to " + url_);
    const qint64 n = socket_->sendTextMessage(QString::fromUtf8(reinterpret_cast<const char*>(data.data()),
                                                               static_cast<qsizetype>(data.size())));
    if (n < 0) return Fail("websocket: send to " + url_ + " failed: " + socket_->errorString().toStdString());
    socket_->flush();
    return Status::Ok();
}

ReceiveResult WebSocketTransport::Receive(std::chrono::milliseconds timeout) {
    if (inbox_.empty() && !closed_ && socket_)
        waiter_.Until([this] { return !inbox_.empty() || closed_; }, static_cast<int>(timeout.count()),
                      [this] { return Cancelled(); });
    if (!inbox_.empty()) {
        Bytes next = std::move(inbox_.front());
        inbox_.pop_front();
        return ReceiveResult::Message(std::move(next));
    }
    if (closed_ || !socket_) {
        reported_closed_ = true;
        return ReceiveResult::Closed(close_reason_.empty() ? "not connected" : close_reason_);
    }
    return ReceiveResult::Timeout();
}

PeerInfo WebSocketTransport::Peer() const {
    PeerInfo p;
    p.name = url_;
    p.address = url_;
    return p;
}

}  // namespace samn::ui
