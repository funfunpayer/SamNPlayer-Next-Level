#pragma once

#include <QString>
#include <memory>
#include <string>
#include <vector>

#include "qt_transport.h"

class QSerialPort;

namespace samn::ui {

// TCode over a USB serial port: 115200 baud 8N1 (kTCodeSerialBaud), one text
// line per Send.
class SerialLineTransport : public QtTransport {
    Q_OBJECT
public:
    explicit SerialLineTransport(std::string port, int baud = 0, QObject* parent = nullptr);
    ~SerialLineTransport() override;

    samn::device::Status Connect() override;
    void Disconnect() override;
    bool IsConnected() const override;
    samn::device::Status Send(const samn::device::Bytes& data, samn::device::WriteMode mode) override;
    samn::device::ReceiveResult Receive(std::chrono::milliseconds timeout) override;
    samn::device::PeerInfo Peer() const override;

private:
    std::string port_name_;
    int baud_;
    std::unique_ptr<QSerialPort> port_;
    bool broken_ = false;
    std::string broken_reason_;
};

// The serial ports of this computer, sorted ("COM3", "/dev/ttyUSB0"), like 0.7.
std::vector<std::string> SerialPorts();

}  // namespace samn::ui
