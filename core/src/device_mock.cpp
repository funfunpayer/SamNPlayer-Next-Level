#include "samn/device/mock.h"

#include <cstdio>
#include <iostream>

namespace samn::device {

MockDevice::MockDevice(bool verbose_, Sink sink) : verbose(verbose_), sink_(std::move(sink)) {}

void MockDevice::Log(const std::string& line) {
    commands.push_back(line);
    if (!verbose) return;
    if (sink_) {
        sink_(line);
    } else {
        std::cout << line << '\n';
    }
}

// The texts are 0.7's (German), kept so logs stay comparable.
Status MockDevice::Connect() {
    Log("[mock] verbunden");
    return Status::Ok();
}

Status MockDevice::Disconnect() {
    Log("[mock] getrennt");
    return Status::Ok();
}

Status MockDevice::SetVibration(double intensity) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "[mock] vibration=%.2f", intensity);
    Log(buf);
    return Status::Ok();
}

Status MockDevice::SetSuction(double intensity) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "[mock] suction=%.2f", intensity);
    Log(buf);
    return Status::Ok();
}

Status MockDevice::SetVibrationRaw(std::uint8_t speed) {
    Log("[mock] vibration_raw=" + std::to_string(speed));
    return Status::Ok();
}

Status MockDevice::SetSuctionRaw(std::uint8_t level) {
    Log("[mock] suction_raw=" + std::to_string(level));
    return Status::Ok();
}

Status MockDevice::Stop() {
    Log("[mock] stop");
    return Status::Ok();
}

Capabilities MockDevice::DeviceCapabilities() const { return Neo2Capabilities("Mock device", false); }

Status MockTransport::Connect() {
    if (!connect_error.empty()) return Status::Error(connect_error);
    connected_ = true;
    dropped_ = false;
    ++connect_count;
    if (on_connect) on_connect();
    return Status::Ok();
}

void MockTransport::Disconnect() {
    if (connected_) ++disconnect_count;
    connected_ = false;
}

Status MockTransport::Send(const Bytes& data, WriteMode mode) {
    if (!connected_ || dropped_) return Status::Error("not connected");
    if (!send_error.empty()) return Status::Error(send_error);
    sent.push_back({data, mode});
    if (on_send) on_send(data);
    return Status::Ok();
}

ReceiveResult MockTransport::Receive(std::chrono::milliseconds) {
    if (!incoming_.empty()) {
        Bytes data = std::move(incoming_.front());
        incoming_.pop_front();
        return ReceiveResult::Message(std::move(data));
    }
    if (!connected_ || dropped_) return ReceiveResult::Closed("connection closed");
    return ReceiveResult::Timeout();
}

std::optional<Bytes> MockTransport::ReadCharacteristic(const std::string&,
                                                       const std::string& characteristic) {
    auto it = characteristics.find(characteristic);
    if (it == characteristics.end()) return std::nullopt;
    return it->second;
}

void MockTransport::Drop() { dropped_ = true; }

}  // namespace samn::device
