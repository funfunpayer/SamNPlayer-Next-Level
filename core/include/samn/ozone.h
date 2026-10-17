#pragma once
// O-zone suggestions, port of SamNPlayer 0.7 funscript/ozone.go: classical,
// signal-only proposals for the primary O-marker (late, high-intensity
// window) and up to a few weaker earlier ones. A proposal: the user
// confirms, moves or discards it.
#include <cstdint>
#include <string>
#include <vector>

#include "samn/funscript.h"

namespace samn {

struct OZoneSuggestion {
    int64_t start_ms = 0;
    int64_t end_ms = 0;
    std::string reason;
    bool ok = false;
    double mean = 0;  // mean position in the window; base of the secondary filter
};

// The window (8 % of the script, at least 1.5 s) with the highest mean
// position in the last 15 %; none when the script is under 4 s, has fewer
// than 4 actions or the tail stays below a mean of 35.
OZoneSuggestion SuggestOZone(const std::vector<Action>& actions);

// Up to max_count earlier, weaker windows of the primary's size: clearly
// above the median of the windows before it (+10) and below 0.85 x the
// primary's mean, not overlapping, in time order. Empty is the normal result.
std::vector<OZoneSuggestion> SuggestSecondaryOZones(const std::vector<Action>& actions,
                                                    const OZoneSuggestion& primary, int max_count);

}  // namespace samn
