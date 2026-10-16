#pragma once

// Byte encoding of the control commands (Go device/protocol.go). Only the
// bytes live here; sending them is the Transport's job.

#include <cstdint>
#include <string>

#include "samn/device/device.h"

namespace samn::device {

class Protocol {
public:
    virtual ~Protocol() = default;
    virtual std::string ServiceUuid() const = 0;
    virtual std::string CharacteristicUuid() const = 0;
    // GATT write-with-response (true) or without (false); device specific.
    virtual bool WriteWithResponse() const = 0;
    virtual Bytes EncodeVibration(double intensity) const = 0;  // 0.0 - 1.0
    virtual Bytes EncodeSuction(double intensity) const = 0;    // 0.0 - 1.0
    virtual Bytes EncodeStop() const = 0;
};

// Optional: raw step values straight into the packet (no quantisation).
class RawEncoder {
public:
    virtual ~RawEncoder() = default;
    virtual Bytes EncodeVibrationRaw(std::uint8_t speed) const = 0;
    virtual Bytes EncodeSuctionRaw(std::uint8_t level) const = 0;
};

// BLE protocol of the SVAKOM Sam Neo 2 / Sam Neo 2 Pro, from the Buttplug
// Rust server (svakom_sam2.rs, svakom-sam2.yml). Advertising name "Sam Neo 2"
// (also matches "... Pro"), service ffe0, TX characteristic ffe1, write WITH
// response. Both commands are 7 bytes to the same characteristic:
//   vibrate:   55 03 00 00 pattern speed 00   (speed 0-10, pattern 05 / 00)
//   constrict: 55 09 00 00 flag    level 00   (level 0-5,  flag 01 / 00)
class SamNeo2Protocol : public Protocol, public RawEncoder {
public:
    std::string ServiceUuid() const override { return "0000ffe0-0000-1000-8000-00805f9b34fb"; }
    std::string CharacteristicUuid() const override { return "0000ffe1-0000-1000-8000-00805f9b34fb"; }
    bool WriteWithResponse() const override { return true; }
    Bytes EncodeVibration(double intensity) const override;
    Bytes EncodeSuction(double intensity) const override;
    Bytes EncodeStop() const override;
    // The 0-10 / 0-5 ranges are Buttplug's step counts, not proven firmware
    // limits; the raw encoders exist to probe the real device.
    Bytes EncodeVibrationRaw(std::uint8_t speed) const override;
    Bytes EncodeSuctionRaw(std::uint8_t level) const override;
};

// The original Sam Neo (not Neo 2): other UUIDs, bytes and ranges. Reference
// only (svakom_sam.rs); the gen2 variant (pattern 04) is not modelled.
class LegacySamProtocol : public Protocol {
public:
    std::string ServiceUuid() const override { return "0000ae00-0000-1000-8000-00805f9b34fb"; }
    std::string CharacteristicUuid() const override { return "0000ae01-0000-1000-8000-00805f9b34fb"; }
    bool WriteWithResponse() const override { return false; }
    Bytes EncodeVibration(double intensity) const override;
    Bytes EncodeSuction(double intensity) const override;
    Bytes EncodeStop() const override;
};

// BLE advertising name prefix of the Sam Neo 2 scan (matches "Sam Neo 2 Pro",
// not the original "Sam Neo").
inline constexpr const char* kSamNeo2NamePrefix = "Sam Neo 2";

// 0.0-1.0 -> integer step 0..max, rounded half away from zero (not cut off:
// with ranges 0-10 / 0-5 that matters). NaN counts as 0.
std::uint8_t ScaleToRange(double intensity, int max);
double Clamp01(double v);  // NaN -> 0

}  // namespace samn::device
