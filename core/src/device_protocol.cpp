#include "samn/device/protocol.h"

#include <cmath>

namespace samn::device {

double Clamp01(double v) {
    if (!(v >= 0)) return 0;  // also NaN
    if (v > 1) return 1;
    return v;
}

std::uint8_t ScaleToRange(double intensity, int max) {
    return static_cast<std::uint8_t>(std::round(Clamp01(intensity) * static_cast<double>(max)));
}

Bytes SamNeo2Protocol::EncodeVibration(double intensity) const {
    return EncodeVibrationRaw(ScaleToRange(intensity, 10));
}

Bytes SamNeo2Protocol::EncodeSuction(double intensity) const {
    return EncodeSuctionRaw(ScaleToRange(intensity, 5));
}

// Vibrate uses the vibrate command with speed 0 (suction stops separately).
Bytes SamNeo2Protocol::EncodeStop() const { return EncodeVibration(0); }

Bytes SamNeo2Protocol::EncodeVibrationRaw(std::uint8_t speed) const {
    const std::uint8_t pattern = speed == 0 ? 0x00 : 0x05;
    return {0x55, 0x03, 0x00, 0x00, pattern, speed, 0x00};
}

Bytes SamNeo2Protocol::EncodeSuctionRaw(std::uint8_t level) const {
    const std::uint8_t flag = level == 0 ? 0x00 : 0x01;
    return {0x55, 0x09, 0x00, 0x00, flag, level, 0x00};
}

Bytes LegacySamProtocol::EncodeVibration(double intensity) const {
    const std::uint8_t speed = ScaleToRange(intensity, 10);
    const std::uint8_t pattern = speed == 0 ? 0 : 5;
    return {18, 1, 3, 0, pattern, speed};
}

// Range 0-1 only: effectively on / off.
Bytes LegacySamProtocol::EncodeSuction(double intensity) const {
    return {18, 6, 1, ScaleToRange(intensity, 1)};
}

Bytes LegacySamProtocol::EncodeStop() const { return EncodeVibration(0); }

}  // namespace samn::device
