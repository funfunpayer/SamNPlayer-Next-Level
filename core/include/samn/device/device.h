#pragma once

// The control interface every device implements, and the optional extras a
// device may add (Go device/device.go). Intensities are 0.0 (off) to 1.0
// (full). No I/O happens here: devices talk through a `Transport`
// (transport.h) that the app provides (Qt Bluetooth, serial, WebSocket).
//
// Threading: devices are NOT internally synchronised. The app calls one
// device from one thread (or serialises the calls itself); timers for
// keepalive / ping call the Tick / Ping methods from that same thread.

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace samn::device {

// Result of an operation that can fail (Go `error`). Empty message = ok.
struct Status {
    bool ok = true;
    std::string message;

    static Status Ok() { return {}; }
    static Status Error(std::string message) { return {false, std::move(message)}; }
    explicit operator bool() const { return ok; }
};

using Bytes = std::vector<std::uint8_t>;

// Optional diagnostics hook (Go `logging.*`); devices call it, never print.
enum class LogLevel { kDebug, kInfo, kWarn, kError };
using LogSink = std::function<void(LogLevel, const std::string&)>;

// What the app shows about the current connection (Go ConnectionInfo).
struct ConnectionInfo {
    bool connected = false;
    std::string name;
    std::string address;
    int rssi = 0;
    int battery_pct = 0;     // 0-100 when battery_ok
    bool battery_ok = false;  // false = device reports no battery / unreadable
};

struct BatteryReading {
    int percent = 0;  // 0-100
    bool ok = false;  // true when the device reported the level
};

// The minimal control interface the player needs.
class Device {
public:
    virtual ~Device() = default;
    virtual Status Connect() = 0;
    virtual Status Disconnect() = 0;
    virtual Status SetVibration(double intensity) = 0;
    virtual Status SetSuction(double intensity) = 0;
    // Both channels off at once (pause / abort).
    virtual Status Stop() = 0;
};

// A device that moves to a position (strokers: The Handy, OSR2 ...).
class LinearDevice {
public:
    virtual ~LinearDevice() = default;
    virtual bool HasLinear() const = 0;
    // pos 0 = bottom, 1 = top, reached within duration.
    virtual Status SetPosition(double pos, std::chrono::milliseconds duration) = 0;
};

// Extra axes besides the stroke (SR6 / OSR2 over TCode).
class MultiAxisDevice {
public:
    virtual ~MultiAxisDevice() = default;
    virtual bool HasAxis(const std::string& axis) const = 0;
    // pos 0-1, 0.5 = centre.
    virtual Status SetAxis(const std::string& axis, double pos,
                           std::chrono::milliseconds duration) = 0;
};

// Scales single motors of a multi-motor device on top of the channel level.
// channel is the feature's channel id ("vibration", "vibration2" ...).
class MotorLevelSetter {
public:
    virtual ~MotorLevelSetter() = default;
    virtual void SetMotorLevel(const std::string& channel, double level) = 0;
};

// Raw step values without quantisation (function test in the Devices tab).
class RawCapable {
public:
    virtual ~RawCapable() = default;
    virtual Status SetVibrationRaw(std::uint8_t speed) = 0;
    virtual Status SetSuctionRaw(std::uint8_t level) = 0;
};

// Devices that can read their battery.
class BatteryReader {
public:
    virtual ~BatteryReader() = default;
    virtual BatteryReading BatteryLevel() = 0;
};

}  // namespace samn::device
