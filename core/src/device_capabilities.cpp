#include "samn/device/capabilities.h"

#include <map>

#include "samn/device/go_json.h"

namespace samn::device {

bool Capabilities::Has(const std::string& channel) const {
    for (const auto& f : features)
        if (f.channel == channel) return true;
    return false;
}

std::vector<std::string> Capabilities::Channels() const {
    std::vector<std::string> out;
    out.reserve(features.size());
    for (const auto& f : features) out.push_back(f.channel);
    return out;
}

Capabilities Neo2Capabilities(const std::string& name, bool battery) {
    Capabilities c;
    c.device = name;
    c.features.push_back({kChVibration, kKindScalar, 0, "", true});
    c.features.push_back({kChSuction, kKindScalar, 1, "", true});
    c.battery = battery;
    return c;
}

Capabilities CapabilitiesOf(const Device& d) {
    if (const auto* r = dynamic_cast<const CapabilityReporter*>(&d)) return r->DeviceCapabilities();
    return Neo2Capabilities("", false);
}

std::vector<Feature> ButtplugFeatures(const nlohmann::json& messages, int constrict_idx) {
    std::vector<Feature> out;
    std::map<std::string, int> count;
    auto name = [&count](const std::string& ch) {
        const int n = ++count[ch];
        if (n == 1) return ch;
        return ch + static_cast<char>('0' + n);
    };
    auto list = [&messages](const char* key) -> const nlohmann::json* {
        if (!messages.is_object()) return nullptr;
        auto it = messages.find(key);
        if (it == messages.end() || !it->is_array()) return nullptr;
        return &*it;
    };

    if (const auto* scalars = list("ScalarCmd")) {
        int idx = 0;
        for (const auto& entry : *scalars) {
            const int this_idx = idx++;
            std::string actuator;
            if (entry.is_object()) {
                auto it = entry.find("ActuatorType");
                if (it != entry.end() && it->is_string()) actuator = it->get<std::string>();
            }
            std::string ch;
            if (actuator == "Vibrate") {
                ch = kChVibration;
            } else if (actuator == "Constrict" || actuator == "Inflate") {
                ch = kChSuction;
            } else if (actuator == "Oscillate") {
                ch = this_idx == constrict_idx ? kChSuction : kChOscillate;
            } else if (actuator == "Rotate") {
                ch = kChRotate;
            } else if (actuator == "Position") {
                ch = kChStroke;
            } else {
                continue;
            }
            const bool driven = ch != kChStroke && (ch != kChSuction || this_idx == constrict_idx);
            out.push_back({name(ch), kKindScalar, this_idx, actuator, driven});
        }
    }
    if (const auto* linears = list("LinearCmd")) {
        for (int idx = 0; idx < static_cast<int>(linears->size()); ++idx)
            out.push_back({name(kChStroke), kKindLinear, idx, "", idx == 0});
    }
    if (const auto* rotates = list("RotateCmd")) {
        for (int idx = 0; idx < static_cast<int>(rotates->size()); ++idx)
            out.push_back({name(kChRotate), kKindRotate, idx, "", true});
    }
    return out;
}

namespace {

nlohmann::ordered_json FeatureJson(const Feature& f) {
    nlohmann::ordered_json j = nlohmann::ordered_json::object();
    j["channel"] = f.channel;
    j["kind"] = f.kind;
    j["index"] = f.index;
    if (!f.actuator.empty()) j["actuator"] = f.actuator;
    j["driven"] = f.driven;
    return j;
}

}  // namespace

std::string ToJson(const Feature& f) { return GoJsonDump(FeatureJson(f)); }

std::string ToJson(const Capabilities& c) {
    nlohmann::ordered_json j = nlohmann::ordered_json::object();
    j["device"] = c.device;
    auto features = nlohmann::ordered_json::array();
    for (const auto& f : c.features) features.push_back(FeatureJson(f));
    j["features"] = std::move(features);
    j["battery"] = c.battery;
    return GoJsonDump(j);
}

std::string ToJson(const ConnectionInfo& info) {
    nlohmann::ordered_json j = nlohmann::ordered_json::object();
    j["Connected"] = info.connected;
    j["Name"] = info.name;
    j["Address"] = info.address;
    j["RSSI"] = info.rssi;
    j["BatteryPct"] = info.battery_pct;
    j["BatteryOK"] = info.battery_ok;
    return GoJsonDump(j);
}

}  // namespace samn::device
