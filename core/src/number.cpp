#include "samn/number.h"

#include <charconv>
#include <cmath>
#include <limits>

namespace samn {

namespace {
bool IsBlank(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }

// from_chars gave result_out_of_range: is the number huge or tiny? Looks at
// the decimal exponent of the first significant digit.
bool IsHuge(const char* b, const char* e) {
    long exp10 = 0;
    long point = -1, first_nonzero = -1, digits = 0;
    const char* p = b;
    if (p < e && (*p == '-' || *p == '+')) ++p;
    for (; p < e && *p != 'e' && *p != 'E'; ++p) {
        if (*p == '.') {
            point = digits;
            continue;
        }
        if (first_nonzero < 0 && *p != '0') first_nonzero = digits;
        ++digits;
    }
    if (p < e) {
        long x = 0;
        const char* q = p + 1;
        if (q < e && *q == '+') ++q;
        std::from_chars(q, e, x);
        exp10 = x;
    }
    if (first_nonzero < 0) return false;
    if (point < 0) point = digits;
    return exp10 + (point - first_nonzero) > 0;
}
}  // namespace

ParsedDouble ParseDoublePrefix(std::string_view s) {
    ParsedDouble r;
    size_t i = 0;
    while (i < s.size() && IsBlank(s[i])) ++i;
    const char* b = s.data() + i;
    const char* e = s.data() + s.size();
    const bool plus = b < e && *b == '+';
    if (plus) ++b;  // from_chars takes '-' but not '+'
    if (plus && b < e && *b == '-') return r;
    // Digits only: from_chars would also read "inf", "nan" and hex.
    if (b >= e || !((*b >= '0' && *b <= '9') || (*b == '.' && b + 1 < e && b[1] >= '0' && b[1] <= '9') ||
                    (*b == '-' && b + 1 < e && ((b[1] >= '0' && b[1] <= '9') || b[1] == '.'))))
        return r;
    double v = 0;
    const auto res = std::from_chars(b, e, v, std::chars_format::general);
    if (res.ec == std::errc::invalid_argument) return r;
    r.ok = true;
    r.used = size_t(res.ptr - s.data());
    if (res.ec == std::errc::result_out_of_range) {
        r.out_of_range = true;
        const double inf = std::numeric_limits<double>::infinity();
        const bool neg = *b == '-';
        v = IsHuge(b, res.ptr) ? (neg ? -inf : inf) : (neg ? -0.0 : 0.0);
    }
    r.value = v;
    return r;
}

double StrToDouble(std::string_view s) { return ParseDoublePrefix(s).value; }

std::string FormatFixed(double value, int decimals) {
    char buf[400];
    const auto res = std::to_chars(buf, buf + sizeof buf, value, std::chars_format::fixed, decimals);
    return res.ec == std::errc() ? std::string(buf, res.ptr) : std::string();
}

}  // namespace samn
