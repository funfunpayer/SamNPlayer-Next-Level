#pragma once

#include <QObject>
#include <functional>

#include "samn/device/transport.h"
#include "waiter.h"

namespace samn::ui {

// Base of the Qt implementations of samn::device::Transport. A transport
// lives on the device worker thread: it is created, used and destroyed there.
class QtTransport : public QObject, public samn::device::Transport {
    Q_OBJECT
public:
    explicit QtTransport(QObject* parent = nullptr) : QObject(parent) {}

    // Polled while connecting / waiting; true aborts the wait ("cancelled").
    void SetCancel(std::function<bool()> cancel) { cancel_ = std::move(cancel); }
    // The raw reason of the last failed Connect / Send (the keywords
    // "refused", "timeout", "no such host" appear in it when they are the cause).
    const std::string& LastError() const { return last_error_; }

signals:
    // Data is waiting, or the link was closed: the owner should pump.
    void incoming();

protected:
    bool Cancelled() const { return cancel_ && cancel_(); }
    samn::device::Status Fail(std::string message) {
        last_error_ = message;
        return samn::device::Status::Error(std::move(message));
    }

    Waiter waiter_;
    std::function<bool()> cancel_;
    std::string last_error_;
};

}  // namespace samn::ui
