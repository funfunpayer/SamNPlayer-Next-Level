#pragma once

#include <QString>
#include <deque>
#include <memory>

#include "qt_transport.h"

class QWebSocket;

namespace samn::ui {

// Buttplug / Intiface: one WebSocket text message per Send / Receive.
class WebSocketTransport : public QtTransport {
    Q_OBJECT
public:
    // `url` e.g. "ws://127.0.0.1:12345" (use samn::device::NormalizeIntifaceUrl).
    // `dial_timeout_ms` bounds the connect.
    explicit WebSocketTransport(std::string url, int dial_timeout_ms = 8000, QObject* parent = nullptr);
    ~WebSocketTransport() override;

    samn::device::Status Connect() override;
    void Disconnect() override;
    bool IsConnected() const override;
    samn::device::Status Send(const samn::device::Bytes& data, samn::device::WriteMode mode) override;
    samn::device::ReceiveResult Receive(std::chrono::milliseconds timeout) override;
    samn::device::PeerInfo Peer() const override;

private:
    std::string url_;
    int dial_timeout_ms_;
    std::unique_ptr<QWebSocket> socket_;
    std::deque<samn::device::Bytes> inbox_;
    bool open_ = false;
    bool closed_ = false;           // the peer closed (or the link failed)
    bool reported_closed_ = false;  // Receive / Disconnect said so
    std::string close_reason_;
};

}  // namespace samn::ui
