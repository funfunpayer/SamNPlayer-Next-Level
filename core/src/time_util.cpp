#include "samn/time_util.h"

namespace samn {

std::tm TimeToTm(std::time_t t, bool utc) {
    std::tm tm{};
#ifdef _WIN32
    if (utc) gmtime_s(&tm, &t); else localtime_s(&tm, &t);
#else
    if (utc) gmtime_r(&t, &tm); else localtime_r(&t, &tm);
#endif
    return tm;
}

}  // namespace samn
