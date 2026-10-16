#pragma once

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTimer>
#include <algorithm>
#include <functional>

namespace samn::ui {

// Blocks the calling thread until a condition holds, while its Qt event loop
// keeps running (so socket / Bluetooth signals are delivered). The transports
// use it to turn Qt's asynchronous APIs into the blocking calls of
// samn::device::Transport. Only ever used on the device worker thread.
//
// A transport connects the signals it waits for to Wake(); the condition is
// re-checked after every wake-up and at least every 25 ms, which is also how
// a cancel request is noticed.
class Waiter {
public:
    using Cancel = std::function<bool()>;

    Waiter() {
        timer_.setSingleShot(true);
        QObject::connect(&timer_, &QTimer::timeout, &loop_, &QEventLoop::quit);
    }

    void Wake() { loop_.quit(); }

    // true: `done()` became true. false: timed out or cancelled.
    // timeout_ms <= 0 only polls (after letting pending events run).
    bool Until(const std::function<bool()>& done, int timeout_ms, const Cancel& cancel = {}) {
        QElapsedTimer clock;
        clock.start();
        if (timeout_ms <= 0) QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        while (!done()) {
            if (cancel && cancel()) return false;
            const qint64 left = timeout_ms - clock.elapsed();
            if (left <= 0) return false;
            timer_.start(static_cast<int>(std::min<qint64>(left, 25)));
            loop_.exec(QEventLoop::ExcludeUserInputEvents);
            timer_.stop();
        }
        return true;
    }

    // Sleeps `ms` with the event loop running; false when cancelled early.
    bool Sleep(int ms, const Cancel& cancel = {}) {
        Until([] { return false; }, ms, cancel);
        return !(cancel && cancel());
    }

private:
    QEventLoop loop_;
    QTimer timer_;
};

}  // namespace samn::ui
