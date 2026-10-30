#pragma once

// Buttplug / Intiface Central over WebSocket (Go device/intiface.go): the
// message exchange of the open Buttplug protocol v3, device adoption and
// channel mapping. Messages are JSON arrays with one object each; the Id is
// a running number. Key order and number format are those of Go's
// encoding/json (see go_json.h), so the bytes equal 0.7's.
//
// The WebSocket itself is a Transport (text messages). Because nothing here
// runs a thread, the app drives three things 0.7 ran in goroutines:
//   - PumpIncoming()  ("readLoop")  whenever the transport has data or every
//     few ms: reads the server's Ok / BatteryLevelReading / Error replies so
//     they never pile up, and notices a closed socket;
//   - Ping()          ("pingLoop")  every kIntifacePingInterval: Buttplug
//     servers stop all devices when the ping stays away;
//   - nothing else; reconnecting is automatic once per healthy connection.
// Driven (D2): every vibration motor, suction, rotation and the stroke
// position of the first device found. Not done: choosing a device by index.
// Sensors: battery only (BatteryLevelCmd), if listed.

#include <chrono>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "samn/device/battery.h"
#include "samn/device/capabilities.h"
#include "samn/device/device.h"
#include "samn/device/samneo2.h"  // SteadyClock
#include "samn/device/transport.h"

namespace samn::device {

inline constexpr const char* kIntifaceDefaultUrl = "ws://127.0.0.1:12345";
inline constexpr std::chrono::seconds kIntifaceTimeout{8};
inline constexpr std::chrono::seconds kIntifacePingInterval{2};
inline constexpr std::chrono::seconds kIntifaceBatteryWait{3};

// A user's input as a WebSocket address: "" -> ws://127.0.0.1:12345,
// "192.168.1.50" -> ws://192.168.1.50:12345, a full URL stays (trailing "/"
// and a missing port are fixed). Intiface Central usually runs on a phone,
// where people type just the IP.
std::string NormalizeIntifaceUrl(const std::string& input);

// The likely cause of a failed dial, from the error text and whether the
// server is local.
std::string IntifaceHint(const std::string& url, const std::string& error_text);

// Buttplug message text: `[{"Name":{...}}]` with the body's Id set.
std::string BuildButtplugMessage(const std::string& name, nlohmann::json body, int id);

class IntifaceDevice : public Device,
                       public LinearDevice,
                       public MotorLevelSetter,
                       public BatteryReader,
                       public CapabilityReporter {
public:
    // `transport` (a WebSocket to the URL) must outlive the device.
    // `url` is only shown (Info) and used in error texts; pass the normalised one.
    IntifaceDevice(Transport& transport, const std::string& url, SteadyClock clock = nullptr);

    // Handshake, device list, adoption of the first usable device (waits up
    // to kIntifaceTimeout per step; scans if none is connected yet).
    Status Connect() override;
    Status Disconnect() override;  // StopDeviceCmd, then close
    Status SetVibration(double intensity) override;  // all vibration motors, other scalars, RotateCmd
    Status SetSuction(double intensity) override;
    Status Stop() override;

    bool HasLinear() const override;
    Status SetPosition(double pos, std::chrono::milliseconds duration) override;
    void SetMotorLevel(const std::string& channel, double level) override;
    // Asks BatteryLevelCmd (cached kBatteryCacheTtl) and waits up to
    // kIntifaceBatteryWait for the reading; the server's Error reply ends the
    // wait. A failed read is cached as long as a reading.
    BatteryReading BatteryLevel() override;
    Capabilities DeviceCapabilities() const override;
    ConnectionInfo Info() const;

    // vibration / suction / battery as Buttplug lists them.
    struct Listed {
        bool vibration = false;
        bool suction = false;
        bool battery = false;
    };
    Listed ListedChannels() const;

    // Read everything the server has sent. Returns false once the socket is
    // found dead (the device is then marked disconnected).
    bool PumpIncoming();
    // Send a Ping; on a dead socket tries the one-shot reconnect.
    Status Ping();
    // One automatic Connect after a dead socket per healthy connection life.
    // True when connected afterwards. Cleared by a successful Connect.
    bool TryReconnectOnce(Status* why = nullptr);

    bool Connected() const { return connected_; }
    void SetLogSink(LogSink sink) { log_ = std::move(sink); }

private:
    Status Send(const std::string& name, nlohmann::json body, int* id = nullptr);
    void MarkDead(const std::string& reason);
    Status ReadUntil(const std::string& want, std::chrono::steady_clock::time_point deadline,
                     nlohmann::json* body);
    void AdoptDevice(const nlohmann::json& body);
    // Handles one received message batch; sets *battery when a reading came.
    bool HandleBatch(const nlohmann::json& batch, double* battery, bool* got_battery);
    Status Command(const std::function<bool(std::string*, nlohmann::json*)>& build);
    Status EnsureConnected();
    nlohmann::json ScalarsFor(const std::string& curve, double value) const;
    double MotorLevel(const std::string& channel) const;
    int LinearIndex() const;
    std::chrono::steady_clock::time_point Now() const { return clock_(); }
    void Log(LogLevel level, const std::string& text) const {
        if (log_) log_(level, text);
    }

    Transport& transport_;
    std::string url_;
    SteadyClock clock_;
    LogSink log_;

    int next_id_ = 0;
    int device_idx_ = 0;
    int vibrate_idx_ = -1;
    int constrict_idx_ = -1;
    // ActuatorType of the second channel as the device reports it
    // (Constrict / Oscillate / Inflate); Buttplug rejects a mismatch.
    std::string constrict_actuator_;
    std::vector<Feature> features_;
    std::map<std::string, double> motor_levels_;
    std::string device_name_;
    bool connected_ = false;
    bool has_battery_ = false;
    int battery_pct_ = 0;
    bool battery_ok_ = false;
    std::chrono::steady_clock::time_point battery_at_{};
    // When the last read failed (no reading, or the server refused it).
    std::optional<std::chrono::steady_clock::time_point> battery_failed_at_;
    bool reconnect_used_ = false;
    std::optional<double> pending_battery_;  // reading received, not yet taken
};

}  // namespace samn::device
