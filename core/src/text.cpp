#include "samn/text.h"

#include <cstdint>

namespace samn {

namespace {

char32_t Lower(char32_t c) {
    if (c < 0x80) return (c >= 'A' && c <= 'Z') ? c + 0x20 : c;
    // Latin-1: À-Þ except ×.
    if (c >= 0xC0 && c <= 0xDE && c != 0xD7) return c + 0x20;
    // Latin Extended-A: upper/lower pairs.
    if (c == 0x130) return U'i';
    if (c == 0x178) return 0xFF;
    if ((c >= 0x100 && c <= 0x137) || (c >= 0x14A && c <= 0x177)) return (c % 2 == 0) ? c + 1 : c;
    if ((c >= 0x139 && c <= 0x148) || (c >= 0x179 && c <= 0x17E)) return (c % 2 == 1) ? c + 1 : c;
    // Greek.
    if (c >= 0x391 && c <= 0x3A9 && c != 0x3A2) return c + 0x20;
    if (c == 0x386) return 0x3AC;
    if (c >= 0x388 && c <= 0x38A) return c + 0x25;
    if (c == 0x38C) return 0x3CC;
    if (c == 0x38E || c == 0x38F) return c + 0x3F;
    // Cyrillic.
    if (c >= 0x400 && c <= 0x40F) return c + 0x50;
    if (c >= 0x410 && c <= 0x42F) return c + 0x20;
    if ((c >= 0x460 && c <= 0x481) || (c >= 0x48A && c <= 0x4BF)) return (c % 2 == 0) ? c + 1 : c;
    // Full-width Latin.
    if (c >= 0xFF21 && c <= 0xFF3A) return c + 0x20;
    return c;
}

void Append(std::string& out, char32_t c) {
    if (c < 0x80) {
        out += static_cast<char>(c);
    } else if (c < 0x800) {
        out += static_cast<char>(0xC0 | (c >> 6));
        out += static_cast<char>(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
        out += static_cast<char>(0xE0 | (c >> 12));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (c & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (c >> 18));
        out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (c & 0x3F));
    }
}

}  // namespace

std::string FoldCase(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    size_t i = 0;
    const auto n = s.size();
    auto cont = [&](size_t k) { return k < n && (static_cast<unsigned char>(s[k]) & 0xC0) == 0x80; };
    while (i < n) {
        const auto b = static_cast<unsigned char>(s[i]);
        char32_t c = 0;
        size_t len = 0;
        if (b < 0x80) {
            c = b;
            len = 1;
        } else if ((b & 0xE0) == 0xC0 && b >= 0xC2 && cont(i + 1)) {
            c = ((b & 0x1F) << 6) | (s[i + 1] & 0x3F);
            len = 2;
        } else if ((b & 0xF0) == 0xE0 && cont(i + 1) && cont(i + 2)) {
            c = ((b & 0x0F) << 12) | ((s[i + 1] & 0x3F) << 6) | (s[i + 2] & 0x3F);
            len = 3;
            if (c < 0x800) len = 0;  // overlong
        } else if ((b & 0xF8) == 0xF0 && cont(i + 1) && cont(i + 2) && cont(i + 3)) {
            c = ((b & 0x07) << 18) | ((s[i + 1] & 0x3F) << 12) | ((s[i + 2] & 0x3F) << 6) | (s[i + 3] & 0x3F);
            len = 4;
            if (c < 0x10000 || c > 0x10FFFF) len = 0;
        }
        if (len == 0) {  // invalid byte: keep it
            out += s[i++];
            continue;
        }
        const char32_t l = Lower(c);
        if (l == c) out.append(s, i, len);
        else Append(out, l);
        i += len;
    }
    return out;
}

}  // namespace samn
