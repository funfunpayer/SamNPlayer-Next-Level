#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QThread>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>
#include <atomic>
#include <functional>
#include <memory>
#include <set>

#include "samn/device/capabilities.h"
#include "samn/device/diagnostics.h"

namespace samn::ui {

class DeviceWorker;
class QtTransport;

// The connection to the toy / stroker, ready for QML (0.7 app_device*.go).
//
// The core's devices are blocking and not thread-safe, so every device and
// transport lives on ONE worker thread owned by this controller. This class
// is the GUI-thread face: every Q_INVOKABLE returns at once and hands the work
// to the worker; results come back as properties and signals. Nothing here
// blocks the GUI thread.
//
// What runs on the worker: connect / disconnect, the levels, Stop, battery
// reads, diagnostics, the 2 s timer (Sam Neo 2 keepalive, Intiface ping) and
// the pumping of incoming Buttplug messages. Level commands are coalesced:
// when the worker lags, only the newest value of each channel is sent.
//
// Kinds (the 0.7 transport names): "ble" (Sam Neo 2), "tcode" (address
// "udp://host:port" / "tcp://...", default UDP :8000), "tcode-serial" (port),
// "intiface" (URL or bare IP), "mock".
class DeviceController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    // "disconnected" | "connecting" | "connected"
    Q_PROPERTY(QString state READ state NOTIFY statusChanged)
    Q_PROPERTY(bool connected READ connected NOTIFY statusChanged)
    Q_PROPERTY(bool connecting READ connecting NOTIFY statusChanged)
    Q_PROPERTY(QString kind READ kind NOTIFY statusChanged)
    Q_PROPERTY(bool mock READ isMock NOTIFY statusChanged)
    Q_PROPERTY(QString name READ name NOTIFY statusChanged)
    Q_PROPERTY(QString address READ address NOTIFY statusChanged)
    Q_PROPERTY(int rssi READ rssi NOTIFY statusChanged)
    // 0-100 when batteryOk, else -1 (show nothing, no placeholder).
    Q_PROPERTY(int batteryPercent READ batteryPercent NOTIFY statusChanged)
    Q_PROPERTY(bool batteryOk READ batteryOk NOTIFY statusChanged)
    // Every channel of the connected device: {channel, kind, index, actuator, driven}.
    Q_PROPERTY(QVariantList capabilities READ capabilities NOTIFY statusChanged)
    // Channel ids in device order, and the linear axes among them.
    Q_PROPERTY(QStringList channels READ channels NOTIFY statusChanged)
    Q_PROPERTY(QStringList axes READ axes NOTIFY statusChanged)
    Q_PROPERTY(bool capVibration READ capVibration NOTIFY statusChanged)
    Q_PROPERTY(bool capSuction READ capSuction NOTIFY statusChanged)
    Q_PROPERTY(bool capLinear READ capLinear NOTIFY statusChanged)
    Q_PROPERTY(bool capBattery READ capBattery NOTIFY statusChanged)
    Q_PROPERTY(bool capRaw READ capRaw NOTIFY statusChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY errorChanged)
    Q_PROPERTY(QString hint READ hint NOTIFY errorChanged)
    Q_PROPERTY(QStringList serialPorts READ serialPorts NOTIFY serialPortsChanged)
    Q_PROPERTY(bool diagnosticsRunning READ diagnosticsRunning NOTIFY diagnosticsRunningChanged)
    // true while playback / training uses the device: connecting, disconnecting
    // and the test commands are then refused with a message (0.7). Each user
    // holds the session on its own (setSessionActiveFor); it stays active
    // while any of them holds it.
    Q_PROPERTY(bool sessionActive READ sessionActive WRITE setSessionActive NOTIFY sessionActiveChanged)
public:
    explicit DeviceController(QObject* parent = nullptr);
    // Stops the device, closes the link and joins the worker thread.
    ~DeviceController() override;

    // --- connection ---
    // kind: see the class comment. target: address / port / URL (unused for ble, mock).
    Q_INVOKABLE void connectDevice(const QString& kind, const QString& target = QString());
    Q_INVOKABLE void connectSamNeo2() { connectDevice("ble"); }
    Q_INVOKABLE void connectTCode(const QString& address) { connectDevice("tcode", address); }
    Q_INVOKABLE void connectTCodeSerial(const QString& port) { connectDevice("tcode-serial", port); }
    Q_INVOKABLE void connectIntiface(const QString& url) { connectDevice("intiface", url); }
    Q_INVOKABLE void connectMock() { connectDevice("mock"); }
    // Stop, then close. Also aborts a connect in progress.
    Q_INVOKABLE void disconnectDevice();
    // The settings remembered from the last successful connect (0.7 prefs).
    Q_INVOKABLE QString savedKind() const;
    Q_INVOKABLE QString savedTarget(const QString& kind) const;

    // --- levels (0-1; positions 0 = bottom, 1 = top, duration in ms) ---
    Q_INVOKABLE void setVibration(double level);
    Q_INVOKABLE void setSuction(double level);
    Q_INVOKABLE void setPosition(double pos, int durationMs);
    Q_INVOKABLE void setAxis(const QString& axis, double pos, int durationMs);
    // axis name -> position (0-1), all with the same duration.
    Q_INVOKABLE void setAxes(const QVariantMap& positions, int durationMs);
    // Scales single motors on top of the channel level ({"vibration": 1, "vibration2": 0.5});
    // remembered for the next connect.
    Q_INVOKABLE void setMotorLevels(const QVariantMap& levels);
    // Raw step value 0-255 without quantisation ("vibration" | "suction").
    Q_INVOKABLE void testRawValue(const QString& channel, int value);
    // Both channels off now (rejected when no device is connected).
    Q_INVOKABLE void stop();
    // Pause / abort: also cancels a running diagnostics and drops queued
    // levels, then stops the device. Silent when nothing is connected.
    Q_INVOKABLE void emergencyStop();

    // --- info ---
    Q_INVOKABLE void refreshSerialPorts();
    // Re-read the battery and the connection now (the 2 s timer does it too).
    Q_INVOKABLE void refreshStatus();
    // The measurement series of docs/SAM_NEO_2_RESEARCH.md; entries stream
    // as diagnosticsEntry, the report arrives as diagnosticsFinished.
    Q_INVOKABLE void runDiagnostics();
    Q_INVOKABLE void cancelDiagnostics();

    // --- C++ knobs (tests) ---
    // Interval of the keepalive / ping / battery timer (default 2 s).
    void setTickIntervalMs(int ms);
    // How long a WebSocket / TCP dial may take (default 8 s) and how long the
    // Bluetooth scan runs (default 15 s, as 0.7).
    void setDialTimeoutMs(int ms);
    void setBleScanTimeoutMs(int ms);
    // Intervals and counts of the diagnostics run (sleep / cancel / on_entry are the controller's).
    void setDiagnosticsOptions(const samn::device::DiagnosticsOptions& options);
    // Builds the link of the "ble" kind instead of the Bluetooth one (a
    // stand-in Sam Neo 2). Called on the device thread; empty = Bluetooth.
    void setBleTransportFactory(std::function<std::unique_ptr<QtTransport>()> factory);

    // --- state (GUI thread) ---
    QString state() const;
    bool connected() const;
    bool connecting() const;
    QString kind() const;
    bool isMock() const;
    QString name() const;
    QString address() const;
    int rssi() const;
    int batteryPercent() const;
    bool batteryOk() const;
    QVariantList capabilities() const;
    QStringList channels() const;
    QStringList axes() const;
    bool capVibration() const;
    bool capSuction() const;
    bool capLinear() const;
    bool capBattery() const;
    bool capRaw() const;
    QString lastError() const { return last_error_; }
    QString hint() const { return hint_; }
    QStringList serialPorts() const { return serial_ports_; }
    bool diagnosticsRunning() const { return diagnostics_running_; }
    bool sessionActive() const { return !session_holders_.empty(); }
    // The property's writer: a hold of its own, apart from every owner's.
    void setSessionActive(bool active) { setSessionActiveFor(this, active); }
    // `owner` (the playback output, the training) starts or ends its use.
    void setSessionActiveFor(const QObject* owner, bool active);

    // What the GUI thread knows of the connection; filled by the worker.
    struct Snapshot {
        bool connected = false;
        bool connecting = false;
        bool mock = false;
        bool raw = false;
        QString kind, name, address;
        int rssi = 0;
        int battery = -1;
        bool battery_ok = false;
        samn::device::Capabilities caps;
        bool operator==(const Snapshot& o) const;
    };

signals:
    // Anything of the connection snapshot changed (connect, loss, battery ...).
    void statusChanged();
    // A failure to show: a connect that failed, a command the device refused,
    // a lost link. `hint` is the likely cause (Intiface / TCode), may be empty.
    void errorChanged();
    void serialPortsChanged();
    void diagnosticsRunningChanged();
    void sessionActiveChanged();
    void diagnosticsEntry(const QString& json);
    void diagnosticsFinished(const QString& json);
    // The devices' own log lines ("samneo2: connected ...").
    void logMessage(const QString& line);

private:
    friend class DeviceWorker;
    // Slots of the worker's results, called on the GUI thread.
    void applySnapshot(const Snapshot& snap);
    void applyError(const QString& message, const QString& hint);
    void applySerialPorts(const QStringList& ports);
    void applyDiagnosticsRunning(bool running);
    bool refuseDuringSession(const QString& what);
    void reportError(const QString& message, const QString& hint = QString());

    QThread thread_;
    DeviceWorker* worker_ = nullptr;
    Snapshot snap_;
    QString last_error_;
    QString hint_;
    QStringList serial_ports_;
    bool diagnostics_running_ = false;
    std::set<const QObject*> session_holders_;  // only compared, never dereferenced
    QVariantMap motor_levels_;
    // Set from the GUI thread, read by transports / diagnostics on the worker.
    std::atomic<bool> abort_{false};
    std::atomic<bool> diagnostics_cancel_{false};
};

}  // namespace samn::ui
