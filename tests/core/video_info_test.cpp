// Issue #4 (MEDIA-01): an unknown codec stays unknown.
#include <doctest/doctest.h>

#include "samn/video_info.h"

namespace {
uint32_t Tag(const char* s) {
    return uint32_t(uint8_t(s[0])) | uint32_t(uint8_t(s[1])) << 8 | uint32_t(uint8_t(s[2])) << 16 |
           uint32_t(uint8_t(s[3])) << 24;
}
}  // namespace

TEST_CASE("codec names: known tags, other tags as they are, the rest unknown") {
    CHECK(samn::CodecName(Tag("avc1")) == "H.264");
    CHECK(samn::CodecName(Tag("H264")) == "H.264");
    CHECK(samn::CodecName(Tag("hvc1")) == "H.265");
    CHECK(samn::CodecName(Tag("MJPG")) == "Motion JPEG");
    CHECK(samn::CodecName(Tag("mp4v")) == "MPEG-4");
    CHECK(samn::CodecName(Tag("abcd")) == "abcd");  // not ours to name, but shown as it is
    // Nothing established: unknown, never a likely default.
    CHECK(samn::CodecName(0).empty());
    CHECK(samn::CodecName(0x00000001u).empty());
    CHECK(samn::CodecName(Tag("    ")).empty());
    CHECK(samn::CodecName(0xFFFFFFFFu).empty());
}
