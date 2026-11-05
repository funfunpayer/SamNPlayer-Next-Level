#pragma once

// The device drivers of the kernel and their registry (D-163). A driver is
// one kind of device connection ("ble", "tcode", "tcode-serial", "intiface",
// "mock": the 0.7 transport names, also the value of the `device.transport`
// setting). It says which transport a connect needs (Resolve), runs a device
// session over that transport (Create) and names the likely cause of a
// failure (Hint).
//
// What a driver never does: build or own a transport, wait, run a timer or a
// thread. The app supplies the transports (Qt Bluetooth, serial port, UDP /
// TCP, WebSocket) from the TransportRequest; its device worker owns the
// transport's cancel, the connect and Stop deadlines and the per-owner
// session holds.
//
// Drivers are compiled in and registered by explicit calls
// (RegisterBuiltinDrivers); there are no runtime-loaded plugins, no plugin
// ABI and no self-registering static objects (the linker drops an object of
// a static library that nothing references).
//
// Threading: a registry is filled once and then only read, from any thread.
// A session is used like its device: from one thread only.

#include <memory>
#include <string>
#include <vector>

#include "samn/device/device.h"
#include "samn/device/samneo2.h"  // SteadyClock
#include "samn/device/transport.h"

namespace samn::device {

// The medium a driver talks over; the app builds one transport per kind.
enum class TransportKind {
    kNone,        // no transport (the mock device)
    kBle,         // Bluetooth LE GATT: scan by name prefix, one characteristic
    kSerialLine,  // text lines over a serial port
    kSocketLine,  // text lines over UDP or TCP
    kWebSocket,   // text messages over a WebSocket
};

// 0.7 testConnectTimeout: how long a whole connect may take.
inline constexpr int kDefaultConnectTimeoutMs = 20000;

// What a driver is, for the device choice and the worker.
struct DriverInfo {
    std::string id;    // the kind: "ble", "tcode", ...
    std::string name;  // the entry of the device choice
    std::string help;  // the note under the choice
    TransportKind transport = TransportKind::kNone;
    bool needs_address = false;   // the target is typed (an address or URL)
    std::string address_example;  // the address field's placeholder
    // The settings key that remembers the last target (0.7 names); "" = none.
    std::string target_setting;
    bool simulated = false;  // no real hardware
    int connect_timeout_ms = kDefaultConnectTimeoutMs;
};

// The transport a connect needs, as Resolve found it.
struct TransportRequest {
    TransportKind kind = TransportKind::kNone;
    // kSocketLine: host:port; kSerialLine: the port; kWebSocket: the URL.
    std::string target;
    std::string network;  // kSocketLine: "udp" or "tcp"
    // kBle: the scan's name prefix, the GATT service and characteristic.
    std::string ble_name_prefix;
    std::string service_uuid;
    std::string characteristic_uuid;
};

// What a session gets from the app besides its transport.
struct SessionOptions {
    LogSink log;        // the device's log lines; may be empty
    SteadyClock clock;  // empty: the steady clock (tests set their own)
};

// What a Pump did, so the worker knows what to look at next.
enum class PumpOutcome {
    kCheck,        // read what waited; the worker checks the link and the device shown
    kDone,         // the session watched its link itself; nothing more to do
    kReconnected,  // the session reconnected: show the device now
};

// One connected (or connecting) device of a driver. Owns the device; the
// transport is the app's and outlives the session.
class DeviceSession {
public:
    virtual ~DeviceSession() = default;

    // The device to connect, drive, stop and disconnect.
    virtual Device& GetDevice() = 0;
    // What the status shows. `read_battery`: read the battery first (the tick).
    virtual ConnectionInfo Info(bool read_battery) = 0;
    // Tells two devices of one name and address apart (Intiface: the adopted
    // device index); "" when name and address do.
    virtual std::string AdoptedId() const { return {}; }
    // The device and its link are still up.
    virtual bool Alive() const = 0;
    // The periodic work (every 2 s): keepalive, ping, a reconnect. A failed
    // Status: the link is lost, the message says why.
    virtual Status Tick() { return Status::Ok(); }
    // Reads what the peer sent so it never piles up (called when the
    // transport has data). A failed Status: the link is lost.
    virtual Status Pump(PumpOutcome* outcome);
    // The device takes raw step values (the function test).
    virtual bool HasRawValues() const { return false; }

protected:
    explicit DeviceSession(Transport* transport = nullptr) : transport_(transport) {}
    Transport* transport() const { return transport_; }

private:
    Transport* transport_;
};

class Driver {
public:
    virtual ~Driver() = default;
    virtual const DriverInfo& Info() const = 0;
    // The transport a connect to `target` (the user's text, trimmed) needs.
    // A failed Status: the target is not valid; the message is shown.
    virtual Status Resolve(const std::string& target, TransportRequest* out) const = 0;
    // A session over `transport`, built from Resolve's request and not
    // connected yet (null for TransportKind::kNone). Null when the driver
    // needs a transport and got none.
    virtual std::unique_ptr<DeviceSession> Create(Transport* transport, const std::string& target,
                                                  const SessionOptions& options) const = 0;
    // The likely cause of a failed connect or a lost link; `raw` is the
    // transport's error text (or the message). "" = nothing to add.
    virtual std::string Hint(const std::string& target, const std::string& raw) const;
};

// The drivers the kernel knows, in the order they were added (the order of
// the device choice).
class DriverRegistry {
public:
    // Refused (nothing added): no driver, an empty id or an id already listed.
    Status Add(std::unique_ptr<Driver> driver);
    // nullptr for an unknown id.
    const Driver* Find(const std::string& id) const;
    std::vector<DriverInfo> List() const;

private:
    std::vector<std::unique_ptr<Driver>> drivers_;
};

// Sam Neo 2 (BLE), TCode over Wi-Fi (UDP / TCP), TCode over USB serial,
// Intiface (Buttplug over WebSocket) and the mock device, in that order.
void RegisterBuiltinDrivers(DriverRegistry& registry);

}  // namespace samn::device
