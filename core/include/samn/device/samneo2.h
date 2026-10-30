#pragma once

// Sam Neo 2 over a BLE transport (Go device/samneo2.go): the channel
// bookkeeping, change detection and keepalive. Scanning, connecting and the
// GATT write are the Transport's job; the Transport must be built for
// protocol.ServiceUuid() / CharacteristicUuid() / kSamNeo2NamePrefix and
// honour WriteMode.
//
// What this class decides, so the Transport can stay dumb:
//   - a packet equal to the last one of its channel is not sent (the device has
//     whole steps only, and every write costs a round trip), and does not
//     reset the idle timer;
//   - Stop() always writes both channels off and remembers them, also when
//     a write fails (the first error is returned);
//   - raw writes always go out and are remembered like Stop();
//   - after kKeepaliveInterval without a new write, both channels' last
//     packets are repeated. The app calls TickKeepalive() every
//     kKeepaliveTickInterval while connected.

#include <chrono>
#include <functional>
#include <memory>
#include <optional>

#include "samn/device/battery.h"
#include "samn/device/capabilities.h"
#include "samn/device/device.h"
#include "samn/device/protocol.h"
#include "samn/device/transport.h"

namespace samn::device {

// Buttplug repeats the last command every 5 s (needed for iOS background
// use); 0.7 does it for the Sam Neo 2 after 4 s of silence, to be safe.
inline constexpr std::chrono::seconds kKeepaliveInterval{4};
inline constexpr std::chrono::seconds kKeepaliveTickInterval{2};  // interval / 2

using SteadyClock = std::function<std::chrono::steady_clock::time_point()>;

class SamNeo2Device : public Device,
                      public RawCapable,
                      public BatteryReader,
                      public CapabilityReporter {
public:
    // `transport` must outlive the device. `clock` is for tests.
    explicit SamNeo2Device(Transport& transport,
                           std::shared_ptr<const Protocol> protocol = nullptr,
                           SteadyClock clock = nullptr);

    Status Connect() override;
    Status Disconnect() override;
    Status SetVibration(double intensity) override;
    Status SetSuction(double intensity) override;
    Status Stop() override;
    Status SetVibrationRaw(std::uint8_t speed) override;
    Status SetSuctionRaw(std::uint8_t level) override;
    BatteryReading BatteryLevel() override;
    Capabilities DeviceCapabilities() const override;

    // Repeat both channels if idle for kKeepaliveInterval. Returns true when
    // packets were sent. No-op while not connected.
    bool TickKeepalive();

    ConnectionInfo Info() const;
    // true once a battery read was tried (success or "no battery service").
    bool BatteryProbed() const { return battery_checked_; }
    bool Connected() const { return connected_; }

    // The last packet sent per channel (what the keepalive would repeat).
    const std::optional<Bytes>& LastVibrationPacket() const { return last_vibration_; }
    const std::optional<Bytes>& LastSuctionPacket() const { return last_suction_; }

    void SetLogSink(LogSink sink) { log_ = std::move(sink); }

private:
    Status WriteChannel(const Bytes& packet, bool vibration);
    Status WriteChannelAlways(const Bytes& packet, bool vibration);
    Status WriteNow(const Bytes& packet);
    Status StopNow();
    void Remember(const Bytes& packet, bool vibration);
    void Log(LogLevel level, const std::string& text) const {
        if (log_) log_(level, text);
    }

    Transport& transport_;
    std::shared_ptr<const Protocol> protocol_;
    SteadyClock clock_;
    LogSink log_;

    bool connected_ = false;
    std::optional<std::chrono::steady_clock::time_point> last_write_at_;
    std::optional<Bytes> last_vibration_;
    std::optional<Bytes> last_suction_;

    PeerInfo peer_;
    int battery_pct_ = 0;
    bool battery_ok_ = false;
    bool battery_checked_ = false;
    std::chrono::steady_clock::time_point battery_at_{};
};

}  // namespace samn::device
