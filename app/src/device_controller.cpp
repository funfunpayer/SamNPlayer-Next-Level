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
#include "device_worker.h"
#include "samn/settings.h"

namespace samn::ui {

namespace dev = samn::device;

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
    : DeviceService(parent), drivers_(std::make_unique<dev::DriverRegistry>()) {
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
