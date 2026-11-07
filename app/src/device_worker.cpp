#include "device_worker.h"

#include <QCoreApplication>
#include <QMetaObject>
#include <QThread>
#include <algorithm>
#include <chrono>
#include <cmath>

#include "app_log.h"
#include "device_io/ble_transport.h"
#include "device_io/serial_line_transport.h"
#include "device_io/socket_line_transport.h"
#include "device_io/websocket_transport.h"

namespace samn::ui {

namespace {

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

// The likely cause of a failed connect or a lost link, from the kind's driver.
std::string HintFor(const dev::Driver* driver, const std::string& target, const std::string& raw) {
    return driver ? driver->Hint(target, raw) : std::string();
}

dev::Status Err(const std::string& m) { return dev::Status::Error(m); }

}  // namespace

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

}  // namespace samn::ui
