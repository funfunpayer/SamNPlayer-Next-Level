#pragma once

#include <QBluetoothDeviceInfo>
#include <QMap>
#include <QString>
#include <memory>

#include "qt_transport.h"

class QLowEnergyController;
class QLowEnergyService;

namespace samn::ui {

// Bluetooth LE central for the Sam Neo 2 (Qt Bluetooth): scans for an
// advertising name starting with `name_prefix`, connects, discovers the
// service and writes the packets of Send() to one characteristic.
// ReadCharacteristic reads other services (battery 0x180F / 0x2A19).
//
// Every failure says "Bluetooth ..." in its message and every wait is bounded
// (scan timeout, connect timeout, write timeout), so a missing adapter, a
// switched-off radio or an absent device returns an error instead of hanging.
class BleTransport : public QtTransport {
    Q_OBJECT
public:
    struct Options {
        std::string name_prefix;      // kSamNeo2NamePrefix
        std::string service_uuid;     // protocol.ServiceUuid()
        std::string characteristic_uuid;
        int scan_timeout_ms = 15000;  // 0.7 SamNeo2.ScanTimeout
        int connect_timeout_ms = 10000;  // connect + service discovery
        int write_timeout_ms = 3000;
    };
    explicit BleTransport(Options options, QObject* parent = nullptr);
    ~BleTransport() override;

    samn::device::Status Connect() override;
    void Disconnect() override;
    bool IsConnected() const override;
    samn::device::Status Send(const samn::device::Bytes& data, samn::device::WriteMode mode) override;
    samn::device::ReceiveResult Receive(std::chrono::milliseconds timeout) override;
    std::optional<samn::device::Bytes> ReadCharacteristic(const std::string& service_uuid,
                                                          const std::string& characteristic_uuid) override;
    samn::device::PeerInfo Peer() const override { return peer_; }

private:
    // Scan phase; fills found_ or returns the reason.
    samn::device::Status Scan();
    // Discover the details of a service object; false on error / timeout.
    bool DiscoverDetails(QLowEnergyService* service, int timeout_ms);
    void Teardown();

    Options options_;
    std::unique_ptr<QLowEnergyController> controller_;
    QBluetoothDeviceInfo found_;
    // Services by lower-case uuid; owned by controller_ (deleted with it).
    QMap<QString, QLowEnergyService*> services_;
    QLowEnergyService* service_ = nullptr;  // the control service
    bool controller_connected_ = false;
    bool closed_ = false;
    std::string link_error_;
    std::string close_reason_;
    samn::device::PeerInfo peer_;
};

}  // namespace samn::ui
