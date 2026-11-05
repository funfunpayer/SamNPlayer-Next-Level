#include "samn/device/driver.h"

#include <chrono>
#include <utility>

#include "samn/device/intiface.h"
#include "samn/device/mock.h"
#include "samn/device/protocol.h"
#include "samn/device/samneo2.h"
#include "samn/device/tcode.h"
#include "samn/text.h"

namespace samn::device {

Status DeviceSession::Pump(PumpOutcome* outcome) {
    *outcome = PumpOutcome::kCheck;
    // A TCode device may talk back; nothing needs it.
    if (transport_)
        while (transport_->IsConnected() &&
               transport_->Receive(std::chrono::milliseconds(0)).kind == ReceiveResult::Kind::kMessage) {
        }
    return Status::Ok();
}

std::string Driver::Hint(const std::string& /*target*/, const std::string& /*raw*/) const { return {}; }

Status DriverRegistry::Add(std::unique_ptr<Driver> driver) {
    if (!driver) return Status::Error("no driver");
    const std::string& id = driver->Info().id;
    if (id.empty()) return Status::Error("a driver needs an id");
    if (Find(id)) return Status::Error("a driver \"" + id + "\" is already registered");
    drivers_.push_back(std::move(driver));
    return Status::Ok();
}

const Driver* DriverRegistry::Find(const std::string& id) const {
    for (const auto& d : drivers_)
        if (d->Info().id == id) return d.get();
    return nullptr;
}

std::vector<DriverInfo> DriverRegistry::List() const {
    std::vector<DriverInfo> out;
    out.reserve(drivers_.size());
    for (const auto& d : drivers_) out.push_back(d->Info());
    return out;
}

namespace {

bool Contains(const std::string& text, const char* word) { return text.find(word) != std::string::npos; }

// --- Sam Neo 2 over Bluetooth LE ---

class SamNeo2Session final : public DeviceSession {
public:
    SamNeo2Session(Transport& transport, const SessionOptions& options)
        : DeviceSession(&transport), device_(transport, nullptr, options.clock) {
        device_.SetLogSink(options.log);
    }
    Device& GetDevice() override { return device_; }
    ConnectionInfo Info(bool read_battery) override {
        if (read_battery) device_.BatteryLevel();
        return device_.Info();
    }
    bool Alive() const override { return device_.Connected() && transport()->IsConnected(); }
    Status Tick() override {
        device_.TickKeepalive();
        return Status::Ok();
    }
    bool HasRawValues() const override { return true; }

private:
    SamNeo2Device device_;
};

class SamNeo2Driver final : public Driver {
public:
    SamNeo2Driver() {
        info_.id = "ble";
        info_.name = "Direct via Bluetooth (Sam Neo 2)";
        info_.help =
            "Searches up to 20 seconds for a device named “Sam Neo 2”. The device must be on and not "
            "connected to another app.";
        info_.transport = TransportKind::kBle;
    }
    const DriverInfo& Info() const override { return info_; }
    Status Resolve(const std::string& /*target*/, TransportRequest* out) const override {
        const SamNeo2Protocol protocol;
        *out = {};
        out->kind = TransportKind::kBle;
        out->ble_name_prefix = kSamNeo2NamePrefix;
        out->service_uuid = protocol.ServiceUuid();
        out->characteristic_uuid = protocol.CharacteristicUuid();
        return Status::Ok();
    }
    std::unique_ptr<DeviceSession> Create(Transport* transport, const std::string& /*target*/,
                                          const SessionOptions& options) const override {
        if (!transport) return nullptr;
        return std::make_unique<SamNeo2Session>(*transport, options);
    }

private:
    DriverInfo info_;
};

// --- TCode over UDP / TCP or a serial port ---

class TCodeSession final : public DeviceSession {
public:
    TCodeSession(Transport& transport, TCodeDevice device) : DeviceSession(&transport), device_(std::move(device)) {}
    Device& GetDevice() override { return device_; }
    ConnectionInfo Info(bool /*read_battery*/) override { return device_.Info(); }
    bool Alive() const override { return device_.Info().connected && transport()->IsConnected(); }

private:
    TCodeDevice device_;
};

class TCodeDriver final : public Driver {
public:
    explicit TCodeDriver(bool serial) : serial_(serial) {
        if (serial_) {
            info_.id = "tcode-serial";
            info_.name = "TCode over USB (OSR2 / SR6)";
            info_.help =
                "Sends TCode to an OSR2 / SR6 (or other TCode device) over its USB cable at 115200 baud. "
                "Choose its port. Close other programs that use the port (e.g. MultiFunPlayer) first.";
            info_.transport = TransportKind::kSerialLine;
            info_.target_setting = "device.tcodeSerialPort";
        } else {
            info_.id = "tcode";
            info_.name = "TCode over Wi-Fi (OSR2 / SR6)";
            info_.help =
                "Sends TCode to an OSR2 / SR6 (or other TCode device) running Wi-Fi firmware. Enter its IP "
                "address; plain addresses use UDP port 8000, or write tcp://host:port. UDP has no handshake: "
                "if the device does not move, check the address and firmware settings.";
            info_.transport = TransportKind::kSocketLine;
            info_.needs_address = true;
            info_.address_example = "e.g. 192.168.1.50 or tcp://osr.local:8000";
            info_.target_setting = "device.tcodeAddress";
        }
    }
    const DriverInfo& Info() const override { return info_; }
    Status Resolve(const std::string& target, TransportRequest* out) const override {
        *out = {};
        out->kind = info_.transport;
        if (serial_) {
            out->target = target;
            return Status::Ok();
        }
        TCodeAddress address;
        const Status s = ParseTCodeAddress(target, &address);
        if (!s) return s;
        out->network = address.network;
        out->target = address.hostport;
        return Status::Ok();
    }
    std::unique_ptr<DeviceSession> Create(Transport* transport, const std::string& target,
                                          const SessionOptions& /*options*/) const override {
        if (!transport) return nullptr;
        return std::make_unique<TCodeSession>(
            *transport, serial_ ? TCodeDevice::OverSerial(*transport, target) : TCodeDevice::OverNetwork(*transport, target));
    }
    std::string Hint(const std::string& /*target*/, const std::string& raw) const override {
        if (serial_) return {};
        const std::string t = AsciiLower(raw);
        if (Contains(t, "refused"))
            return "the device is reachable but nothing is listening on this port. Check the port "
                   "(default 8000) and that the TCode server of the firmware is running";
        if (Contains(t, "timeout") || Contains(t, "unreachable") || Contains(t, "no route"))
            return "no response. Are the device and the computer on the same network? "
                   "Is the IP address correct? Is a firewall blocking the port?";
        if (Contains(t, "no such host")) return "hostname could not be resolved — enter the IP address directly";
        return {};
    }

private:
    bool serial_;
    DriverInfo info_;
};

// --- Intiface Central (Buttplug over a WebSocket) ---

class IntifaceSession final : public DeviceSession {
public:
    IntifaceSession(Transport& transport, const std::string& url, const SessionOptions& options)
        : DeviceSession(&transport), device_(transport, url, options.clock) {
        device_.SetLogSink(options.log);
    }
    Device& GetDevice() override { return device_; }
    ConnectionInfo Info(bool read_battery) override {
        if (read_battery) device_.BatteryLevel();
        return device_.Info();
    }
    std::string AdoptedId() const override { return std::to_string(device_.AdoptedIndex()); }
    bool Alive() const override { return device_.Connected(); }
    // Incoming replies, the one-shot reconnect after a dead socket, then the
    // ping (Buttplug servers stop every device when it stays away).
    Status Tick() override {
        if (!device_.PumpIncoming()) {
            const Status back = Reconnect();
            if (!back) return back;
        }
        const Status s = device_.Ping();
        if (!s && !device_.Connected()) return s;
        return Status::Ok();
    }
    Status Pump(PumpOutcome* outcome) override {
        *outcome = PumpOutcome::kDone;
        if (device_.PumpIncoming()) return Status::Ok();
        const Status back = Reconnect();
        if (!back) return back;
        // It may have adopted another toy: the worker shows it at once.
        *outcome = PumpOutcome::kReconnected;
        return Status::Ok();
    }

private:
    // The socket was found dead: the one automatic reconnect, else why not.
    Status Reconnect() {
        Status why;
        if (device_.TryReconnectOnce(&why)) return Status::Ok();
        return Status::Error(why.message.empty() ? "the Buttplug server closed the connection" : why.message);
    }

    IntifaceDevice device_;
};

class IntifaceDriver final : public Driver {
public:
    IntifaceDriver() {
        info_.id = "intiface";
        info_.name = "Via Intiface Central";
        info_.help =
            "Connects via a running Buttplug server. Start Intiface Central, connect the device there and start "
            "the server. Leave the address empty for ws://127.0.0.1:12345.";
        info_.transport = TransportKind::kWebSocket;
        info_.needs_address = true;
        info_.address_example = "e.g. 192.168.1.50 or empty = this machine";
        info_.target_setting = "device.intifaceUrl";
        // The server may still be scanning (0.7: 30 s for Intiface).
        info_.connect_timeout_ms = 30000;
    }
    const DriverInfo& Info() const override { return info_; }
    Status Resolve(const std::string& target, TransportRequest* out) const override {
        *out = {};
        out->kind = TransportKind::kWebSocket;
        out->target = NormalizeIntifaceUrl(target);
        return Status::Ok();
    }
    std::unique_ptr<DeviceSession> Create(Transport* transport, const std::string& target,
                                          const SessionOptions& options) const override {
        if (!transport) return nullptr;
        return std::make_unique<IntifaceSession>(*transport, NormalizeIntifaceUrl(target), options);
    }
    // The 0.7 texts.
    std::string Hint(const std::string& target, const std::string& raw) const override {
        return IntifaceHint(NormalizeIntifaceUrl(target), raw);
    }

private:
    DriverInfo info_;
};

// --- The mock device (no hardware) ---

class MockSession final : public DeviceSession {
public:
    explicit MockSession(const SessionOptions& options)
        : device_(true, [log = options.log](const std::string& line) {
              if (log) log(LogLevel::kInfo, line);
          }) {}
    Device& GetDevice() override { return device_; }
    ConnectionInfo Info(bool /*read_battery*/) override {
        ConnectionInfo info;
        info.connected = true;
        info.name = "Mock device (no real hardware)";
        return info;
    }
    bool Alive() const override { return true; }
    bool HasRawValues() const override { return true; }

private:
    MockDevice device_;
};

class MockDriver final : public Driver {
public:
    MockDriver() {
        info_.id = "mock";
        info_.name = "Mock device (no hardware)";
        info_.help = "Simulates a device to test the UI without hardware.";
        info_.simulated = true;
    }
    const DriverInfo& Info() const override { return info_; }
    Status Resolve(const std::string& /*target*/, TransportRequest* out) const override {
        *out = {};
        return Status::Ok();
    }
    std::unique_ptr<DeviceSession> Create(Transport* /*transport*/, const std::string& /*target*/,
                                          const SessionOptions& options) const override {
        return std::make_unique<MockSession>(options);
    }

private:
    DriverInfo info_;
};

}  // namespace

void RegisterBuiltinDrivers(DriverRegistry& registry) {
    // The order of the device choice (0.7).
    (void)registry.Add(std::make_unique<SamNeo2Driver>());
    (void)registry.Add(std::make_unique<TCodeDriver>(false));
    (void)registry.Add(std::make_unique<TCodeDriver>(true));
    (void)registry.Add(std::make_unique<IntifaceDriver>());
    (void)registry.Add(std::make_unique<MockDriver>());
}

}  // namespace samn::device
