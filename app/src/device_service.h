#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>

namespace samn::ui {

// What Player, Training and the PlaybackOutput need of the device connection
// (D-169; the battery, `generation` and `sessionActive` are for the UI and tests): connection state, the channels, the levels, Stop and the session
// holds. The one real implementation is DeviceController; tests use a fake
// (tests/app/fake_device_service.h). Everything is called on the GUI thread.
// No kernel logic here: the worker, the drivers and the transports stay
// behind DeviceController.
class DeviceService : public QObject {
    Q_OBJECT
public:
    explicit DeviceService(QObject* parent = nullptr) : QObject(parent) {}
    ~DeviceService() override = default;

    // --- connection ---
    virtual bool connected() const = 0;
    virtual bool connecting() const = 0;
    virtual QString name() const = 0;
    virtual QString lastError() const = 0;
    virtual int batteryPercent() const = 0;  // 0-100 when batteryOk, else -1
    virtual bool batteryOk() const = 0;
    virtual void connectDevice(const QString& kind, const QString& target = QString()) = 0;
    virtual void disconnectDevice() = 0;
    // The settings remembered from the last successful connect.
    virtual QString savedKind() const = 0;
    virtual QString savedTarget(const QString& kind) const = 0;

    // --- channels ---
    virtual QVariantList capabilities() const = 0;
    virtual QStringList axes() const = 0;
    virtual bool capVibration() const = 0;
    virtual bool capSuction() const = 0;
    virtual bool capLinear() const = 0;
    // Which device the connection drives; empty without one. Another value =
    // another device; `generation` counts the changes the GUI has seen.
    virtual QString deviceIdentity() const = 0;
    virtual quint64 generation() const = 0;

    // --- levels (0-1; positions 0 = bottom, 1 = top, duration in ms) ---
    virtual void setVibration(double level) = 0;
    virtual void setSuction(double level) = 0;
    virtual void setPosition(double pos, int durationMs) = 0;
    virtual void setAxis(const QString& axis, double pos, int durationMs) = 0;
    // Both channels off now.
    virtual void stop() = 0;
    // Pause / abort: drops queued levels, then stops. Silent when nothing is connected.
    virtual void emergencyStop() = 0;

    // --- session holds ---
    // Connecting, disconnecting and the test commands are refused while any
    // owner holds the session; `owner` starts or ends its own use.
    virtual bool sessionActive() const = 0;
    virtual void setSessionActiveFor(const QObject* owner, bool active) = 0;

    // Opens the mock device unless one is there (the Player's mock playback).
    void connectMock() { connectDevice(QStringLiteral("mock")); }

signals:
    // Anything of the connection snapshot changed (connect, loss, battery ...).
    void statusChanged();
};

}  // namespace samn::ui
