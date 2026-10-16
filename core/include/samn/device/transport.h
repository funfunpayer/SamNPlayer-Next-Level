#pragma once

// What a device needs from the outside world. The C++ core has no Bluetooth,
// serial or WebSocket code: the app implements this interface once per
// medium (Qt Bluetooth GATT, serial port / UDP / TCP line writer, Qt
// WebSockets) and hands it to a device class. `MockTransport` (mock.h) is the
// in-memory implementation used by the tests.
//
// One Transport = one peer. Send / Receive carry whole messages:
//   - BLE GATT: one characteristic write per Send; `WriteMode` picks
//     write-with-response (Sam Neo 2 needs it) or without.
//   - TCode: one text line (with its "\n") per Send, UDP datagram, TCP
//     write or serial write; Receive is not used.
//   - Buttplug / Intiface: one WebSocket text message per Send / Receive.

#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "samn/device/device.h"

namespace samn::device {

enum class WriteMode { kWithResponse, kWithoutResponse };

// What the transport knows about the peer it connected to (BLE scan result).
struct PeerInfo {
    std::string name;
    std::string address;
    int rssi = 0;
};

struct ReceiveResult {
    enum class Kind { kMessage, kTimeout, kClosed };
    Kind kind = Kind::kTimeout;
    Bytes data;         // kMessage
    std::string error;  // kClosed: why (shown in logs / errors)

    static ReceiveResult Message(Bytes data) { return {Kind::kMessage, std::move(data), {}}; }
    static ReceiveResult Timeout() { return {Kind::kTimeout, {}, {}}; }
    static ReceiveResult Closed(std::string error) { return {Kind::kClosed, {}, std::move(error)}; }
};

class Transport {
public:
    virtual ~Transport() = default;

    // Open the link (scan + connect, dial, open the port). The message of a
    // failed Status is shown to the user; say what went wrong.
    virtual Status Connect() = 0;
    virtual void Disconnect() = 0;
    virtual bool IsConnected() const = 0;

    // Send one message. A failed Status means the link is dead.
    virtual Status Send(const Bytes& data, WriteMode mode = WriteMode::kWithResponse) = 0;

    // Wait up to `timeout` (0 = poll) for the next incoming message.
    virtual ReceiveResult Receive(std::chrono::milliseconds timeout) = 0;

    // BLE only: read one GATT characteristic (the standard battery level
    // 0x180F / 0x2A19). nullopt = not supported or not readable.
    virtual std::optional<Bytes> ReadCharacteristic(const std::string& /*service_uuid*/,
                                                    const std::string& /*characteristic_uuid*/) {
        return std::nullopt;
    }

    // Who we are connected to (valid after a successful Connect).
    virtual PeerInfo Peer() const { return {}; }
};

inline Bytes ToBytes(std::string_view text) { return Bytes(text.begin(), text.end()); }
inline std::string ToText(const Bytes& data) { return std::string(data.begin(), data.end()); }

}  // namespace samn::device
