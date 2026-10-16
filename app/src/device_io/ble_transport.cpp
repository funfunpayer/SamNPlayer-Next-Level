#include "ble_transport.h"

#include <QBluetoothAddress>
#include <QBluetoothDeviceDiscoveryAgent>
#include <QBluetoothLocalDevice>
#include <QBluetoothUuid>
#include <QLowEnergyCharacteristic>
#include <QLowEnergyController>
#include <QLowEnergyService>

namespace samn::ui {

using samn::device::Bytes;
using samn::device::PeerInfo;
using samn::device::ReceiveResult;
using samn::device::Status;
using samn::device::WriteMode;

namespace {

QBluetoothUuid Uuid(const std::string& text) { return QBluetoothUuid(QString::fromStdString(text)); }

QString AddressOf(const QBluetoothDeviceInfo& info) {
    // macOS hides the hardware address and gives a per-host UUID instead.
    const QString address = info.address().toString();
    if (info.address().isNull()) return info.deviceUuid().toString(QUuid::WithoutBraces);
    return address;
}

}  // namespace

BleTransport::BleTransport(Options options, QObject* parent)
    : QtTransport(parent), options_(std::move(options)) {}

BleTransport::~BleTransport() { Teardown(); }

Status BleTransport::Scan() {
#ifdef Q_OS_LINUX
    // Qt asks BlueZ over D-Bus; without an adapter (a CI machine, a VM, a
    // desktop without Bluetooth) there is nothing to scan with.
    const QList<QBluetoothHostInfo> adapters = QBluetoothLocalDevice::allDevices();
    if (adapters.isEmpty())
        return Fail("Bluetooth: no Bluetooth adapter found (is the computer's Bluetooth built in, "
                    "enabled, and is the BlueZ service running?)");
    QBluetoothLocalDevice radio(adapters.first().address());
    if (radio.isValid() && radio.hostMode() == QBluetoothLocalDevice::HostPoweredOff)
        return Fail("Bluetooth: the Bluetooth adapter is switched off - turn it on and try again");
#endif
    QBluetoothDeviceDiscoveryAgent agent;
    agent.setLowEnergyDiscoveryTimeout(options_.scan_timeout_ms);
    bool have = false;
    bool finished = false;
    QString error_text;
    const QString prefix = QString::fromStdString(options_.name_prefix);
    auto consider = [&](const QBluetoothDeviceInfo& info) {
        if (!have && (info.coreConfigurations() & QBluetoothDeviceInfo::LowEnergyCoreConfiguration) &&
            info.name().startsWith(prefix)) {
            found_ = info;
            have = true;
            waiter_.Wake();
        }
    };
    QObject::connect(&agent, &QBluetoothDeviceDiscoveryAgent::deviceDiscovered, &agent, consider);
    QObject::connect(&agent, &QBluetoothDeviceDiscoveryAgent::deviceUpdated, &agent,
                     [&](const QBluetoothDeviceInfo& info, QBluetoothDeviceInfo::Fields) { consider(info); });
    QObject::connect(&agent, &QBluetoothDeviceDiscoveryAgent::finished, &agent, [&] {
        finished = true;
        waiter_.Wake();
    });
    QObject::connect(&agent, &QBluetoothDeviceDiscoveryAgent::canceled, &agent, [&] {
        finished = true;
        waiter_.Wake();
    });
    QObject::connect(&agent, &QBluetoothDeviceDiscoveryAgent::errorOccurred, &agent,
                     [&](QBluetoothDeviceDiscoveryAgent::Error) {
                         error_text = agent.errorString();
                         waiter_.Wake();
                     });
    agent.start(QBluetoothDeviceDiscoveryAgent::LowEnergyMethod);
    // The agent stops itself after the timeout; allow it a little slack.
    waiter_.Until([&] { return have || finished || !error_text.isEmpty(); },
                  options_.scan_timeout_ms + 2000, [this] { return Cancelled(); });
    if (agent.isActive()) agent.stop();
    if (have) return Status::Ok();
    if (Cancelled()) return Fail("Bluetooth: cancelled");
    if (!error_text.isEmpty())
        return Fail("Bluetooth: scan failed - " + error_text.toStdString() +
                    " (is a Bluetooth adapter present and switched on?)");
    return Fail("Bluetooth: no device with the name \"" + options_.name_prefix + "...\" found within " +
                std::to_string(options_.scan_timeout_ms / 1000) +
                " s - switch the device on and bring it close (it can only have one connection)");
}

Status BleTransport::Connect() {
    Teardown();
    closed_ = false;
    link_error_.clear();
    close_reason_.clear();
    Status scanned = Scan();
    if (!scanned) return scanned;

    controller_.reset(QLowEnergyController::createCentral(found_));
    QLowEnergyController* ctl = controller_.get();
    bool discovered = false;
    QObject::connect(ctl, &QLowEnergyController::connected, ctl, [this] {
        controller_connected_ = true;
        waiter_.Wake();
    });
    QObject::connect(ctl, &QLowEnergyController::disconnected, ctl, [this] {
        controller_connected_ = false;
        closed_ = true;
        if (close_reason_.empty()) close_reason_ = "Bluetooth: the device disconnected";
        waiter_.Wake();
        emit incoming();
    });
    QObject::connect(ctl, &QLowEnergyController::errorOccurred, ctl,
                     [this, ctl](QLowEnergyController::Error) {
                         link_error_ = ctl->errorString().toStdString();
                         if (close_reason_.empty()) close_reason_ = "Bluetooth: " + link_error_;
                         waiter_.Wake();
                     });
    QObject::connect(ctl, &QLowEnergyController::discoveryFinished, ctl, [&, this] {
        discovered = true;
        waiter_.Wake();
    });
    auto cancel = [this] { return Cancelled(); };
    auto failure = [&](const std::string& step) {
        std::string why = "Bluetooth: " + step;
        if (Cancelled())
            why = "Bluetooth: cancelled";
        else if (!link_error_.empty())
            why += " (" + link_error_ + ")";
        else
            why += " (timeout)";
        Teardown();
        return Fail(why);
    };

    ctl->connectToDevice();
    if (!waiter_.Until([&] { return controller_connected_ || !link_error_.empty(); },
                       options_.connect_timeout_ms, cancel) ||
        !controller_connected_)
        return failure("cannot connect to \"" + found_.name().toStdString() + "\"");

    ctl->discoverServices();
    if (!waiter_.Until([&] { return discovered || !link_error_.empty(); }, options_.connect_timeout_ms,
                       cancel) ||
        !discovered)
        return failure("service discovery failed");
    // Later events must not touch this function's locals.
    QObject::disconnect(ctl, &QLowEnergyController::discoveryFinished, nullptr, nullptr);

    service_ = ctl->createServiceObject(Uuid(options_.service_uuid), ctl);
    if (!service_)
        return failure("the device has no service " + options_.service_uuid +
                       " - it is not a Sam Neo 2, or it is paired with another app");
    services_[QString::fromStdString(options_.service_uuid).toLower()] = service_;
    if (!DiscoverDetails(service_, options_.connect_timeout_ms))
        return failure("cannot read the service of the device");
    if (!service_->characteristic(Uuid(options_.characteristic_uuid)).isValid())
        return failure("the service has no characteristic " + options_.characteristic_uuid);

    peer_.name = found_.name().toStdString();
    peer_.address = AddressOf(found_).toStdString();
    peer_.rssi = found_.rssi();
    return Status::Ok();
}

bool BleTransport::DiscoverDetails(QLowEnergyService* service, int timeout_ms) {
    if (service->state() == QLowEnergyService::RemoteServiceDiscovered) return true;
    bool error = false;
    QObject::connect(service, &QLowEnergyService::stateChanged, service,
                     [this](QLowEnergyService::ServiceState) { waiter_.Wake(); });
    QObject::connect(service, &QLowEnergyService::errorOccurred, service,
                     [this, &error](QLowEnergyService::ServiceError) {
                         error = true;
                         waiter_.Wake();
                     });
    service->discoverDetails();
    const bool ok = waiter_.Until(
        [&] { return error || service->state() == QLowEnergyService::RemoteServiceDiscovered; },
        timeout_ms, [this] { return Cancelled(); });
    QObject::disconnect(service, &QLowEnergyService::errorOccurred, nullptr, nullptr);
    return ok && !error;
}

void BleTransport::Teardown() {
    if (controller_) {
        QObject::disconnect(controller_.get(), nullptr, nullptr, nullptr);
        if (controller_->state() != QLowEnergyController::UnconnectedState) {
            controller_->disconnectFromDevice();
            bool done = false;
            QObject::connect(controller_.get(), &QLowEnergyController::stateChanged, controller_.get(),
                             [&](QLowEnergyController::ControllerState s) {
                                 if (s == QLowEnergyController::UnconnectedState) {
                                     done = true;
                                     waiter_.Wake();
                                 }
                             });
            waiter_.Until([&] { return done; }, 1500);
        }
    }
    services_.clear();
    service_ = nullptr;
    controller_.reset();
    controller_connected_ = false;
}

void BleTransport::Disconnect() {
    Teardown();
    closed_ = true;
}

bool BleTransport::IsConnected() const {
    return controller_ && controller_connected_ && !closed_ && service_ &&
           service_->state() == QLowEnergyService::RemoteServiceDiscovered;
}

Status BleTransport::Send(const Bytes& data, WriteMode mode) {
    if (!IsConnected())
        return Fail(close_reason_.empty() ? "Bluetooth: not connected" : close_reason_);
    const QLowEnergyCharacteristic ch = service_->characteristic(Uuid(options_.characteristic_uuid));
    if (!ch.isValid()) return Fail("Bluetooth: the characteristic disappeared");
    const QByteArray out(reinterpret_cast<const char*>(data.data()), static_cast<int>(data.size()));
    if (mode == WriteMode::kWithoutResponse) {
        service_->writeCharacteristic(ch, out, QLowEnergyService::WriteWithoutResponse);
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        return Status::Ok();
    }
    bool written = false;
    bool error = false;
    auto c1 = QObject::connect(service_, &QLowEnergyService::characteristicWritten, service_,
                               [&](const QLowEnergyCharacteristic&, const QByteArray&) {
                                   written = true;
                                   waiter_.Wake();
                               });
    auto c2 = QObject::connect(service_, &QLowEnergyService::errorOccurred, service_,
                               [&](QLowEnergyService::ServiceError) {
                                   error = true;
                                   waiter_.Wake();
                               });
    service_->writeCharacteristic(ch, out, QLowEnergyService::WriteWithResponse);
    const bool ok = waiter_.Until([&] { return written || error || !IsConnected(); },
                                  options_.write_timeout_ms, [this] { return Cancelled(); });
    QObject::disconnect(c1);
    QObject::disconnect(c2);
    if (written) return Status::Ok();
    if (!IsConnected())
        return Fail(close_reason_.empty() ? "Bluetooth: the device disconnected" : close_reason_);
    return Fail(error ? "Bluetooth: GATT write failed" : (ok ? "Bluetooth: GATT write failed"
                                                           : "Bluetooth: GATT write timeout"));
}

ReceiveResult BleTransport::Receive(std::chrono::milliseconds timeout) {
    // The Sam Neo 2 never answers; this only reports a lost link.
    if (IsConnected())
        waiter_.Until([this] { return !IsConnected(); }, static_cast<int>(timeout.count()),
                      [this] { return Cancelled(); });
    if (!IsConnected())
        return ReceiveResult::Closed(close_reason_.empty() ? "Bluetooth: not connected" : close_reason_);
    return ReceiveResult::Timeout();
}

std::optional<Bytes> BleTransport::ReadCharacteristic(const std::string& service_uuid,
                                                      const std::string& characteristic_uuid) {
    if (!IsConnected() || !controller_) return std::nullopt;
    const QString key = QString::fromStdString(service_uuid).toLower();
    QLowEnergyService* service = services_.value(key, nullptr);
    if (!service) {
        service = controller_->createServiceObject(Uuid(service_uuid), controller_.get());
        if (!service) return std::nullopt;  // the device has no such service
        services_[key] = service;
    }
    if (!DiscoverDetails(service, options_.connect_timeout_ms)) return std::nullopt;
    const QLowEnergyCharacteristic ch = service->characteristic(Uuid(characteristic_uuid));
    if (!ch.isValid() || !(ch.properties() & QLowEnergyCharacteristic::Read)) return std::nullopt;

    bool got = false;
    bool error = false;
    QByteArray value;
    auto c1 = QObject::connect(service, &QLowEnergyService::characteristicRead, service,
                               [&](const QLowEnergyCharacteristic& c, const QByteArray& v) {
                                   if (c.uuid() != ch.uuid()) return;
                                   value = v;
                                   got = true;
                                   waiter_.Wake();
                               });
    auto c2 = QObject::connect(service, &QLowEnergyService::errorOccurred, service,
                               [&](QLowEnergyService::ServiceError) {
                                   error = true;
                                   waiter_.Wake();
                               });
    service->readCharacteristic(ch);
    waiter_.Until([&] { return got || error || !IsConnected(); }, options_.write_timeout_ms,
                  [this] { return Cancelled(); });
    QObject::disconnect(c1);
    QObject::disconnect(c2);
    if (!got) return std::nullopt;
    return Bytes(value.begin(), value.end());
}

}  // namespace samn::ui
