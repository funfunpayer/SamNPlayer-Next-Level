#include "device_controller.h"

#include "app_log.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QMetaObject>
#include <QTimer>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <optional>

#include "device_io/ble_transport.h"
#include "device_io/serial_line_transport.h"
#include "device_io/socket_line_transport.h"
#include "device_io/websocket_transport.h"
#include "samn/device/capabilities.h"
#include "samn/device/diagnostics.h"
#include "samn/device/driver.h"
#include "samn/settings.h"

namespace samn::ui {

namespace dev = samn::device;

namespace {

// Settings keys of 0.7 (app_settings.go). The key of each kind's last
// target is its driver's (DriverInfo::target_setting).
constexpr const char* kPrefTransport = "device.transport";
constexpr const char* kPrefConnectTest = "device.connect_test";

// How long stops that follow each other (a stop, the close after it, the
// quit) may wait together for a link that stopped answering: two off packets
// at BleTransport's 3 s write timeout. A healthy Sam Neo 2 acknowledges each
// in well under 100 ms; the bound only keeps a quit from hanging.
constexpr int kStopBudgetMs = 6500;
// A stop that starts within this long after the budget ran out does not wait
// again: the link is gone (review of #138: stop + disconnect cost 2 budgets).
constexpr int kStopGraceMs = 1000;

constexpr const char* kNoDevice = "no device connected \xE2\x80\x94 connect in Settings \xE2\x86\x92 Device first";

QString Q(const std::string& s) { return QString::fromStdString(s); }

// The likely cause of a failed connect or a lost link, from the kind's driver.
std::string HintFor(const dev::Driver* driver, const std::string& target, const std::string& raw) {
    return driver ? driver->Hint(target, raw) : std::string();
}

dev::Status Err(const std::string& m) { return dev::Status::Error(m); }

}  // namespace

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

void DeviceWorker::EnsureInit() {
    if (waiter_) return;
    waiter_ = std::make_unique<Waiter>();
    tick_timer_ = std::make_unique<QTimer>();
    tick_timer_->setInterval(tick_ms_);
    // The timer only queues a task: a tick must never run inside the nested
    // event loop of a transport that is waiting for a reply.
    QObject::connect(tick_timer_.get(), &QTimer::timeout, tick_timer_.get(), [this] {
        if (tick_pending_) return;
        tick_pending_ = true;
        Post([this] {
            tick_pending_ = false;
            Tick(true);
        });
    });
}

void DeviceWorker::Drain() {
    EnsureInit();
    if (draining_) return;  // a nested loop of a transport: the outer drain continues
    draining_ = true;
    for (;;) {
        std::function<void()> task;
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            if (queue_.empty()) break;
            task = std::move(queue_.front());
            queue_.pop_front();
        }
        try {
            task();
        } catch (const std::exception& e) {
            EmitError(QString("internal error: ") + e.what());
        }
    }
    draining_ = false;
}

void DeviceWorker::ForGenerationLocked(quint64 generation) {
    if (pending_.generation && *pending_.generation != generation) {
        // Not "newest wins" for the whole batch: an old suction kept beside a
        // new vibration would reach the device it was not meant for.
        pending_.vibration.reset();
        pending_.suction.reset();
        pending_.position.reset();
        pending_.axes.clear();
    }
    pending_.generation = generation;
}

void DeviceWorker::ScheduleFlushLocked() {
    if (flush_scheduled_) return;
    flush_scheduled_ = true;
    Post([this] { Flush(); });
}

void DeviceWorker::Flush() {
    Pending p;
    {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        p = std::move(pending_);
        pending_ = {};
        flush_scheduled_ = false;
    }
    ApplyLevels(p);
}

samn::Settings& DeviceWorker::Settings() {
    if (!settings_) settings_ = std::make_unique<samn::Settings>();
    return *settings_;
}

dev::LogSink DeviceWorker::LogSink() {
    return [this](dev::LogLevel level, const std::string& text) { EmitLog(level, text); };
}

void DeviceWorker::EmitLog(dev::LogLevel level, const std::string& text) {
    DeviceController* ctl = ctl_;
    const QString line = Q(text);
    // Into the app log (Settings → Log). The mock device echoes every level
    // it gets (about 20 a second while playing): those lines are debug, or
    // they would push everything else out of the log.
    const bool echo = line.startsWith(QLatin1String("[mock] ")) && line.contains(QLatin1Char('='));
    const samn::LogSeverity sev = echo || level == dev::LogLevel::kDebug ? samn::LogSeverity::kDebug
                                  : level == dev::LogLevel::kWarn        ? samn::LogSeverity::kWarn
                                  : level == dev::LogLevel::kError       ? samn::LogSeverity::kError
                                                                         : samn::LogSeverity::kInfo;
    AppLog::instance().add(sev, line);
    QMetaObject::invokeMethod(ctl, [ctl, line] { emit ctl->logMessage(line); }, Qt::QueuedConnection);
}

void DeviceWorker::EmitError(const QString& message, const QString& hint) {
    DeviceController* ctl = ctl_;
    QMetaObject::invokeMethod(ctl, [ctl, message, hint] { ctl->applyError(message, hint); },
                              Qt::QueuedConnection);
}

// A refused command: shown once, not at every repeat of a 20 Hz stream.
void DeviceWorker::CommandError(const std::string& message) {
    if (message == last_command_error_) return;
    last_command_error_ = message;
    EmitError(Q(message));
}

dev::ConnectionInfo DeviceWorker::SessionInfo(bool read_battery) { return session_->device->Info(read_battery); }

// The Intiface index tells two toys of one name apart (AdoptedId).
QString DeviceWorker::IdentityOf(const dev::ConnectionInfo& info) const {
    const Session& se = *session_;
    const QString adopted = Q(se.device->AdoptedId());
    return se.kind + QLatin1Char('\n') + Q(info.name) + QLatin1Char('\n') + Q(info.address) +
           QLatin1Char('\n') + adopted;
}

QString DeviceWorker::CurrentIdentity() { return session_ ? IdentityOf(SessionInfo(false)) : QString(); }

DeviceController::Snapshot DeviceWorker::MakeSnapshot(bool read_battery) {
    DeviceController::Snapshot s;
    s.connecting = connecting_;
    if (!session_) return s;
    Session& se = *session_;
    s.kind = se.kind;
    s.mock = se.driver->Info().simulated;
    s.raw = se.device->HasRawValues();
    const dev::ConnectionInfo info = SessionInfo(read_battery);
    s.connected = info.connected;
    s.name = Q(info.name);
    s.address = Q(info.address);
    s.rssi = info.rssi;
    // A reading belongs to the live link: none while the device says it is
    // not connected (the devices also drop it themselves, and age it out).
    s.battery_ok = info.connected && info.battery_ok;
    s.battery = s.battery_ok ? info.battery_pct : -1;
    s.caps = dev::CapabilitiesOf(se.device->GetDevice());
    s.identity = IdentityOf(info);
    return s;
}

void DeviceWorker::EmitSnapshot(bool read_battery) {
    DeviceController* ctl = ctl_;
    DeviceController::Snapshot snap = MakeSnapshot(read_battery);
    if (snap.identity != last_identity_) {
        last_identity_ = snap.identity;
        ++generation_;
    }
    snap.generation = generation_;
    QMetaObject::invokeMethod(ctl, [ctl, snap] { ctl->applySnapshot(snap); }, Qt::QueuedConnection);
}

bool DeviceWorker::Alive(std::string* why) {
    if (!session_) return false;
    Session& se = *session_;
    if (se.device->Alive()) return true;
    const std::string transport_error = se.transport ? se.transport->LastError() : std::string();
    *why = transport_error.empty() ? "the link was closed" : transport_error;
    return false;
}

// After a command: a lost link is reported, and a reconnect that adopted
// another device is shown now, not at the next tick (its levels stop at once).
void DeviceWorker::CheckAlive() {
    std::string why;
    if (session_ && !Alive(&why))
        HandleLost(why);
    else if (CurrentIdentity() != last_identity_)
        EmitSnapshot(false);
}

void DeviceWorker::HandleLost(const std::string& why) {
    const dev::Driver* driver = session_ ? session_->driver : nullptr;
    const std::string target = session_ ? session_->target : std::string();
    DestroySession(false);
    EmitError("connection lost: " + Q(why), Q(HintFor(driver, target, why)));
    EmitSnapshot(false);
}

std::function<bool()> DeviceWorker::SessionCancel() const {
    DeviceController* ctl = ctl_;
    return [ctl] { return ctl->abort_.load(); };
}

void DeviceWorker::BeginStopWindow() {
    if (!session_ || !session_->transport) return;
    // One deadline for stops that follow each other.
    if (!stop_clock_.isValid() || stop_clock_.hasExpired(kStopBudgetMs + kStopGraceMs)) stop_clock_.start();
    const QElapsedTimer clock = stop_clock_;
    session_->transport->SetCancel([clock] { return clock.hasExpired(kStopBudgetMs); });
}

void DeviceWorker::EndStopWindow() {
    if (session_ && session_->transport) session_->transport->SetCancel(SessionCancel());
}

void DeviceWorker::DestroySession(bool stop_first) {
    if (tick_timer_) tick_timer_->stop();
    if (!session_) return;
    // The stop and the device's own stop in Disconnect() share one budget;
    // the session (and its transport) goes away right after.
    BeginStopWindow();
    if (stop_first) {
        // Always switch off before closing, or a device that just got a test
        // pulse keeps running (0.7).
        const dev::Status s = session_->device->GetDevice().Stop();
        if (!s) EmitLog(dev::LogLevel::kWarn, "device: stop before disconnect failed: " + s.message);
    }
    session_->device->GetDevice().Disconnect();
    session_.reset();
    last_command_error_.clear();
}

void DeviceWorker::ApplyMotorLevels() {
    if (!session_) return;
    auto* setter = dynamic_cast<dev::MotorLevelSetter*>(&session_->device->GetDevice());
    if (!setter) return;
    for (const auto& [channel, level] : motor_levels_) setter->SetMotorLevel(channel, level);
}

void DeviceWorker::SetMotorLevels(std::map<std::string, double> levels) {
    motor_levels_ = std::move(levels);
    ApplyMotorLevels();
}

void DeviceWorker::SetTickInterval(int ms) {
    tick_ms_ = std::max(10, ms);
    if (tick_timer_) tick_timer_->setInterval(tick_ms_);
}

void DeviceWorker::SchedulePump() {
    if (pump_pending_) return;
    pump_pending_ = true;
    Post([this] {
        pump_pending_ = false;
        Pump();
    });
}

// Read what the peer sent so it never piles up, and notice a dead link.
void DeviceWorker::Pump() {
    if (!session_) return;
    dev::PumpOutcome outcome = dev::PumpOutcome::kCheck;
    const dev::Status s = session_->device->Pump(&outcome);
    if (!s) {
        HandleLost(s.message);
        return;
    }
    if (outcome == dev::PumpOutcome::kReconnected)
        EmitSnapshot(false);
    else if (outcome == dev::PumpOutcome::kCheck)
        CheckAlive();
}

std::unique_ptr<QtTransport> DeviceWorker::MakeTransport(const dev::TransportRequest& request) const {
    switch (request.kind) {
    case dev::TransportKind::kNone:
        return nullptr;
    case dev::TransportKind::kBle: {
        if (ble_factory_) return ble_factory_();
        BleTransport::Options options;
        options.name_prefix = request.ble_name_prefix;
        options.service_uuid = request.service_uuid;
        options.characteristic_uuid = request.characteristic_uuid;
        options.scan_timeout_ms = ble_scan_ms_;
        return std::make_unique<BleTransport>(options);
    }
    case dev::TransportKind::kSerialLine:
        return std::make_unique<SerialLineTransport>(request.target);
    case dev::TransportKind::kSocketLine:
        return std::make_unique<SocketLineTransport>(request.network, request.target, dial_ms_);
    case dev::TransportKind::kWebSocket:
        return std::make_unique<WebSocketTransport>(request.target, dial_ms_);
    }
    return nullptr;
}

void DeviceWorker::Connect(QString kind, QString target) {
    if (session_) {
        EmitError("already connected \xE2\x80\x94 disconnect first");
        EmitSnapshot(false);
        return;
    }
    EnsureInit();
    DeviceController* ctl = ctl_;
    ctl->abort_ = false;
    connecting_ = true;
    EmitSnapshot(false);

    const dev::Driver* driver = ctl->drivers_->Find(kind.toStdString());
    const int deadline_ms = driver ? driver->Info().connect_timeout_ms : dev::kDefaultConnectTimeoutMs;
    QElapsedTimer clock;
    clock.start();
    bool deadline_hit = false;
    auto cancel = [ctl, &clock, &deadline_hit, deadline_ms] {
        if (ctl->abort_.load()) return true;
        if (clock.elapsed() > deadline_ms) {
            deadline_hit = true;
            return true;
        }
        return false;
    };

    auto se = std::make_unique<Session>();
    se->kind = kind;
    se->driver = driver;
    const std::string text = target.trimmed().toStdString();
    se->target = text;
    dev::Status setup = dev::Status::Ok();

    // The driver says what the kind needs; the transport is the app's.
    dev::TransportRequest request;
    if (!driver)
        setup = Err("unknown device kind \"" + kind.toStdString() + "\"");
    else
        setup = driver->Resolve(text, &request);
    if (setup) {
        se->transport = MakeTransport(request);
        se->device = driver->Create(se->transport.get(), text, dev::SessionOptions{LogSink(), device_clock_});
        if (!se->device) setup = Err("no transport for the device kind \"" + kind.toStdString() + "\"");
    }

    dev::Status result = setup;
    if (setup) {
        if (se->transport) {
            se->transport->SetCancel(cancel);
            QObject::connect(se->transport.get(), &QtTransport::incoming, se->transport.get(),
                             [this] { SchedulePump(); });
        }
        QElapsedTimer latency;
        latency.start();
        result = se->device->GetDevice().Connect();
        se->connect_latency_ms = latency.nsecsElapsed() / 1e6;
        if (se->transport) se->transport->SetCancel(SessionCancel());
    }

    connecting_ = false;
    if (!result) {
        std::string message = result.message;
        if (deadline_hit)
            message += " (timeout: no connection within " + std::to_string(deadline_ms / 1000) + " s)";
        const std::string raw = se->transport && !se->transport->LastError().empty() ? se->transport->LastError() : message;
        EmitLog(dev::LogLevel::kWarn, "device: connection failed: " + message);
        EmitError(Q(message), Q(HintFor(driver, text, raw)));
        se.reset();  // device first, then its transport
        EmitSnapshot(false);
        return;
    }

    session_ = std::move(se);
    ApplyMotorLevels();
    try {
        // Remember the successful choice: preselected next time (0.7).
        Settings().set(kPrefTransport, kind.toStdString());
        const std::string& key = driver->Info().target_setting;
        if (!key.empty()) Settings().set(key, text);
    } catch (const std::exception& e) {
        EmitLog(dev::LogLevel::kWarn, std::string("device: choice not saved: ") + e.what());
    }
    EmitError(QString(), QString());  // clear the last error
    EmitLog(dev::LogLevel::kInfo, "device: connected (" + kind.toStdString() + ")");

    // Optional post-connect check (setting "device.connect_test", default off):
    // a short pulse on both channels, then off, to prove commands get through.
    bool connect_test = false;
    try {
        connect_test = Settings().get(kPrefConnectTest, false).get<bool>();
    } catch (const std::exception&) {
    }
    if (connect_test) {
        dev::Device& device = session_->device->GetDevice();
        (void)device.SetVibration(0.3);
        waiter_->Sleep(150);
        (void)device.SetSuction(0.3);
        waiter_->Sleep(150);
        BeginStopWindow();
        const dev::Status s = device.Stop();
        EndStopWindow();
        if (!s) EmitLog(dev::LogLevel::kWarn, "device: post-connect stop failed: " + s.message);
    }

    tick_timer_->start(tick_ms_);
    EmitSnapshot(true);
}

void DeviceWorker::Disconnect() {
    DestroySession(true);
    connecting_ = false;
    EmitLog(dev::LogLevel::kInfo, "device: disconnected");
    EmitSnapshot(false);
}

void DeviceWorker::Stop(bool silent) {
    if (!session_) {
        if (!silent) CommandError(kNoDevice);
        return;
    }
    BeginStopWindow();
    const dev::Status s = session_->device->GetDevice().Stop();
    EndStopWindow();
    if (!s)
        CommandError(s.message);
    else
        CommandOk();
    CheckAlive();
}

dev::Status DeviceWorker::Move(const std::string& axis, double pos, int ms) {
    const auto duration = std::chrono::milliseconds(std::max(0, ms));
    dev::Device* d = &session_->device->GetDevice();
    if (axis.empty() || axis == dev::kChStroke) {
        auto* lin = dynamic_cast<dev::LinearDevice*>(d);
        if (!lin || !lin->HasLinear()) return Err("this device has no linear axis");
        return lin->SetPosition(dev::Clamp01(pos), duration);
    }
    auto* multi = dynamic_cast<dev::MultiAxisDevice*>(d);
    if (!multi || !multi->HasAxis(axis)) return Err("this device has no axis \"" + axis + "\"");
    return multi->SetAxis(axis, dev::Clamp01(pos), duration);
}

void DeviceWorker::ApplyLevels(const Pending& p) {
    if (!p.vibration && !p.suction && !p.position && p.axes.empty()) return;
    if (!session_) {
        CommandError(kNoDevice);
        return;
    }
    // Given while the GUI still showed an earlier device (the link was lost,
    // or a reconnect adopted another toy): not for the one connected now.
    if (p.generation && *p.generation != generation_) {
        EmitLog(dev::LogLevel::kDebug, "device: levels for the previous device dropped");
        return;
    }
    dev::Device& d = session_->device->GetDevice();
    const QString meant = last_identity_;
    dev::Status first_error = dev::Status::Ok();
    auto note = [&](const dev::Status& s) {
        if (!s && first_error) first_error = s;
    };
    // A command of this batch may reconnect to another device: the rest of
    // the batch is not sent to it.
    auto same = [&] { return CurrentIdentity() == meant; };
    if (p.vibration && same()) note(d.SetVibration(dev::Clamp01(*p.vibration)));
    if (p.suction && same()) note(d.SetSuction(dev::Clamp01(*p.suction)));
    if (p.position && same()) note(Move(dev::kChStroke, p.position->first, p.position->second));
    for (const auto& [axis, move] : p.axes)
        if (same()) note(Move(axis, move.first, move.second));
    if (!first_error)
        CommandError(first_error.message);
    else
        CommandOk();
    CheckAlive();
}

void DeviceWorker::TestRaw(QString channel, int value) {
    if (!session_) {
        CommandError(kNoDevice);
        return;
    }
    auto* raw = dynamic_cast<dev::RawCapable*>(&session_->device->GetDevice());
    if (!raw) {
        CommandError("this device does not support raw values");
        return;
    }
    if (value < 0 || value > 255) {
        CommandError("raw value must be between 0 and 255 (got " + std::to_string(value) + ")");
        return;
    }
    dev::Status s = dev::Status::Ok();
    if (channel == "vibration")
        s = raw->SetVibrationRaw(static_cast<std::uint8_t>(value));
    else if (channel == "suction")
        s = raw->SetSuctionRaw(static_cast<std::uint8_t>(value));
    else
        s = Err("unknown channel \"" + channel.toStdString() + "\"");
    if (!s)
        CommandError(s.message);
    else
        CommandOk();
    CheckAlive();
}

void DeviceWorker::ListSerialPorts() {
    QStringList ports;
    for (const std::string& p : SerialPorts()) ports << Q(p);
    DeviceController* ctl = ctl_;
    QMetaObject::invokeMethod(ctl, [ctl, ports] { ctl->applySerialPorts(ports); }, Qt::QueuedConnection);
}

void DeviceWorker::Tick(bool read_battery) {
    if (!session_) return;
    // Sam Neo 2 keepalive; Intiface: pump, the one-shot reconnect and the ping.
    const dev::Status s = session_->device->Tick();
    if (!s) {
        HandleLost(s.message);
        return;
    }
    std::string why;
    if (!Alive(&why)) {
        HandleLost(why);
        return;
    }
    EmitSnapshot(read_battery);
}

void DeviceWorker::RunDiagnostics() {
    DeviceController* ctl = ctl_;
    auto finish = [this, ctl](bool running) {
        QMetaObject::invokeMethod(ctl, [ctl, running] { ctl->applyDiagnosticsRunning(running); },
                                  Qt::QueuedConnection);
    };
    if (!session_) {
        CommandError(kNoDevice);
        finish(false);
        return;
    }
    ctl->diagnostics_cancel_ = false;
    dev::DiagnosticsOptions options = diag_options_;
    options.sleep = [this, ctl](std::chrono::milliseconds d) {
        waiter_->Sleep(static_cast<int>(d.count()), [ctl] { return ctl->diagnostics_cancel_.load(); });
    };
    options.cancelled = [ctl] { return ctl->diagnostics_cancel_.load(); };
    options.on_entry = [ctl](const dev::DiagnosticsEntry& e) {
        const QString json = Q(dev::ToJson(e));
        QMetaObject::invokeMethod(ctl, [ctl, json] { emit ctl->diagnosticsEntry(json); }, Qt::QueuedConnection);
    };
    const dev::DiagnosticsReport report =
        dev::RunDiagnostics(session_->device->GetDevice(), session_->connect_latency_ms, options);
    const QString json = Q(dev::ToJson(report));
    QMetaObject::invokeMethod(ctl, [ctl, json] { emit ctl->diagnosticsFinished(json); }, Qt::QueuedConnection);
    finish(false);
    CheckAlive();
}

void DeviceWorker::Shutdown() {
    if (tick_timer_) tick_timer_->stop();
    DestroySession(true);
    tick_timer_.reset();
    waiter_.reset();
    QThread::currentThread()->quit();
}

// ---------------------------------------------------------------------------
// DeviceController (GUI thread)
// ---------------------------------------------------------------------------

bool DeviceController::Snapshot::operator==(const Snapshot& o) const {
    return connected == o.connected && connecting == o.connecting && mock == o.mock && raw == o.raw &&
           kind == o.kind && name == o.name && address == o.address && rssi == o.rssi &&
           battery == o.battery && battery_ok == o.battery_ok && identity == o.identity && generation == o.generation &&
           dev::ToJson(caps) == dev::ToJson(o.caps);
}

DeviceController::DeviceController(QObject* parent)
    : QObject(parent), drivers_(std::make_unique<dev::DriverRegistry>()) {
    dev::RegisterBuiltinDrivers(*drivers_);
    worker_ = new DeviceWorker(this);
    worker_->moveToThread(&thread_);
    thread_.setObjectName("samn-device");
    thread_.start();
}

DeviceController::~DeviceController() {
    abort_ = true;
    diagnostics_cancel_ = true;
    worker_->Post([w = worker_] { w->Shutdown(); }, true);
    thread_.wait();
    delete worker_;
}

QString DeviceController::state() const {
    return snap_.connected ? "connected" : (snap_.connecting ? "connecting" : "disconnected");
}
bool DeviceController::connected() const { return snap_.connected; }
bool DeviceController::connecting() const { return snap_.connecting; }
QString DeviceController::kind() const { return snap_.kind; }
bool DeviceController::isMock() const { return snap_.mock; }
QString DeviceController::name() const { return snap_.name; }
QString DeviceController::address() const { return snap_.address; }
int DeviceController::rssi() const { return snap_.rssi; }
int DeviceController::batteryPercent() const { return snap_.battery; }
bool DeviceController::batteryOk() const { return snap_.battery_ok; }

QVariantList DeviceController::capabilities() const {
    QVariantList out;
    for (const dev::Feature& f : snap_.caps.features) {
        QVariantMap m;
        m["channel"] = Q(f.channel);
        m["kind"] = Q(f.kind);
        m["index"] = f.index;
        m["actuator"] = Q(f.actuator);
        m["driven"] = f.driven;
        out << m;
    }
    return out;
}
QStringList DeviceController::channels() const {
    QStringList out;
    for (const std::string& c : snap_.caps.Channels()) out << Q(c);
    return out;
}
QStringList DeviceController::axes() const {
    QStringList out;
    for (const dev::Feature& f : snap_.caps.features)
        if (f.kind == dev::kKindLinear) out << Q(f.channel);
    return out;
}
bool DeviceController::capVibration() const { return snap_.connected && snap_.caps.Has(dev::kChVibration); }
bool DeviceController::capSuction() const { return snap_.connected && snap_.caps.Has(dev::kChSuction); }
bool DeviceController::capLinear() const { return snap_.connected && snap_.caps.Has(dev::kChStroke); }
bool DeviceController::capBattery() const { return snap_.connected && (snap_.caps.battery || snap_.battery_ok); }
bool DeviceController::capRaw() const { return snap_.connected && snap_.raw; }

QVariantList DeviceController::drivers() const {
    QVariantList out;
    for (const dev::DriverInfo& d : drivers_->List()) {
        QVariantMap m;
        m["id"] = Q(d.id);
        m["name"] = Q(d.name);
        m["help"] = Q(d.help);
        m["needsAddress"] = d.needs_address;
        m["addressExample"] = Q(d.address_example);
        m["serialPort"] = d.transport == dev::TransportKind::kSerialLine;
        m["simulated"] = d.simulated;
        out << m;
    }
    return out;
}

void DeviceController::applySnapshot(const Snapshot& snap) {
    if (snap == snap_) return;
    snap_ = snap;
    emit statusChanged();
}

void DeviceController::applyError(const QString& message, const QString& hint) {
    if (message == last_error_ && hint == hint_) return;
    last_error_ = message;
    hint_ = hint;
    emit errorChanged();
    if (!message.isEmpty()) AppLog::instance().reportError(hint.isEmpty() ? message : message + QStringLiteral(" — ") + hint);
}

// A refused request (busy, already connected): a hint in the status, no toast.
void DeviceController::reportError(const QString& message, const QString& hint) {
    last_error_ = message;
    hint_ = hint;
    emit errorChanged();
}

void DeviceController::applySerialPorts(const QStringList& ports) {
    serial_ports_ = ports;
    emit serialPortsChanged();
}

void DeviceController::applyDiagnosticsRunning(bool running) {
    if (diagnostics_running_ == running) return;
    diagnostics_running_ = running;
    emit diagnosticsRunningChanged();
}

void DeviceController::setSessionActiveFor(const QObject* owner, bool active) {
    const bool was = sessionActive();
    if (active)
        session_holders_.insert(owner);
    else
        session_holders_.erase(owner);
    if (sessionActive() != was) emit sessionActiveChanged();
}

void DeviceController::connectDevice(const QString& kind, const QString& target) {
    if (sessionActive()) {
        reportError("playback or training is in progress \xE2\x80\x94 stop it first; the device can only "
                    "hold one connection at a time");
        return;
    }
    if (snap_.connected || snap_.connecting) {
        reportError("already connected \xE2\x80\x94 disconnect first");
        return;
    }
    snap_.connecting = true;  // show it now; the worker's snapshots follow
    emit statusChanged();
    worker_->Post([w = worker_, kind, target] { w->Connect(kind, target); });
}

void DeviceController::disconnectDevice() {
    if (sessionActive()) {
        reportError("playback or training is using this connection \xE2\x80\x94 stop it there first");
        return;
    }
    abort_ = true;  // a connect in progress gives up
    diagnostics_cancel_ = true;
    worker_->DropPending();
    worker_->Post([w = worker_] { w->Disconnect(); });
}

QString DeviceController::savedKind() const {
    try {
        samn::Settings settings;
        return Q(settings.get(kPrefTransport, "").get<std::string>());
    } catch (const std::exception&) {
        return {};
    }
}

QString DeviceController::savedTarget(const QString& kind) const {
    const dev::Driver* driver = drivers_->Find(kind.toStdString());
    const std::string key = driver ? driver->Info().target_setting : std::string();
    if (key.empty()) return {};
    try {
        samn::Settings settings;
        return Q(settings.get(key, "").get<std::string>());
    } catch (const std::exception&) {
        return {};
    }
}

void DeviceController::setVibration(double level) { worker_->QueueVibration(dev::Clamp01(level), snap_.generation); }
void DeviceController::setSuction(double level) { worker_->QueueSuction(dev::Clamp01(level), snap_.generation); }
void DeviceController::setPosition(double pos, int durationMs) {
    worker_->QueuePosition(dev::Clamp01(pos), std::max(0, durationMs), snap_.generation);
}
void DeviceController::setAxis(const QString& axis, double pos, int durationMs) {
    worker_->QueueAxis(axis.toStdString(), dev::Clamp01(pos), std::max(0, durationMs), snap_.generation);
}
void DeviceController::setAxes(const QVariantMap& positions, int durationMs) {
    for (auto it = positions.begin(); it != positions.end(); ++it)
        worker_->QueueAxis(it.key().toStdString(), dev::Clamp01(it.value().toDouble()), std::max(0, durationMs),
                           snap_.generation);
}

void DeviceController::setVibrationForGeneration(double level, quint64 generation) {
    worker_->QueueVibration(dev::Clamp01(level), generation);
}
void DeviceController::setSuctionForGeneration(double level, quint64 generation) {
    worker_->QueueSuction(dev::Clamp01(level), generation);
}

void DeviceController::setMotorLevels(const QVariantMap& levels) {
    motor_levels_ = levels;
    std::map<std::string, double> clean;
    for (auto it = levels.begin(); it != levels.end(); ++it) {
        double l = it.value().toDouble();
        if (!(l >= 0)) l = 0;  // NaN too
        clean[it.key().toStdString()] = std::min(l, 1.0);
    }
    worker_->Post([w = worker_, clean] { w->SetMotorLevels(clean); });
}

void DeviceController::testRawValue(const QString& channel, int value) {
    if (sessionActive()) {
        reportError("device test not available during playback or training");
        return;
    }
    worker_->Post([w = worker_, channel, value] { w->TestRaw(channel, value); });
}

void DeviceController::stop() {
    worker_->DropPending();
    worker_->Post([w = worker_] { w->Stop(false); }, true);
}

void DeviceController::emergencyStop() {
    diagnostics_cancel_ = true;
    worker_->DropPending();
    worker_->Post([w = worker_] { w->Stop(true); }, true);
}

void DeviceController::refreshSerialPorts() {
    worker_->Post([w = worker_] { w->ListSerialPorts(); });
}

void DeviceController::refreshStatus() {
    worker_->Post([w = worker_] { w->Tick(true); });
}

void DeviceController::runDiagnostics() {
    if (sessionActive()) {
        reportError("device test not available during playback or training");
        return;
    }
    if (diagnostics_running_) {
        reportError("a diagnostics run is already in progress");
        return;
    }
    diagnostics_running_ = true;  // one run at a time, decided here
    emit diagnosticsRunningChanged();
    worker_->Post([w = worker_] { w->RunDiagnostics(); });
}

void DeviceController::cancelDiagnostics() { diagnostics_cancel_ = true; }

void DeviceController::setTickIntervalMs(int ms) {
    worker_->Post([w = worker_, ms] { w->SetTickInterval(ms); });
}

void DeviceController::setDialTimeoutMs(int ms) {
    worker_->Post([w = worker_, ms] { w->SetTimeouts(ms, 0); });
}

void DeviceController::setBleScanTimeoutMs(int ms) {
    worker_->Post([w = worker_, ms] { w->SetTimeouts(0, ms); });
}

void DeviceController::setDiagnosticsOptions(const samn::device::DiagnosticsOptions& options) {
    worker_->Post([w = worker_, options] { w->SetDiagnosticsOptions(options); });
}

void DeviceController::setBleTransportFactory(std::function<std::unique_ptr<QtTransport>()> factory) {
    worker_->Post([w = worker_, factory = std::move(factory)] { w->SetBleTransportFactory(factory); });
}

void DeviceController::setDeviceClock(std::function<std::chrono::steady_clock::time_point()> clock) {
    worker_->Post([w = worker_, clock = std::move(clock)] { w->SetDeviceClock(clock); });
}

}  // namespace samn::ui
