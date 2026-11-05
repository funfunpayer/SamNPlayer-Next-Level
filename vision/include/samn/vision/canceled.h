#pragma once

// The error text of a run that the user stopped. A canceled run is not a
// failure: the frame loops in vision and the engine set this text and the
// callers (engine, app) compare against it to tell the two apart. The value
// is part of the contract; do not reword it.

namespace samn {

inline constexpr const char* kCanceled = "canceled";

}  // namespace samn
