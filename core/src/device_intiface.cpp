#include "samn/device/intiface.h"

#include <algorithm>
#include <cctype>
#include <optional>

#include "samn/device/go_json.h"
#include "samn/device/protocol.h"

namespace samn::device {

using nlohmann::json;

namespace {

std::string Trim(const std::string& s) {
    const char* ws = " \t\r\n\v\f";
    const std::size_t b = s.find_first_not_of(ws);
    if (b == std::string::npos) return "";
    const std::size_t e = s.find_last_not_of(ws);
    return s.substr(b, e - b + 1);
}

std::string Lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool Contains(const std::string& s, const char* needle) { return s.find(needle) != std::string::npos; }

// Go's %v for the ErrorMessage of a server Error.
std::string DisplayValue(const json& v) {
    if (v.is_string()) return v.get<std::string>();
    return GoJsonDump(v);
}

}  // namespace

std::string NormalizeIntifaceUrl(const std::string& input) {
    std::string value = Trim(input);
    if (value.empty()) return kIntifaceDefaultUrl;
    if (value.find("://") == std::string::npos) value = "ws://" + value;
    if (!value.empty() && value.back() == '/') value.pop_back();
    // Add the port when none was given: look for a colon AFTER the scheme.
    const std::string rest = value.substr(value.find("://") + 3);
    if (rest.find(':') == std::string::npos) value += ":12345";
    return value;
}

std::string IntifaceHint(const std::string& url, const std::string& error_text) {
    const bool local = Contains(url, "127.0.0.1") || Contains(url, "localhost");
    const std::string text = Lower(error_text);

    if (Contains(text, "refused")) {
        if (local)
            return "the host is reachable but nothing is listening on this port. "
                   "Start the server in Intiface Central";
        return "the device is reachable but nothing is listening on this port. "
               "Start the server in Intiface Central and check it is exposed on the network "
               "(not only on the device itself)";
    }
    if (Contains(text, "timeout") || Contains(text, "deadline") || Contains(text, "no route") ||
        Contains(text, "unreachable")) {
        if (local) return "no response — is Intiface Central running?";
        return "no response. Are the phone and computer on the same Wi-Fi? "
               "Is the IP address correct? Is a firewall blocking the port?";
    }
    if (Contains(text, "no such host"))
        return "hostname could not be resolved — enter the IP address directly";
    if (local) return "is Intiface Central running with the server started?";
    return "check the address and network connection";
}

std::string BuildButtplugMessage(const std::string& name, json body, int id) {
    body["Id"] = id;
    json message = json::object();
    message[name] = std::move(body);
    json batch = json::array();
    batch.push_back(std::move(message));
    return GoJsonDump(batch);
}

IntifaceDevice::IntifaceDevice(Transport& transport, const std::string& url, SteadyClock clock)
    : transport_(transport),
      url_(url),
      clock_(clock ? std::move(clock) : SteadyClock([] { return std::chrono::steady_clock::now(); })) {}

Status IntifaceDevice::Send(const std::string& name, json body, int* id_out) {
    if (!transport_.IsConnected()) return Status::Error("intiface: not connected");
    const int id = ++next_id_;
    if (id_out) *id_out = id;
    Status s = transport_.Send(ToBytes(BuildButtplugMessage(name, std::move(body), id)),
                               WriteMode::kWithoutResponse);
    if (!s) {
        // Drop dead sockets so Info() / the GUI stop showing "connected"
        // while every write fails.
        MarkDead(s.message);
        return s;
    }
    return Status::Ok();
}

void IntifaceDevice::MarkDead(const std::string& reason) {
    const bool was = connected_ || transport_.IsConnected();
    if (transport_.IsConnected()) transport_.Disconnect();
    connected_ = false;
    if (was && !reason.empty()) Log(LogLevel::kWarn, "intiface: connection lost: " + reason);
}

Status IntifaceDevice::ReadUntil(const std::string& want, std::chrono::steady_clock::time_point deadline,
                                 json* body_out) {
    if (!transport_.IsConnected()) return Status::Error("intiface: not connected");
    while (Now() < deadline) {
        if (!transport_.IsConnected()) return Status::Error("intiface: not connected");
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Now());
        ReceiveResult r = transport_.Receive(remaining);
        if (r.kind == ReceiveResult::Kind::kTimeout)
            return Status::Error("intiface: response not readable: i/o timeout");
        if (r.kind == ReceiveResult::Kind::kClosed)
            return Status::Error("intiface: response not readable: " + r.error);
        json batch = json::parse(ToText(r.data), nullptr, false);
        if (!batch.is_array())
            return Status::Error("intiface: response not readable: invalid message");
        for (const auto& message : batch) {
            if (!message.is_object()) continue;
            for (const auto& item : message.items()) {
                const std::string& name = item.key();
                const json body = item.value().is_object() ? item.value() : json::object();
                // The server sends DeviceAdded unasked; dropping it would
                // mean missing the connected device.
                if (name == "DeviceAdded") AdoptDevice(body);
                if (name == "Error") {
                    const auto it = body.find("ErrorMessage");
                    return Status::Error("intiface: server reported error: " +
                                         (it == body.end() ? std::string("<nil>") : DisplayValue(*it)));
                }
                if (name == want) {
                    if (body_out) *body_out = body;
                    return Status::Ok();
                }
            }
        }
    }
    return Status::Error("intiface: no response of type \"" + want + "\" within timeout");
}

// Adopts the first device that lists a usable channel.
void IntifaceDevice::AdoptDevice(const json& body) {
    if (!device_name_.empty()) return;  // one already adopted
    const auto idx_it = body.find("DeviceIndex");
    if (idx_it == body.end() || !idx_it->is_number()) return;
    const double index = idx_it->get<double>();
    std::string name;
    if (auto it = body.find("DeviceName"); it != body.end() && it->is_string()) name = it->get<std::string>();

    json messages = json::object();
    if (auto it = body.find("DeviceMessages"); it != body.end() && it->is_object()) messages = *it;
    if (auto it = messages.find("ScalarCmd"); it != messages.end() && it->is_array()) {
        int idx = 0;
        for (const auto& entry : *it) {
            const int this_idx = idx++;
            std::string actuator;
            if (entry.is_object()) {
                auto a = entry.find("ActuatorType");
                if (a != entry.end() && a->is_string()) actuator = a->get<std::string>();
            }
            if (actuator == "Vibrate") {
                if (vibrate_idx_ < 0) vibrate_idx_ = this_idx;
            } else if (actuator == "Constrict" || actuator == "Oscillate" || actuator == "Inflate") {
                // Suction is called differently per device; Oscillate and
                // Inflate are the nearest match when Constrict is missing.
                if (constrict_idx_ < 0) {
                    constrict_idx_ = this_idx;
                    constrict_actuator_ = actuator;
                }
            }
        }
    }
    std::vector<Feature> features = ButtplugFeatures(messages, constrict_idx_);
    if (features.empty()) {
        Log(LogLevel::kWarn, "intiface: device skipped (no usable channels): " + name);
        return;
    }
    // Strokers (The Handy: LinearCmd only) are adopted too.
    features_ = std::move(features);
    if (messages.contains("BatteryLevelCmd")) has_battery_ = true;
    device_idx_ = static_cast<int>(index);
    device_name_ = name;
    Log(LogLevel::kInfo, "intiface: device adopted: " + name);
}

Status IntifaceDevice::Connect() {
    if (transport_.IsConnected()) transport_.Disconnect();
    Status dial = transport_.Connect();
    if (!dial)
        return Status::Error("intiface: no connection to " + url_ + " - " + IntifaceHint(url_, dial.message) +
                             " (" + dial.message + ")");
    next_id_ = 0;
    device_name_.clear();
    vibrate_idx_ = constrict_idx_ = -1;
    constrict_actuator_.clear();
    features_.clear();
    has_battery_ = false;
    battery_ok_ = false;
    battery_pct_ = 0;
    battery_at_ = {};
    pending_battery_.reset();
    connected_ = false;

    const auto deadline = Now() + kIntifaceTimeout;
    Status s = Send("RequestServerInfo", {{"ClientName", "SamNPlayer"}, {"MessageVersion", 3}});
    if (!s) {
        transport_.Disconnect();
        return Status::Error("intiface: handshake failed: " + s.message);
    }
    json info;
    s = ReadUntil("ServerInfo", deadline, &info);
    if (!s) {
        transport_.Disconnect();
        return s;
    }
    Log(LogLevel::kInfo, "intiface: connected");

    // Ask for devices already connected; without it only a device that
    // arrives DURING our session would be found.
    s = Send("RequestDeviceList", json::object());
    if (!s) {
        transport_.Disconnect();
        return s;
    }
    json list;
    if (ReadUntil("DeviceList", deadline, &list)) {
        if (auto it = list.find("Devices"); it != list.end() && it->is_array())
            for (const auto& entry : *it)
                if (entry.is_object()) AdoptDevice(entry);
    }

    if (device_name_.empty()) {
        // Wait briefly for a device that is just being paired.
        if (Send("StartScanning", json::object())) {
            (void)ReadUntil("DeviceAdded", Now() + kIntifaceTimeout, nullptr);
            (void)Send("StopScanning", json::object());
        }
    }
    if (device_name_.empty()) {
        transport_.Disconnect();
        return Status::Error(
            "intiface: no usable device found — connect the device in Intiface Central and try again");
    }

    connected_ = true;
    reconnect_used_ = false;
    return Status::Ok();
}

bool IntifaceDevice::HandleBatch(const json& batch, double* battery, bool* got_battery) {
    if (!batch.is_array()) return false;
    for (const auto& message : batch) {
        if (!message.is_object()) continue;
        for (const auto& item : message.items()) {
            const std::string& name = item.key();
            if (name == "BatteryLevelReading") {
                const auto& body = item.value();
                if (body.is_object()) {
                    auto it = body.find("BatteryLevel");
                    if (it != body.end() && it->is_number() && !*got_battery) {
                        *battery = it->get<double>();
                        *got_battery = true;
                    }
                }
            } else if (name == "Error") {
                Log(LogLevel::kWarn, "intiface: server error: " + GoJsonDump(item.value()));
            } else if (name == "DeviceRemoved") {
                Log(LogLevel::kWarn, "intiface: device removed: " + GoJsonDump(item.value()));
            }
        }
    }
    return true;
}

bool IntifaceDevice::PumpIncoming() {
    for (;;) {
        if (!transport_.IsConnected()) return connected_;
        ReceiveResult r = transport_.Receive(std::chrono::milliseconds(0));
        if (r.kind == ReceiveResult::Kind::kTimeout) return true;
        if (r.kind == ReceiveResult::Kind::kClosed) {
            MarkDead("read: " + r.error);
            return false;
        }
        json batch = json::parse(ToText(r.data), nullptr, false);
        double reading = 0;
        bool got = false;
        if (!HandleBatch(batch, &reading, &got)) {
            MarkDead("read: invalid message");
            return false;
        }
        if (got && !pending_battery_) pending_battery_ = reading;
    }
}

bool IntifaceDevice::TryReconnectOnce(Status* why) {
    if (connected_ && transport_.IsConnected()) return true;
    if (reconnect_used_) {
        if (why) *why = Status::Error("intiface: one-shot reconnect already used");
        return false;
    }
    reconnect_used_ = true;
    Status s = Connect();
    if (!s) {
        Log(LogLevel::kWarn, "intiface: one-shot reconnect failed: " + s.message);
        if (why) *why = s;
        return false;
    }
    Log(LogLevel::kInfo, "intiface: one-shot reconnect ok");
    return true;
}

Status IntifaceDevice::EnsureConnected() {
    if (connected_ && transport_.IsConnected()) return Status::Ok();
    Status why;
    if (TryReconnectOnce(&why) && connected_ && transport_.IsConnected()) return Status::Ok();
    if (!why) return Status::Error("intiface: not connected (" + why.message + ")");
    return Status::Error("intiface: not connected");
}

Status IntifaceDevice::Ping() {
    if (!transport_.IsConnected()) return Status::Ok();
    Status s = Send("Ping", json::object());
    if (s) return s;
    // Send marked the socket dead; one-shot reconnect, like 0.7's pingLoop.
    return TryReconnectOnce() ? Status::Ok() : s;
}

// Sends one message built by `build` (called when needed, so it sees the
// current device index); on a dead socket it reconnects once and retries.
// build returning false = nothing to send for this device.
Status IntifaceDevice::Command(const std::function<bool(std::string*, json*)>& build) {
    Status s = EnsureConnected();
    if (!s) return s;
    std::string name;
    json body;
    if (!build(&name, &body)) return Status::Ok();
    s = Send(name, body);
    if (s) return s;
    // Dead socket: one-shot reconnect + single retry.
    if (!TryReconnectOnce()) return s;
    if (!connected_ || !transport_.IsConnected()) return s;
    if (!build(&name, &body)) return Status::Ok();
    return Send(name, body);
}

double IntifaceDevice::MotorLevel(const std::string& channel) const {
    auto it = motor_levels_.find(channel);
    return it == motor_levels_.end() ? 1.0 : it->second;
}

void IntifaceDevice::SetMotorLevel(const std::string& channel, double level) {
    motor_levels_[channel] = Clamp01(level);
}

// "vibration" drives every driven scalar except suction, "suction" the
// suction feature only.
json IntifaceDevice::ScalarsFor(const std::string& curve, double value) const {
    json out = json::array();
    for (const auto& f : features_) {
        if (f.kind != kKindScalar || !f.driven) continue;
        const bool is_suction = f.index == constrict_idx_;
        if ((curve == kChSuction) != is_suction) continue;
        out.push_back({{"Index", f.index},
                       {"Scalar", value * MotorLevel(f.channel)},
                       {"ActuatorType", f.actuator}});
    }
    return out;
}

Status IntifaceDevice::SetVibration(double intensity) {
    const double v = Clamp01(intensity);
    Status s = Command([&](std::string* name, json* body) {
        json scalars = ScalarsFor(kChVibration, v);
        if (scalars.empty()) return false;
        *name = "ScalarCmd";
        *body = {{"DeviceIndex", device_idx_}, {"Scalars", std::move(scalars)}};
        return true;
    });
    if (!s) return s;
    return Command([&](std::string* name, json* body) {
        json rotations = json::array();
        for (const auto& f : features_)
            if (f.kind == kKindRotate && f.driven)
                rotations.push_back({{"Index", f.index},
                                     {"Speed", v * MotorLevel(f.channel)},
                                     {"Clockwise", true}});
        if (rotations.empty()) return false;
        *name = "RotateCmd";
        *body = {{"DeviceIndex", device_idx_}, {"Rotations", std::move(rotations)}};
        return true;
    });
}

Status IntifaceDevice::SetSuction(double intensity) {
    const double v = Clamp01(intensity);
    return Command([&](std::string* name, json* body) {
        json scalars = ScalarsFor(kChSuction, v);
        if (scalars.empty()) return false;
        *name = "ScalarCmd";
        *body = {{"DeviceIndex", device_idx_}, {"Scalars", std::move(scalars)}};
        return true;
    });
}

int IntifaceDevice::LinearIndex() const {
    for (const auto& f : features_)
        if (f.kind == kKindLinear && f.driven) return f.index;
    return -1;
}

bool IntifaceDevice::HasLinear() const { return LinearIndex() >= 0; }

Status IntifaceDevice::SetPosition(double pos, std::chrono::milliseconds duration) {
    const double p = Clamp01(pos);
    long long ms = duration.count();
    if (ms < 1) ms = 1;
    return Command([&](std::string* name, json* body) {
        const int idx = LinearIndex();
        if (idx < 0) return false;
        *name = "LinearCmd";
        *body = {{"DeviceIndex", device_idx_},
                 {"Vectors", json::array({json{{"Index", idx}, {"Duration", ms}, {"Position", p}}})}};
        return true;
    });
}

Status IntifaceDevice::Stop() {
    if (!connected_ || !transport_.IsConnected()) return Status::Ok();
    return Send("StopDeviceCmd", {{"DeviceIndex", device_idx_}});
}

Status IntifaceDevice::Disconnect() {
    const bool open = transport_.IsConnected();
    connected_ = false;
    if (!open) return Status::Ok();
    // Switch off before closing; otherwise the device keeps running until the
    // server notices the missing ping.
    (void)Send("StopDeviceCmd", {{"DeviceIndex", device_idx_}});
    if (transport_.IsConnected()) transport_.Disconnect();
    return Status::Ok();
}

IntifaceDevice::Listed IntifaceDevice::ListedChannels() const {
    return {vibrate_idx_ >= 0, constrict_idx_ >= 0, has_battery_};
}

Capabilities IntifaceDevice::DeviceCapabilities() const {
    Capabilities c;
    c.device = device_name_;
    c.features = features_;
    c.battery = has_battery_;
    return c;
}

ConnectionInfo IntifaceDevice::Info() const {
    std::string name = device_name_;
    if (!name.empty()) {
        std::string channels;
        for (const auto& f : features_) {
            std::string label = f.channel;
            if (!label.empty()) label[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(label[0])));
            if (!f.driven) label += " (not driven yet)";
            if (!channels.empty()) channels += " + ";
            channels += label;
        }
        name += " (via Intiface, " + channels + ")";
    }
    ConnectionInfo info;
    info.connected = connected_;
    info.name = name;
    info.address = url_;
    info.battery_pct = battery_pct_;
    info.battery_ok = battery_ok_;
    return info;
}

BatteryReading IntifaceDevice::BatteryLevel() {
    if (!connected_ || !transport_.IsConnected() || !has_battery_) return {};
    if (battery_ok_ && Now() - battery_at_ < kBatteryCacheTtl) return {battery_pct_, true};

    pending_battery_.reset();  // a stale answer of an expired request
    if (!Send("BatteryLevelCmd", {{"DeviceIndex", device_idx_}})) return {};

    const auto deadline = Now() + kIntifaceBatteryWait;
    while (!pending_battery_) {
        if (!transport_.IsConnected()) return {};
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Now());
        ReceiveResult r = transport_.Receive(left.count() > 0 ? left : std::chrono::milliseconds(0));
        if (r.kind == ReceiveResult::Kind::kTimeout) {
            Log(LogLevel::kDebug, "intiface: battery not readable (no reading within 3s)");
            return {};
        }
        if (r.kind == ReceiveResult::Kind::kClosed) {
            MarkDead("read: " + r.error);
            return {};
        }
        double reading = 0;
        bool got = false;
        json batch = json::parse(ToText(r.data), nullptr, false);
        if (!HandleBatch(batch, &reading, &got)) {
            MarkDead("read: invalid message");
            return {};
        }
        if (got) pending_battery_ = reading;
    }
    battery_pct_ = BatteryFromButtplug(*pending_battery_);
    pending_battery_.reset();
    battery_ok_ = true;
    battery_at_ = Now();
    Log(LogLevel::kInfo, "intiface: battery read " + std::to_string(battery_pct_));
    return {battery_pct_, true};
}

}  // namespace samn::device
