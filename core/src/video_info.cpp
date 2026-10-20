#include "samn/video_info.h"

#include <cctype>
#include <map>

namespace samn {

std::string CodecName(uint32_t fourcc) {
    if (fourcc == 0) return "";
    std::string tag;
    for (int i = 0; i < 4; ++i) {
        const char c = char((fourcc >> (8 * i)) & 0xFF);
        if (!std::isprint(static_cast<unsigned char>(c))) return "";  // not a tag: unknown
        tag += c;
    }
    while (!tag.empty() && tag.back() == ' ') tag.pop_back();
    if (tag.empty()) return "";
    std::string key = tag;
    for (auto& c : key) c = char(std::tolower(static_cast<unsigned char>(c)));
    static const std::map<std::string, std::string> kNames{
        {"avc1", "H.264"}, {"avc3", "H.264"}, {"h264", "H.264"}, {"x264", "H.264"},
        {"hvc1", "H.265"}, {"hev1", "H.265"}, {"hevc", "H.265"}, {"h265", "H.265"},
        {"av01", "AV1"},   {"vp09", "VP9"},   {"vp90", "VP9"},   {"vp80", "VP8"},
        {"mjpg", "Motion JPEG"}, {"mp4v", "MPEG-4"}, {"fmp4", "MPEG-4"}, {"xvid", "MPEG-4"},
        {"divx", "MPEG-4"}, {"mpg2", "MPEG-2"}, {"wmv3", "WMV"},
    };
    const auto it = kNames.find(key);
    return it != kNames.end() ? it->second : tag;
}

}  // namespace samn
