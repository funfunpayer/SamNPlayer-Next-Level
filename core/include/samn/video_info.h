#pragma once
// What a video's header says, without guessing (issue #4, MEDIA-01): a codec
// the probe could not establish stays unknown; it is never filled in with a
// likely default such as H.264, and nothing downstream treats "unknown" as
// "playable". Playability comes from actually opening and decoding the file.

#include <cstdint>
#include <string>

namespace samn {

// The codec's name for a FourCC (OpenCV CAP_PROP_FOURCC, little-endian):
// common tags get their usual name ("avc1" -> "H.264"), any other printable
// tag is shown as it is, and 0 or a tag that is not four printable
// characters gives "" (unknown).
std::string CodecName(uint32_t fourcc);

}  // namespace samn
