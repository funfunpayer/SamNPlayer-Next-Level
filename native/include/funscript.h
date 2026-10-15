#pragma once
#include <vector>
#include <string>
#include <cstdint>
#include <optional>
#include <utility>

namespace samn {

struct Action {
    int64_t at = 0;
    int pos = 0;
};

struct Script {
    std::vector<Action> actions;
    bool inverted = false;
    int dropped = 0;
    int64_t duration_meta = 0;  // ms, after OFS scaling rules
};

// MaxActionMs: action times above this (48 h) are treated as corrupt.
constexpr int64_t MaxActionMs = 48LL * 3600 * 1000;

// Parse a funscript JSON string. Throws std::runtime_error on hard failures
// (invalid JSON, no actions, all times corrupt). Soft drops corrupt times.
Script Parse(const std::string& json_str);

// Write a minimal funscript JSON (actions + inverted + duration if set).
// Full metadata is not preserved; this is the hot-path subset for M1.
std::string Write(const Script& s);

// ShiftActions moves every action by deltaMs (positive = later).
// Actions that would land before 0 ms are dropped and counted.
// Input is not modified. Parity with funscript/shift.go.
std::pair<std::vector<Action>, int> ShiftActions(const std::vector<Action>& actions, int64_t deltaMs);

}  // namespace samn
