#pragma once

// Battery helpers (Go device/battery.go). Reading the standard GATT battery
// service is the transport's job (Transport::ReadCharacteristic).

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

#include "samn/device/device.h"

namespace samn::device {

// A reading is reused this long so the 2 s status poll does not hammer the link.
inline constexpr std::chrono::seconds kBatteryCacheTtl{30};

// Standard GATT battery service / level characteristic (128-bit form).
inline constexpr const char* kBatteryServiceUuid = "0000180f-0000-1000-8000-00805f9b34fb";
inline constexpr const char* kBatteryLevelUuid = "00002a19-0000-1000-8000-00805f9b34fb";

// "Battery 73%"
std::string FormatBatteryPct(int pct);

// GATT level byte -> percent (values above 100 become 100); nullopt for an
// empty read.
std::optional<int> BatteryFromGatt(const Bytes& value);

// Buttplug BatteryLevelReading (0.0-1.0) -> percent, rounded, clamped 0-100.
int BatteryFromButtplug(double level);

}  // namespace samn::device
