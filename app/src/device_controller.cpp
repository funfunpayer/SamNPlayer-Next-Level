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
#include "samn/device/intiface.h"
#include "samn/device/mock.h"
#include "samn/device/protocol.h"
#include "samn/device/samneo2.h"
#include "samn/device/tcode.h"
#include "samn/settings.h"

namespace samn::ui {

namespace dev = samn::device;

namespace {

// Settings keys of 0.7 (app_settings.go).
constexpr const char* kPrefTransport = "device.transport";
constexpr const char* kPrefIntifaceUrl = "device.intifaceUrl";
constexpr const char* kPrefTCodeAddress = "device.tcodeAddress";
constexpr const char* kPrefTCodeSerialPort = "device.tcodeSerialPort";
constexpr const char* kPrefConnectTest = "device.connect_test";

// 0.7 testConnectTimeout (20 s; Intiface 30 s, the server may still be scanning).
constexpr int kConnectTimeoutMs = 20000;
constexpr int kIntifaceConnectTimeoutMs = 30000;

// How long a stop, with the close after it, may wait for a link that stopped
// answering. A healthy Sam Neo 2 acknowledges each off packet in well under
// 100 ms; the bound only keeps a quit from hanging.
constexpr int kStopBudgetMs = 4000;

constexpr const char* kNoDevice = "no device connected \xE2\x80\x94 connect in Settings \xE2\x86\x92 Device first";

QString Q(const std::string& s) { return QString::fromStdString(s); }

std::string Lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
bool Contains(const std::string& text, const char* word) { return text.find(word) != std::string::npos; }

// The likely cause of a failed connect (Intiface: the 0.7 texts).
std::string HintFor(const QString& kind, const std::string& url, const std::string& raw) {
    if (kind == "intiface") return dev::IntifaceHint(url, raw);
    if (kind == "tcode") {
        const std::string t = Lower(raw);
        if (Contains(t, "refused"))
            return "the device is reachable but nothing is listening on this port. Check the port "
                   "(default 8000) and that the TCode server of the firmware is running";
        if (Contains(t, "timeout") || Contains(t, "unreachable") || Contains(t, "no route"))
            return "no response. Are the device and the computer on the same network? "
                   "Is the IP address correct? Is a firewall blocking the port?";
        if (Contains(t, "no such host"))
            return "hostname could not be resolved \xE2\x80\x94 enter the IP address directly";
    }
    return {};
}

dev::Status Err(const std::string& m) { return dev::Status::Error(m); }

}  // namespace

// ---------------------------------------------------------------------------
// The worker: lives on the device thread. Every device and transport is
// created, used and destroyed in its tasks, one at a time.
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
    void QueueVibration(double v) {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        pending_.vibration = v;
        ScheduleFlushLocked();
    }
    void QueueSuction(double v) {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        pending_.suction = v;
        ScheduleFlushLocked();
    }
    void QueuePosition(double pos, int ms) {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        pending_.position = {pos, ms};
        ScheduleFlushLocked();
    }
    void QueueAxis(const std::string& axis, double pos, int ms) {
        std::lock_guard<std::mutex> lock(pending_mutex_);
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
    void Shutdown();

private:
    struct Session {
        QString kind;
        std::string url;  // Intiface: the normalised URL
        std::unique_ptr<QtTransport> transport;  // declared first: destroyed last
        std::unique_ptr<dev::Device> device;
        dev::SamNeo2Device* neo = nullptr;
        dev::TCodeDevice* tcode = nullptr;
        dev::IntifaceDevice* intiface = nullptr;
        dev::MockDevice* mock = nullptr;
        double connect_latency_ms = 0;
    };
    struct Pending {
        std::optional<double> vibration, suction;
        std::optional<std::pair<double, int>> position;
        std::map<std::string, std::pair<double, int>> axes;
    };

    void EnsureInit();
    void Drain();
    void ScheduleFlushLocked();
    void Flush();
    void ApplyLevels(const Pending& p);
    dev::Status Move(const std::string& axis, double pos, int ms);

    DeviceController::Snapshot MakeSnapshot(bool read_battery);
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

    std::unique_ptr<Session> session_;
    bool connecting_ = false;
    std::map<std::string, double> motor_levels_;
    std::string last_command_error_;
    dev::DiagnosticsOptions diag_options_;
    std::function<std::unique_ptr<QtTransport>()> ble_factory_;
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

DeviceController::Snapshot DeviceWorker::MakeSnapshot(bool read_battery) {
    DeviceController::Snapshot s;
    s.connecting = connecting_;
    if (!session_) return s;
    Session& se = *session_;
    s.kind = se.kind;
    s.mock = se.mock != nullptr;
    dev::ConnectionInfo info;
    info.connected = true;
    if (se.neo) {
        if (read_battery) se.neo->BatteryLevel();
        info = se.neo->Info();
        s.raw = true;
    } else if (se.intiface) {
        if (read_battery) se.intiface->BatteryLevel();
        info = se.intiface->Info();
    } else if (se.tcode) {
        info = se.tcode->Info();
    } else {
        info.name = "Mock device (no real hardware)";
        s.raw = true;
    }
    s.connected = info.connected;
    s.name = Q(info.name);
    s.address = Q(info.address);
    s.rssi = info.rssi;
    s.battery_ok = info.battery_ok;
    s.battery = info.battery_ok ? info.battery_pct : -1;
    s.caps = dev::CapabilitiesOf(*se.device);
    return s;
}

void DeviceWorker::EmitSnapshot(bool read_battery) {
    DeviceController* ctl = ctl_;
    DeviceController::Snapshot snap = MakeSnapshot(read_battery);
    QMetaObject::invokeMethod(ctl, [ctl, snap] { ctl->applySnapshot(snap); }, Qt::QueuedConnection);
}

bool DeviceWorker::Alive(std::string* why) {
    if (!session_) return false;
    Session& se = *session_;
    if (se.mock) return true;
    const std::string transport_error = se.transport ? se.transport->LastError() : std::string();
    if (se.neo) {
        if (se.neo->Connected() && se.transport->IsConnected()) return true;
    } else if (se.tcode) {
        if (se.tcode->Info().connected && se.transport->IsConnected()) return true;
    } else if (se.intiface) {
        if (se.intiface->Connected()) return true;
    }
    *why = transport_error.empty() ? "the link was closed" : transport_error;
    return false;
}

void DeviceWorker::CheckAlive() {
    std::string why;
    if (session_ && !Alive(&why)) HandleLost(why);
}

void DeviceWorker::HandleLost(const std::string& why) {
    const QString kind = session_ ? session_->kind : QString();
    const std::string url = session_ ? session_->url : std::string();
    DestroySession(false);
    EmitError("connection lost: " + Q(why), Q(HintFor(kind, url, why)));
    EmitSnapshot(false);
}

std::function<bool()> DeviceWorker::SessionCancel() const {
    DeviceController* ctl = ctl_;
    return [ctl] { return ctl->abort_.load(); };
}

void DeviceWorker::BeginStopWindow() {
    if (!session_ || !session_->transport) return;
    QElapsedTimer clock;
    clock.start();
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
        const dev::Status s = session_->device->Stop();
        if (!s) EmitLog(dev::LogLevel::kWarn, "device: stop before disconnect failed: " + s.message);
    }
    session_->device->Disconnect();
    session_.reset();
    last_command_error_.clear();
}

void DeviceWorker::ApplyMotorLevels() {
    if (!session_) return;
    auto* setter = dynamic_cast<dev::MotorLevelSetter*>(session_->device.get());
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
    Session& se = *session_;
    if (se.intiface) {
        if (!se.intiface->PumpIncoming()) {
            dev::Status why;
            if (!se.intiface->TryReconnectOnce(&why)) {
                HandleLost(why.message.empty() ? "the Buttplug server closed the connection" : why.message);
                return;
            }
            EmitSnapshot(false);
        }
        return;
    }
    if (se.transport) {
        // TCode devices may talk back; nothing needs it.
        while (se.transport->IsConnected() &&
               se.transport->Receive(std::chrono::milliseconds(0)).kind == dev::ReceiveResult::Kind::kMessage) {
        }
    }
    CheckAlive();
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

    const bool intiface = kind == "intiface";
    const int deadline_ms = intiface ? kIntifaceConnectTimeoutMs : kConnectTimeoutMs;
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
    const std::string text = target.trimmed().toStdString();
    dev::Status setup = dev::Status::Ok();

    if (kind == "mock") {
        auto mock = std::make_unique<dev::MockDevice>(true, [this](const std::string& line) {
            EmitLog(dev::LogLevel::kInfo, line);
        });
        se->mock = mock.get();
        se->device = std::move(mock);
    } else if (intiface) {
        se->url = dev::NormalizeIntifaceUrl(text);
        se->transport = std::make_unique<WebSocketTransport>(se->url, dial_ms_);
        auto device = std::make_unique<dev::IntifaceDevice>(*se->transport, se->url);
        device->SetLogSink(LogSink());
        se->intiface = device.get();
        se->device = std::move(device);
    } else if (kind == "tcode") {
        dev::TCodeAddress addr;
        setup = dev::ParseTCodeAddress(text, &addr);
        if (setup) {
            se->transport = std::make_unique<SocketLineTransport>(addr.network, addr.hostport, dial_ms_);
            auto device = std::make_unique<dev::TCodeDevice>(dev::TCodeDevice::OverNetwork(*se->transport, text));
            se->tcode = device.get();
            se->device = std::move(device);
        }
    } else if (kind == "tcode-serial") {
        se->transport = std::make_unique<SerialLineTransport>(text);
        auto device = std::make_unique<dev::TCodeDevice>(dev::TCodeDevice::OverSerial(*se->transport, text));
        se->tcode = device.get();
        se->device = std::move(device);
    } else if (kind == "ble") {
        dev::SamNeo2Protocol protocol;
        BleTransport::Options options;
        options.name_prefix = dev::kSamNeo2NamePrefix;
        options.service_uuid = protocol.ServiceUuid();
        options.characteristic_uuid = protocol.CharacteristicUuid();
        options.scan_timeout_ms = ble_scan_ms_;
        se->transport = ble_factory_ ? ble_factory_() : std::make_unique<BleTransport>(options);
        auto device = std::make_unique<dev::SamNeo2Device>(*se->transport);
        device->SetLogSink(LogSink());
        se->neo = device.get();
        se->device = std::move(device);
    } else {
        setup = Err("unknown device kind \"" + kind.toStdString() + "\"");
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
        result = se->device->Connect();
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
        EmitError(Q(message), Q(HintFor(kind, se->url, raw)));
        se.reset();  // device first, then its transport
        EmitSnapshot(false);
        return;
    }

    session_ = std::move(se);
    ApplyMotorLevels();
    try {
        // Remember the successful choice: preselected next time (0.7).
        Settings().set(kPrefTransport, kind.toStdString());
        if (intiface) Settings().set(kPrefIntifaceUrl, text);
        if (kind == "tcode") Settings().set(kPrefTCodeAddress, text);
        if (kind == "tcode-serial") Settings().set(kPrefTCodeSerialPort, text);
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
        (void)session_->device->SetVibration(0.3);
        waiter_->Sleep(150);
        (void)session_->device->SetSuction(0.3);
        waiter_->Sleep(150);
        const dev::Status s = session_->device->Stop();
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
    const dev::Status s = session_->device->Stop();
    EndStopWindow();
    if (!s)
        CommandError(s.message);
    else
        CommandOk();
    CheckAlive();
}

dev::Status DeviceWorker::Move(const std::string& axis, double pos, int ms) {
    const auto duration = std::chrono::milliseconds(std::max(0, ms));
    dev::Device* d = session_->device.get();
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
    dev::Device& d = *session_->device;
    dev::Status first_error = dev::Status::Ok();
    auto note = [&](const dev::Status& s) {
        if (!s && first_error) first_error = s;
    };
    if (p.vibration) note(d.SetVibration(dev::Clamp01(*p.vibration)));
    if (p.suction) note(d.SetSuction(dev::Clamp01(*p.suction)));
    if (p.position) note(Move(dev::kChStroke, p.position->first, p.position->second));
    for (const auto& [axis, move] : p.axes) note(Move(axis, move.first, move.second));
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
    auto* raw = dynamic_cast<dev::RawCapable*>(session_->device.get());
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
    Session& se = *session_;
    if (se.neo) {
        se.neo->TickKeepalive();
    } else if (se.intiface) {
        if (!se.intiface->PumpIncoming()) {
            dev::Status why;
            if (!se.intiface->TryReconnectOnce(&why)) {
                HandleLost(why.message.empty() ? "the Buttplug server closed the connection" : why.message);
                return;
            }
        }
        const dev::Status s = se.intiface->Ping();
        if (!s && !se.intiface->Connected()) {
            HandleLost(s.message);
            return;
        }
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
        dev::RunDiagnostics(*session_->device, session_->connect_latency_ms, options);
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
           battery == o.battery && battery_ok == o.battery_ok &&
           dev::ToJson(caps) == dev::ToJson(o.caps);
}

DeviceController::DeviceController(QObject* parent) : QObject(parent) {
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
    const char* key = kind == "intiface" ? kPrefIntifaceUrl
                      : kind == "tcode"  ? kPrefTCodeAddress
                      : kind == "tcode-serial" ? kPrefTCodeSerialPort
                                               : nullptr;
    if (!key) return {};
    try {
        samn::Settings settings;
        return Q(settings.get(key, "").get<std::string>());
    } catch (const std::exception&) {
        return {};
    }
}

void DeviceController::setVibration(double level) { worker_->QueueVibration(dev::Clamp01(level)); }
void DeviceController::setSuction(double level) { worker_->QueueSuction(dev::Clamp01(level)); }
void DeviceController::setPosition(double pos, int durationMs) {
    worker_->QueuePosition(dev::Clamp01(pos), std::max(0, durationMs));
}
void DeviceController::setAxis(const QString& axis, double pos, int durationMs) {
    worker_->QueueAxis(axis.toStdString(), dev::Clamp01(pos), std::max(0, durationMs));
}
void DeviceController::setAxes(const QVariantMap& positions, int durationMs) {
    for (auto it = positions.begin(); it != positions.end(); ++it)
        worker_->QueueAxis(it.key().toStdString(), dev::Clamp01(it.value().toDouble()), std::max(0, durationMs));
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

}  // namespace samn::ui
