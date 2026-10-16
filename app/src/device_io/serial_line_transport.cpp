#include "serial_line_transport.h"

#include <QSerialPort>
#include <QSerialPortInfo>
#include <algorithm>

#include "samn/device/tcode.h"

namespace samn::ui {

using samn::device::Bytes;
using samn::device::PeerInfo;
using samn::device::ReceiveResult;
using samn::device::Status;
using samn::device::WriteMode;

std::vector<std::string> SerialPorts() {
    std::vector<std::string> out;
    for (const QSerialPortInfo& info : QSerialPortInfo::availablePorts()) {
#ifdef Q_OS_WIN
        out.push_back(info.portName().toStdString());  // "COM3"
#else
        out.push_back(info.systemLocation().toStdString());  // "/dev/ttyUSB0"
#endif
    }
    std::sort(out.begin(), out.end());
    return out;
}

SerialLineTransport::SerialLineTransport(std::string port, int baud, QObject* parent)
    : QtTransport(parent),
      port_name_(std::move(port)),
      baud_(baud > 0 ? baud : samn::device::kTCodeSerialBaud) {}

SerialLineTransport::~SerialLineTransport() { SerialLineTransport::Disconnect(); }

Status SerialLineTransport::Connect() {
    Disconnect();
    broken_ = false;
    broken_reason_.clear();
    if (port_name_.empty()) return Fail("no serial port chosen");
    port_ = std::make_unique<QSerialPort>();
    port_->setPortName(QString::fromStdString(port_name_));
    port_->setBaudRate(baud_);
    port_->setDataBits(QSerialPort::Data8);
    port_->setParity(QSerialPort::NoParity);
    port_->setStopBits(QSerialPort::OneStop);
    port_->setFlowControl(QSerialPort::NoFlowControl);
    if (!port_->open(QIODevice::ReadWrite)) {
        const std::string why = port_->errorString().toStdString();
        const bool missing = port_->error() == QSerialPort::DeviceNotFoundError;
        port_.reset();
        return Fail(std::string(missing ? "no such serial port " : "cannot open serial port ") +
                    port_name_ + ": " + why);
    }
    QObject::connect(port_.get(), &QSerialPort::errorOccurred, port_.get(),
                     [this](QSerialPort::SerialPortError e) {
                         // Unplugged cable and the like: the link is dead.
                         if (e == QSerialPort::ResourceError || e == QSerialPort::DeviceNotFoundError ||
                             e == QSerialPort::PermissionError) {
                             broken_ = true;
                             broken_reason_ = port_ ? port_->errorString().toStdString() : "port error";
                             emit incoming();
                         }
                     });
    QObject::connect(port_.get(), &QSerialPort::readyRead, port_.get(), [this] {
        waiter_.Wake();
        emit incoming();
    });
    return Status::Ok();
}

void SerialLineTransport::Disconnect() {
    if (!port_) return;
    QObject::disconnect(port_.get(), nullptr, nullptr, nullptr);
    if (port_->isOpen()) port_->close();
    port_.reset();
}

bool SerialLineTransport::IsConnected() const { return port_ && port_->isOpen() && !broken_; }

Status SerialLineTransport::Send(const Bytes& data, WriteMode) {
    if (!IsConnected())
        return Fail("serial port " + port_name_ + " is not open" +
                    (broken_reason_.empty() ? "" : ": " + broken_reason_));
    const QByteArray out(reinterpret_cast<const char*>(data.data()), static_cast<int>(data.size()));
    if (port_->write(out) != out.size() || !port_->waitForBytesWritten(1000)) {
        broken_ = port_->error() != QSerialPort::NoError && port_->error() != QSerialPort::TimeoutError;
        return Fail("write to " + port_name_ + " failed: " + port_->errorString().toStdString());
    }
    return Status::Ok();
}

ReceiveResult SerialLineTransport::Receive(std::chrono::milliseconds timeout) {
    if (!IsConnected()) return ReceiveResult::Closed(broken_reason_.empty() ? "port closed" : broken_reason_);
    if (port_->bytesAvailable() == 0)
        waiter_.Until([this] { return !IsConnected() || port_->bytesAvailable() > 0; },
                      static_cast<int>(timeout.count()), [this] { return Cancelled(); });
    if (port_ && port_->bytesAvailable() > 0) {
        const QByteArray got = port_->readAll();
        return ReceiveResult::Message(Bytes(got.begin(), got.end()));
    }
    if (!IsConnected()) return ReceiveResult::Closed(broken_reason_.empty() ? "port closed" : broken_reason_);
    return ReceiveResult::Timeout();
}

PeerInfo SerialLineTransport::Peer() const {
    PeerInfo p;
    p.name = port_name_;
    p.address = "serial://" + port_name_;
    return p;
}

}  // namespace samn::ui
