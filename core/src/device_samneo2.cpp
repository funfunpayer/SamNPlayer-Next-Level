#include "samn/device/samneo2.h"

namespace samn::device {

SamNeo2Device::SamNeo2Device(Transport& transport, std::shared_ptr<const Protocol> protocol,
                             SteadyClock clock)
    : transport_(transport),
      protocol_(protocol ? std::move(protocol) : std::make_shared<SamNeo2Protocol>()),
      clock_(clock ? std::move(clock) : SteadyClock([] { return std::chrono::steady_clock::now(); })) {}

Status SamNeo2Device::Connect() {
    if (connected_) return Status::Error("samneo2: already connected — call Disconnect() first");
    Log(LogLevel::kInfo, "samneo2: connecting");
    Status s = transport_.Connect();
    if (!s) return s;

    // A fresh link starts from nothing. (0.7 kept the packets of the last
    // session: after a failed Stop on a dead link the same value was then
    // swallowed as "unchanged" and the device stayed silent.)
    last_vibration_.reset();
    last_suction_.reset();
    last_write_at_.reset();
    peer_ = transport_.Peer();
    battery_pct_ = 0;
    battery_ok_ = false;
    battery_checked_ = false;
    battery_at_ = {};
    connected_ = true;
    Log(LogLevel::kInfo, "samneo2: connected " + peer_.address);
    BatteryLevel();  // optional; a missing battery service is fine
    return Status::Ok();
}

Status SamNeo2Device::Disconnect() {
    Log(LogLevel::kInfo, "samneo2: disconnecting");
    const bool was_connected = connected_;
    battery_ok_ = false;
    battery_checked_ = false;
    if (was_connected) StopNow();  // best effort, like 0.7
    connected_ = false;
    if (transport_.IsConnected()) transport_.Disconnect();
    return Status::Ok();
}

Status SamNeo2Device::SetVibration(double intensity) {
    if (!connected_) return Status::Error("samneo2: not connected");
    return WriteChannel(protocol_->EncodeVibration(intensity), true);
}

Status SamNeo2Device::SetSuction(double intensity) {
    if (!connected_) return Status::Error("samneo2: not connected");
    return WriteChannel(protocol_->EncodeSuction(intensity), false);
}

Status SamNeo2Device::SetVibrationRaw(std::uint8_t speed) {
    if (!connected_) return Status::Error("samneo2: not connected");
    const auto* enc = dynamic_cast<const RawEncoder*>(protocol_.get());
    if (!enc) return Status::Error("device: this protocol does not support raw values");
    return WriteChannelAlways(enc->EncodeVibrationRaw(speed), true);
}

Status SamNeo2Device::SetSuctionRaw(std::uint8_t level) {
    if (!connected_) return Status::Error("samneo2: not connected");
    const auto* enc = dynamic_cast<const RawEncoder*>(protocol_.get());
    if (!enc) return Status::Error("device: this protocol does not support raw values");
    return WriteChannelAlways(enc->EncodeSuctionRaw(level), false);
}

// Stop always writes (also when the remembered state already was zero) and
// remembers both off packets: otherwise the next SetVibration with the old
// value would be swallowed as unchanged, and the keepalive would restart the
// device a few seconds after "Stop" (found with real values in 0.7).
Status SamNeo2Device::Stop() {
    if (!connected_) return Status::Ok();
    return StopNow();
}

Status SamNeo2Device::StopNow() {
    const Bytes vibration_off = protocol_->EncodeVibration(0);
    const Bytes suction_off = protocol_->EncodeSuction(0);
    Status s = WriteNow(vibration_off);
    if (!s) return s;
    last_vibration_ = vibration_off;
    s = WriteNow(suction_off);
    if (!s) return s;
    last_suction_ = suction_off;
    return Status::Ok();
}

Status SamNeo2Device::WriteChannel(const Bytes& packet, bool vibration) {
    const auto& previous = vibration ? last_vibration_ : last_suction_;
    // Unchanged: not sent, and the idle timer is NOT reset, otherwise a long
    // run of equal values would prevent the keepalive exactly when needed.
    if (previous && *previous == packet) return Status::Ok();
    Status s = WriteNow(packet);
    if (!s) return s;
    Remember(packet, vibration);
    return Status::Ok();
}

// Raw writes always go out and are remembered like Stop().
Status SamNeo2Device::WriteChannelAlways(const Bytes& packet, bool vibration) {
    Status s = WriteNow(packet);
    if (!s) return s;
    Remember(packet, vibration);
    return Status::Ok();
}

void SamNeo2Device::Remember(const Bytes& packet, bool vibration) {
    (vibration ? last_vibration_ : last_suction_) = packet;
}

Status SamNeo2Device::WriteNow(const Bytes& packet) {
    const WriteMode mode =
        protocol_->WriteWithResponse() ? WriteMode::kWithResponse : WriteMode::kWithoutResponse;
    Status s = transport_.Send(packet, mode);
    if (!s) {
        Log(LogLevel::kError, "samneo2: GATT write failed: " + s.message);
        return s;
    }
    last_write_at_ = clock_();
    return Status::Ok();
}

// A keepalive replay is not new content: last_write_at_ stays, so a stream of
// replays never resets the idle timer. BOTH channels are repeated, or the
// other one would fall silent.
bool SamNeo2Device::TickKeepalive() {
    if (!connected_) return false;
    if (!last_vibration_ && !last_suction_) return false;
    if (!last_write_at_ || clock_() - *last_write_at_ < kKeepaliveInterval) return false;
    const WriteMode mode =
        protocol_->WriteWithResponse() ? WriteMode::kWithResponse : WriteMode::kWithoutResponse;
    for (const auto* packet : {&last_vibration_, &last_suction_}) {
        if (!*packet) continue;
        (void)transport_.Send(**packet, mode);  // errors ignored, like 0.7
    }
    return true;
}

BatteryReading SamNeo2Device::BatteryLevel() {
    if (!connected_) return {};
    if (battery_checked_ && clock_() - battery_at_ < kBatteryCacheTtl)
        return {battery_pct_, battery_ok_};

    std::optional<int> pct;
    if (auto value = transport_.ReadCharacteristic(kBatteryServiceUuid, kBatteryLevelUuid))
        pct = BatteryFromGatt(*value);
    battery_pct_ = pct.value_or(0);
    battery_ok_ = pct.has_value();
    battery_checked_ = true;
    battery_at_ = clock_();
    if (battery_ok_) Log(LogLevel::kInfo, "samneo2: battery read " + std::to_string(battery_pct_));
    return {battery_pct_, battery_ok_};
}

Capabilities SamNeo2Device::DeviceCapabilities() const {
    // Cached battery state only: a capability query must not start a BLE read.
    return Neo2Capabilities(peer_.name, battery_checked_ && battery_ok_);
}

ConnectionInfo SamNeo2Device::Info() const {
    ConnectionInfo info;
    info.connected = connected_;
    info.name = peer_.name;
    info.address = peer_.address;
    info.rssi = peer_.rssi;
    info.battery_pct = battery_pct_;
    info.battery_ok = battery_ok_;
    return info;
}

}  // namespace samn::device
