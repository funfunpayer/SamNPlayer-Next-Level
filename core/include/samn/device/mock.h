#pragma once

// In-memory implementations for tests and for running the player without a
// device (Go device/mock.go).

#include <deque>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "samn/device/capabilities.h"
#include "samn/device/device.h"
#include "samn/device/transport.h"

namespace samn::device {

// The mock device: logs control commands instead of sending them. It is a
// full Device and reports the two Neo 2 channels, both driven. With
// `verbose` each command goes to the sink (default: stdout) in the 0.7 text
// ("[mock] vibration=0.50"); every command is also kept in `commands`.
class MockDevice : public Device, public RawCapable, public CapabilityReporter {
public:
    using Sink = std::function<void(const std::string&)>;

    explicit MockDevice(bool verbose = false, Sink sink = nullptr);

    Status Connect() override;
    Status Disconnect() override;
    Status SetVibration(double intensity) override;
    Status SetSuction(double intensity) override;
    Status SetVibrationRaw(std::uint8_t speed) override;
    Status SetSuctionRaw(std::uint8_t level) override;
    Status Stop() override;
    Capabilities DeviceCapabilities() const override;

    bool verbose;
    std::vector<std::string> commands;  // the logged lines, always recorded

private:
    void Log(const std::string& line);
    Sink sink_;
};

// A Transport that lives in memory. Records everything sent and replays what
// the test queues, so a device (or a scripted fake server) can run without
// hardware or network.
class MockTransport : public Transport {
public:
    struct Sent {
        Bytes data;
        WriteMode mode = WriteMode::kWithResponse;
    };

    Status Connect() override;
    void Disconnect() override;
    bool IsConnected() const override { return connected_; }
    Status Send(const Bytes& data, WriteMode mode) override;
    ReceiveResult Receive(std::chrono::milliseconds timeout) override;
    std::optional<Bytes> ReadCharacteristic(const std::string& service,
                                            const std::string& characteristic) override;
    PeerInfo Peer() const override { return peer; }

    // Queue an incoming message for Receive.
    void Push(const Bytes& data) { incoming_.push_back(data); }
    void PushText(const std::string& text) { incoming_.push_back(ToBytes(text)); }
    // The far end drops the link: Send fails, Receive reports closed.
    void Drop();

    // --- script / inspect ---
    std::string connect_error;  // non-empty: Connect fails with it
    std::string send_error;     // non-empty: Send fails with it (link stays "up")
    PeerInfo peer;
    // GATT reads by characteristic uuid; absent = not readable.
    std::map<std::string, Bytes> characteristics;
    // Called after every successful Send (a fake server queues its replies
    // here); and after every successful Connect.
    std::function<void(const Bytes&)> on_send;
    std::function<void()> on_connect;

    std::vector<Sent> sent;
    int connect_count = 0;
    int disconnect_count = 0;

    // Convenience: sent[i].data as text.
    std::string SentText(std::size_t i) const { return ToText(sent.at(i).data); }

private:
    bool connected_ = false;
    bool dropped_ = false;
    std::deque<Bytes> incoming_;
};

}  // namespace samn::device
