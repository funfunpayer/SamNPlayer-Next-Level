#include "samn/device/go_json.h"

#include <charconv>
#include <cmath>
#include <cstdio>

namespace samn::device {

std::string GoFloat(double v) {
    if (!std::isfinite(v)) return "null";
    if (v == 0) return std::signbit(v) ? "-0" : "0";

    char buf[64];
    const auto res = std::to_chars(buf, buf + sizeof buf, v, std::chars_format::scientific);
    std::string sci(buf, res.ptr);  // "-1.2345e-07"

    std::string sign;
    std::size_t pos = 0;
    if (sci[0] == '-') {
        sign = "-";
        pos = 1;
    }
    const std::size_t epos = sci.find('e');
    std::string digits;
    for (std::size_t i = pos; i < epos; ++i)
        if (sci[i] != '.') digits += sci[i];
    const int exp10 = std::stoi(sci.substr(epos + 1));

    const double abs_v = std::fabs(v);
    if (abs_v < 1e-6 || abs_v >= 1e21) {
        std::string out = sign;
        out += digits[0];
        if (digits.size() > 1) out += "." + digits.substr(1);
        out += 'e';
        out += exp10 < 0 ? '-' : '+';
        const int a = std::abs(exp10);
        if (a < 10) out += '0';
        out += std::to_string(a);
        // Go: "e-07" -> "e-7" (only for negative exponents).
        const std::size_t n = out.size();
        if (n >= 4 && out[n - 4] == 'e' && out[n - 3] == '-' && out[n - 2] == '0') {
            out[n - 2] = out[n - 1];
            out.pop_back();
        }
        return out;
    }

    const int dp = exp10 + 1;  // digits before the decimal point
    std::string out = sign;
    if (dp <= 0) {
        out += "0." + std::string(static_cast<std::size_t>(-dp), '0') + digits;
    } else if (static_cast<std::size_t>(dp) >= digits.size()) {
        out += digits + std::string(static_cast<std::size_t>(dp) - digits.size(), '0');
    } else {
        out += digits.substr(0, static_cast<std::size_t>(dp)) + "." +
               digits.substr(static_cast<std::size_t>(dp));
    }
    return out;
}

namespace {

void DumpString(const std::string& s, std::string& out) {
    static const char* hex = "0123456789abcdef";
    out += '"';
    for (std::size_t i = 0; i < s.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case '<': case '>': case '&':
                out += "\\u00";
                out += hex[c >> 4];
                out += hex[c & 15];
                break;
            default:
                if (c < 0x20) {
                    out += "\\u00";
                    out += hex[c >> 4];
                    out += hex[c & 15];
                } else if (c == 0xE2 && i + 2 < s.size() &&
                           static_cast<unsigned char>(s[i + 1]) == 0x80 &&
                           (static_cast<unsigned char>(s[i + 2]) & 0xFE) == 0xA8) {
                    out += static_cast<unsigned char>(s[i + 2]) == 0xA8 ? "\\u2028" : "\\u2029";
                    i += 2;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    out += '"';
}

template <class J>
void Dump(const J& v, std::string& out) {
    switch (v.type()) {
        case nlohmann::json::value_t::null: out += "null"; break;
        case nlohmann::json::value_t::boolean: out += v.template get<bool>() ? "true" : "false"; break;
        case nlohmann::json::value_t::number_integer:
            out += std::to_string(v.template get<long long>());
            break;
        case nlohmann::json::value_t::number_unsigned:
            out += std::to_string(v.template get<unsigned long long>());
            break;
        case nlohmann::json::value_t::number_float: out += GoFloat(v.template get<double>()); break;
        case nlohmann::json::value_t::string: DumpString(v.template get<std::string>(), out); break;
        case nlohmann::json::value_t::array: {
            out += '[';
            bool first = true;
            for (const auto& e : v) {
                if (!first) out += ',';
                first = false;
                Dump(e, out);
            }
            out += ']';
            break;
        }
        case nlohmann::json::value_t::object: {
            out += '{';
            bool first = true;
            for (const auto& item : v.items()) {
                if (!first) out += ',';
                first = false;
                DumpString(item.key(), out);
                out += ':';
                Dump(item.value(), out);
            }
            out += '}';
            break;
        }
        default: out += "null"; break;  // binary / discarded
    }
}

}  // namespace

std::string GoJsonDump(const nlohmann::json& value) {
    std::string out;
    Dump(value, out);
    return out;
}

std::string GoJsonDump(const nlohmann::ordered_json& value) {
    std::string out;
    Dump(value, out);
    return out;
}

}  // namespace samn::device
