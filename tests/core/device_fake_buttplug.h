#pragma once

// A scripted Buttplug server on a MockTransport (port of Go's fakeButtplug in
// intiface_test.go): answers the handshake as the open protocol describes and
// records what the client sends, so tests can check the messages without a
// real server or device.

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "samn/device/mock.h"

namespace samn_test {

struct FakeButtplug {
    using json = nlohmann::json;

    explicit FakeButtplug(json messages) : device_messages(std::move(messages)) {
        transport.on_send = [this](const samn::device::Bytes& data) { Handle(data); };
    }

    samn::device::MockTransport transport;
    json device_messages;  // which channels the reported device has
    bool no_devices = false;
    std::vector<json> received;  // one {"Name": body} per received message

    std::vector<std::string> Names() const {
        std::vector<std::string> out;
        for (const auto& m : received)
            for (const auto& item : m.items()) out.push_back(item.key());
        return out;
    }

    // The bodies of every received message of that name.
    std::vector<json> Commands(const std::string& name) const {
        std::vector<json> out;
        for (const auto& m : received)
            if (m.contains(name)) out.push_back(m.at(name));
        return out;
    }

    static json StandardDeviceMessages() {
        return {{"ScalarCmd",
                 json::array({json{{"StepCount", 10}, {"ActuatorType", "Vibrate"}, {"FeatureDescriptor", "Vib"}},
                              json{{"StepCount", 5}, {"ActuatorType", "Constrict"}, {"FeatureDescriptor", "Sog"}}})},
                {"StopDeviceCmd", json::object()}};
    }

    static json MessagesWithBattery() {
        json m = StandardDeviceMessages();
        m["BatteryLevelCmd"] = json::object();
        return m;
    }

private:
    void Reply(const std::string& name, json body) {
        transport.PushText(json::array({json{{name, std::move(body)}}}).dump());
    }

    void Handle(const samn::device::Bytes& data) {
        json batch = json::parse(samn::device::ToText(data));
        for (const auto& message : batch) {
            received.push_back(message);
            for (const auto& item : message.items()) {
                const std::string& name = item.key();
                const json id = item.value().value("Id", json());
                if (name == "RequestServerInfo") {
                    Reply("ServerInfo", {{"Id", id}, {"ServerName", "FakeServer"},
                                         {"MessageVersion", 3}, {"MaxPingTime", 5000}});
                } else if (name == "RequestDeviceList") {
                    json devices = json::array();
                    if (!no_devices)
                        devices.push_back({{"DeviceIndex", 7}, {"DeviceName", "Testgerät"},
                                           {"DeviceMessages", device_messages}});
                    Reply("DeviceList", {{"Id", id}, {"Devices", devices}});
                } else if (name == "BatteryLevelCmd") {
                    Reply("BatteryLevelReading", {{"Id", id},
                                                  {"DeviceIndex", item.value().value("DeviceIndex", json())},
                                                  {"BatteryLevel", 0.73}});
                } else if (name == "StartScanning" || name == "StopScanning" || name == "Ping" ||
                           name == "ScalarCmd" || name == "LinearCmd" || name == "RotateCmd" ||
                           name == "StopDeviceCmd") {
                    Reply("Ok", {{"Id", id}});
                }
            }
        }
    }
};

}  // namespace samn_test
