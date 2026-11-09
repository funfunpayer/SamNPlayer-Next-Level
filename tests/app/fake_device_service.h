#pragma once

// A scripted DeviceService for tests: no worker thread, no transport. Every
// call is recorded in order; the state is set by the test and announced with
// statusChanged() like the real controller does.

#include <QRegularExpression>
#include <QStringList>
#include <QVariantMap>
#include <set>

#include "device_service.h"

class FakeDeviceService : public samn::ui::DeviceService {
public:
    // What happened, as "name arg arg" lines in call order.
    QStringList calls;
    QString kind_saved = QStringLiteral("mock");
    QString target_saved;
    // connectDevice(): connect at once (a mock device), or stay "connecting" / fail.
    bool connect_succeeds = true;

    // --- state set by the test ---
    void setConnected(bool on, const QStringList& axes = {}, bool vib = true, bool suc = true) {
        connected_ = on;
        connecting_ = false;
        axes_ = on ? axes : QStringList{};
        vib_ = on && vib;
        suc_ = on && suc;
        lin_ = on && !axes_.isEmpty();
        identity_ = on ? QStringLiteral("fake#%1").arg(generation_ + 1) : QString();
        ++generation_;
        emit statusChanged();
    }
    // Another device behind the same connection (the Intiface reconnect case).
    void swapDevice() {
        identity_ = QStringLiteral("fake#%1").arg(++generation_);
        emit statusChanged();
    }
    void setError(const QString& e) { error_ = e; connecting_ = false; emit statusChanged(); }
    void setBattery(int percent) { battery_ = percent; emit statusChanged(); }
    int count(const QString& prefix) const {
        int n = 0;
        for (const QString& c : calls) n += c.startsWith(prefix) ? 1 : 0;
        return n;
    }
    QStringList callsOf(const QString& prefix) const { return calls.filter(QRegularExpression("^" + prefix + "( |$)")); }

    // --- DeviceService ---
    bool connected() const override { return connected_; }
    bool connecting() const override { return connecting_; }
    QString name() const override { return connected_ ? QStringLiteral("Fake") : QString(); }
    QString lastError() const override { return error_; }
    int batteryPercent() const override { return battery_; }
    bool batteryOk() const override { return battery_ >= 0; }
    void connectDevice(const QString& kind, const QString& target = QString()) override {
        calls << QStringLiteral("connect %1 %2").arg(kind, target);
        // Like the real controller, the answer comes later from the event loop, never inside the call.
        if (connect_succeeds) {
            QMetaObject::invokeMethod(this, [this] { setConnected(true); }, Qt::QueuedConnection);
        } else {
            connecting_ = true;
            QMetaObject::invokeMethod(this, [this] { emit statusChanged(); }, Qt::QueuedConnection);
        }
    }
    void disconnectDevice() override {
        calls << QStringLiteral("disconnect");
        if (connected_ || connecting_) QMetaObject::invokeMethod(this, [this] { setConnected(false); }, Qt::QueuedConnection);
    }
    QString savedKind() const override { return kind_saved; }
    QString savedTarget(const QString&) const override { return target_saved; }
    QVariantList capabilities() const override {
        QVariantList out;
        if (vib_) out << QVariantMap{{"channel", "vibration"}};
        if (suc_) out << QVariantMap{{"channel", "suction"}};
        for (const QString& a : axes_) out << QVariantMap{{"channel", a}};
        return out;
    }
    QStringList axes() const override { return axes_; }
    bool capVibration() const override { return vib_; }
    bool capSuction() const override { return suc_; }
    bool capLinear() const override { return lin_; }
    QString deviceIdentity() const override { return identity_; }
    quint64 generation() const override { return generation_; }
    void setVibration(double level) override { calls << QStringLiteral("vibration %1").arg(level, 0, 'f', 2); }
    void setSuction(double level) override { calls << QStringLiteral("suction %1").arg(level, 0, 'f', 2); }
    void setPosition(double pos, int ms) override { calls << QStringLiteral("position %1 %2").arg(pos, 0, 'f', 2).arg(ms); }
    void setAxis(const QString& axis, double pos, int ms) override {
        calls << QStringLiteral("axis %1 %2 %3").arg(axis).arg(pos, 0, 'f', 2).arg(ms);
    }
    void stop() override { calls << QStringLiteral("stop"); }
    void emergencyStop() override { calls << QStringLiteral("emergencyStop"); }
    bool sessionActive() const override { return !holders_.empty(); }
    void setSessionActiveFor(const QObject* owner, bool active) override {
        calls << QStringLiteral("hold %1").arg(active ? 1 : 0);
        if (active) holders_.insert(owner);
        else holders_.erase(owner);
    }

private:
    bool connected_ = false, connecting_ = false, vib_ = false, suc_ = false, lin_ = false;
    QStringList axes_;
    QString identity_, error_;
    quint64 generation_ = 0;
    int battery_ = -1;
    std::set<const QObject*> holders_;
};
