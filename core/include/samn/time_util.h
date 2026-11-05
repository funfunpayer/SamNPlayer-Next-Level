#pragma once

// Broken-down time without the platform split (gmtime_r / gmtime_s and
// localtime_r / localtime_s).

#include <ctime>

namespace samn {

// `t` as UTC (utc = true) or in the local time zone.
std::tm TimeToTm(std::time_t t, bool utc);

}  // namespace samn
