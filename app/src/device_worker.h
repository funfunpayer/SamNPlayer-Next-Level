#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QTimer>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include "device_controller.h"
#include "device_io/qt_transport.h"
#include "device_io/waiter.h"
#include "samn/device/capabilities.h"
#include "samn/device/diagnostics.h"
#include "samn/device/driver.h"
#include "samn/settings.h"

namespace samn::ui {

namespace dev = samn::device;

// Shared by the worker and the controller.
// Settings keys of 0.7 (app_settings.go). The key of each kind's last
// target is its driver's (DriverInfo::target_setting).
constexpr const char* kPrefTransport = "device.transport";
constexpr const char* kPrefDiagnosticsHistoryPath = "device.diagnosticsHistoryPath";

inline QString Q(const std::string& s) { return QString::fromStdString(s); }

// ---------------------------------------------------------------------------
// The worker: lives on the device thread. Every device and transport is
// created, used and destroyed in its tasks, one at a time. The drivers say
// what a kind needs; the worker builds the transport and keeps its cancel,
// the connect and Stop deadlines and the generation counter.
// ---------------------------------------------------------------------------
class DeviceWorker : public QObject {
public:
    DeviceWorker(DeviceController* ctl) : ctl_(ctl) {}

    // Thread-safe: queue a task. `front` = jump the queue (Stop).
    void Post(std::function<void()> task, bool front = false) {
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            if (front)
                queue_.push_front(std::move(task));
            else
                queue_.push_back(std::move(task));
        }
        QMetaObject::invokeMethod(this, [this] { Drain(); }, Qt::QueuedConnection);
    }

    // --- thread-safe, coalescing level API (called from the GUI thread) ---
    // `generation`: the snapshot generation the caller saw (the device it
    // means); levels for an earlier one are dropped (ApplyLevels).
    void QueueVibration(double v, quint64 generation) {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        ForGenerationLocked(generation);
        pending_.vibration = v;
        ScheduleFlushLocked();
    }
    void QueueSuction(double v, quint64 generation) {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        ForGenerationLocked(generation);
        pending_.suction = v;
        ScheduleFlushLocked();
    }
    void QueuePosition(double pos, int ms, quint64 generation) {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        ForGenerationLocked(generation);
        pending_.position = {pos, ms};
        ScheduleFlushLocked();
    }
    void QueueAxis(const std::string& axis, double pos, int ms, quint64 generation) {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        ForGenerationLocked(generation);
        pending_.axes[axis] = {pos, ms};
        ScheduleFlushLocked();
    }
    void DropPending() {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        pending_ = {};
    }

    // --- tasks (worker thread only) ---
    void Connect(QString kind, QString target);
    void Disconnect();
    void Stop(bool silent);
    void TestRaw(QString channel, int value);
    void RunDiagnostics();
    void ListSerialPorts();
    void Tick(bool read_battery);
    void SetMotorLevels(std::map<std::string, double> levels);
    void SetTickInterval(int ms);
    void SetTimeouts(int dial_ms, int ble_scan_ms) {
        if (dial_ms > 0) dial_ms_ = dial_ms;
        if (ble_scan_ms > 0) ble_scan_ms_ = ble_scan_ms;
    }
    void SetDiagnosticsOptions(dev::DiagnosticsOptions options) { diag_options_ = std::move(options); }
    void SetBleTransportFactory(std::function<std::unique_ptr<QtTransport>()> factory) {
        ble_factory_ = std::move(factory);
    }
    void SetDeviceClock(dev::SteadyClock clock) { device_clock_ = std::move(clock); }
    void Shutdown();

private:
    struct Session {
        std::unique_ptr<QtTransport> transport;  // declared first: destroyed last
        QString kind;
        std::string target;  // as typed (trimmed); the driver's hints read it
        const dev::Driver* driver = nullptr;
        std::unique_ptr<dev::DeviceSession> device;  // owns the device
        double connect_latency_ms = 0;
    };
    struct Pending {
        std::optional<double> vibration, suction;
        std::optional<std::pair<double, int>> position;
        std::map<std::string, std::pair<double, int>> axes;
        std::optional<quint64> generation;  // the one every value of the batch was given for
    };

    void EnsureInit();
    void Drain();
    // The app's transport for a driver's request; null for kNone (and for a
    // kind this app has no transport for).
    std::unique_ptr<QtTransport> MakeTransport(const dev::TransportRequest& request) const;
    // A value for another generation than the waiting batch's: the batch's
    // values were meant for an earlier device and go; it is that one's batch now.
    void ForGenerationLocked(quint64 generation);
    void ScheduleFlushLocked();
    void Flush();
    void ApplyLevels(const Pending& p);
    dev::Status Move(const std::string& axis, double pos, int ms);

    DeviceController::Snapshot MakeSnapshot(bool read_battery);
    dev::ConnectionInfo SessionInfo(bool read_battery);
    // Which device the session drives (DeviceController::deviceIdentity).
    QString IdentityOf(const dev::ConnectionInfo& info) const;
    QString CurrentIdentity();
    void EmitSnapshot(bool read_battery);
    void EmitError(const QString& message, const QString& hint = QString());
    void CommandError(const std::string& message);
    void CommandOk() { last_command_error_.clear(); }
    void EmitLog(dev::LogLevel level, const std::string& text);
    bool Alive(std::string* why);
    void HandleLost(const std::string& why);
    void CheckAlive();
    void ApplyMotorLevels();
    void SchedulePump();
    void Pump();
    void DestroySession(bool stop_first);
    // The transport's cancel while connected: a disconnect or quit (abort_)
    // ends a wait, e.g. a battery read.
    std::function<bool()> SessionCancel() const;
    // A stop gets a cancel of its own, bounded by kStopBudgetMs: abort_ is
    // set by a disconnect or quit BEFORE the stop runs, and would cut the
    // off packets short (a Sam Neo 2 write waits for its acknowledgement).
    void BeginStopWindow();
    void EndStopWindow();
    QElapsedTimer stop_clock_;  // the shared stop deadline (BeginStopWindow)
    dev::LogSink LogSink();
    samn::Settings& Settings();

    DeviceController* ctl_;
    std::mutex queue_mutex_;
    std::deque<std::function<void()>> queue_;
    bool draining_ = false;  // worker thread only

    std::mutex pending_mutex_;
    Pending pending_;
    bool flush_scheduled_ = false;

    // Created on the worker thread (EnsureInit).
    std::unique_ptr<Waiter> waiter_;
    std::unique_ptr<QTimer> tick_timer_;
    int tick_ms_ = 2000;
    int dial_ms_ = 8000;
    int ble_scan_ms_ = 15000;
    bool tick_pending_ = false;
    bool pump_pending_ = false;
    // The identity of the last snapshot and how often it changed (each
    // change is a new generation; Snapshot::generation).
    QString last_identity_;
    quint64 generation_ = 0;

    std::unique_ptr<Session> session_;
    bool connecting_ = false;
    std::map<std::string, double> motor_levels_;
    std::string last_command_error_;
    dev::DiagnosticsOptions diag_options_;
    std::function<std::unique_ptr<QtTransport>()> ble_factory_;
    dev::SteadyClock device_clock_;  // empty: the devices' own steady clock
    std::unique_ptr<samn::Settings> settings_;
};

}  // namespace samn::ui
