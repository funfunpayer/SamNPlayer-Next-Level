#pragma once

// JSON text exactly as Go's encoding/json writes it, so Buttplug messages and
// the GUI structs are byte-identical with 0.7:
//   - compact, no spaces; objects as the container orders them (nlohmann::json
//     sorts keys, like Go's map[string]any; ordered_json keeps insertion order,
//     like a Go struct);
//   - float64 as Go prints it: 1 not 1.0, 0.5, 1e-07 -> 1e-7, 1e+21;
//   - strings escaped like Go: also < > & (as < ...), U+2028 / U+2029.
// NaN / Inf cannot be encoded (Go returns an error); they are written as null.

#include <string>

#include <nlohmann/json.hpp>

namespace samn::device {

std::string GoJsonDump(const nlohmann::json& value);
std::string GoJsonDump(const nlohmann::ordered_json& value);

// One float64 as Go formats it (strconv 'f' / 'e', shortest round trip).
std::string GoFloat(double v);

}  // namespace samn::device
