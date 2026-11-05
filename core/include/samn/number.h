#pragma once

// Decimal numbers that do not depend on the process locale. strtod, stod and
// snprintf("%f") read the C locale, and Qt on Unix calls setlocale(LC_ALL, "")
// at start-up: under a comma-decimal locale "1.6" parsed as 1 and 62.5 was
// written as "62,5".

#include <cstddef>
#include <string>
#include <string_view>

namespace samn {

struct ParsedDouble {
    double value = 0;
    size_t used = 0;           // characters consumed, leading blanks included
    bool ok = false;           // a number was found
    bool out_of_range = false; // ok, but the value is +-inf (too large) or 0 (too small)
};

// strtod's prefix rules for decimal numbers: leading blanks, an optional sign,
// digits with an optional fraction and exponent ('.' is always the point).
// Hex floats are not read.
ParsedDouble ParseDoublePrefix(std::string_view s);

// strtod's result: the value of the longest number prefix, 0 when there is
// none, +-inf on overflow.
double StrToDouble(std::string_view s);

// Fixed notation with `decimals` digits after the point ('.' always).
std::string FormatFixed(double value, int decimals);

}  // namespace samn
